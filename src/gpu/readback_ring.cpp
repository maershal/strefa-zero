#include "gpu/readback_ring.hpp"
#include <algorithm>
#include <cstring>
#include <string>

namespace strefa::gpu::detail {
ReadbackRing::~ReadbackRing(){release();}
void ReadbackRing::release(){
    // GL keeps storage alive for queued commands. No CPU pointer is used again.
    for(auto& slot:slots_){
        if(slot.fence)gl_.DeleteSync(slot.fence);
        if(slot.mapped){gl_.BindBuffer(GL_COPY_WRITE_BUFFER,slot.buffer);gl_.UnmapBuffer(GL_COPY_WRITE_BUFFER);}
        if(slot.buffer)gl_.DeleteBuffers(1,&slot.buffer);
        slot={};
    }
    head_=0;stats_.pending=0;
}
void ReadbackRing::configure(ReadbackMode requested,bool debug_labels){
    const auto mode=requested==ReadbackMode::automatic?(gl_.persistent_storage?ReadbackMode::persistent:ReadbackMode::staging):requested;
    if(mode==ReadbackMode::persistent&&!gl_.persistent_storage)throw std::runtime_error{"Trwale mapowane odczyty wymagaja ARB_buffer_storage lub OpenGL 4.4."};
    if(stats_.capacity!=0&&stats_.mode==mode)return;
    release();stats_={};stats_.mode=mode;
    stats_.capacity=(mode==ReadbackMode::legacy||mode==ReadbackMode::stream)?1U:3U;
    try{
        for(unsigned i=0;i<stats_.capacity;++i){
            auto& slot=slots_[i];gl_.GenBuffers(1,&slot.buffer);gl_.BindBuffer(GL_COPY_WRITE_BUFFER,slot.buffer);
            if(mode==ReadbackMode::persistent){
                constexpr GLbitfield access=GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT;
                gl_.BufferStorage(GL_COPY_WRITE_BUFFER,sizeof(ReadbackPacket),nullptr,access|GL_CLIENT_STORAGE_BIT);
                slot.mapped=gl_.MapBufferRange(GL_COPY_WRITE_BUFFER,0,sizeof(ReadbackPacket),access);
                if(!slot.mapped)throw std::runtime_error{"Nie udalo sie trwale zmapowac bufora odczytu GPU."};
            }else gl_.BufferData(GL_COPY_WRITE_BUFFER,sizeof(ReadbackPacket),nullptr,mode==ReadbackMode::legacy?GL_DYNAMIC_COPY:GL_STREAM_READ);
            if(debug_labels){const auto label="HUD "+std::string{readback_mode_name(mode)}+" slot "+std::to_string(i);gl_.ObjectLabel(GL_BUFFER,slot.buffer,-1,label.c_str());}
        }
    }catch(...){release();stats_.capacity=0;throw;}
}
bool ReadbackRing::submit(GLuint source,unsigned generation,std::uint64_t revision,std::uint64_t tick){
    if(stats_.capacity==0)throw std::logic_error{"Readback ring not configured"};
    if(stats_.pending==stats_.capacity){++stats_.dropped;if(gl_.observation)++gl_.observation->stage_drops;return false;}
    auto& slot=slots_[(head_+stats_.pending)%stats_.capacity];
    gl_.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl_.BindBuffer(GL_COPY_READ_BUFFER,source);gl_.BindBuffer(GL_COPY_WRITE_BUFFER,slot.buffer);
    gl_.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,sizeof(ReadbackPacket));
    // The next tick may write the same source SSBO after this copy has read it.
    gl_.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    slot.fence=gl_.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!slot.fence)throw std::runtime_error{"GPU readback fence creation failed"};
    slot.generation=generation;slot.revision=revision;slot.tick=tick;
    ++stats_.submitted;++stats_.pending;stats_.max_pending=std::max(stats_.max_pending,stats_.pending);
    if(gl_.observation)++gl_.observation->stage_copies;
    return true;
}
std::optional<ReadbackPacket> ReadbackRing::poll(unsigned generation,std::uint64_t revision,std::uint64_t tick){
    timings_={};std::optional<ReadbackPacket> latest;
    const auto stamp=[&](){return gl_.observation?SDL_GetTicksNS():std::uint64_t{0};};
    while(stats_.pending!=0){
        auto& slot=slots_[head_];const auto fence_started=stamp();
        const auto status=gl_.ClientWaitSync(slot.fence,0,0);
        timings_.fence_ms+=static_cast<double>(stamp()-fence_started)/1e6;
        if(status==GL_WAIT_FAILED)throw std::runtime_error{"GPU readback fence polling failed"};
        if(status!=GL_ALREADY_SIGNALED&&status!=GL_CONDITION_SATISFIED){++stats_.busy_polls;break;}
        if(slot.generation==generation&&slot.revision==revision){
            const auto read_started=stamp();ReadbackPacket packet;
            if(slot.mapped){std::memcpy(&packet,slot.mapped,sizeof(packet));if(gl_.observation)gl_.observation->mapped_read_bytes+=sizeof(packet);}
            else{gl_.BindBuffer(GL_COPY_WRITE_BUFFER,slot.buffer);gl_.GetBufferSubData(GL_COPY_WRITE_BUFFER,0,sizeof(packet),&packet);}
            timings_.read_ms+=static_cast<double>(stamp()-read_started)/1e6;
            latest=packet;++stats_.completed;stats_.last_latency_ticks=tick>=slot.tick?tick-slot.tick:0;
        }else{++stats_.stale;if(gl_.observation)++gl_.observation->stale_readbacks;}
        gl_.DeleteSync(slot.fence);slot.fence=nullptr;
        head_=(head_+1)%stats_.capacity;--stats_.pending;
    }
    return latest;
}
}
