#include "gpu/upload_ring.hpp"
#include <algorithm>

namespace strefa::gpu::detail {
UploadRing::~UploadRing(){
    for(auto& slot:slots_){
        if(slot.fence)gl_.DeleteSync(slot.fence);
        if(slot.buffer)gl_.DeleteBuffers(1,&slot.buffer);
    }
}
void UploadRing::configure(DeviceStorageMode requested){
    stats_.mode=resolve_device_storage(requested,gl_.persistent_storage);
    if(stats_.mode==DeviceStorageMode::immutable&&!gl_.persistent_storage)
        throw std::runtime_error{"Immutable device buffers require ARB_buffer_storage or OpenGL 4.4."};
}
void UploadRing::allocate(GLuint destination,GLsizeiptr bytes){
    gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER,destination);
    if(stats_.mode==DeviceStorageMode::immutable){
        // No CLIENT_STORAGE, MAP_* or DYNAMIC_STORAGE: CPU updates are forbidden.
        // glCopyBufferSubData remains legal and is the only upload path here.
        gl_.BufferStorage(GL_SHADER_STORAGE_BUFFER,bytes,nullptr,0);
    }else gl_.BufferData(GL_SHADER_STORAGE_BUFFER,bytes,nullptr,GL_DYNAMIC_COPY);
    stats_.device_bytes+=static_cast<std::uint64_t>(bytes);
}
void UploadRing::write(GLuint destination,GLintptr offset,GLsizeiptr bytes,const void* data){
    if(offset<0||bytes<0||(bytes!=0&&data==nullptr))throw std::invalid_argument{"Invalid GPU upload"};
    if(bytes==0)return;
    if(stats_.mode==DeviceStorageMode::direct){
        gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER,destination);
        gl_.BufferSubData(GL_SHADER_STORAGE_BUFFER,offset,bytes,data);
    }else{
        auto& slot=slots_[cursor_];bool busy=false;
        if(slot.fence){
            ++stats_.fence_polls;const auto state=gl_.ClientWaitSync(slot.fence,0,0);
            if(state==GL_WAIT_FAILED)throw std::runtime_error{"GPU upload fence polling failed"};
            busy=state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED;
            gl_.DeleteSync(slot.fence);slot.fence=nullptr;
        }
        if(!slot.buffer)gl_.GenBuffers(1,&slot.buffer);
        gl_.BindBuffer(GL_COPY_READ_BUFFER,slot.buffer);
        if(busy||slot.capacity<bytes){
            // A tiny runtime command must not orphan a former 2 MiB scene
            // upload into another 2 MiB store. Retire only the old store;
            // allocate the new one to the size of this payload.
            const auto capacity=bytes;
            gl_.BufferData(GL_COPY_READ_BUFFER,capacity,nullptr,GL_STREAM_DRAW);
            stats_.staging_bytes-=static_cast<std::uint64_t>(slot.capacity);
            stats_.staging_bytes+=static_cast<std::uint64_t>(capacity);
            stats_.staging_peak_bytes=std::max(stats_.staging_peak_bytes,stats_.staging_bytes);
            slot.capacity=capacity;++stats_.allocations;
            if(busy)++stats_.busy_orphans;
        }
        gl_.BufferSubData(GL_COPY_READ_BUFFER,0,bytes,data);
        gl_.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        gl_.BindBuffer(GL_COPY_WRITE_BUFFER,destination);
        gl_.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,offset,bytes);
        gl_.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
        slot.fence=gl_.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if(!slot.fence)throw std::runtime_error{"GPU upload fence creation failed"};
        ++stats_.copies;cursor_=(cursor_+1)%static_cast<unsigned>(slots_.size());
    }
    ++stats_.writes;stats_.bytes+=static_cast<std::uint64_t>(bytes);
}
}
