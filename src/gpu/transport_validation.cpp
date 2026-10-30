#include "gpu/world.hpp"
#include "gpu/upload_ring.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error{message};}
GLenum APIENTRY forced_busy(GLsync,GLbitfield,GLuint64){return GL_TIMEOUT_EXPIRED;}
struct BusyFence {
    detail::Api& gl;PFNGLCLIENTWAITSYNCPROC saved;
    explicit BusyFence(detail::Api& api):gl{api},saved{api.ClientWaitSync.function}{gl.ClientWaitSync.function=forced_busy;}
    ~BusyFence(){gl.ClientWaitSync.function=saved;}
};
struct Buffer {
    detail::Api& gl;GLuint id{};
    explicit Buffer(detail::Api& api):gl{api}{gl.GenBuffers(1,&id);}
    ~Buffer(){gl.DeleteBuffers(1,&id);}
};
void check_uploads(detail::Api& gl,DeviceStorageMode requested,std::ostream& report){
    Buffer target{gl};detail::UploadRing ring{gl};ring.configure(requested);
    constexpr unsigned words=32768;ring.allocate(target.id,words*4);
    if(ring.stats().mode==DeviceStorageMode::immutable){
        const auto parameter=reinterpret_cast<PFNGLGETBUFFERPARAMETERIVPROC>(SDL_GL_GetProcAddress("glGetBufferParameteriv"));
        require(parameter!=nullptr,"Cannot query device buffer storage");
        GLint immutable{},flags{};parameter(GL_SHADER_STORAGE_BUFFER,GL_BUFFER_IMMUTABLE_STORAGE,&immutable);
        parameter(GL_SHADER_STORAGE_BUFFER,GL_BUFFER_STORAGE_FLAGS,&flags);
        require(immutable==GL_TRUE&&flags==0,"Device buffer permits client updates or mapping");
    }
    std::vector<std::uint32_t> expected(words),actual(words);
    ring.write(target.id,0,words*4,expected.data());
    const auto transfers=[&](){
        for(unsigned round=0;round<12;++round){
            const auto count=31U+(round*977U)%17000U,offset=(round*331U)%(words-count);
            std::vector<std::uint32_t> payload(count);
            for(unsigned i=0;i<count;++i)payload[i]=round*100000U+i+1;
            std::copy(payload.begin(),payload.end(),expected.begin()+offset);
            ring.write(target.id,offset*4,count*4,payload.data());
            // Client storage is no longer needed after write() returns.
            std::ranges::fill(payload,0xffffffffU);
        }
    };
    if(ring.stats().mode==DeviceStorageMode::direct)transfers();
    else{BusyFence force_orphan{gl};transfers();require(ring.stats().busy_orphans>0,"Busy upload reuse was not exercised");}
    const auto before=ring.stats().writes;ring.write(target.id,0,0,nullptr);
    require(ring.stats().writes==before,"Empty upload issued a transfer");
    bool invalid=false;try{ring.write(target.id,-1,4,actual.data());}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"Negative upload offset accepted");
    glFinish();gl.BindBuffer(GL_COPY_READ_BUFFER,target.id);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,words*4,actual.data());
    require(actual==expected,"Ring wrap/orphan corrupted a queued upload or its offset");
    const auto stats=ring.stats();
    require(stats.writes==13&&stats.copies==(stats.mode==DeviceStorageMode::direct?0U:13U),"Upload accounting lost a write");
    require(glGetError()==GL_NO_ERROR,"OpenGL error during upload validation");
    report<<"ring requested="<<device_storage_name(requested)<<" effective="<<device_storage_name(stats.mode)
        <<" writes="<<stats.writes<<" copies="<<stats.copies<<" forced_busy_orphans="<<stats.busy_orphans<<" passed\n";
}
Grain grain(float x,float y,unsigned kind){Grain g;g.position={x,y,x,y};g.motion={0,0,kind==5?2.65F:1.F,static_cast<float>(kind)};return g;}
unsigned alive(const std::vector<Grain>& grains){return static_cast<unsigned>(std::ranges::count_if(grains,[](const auto& g){return g.motion[3]>.5F;}));}
}
std::expected<void,std::string> validate_transport(const std::filesystem::path& output){
    try{
        std::filesystem::create_directories(output);std::ofstream report{output/"transport-tests.txt"};
        detail::Api gl;gl.load();
        require(resolve_device_storage(DeviceStorageMode::automatic,false)==DeviceStorageMode::staged,"GL 4.3 storage fallback changed");
        require(resolve_device_storage(DeviceStorageMode::automatic,true)==DeviceStorageMode::immutable,"Automatic immutable selection changed");
        for(auto mode:{DeviceStorageMode::direct,DeviceStorageMode::staged,DeviceStorageMode::immutable,DeviceStorageMode::automatic}){
            if(mode==DeviceStorageMode::immutable&&!gl.persistent_storage)continue;
            check_uploads(gl,mode,report);
        }
        // Disable the optional capability in the loader: exercise automatic
        // fallback using only core GL 4.3 operations on the same device.
        {detail::Api fallback;fallback.load();fallback.persistent_storage=false;
            check_uploads(fallback,DeviceStorageMode::automatic,report);
            detail::UploadRing explicit_immutable{fallback};bool rejected=false;
            try{explicit_immutable.configure(DeviceStorageMode::immutable);}catch(const std::runtime_error&){rejected=true;}
            require(rejected,"Explicit immutable accepted an unsupported capability");}
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        std::vector<Grain> reference;std::vector<std::uint32_t> walls(width*height);
        for(int y=180;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
        const std::array fixture{grain(20,179.7F,1),grain(40,100,2),grain(70,90,5),grain(71.3F,90,5),grain(110,70,6),grain(150,110,4)};
        for(auto mode:{DeviceStorageMode::direct,DeviceStorageMode::staged,DeviceStorageMode::immutable,DeviceStorageMode::automatic}){
            if(mode==DeviceStorageMode::immutable&&!gl.persistent_storage)continue;
            ExecutionOptions execution;execution.storage=mode;
            ProfileOptions quiet;quiet.collect=true;quiet.gpu_timers=false;quiet.readback_period=0;
            auto made=ParticleWorld::create(shaders,execution,quiet);require(made.has_value(),made?"":made.error().c_str());auto world=std::move(*made);
            require(world->readback_stats().submitted==0,"Transport policy performed an initial HUD copy");
            world->load(fixture,walls);for(unsigned tick=0;tick<8;++tick)world->step();
            auto state=world->readback();double error=0;
            if(reference.empty())reference=state;
            else for(unsigned id=0;id<capacity;++id){
                require(reference[id].motion[3]==state[id].motion[3],"Transport changed stable IDs/materials");
                if(state[id].motion[3]<.5F)continue;
                const auto a=std::bit_cast<std::array<float,16>>(reference[id]),b=std::bit_cast<std::array<float,16>>(state[id]);
                for(unsigned field=0;field<16;++field){require(std::isfinite(b[field]),"Nonfinite state after staged upload");error=std::max(error,static_cast<double>(std::abs(a[field]-b[field])));}
            }
            require(error<.0001,"Storage path changed the small reference scene");
            world->paint({220,40,220,40,2,Material::water});require(alive(world->readback())>fixture.size(),"Staged paint lost new carriers");
            world->paint({220,40,220,40,3,Material::erase});require(alive(world->readback())==fixture.size(),"Staged erase retained old carriers");
            const std::array<std::array<float,4>,1> displacement{{{.125F,-.0625F,0,0}}};world->displace_for_test(displacement);
            static_cast<void>(world->audit_neighbors());
            std::vector<Grain> full(capacity);for(unsigned id=0;id<capacity;++id)full[id]=grain(2.5F+static_cast<float>(id%300),2.5F+static_cast<float>(id/300),1);
            world->load(full,walls);world->step();require(alive(world->readback())==capacity,"Full-pool staged load lost particles");
            world->load({},walls);MissionConfig config;config.spawn_y=175;world->configure_mission(config);
            world->queue_explosion({1,100,80,1,1});world->queue_explosion({2,110,80,1,1});world->step();world->step();
            const auto mission=world->read_mission();require(mission.events[2]==2&&mission.counts[0]==2,"Staged commands lost or replayed explosions");
            world->reset(Scene::empty);require(alive(world->readback())==0,"Reset retained an old generation");
            require(world->readback_stats().submitted==0,"Disabled HUD copied during transport validation");
            const auto upload=world->upload_stats();require(upload.mode==resolve_device_storage(mode,gl.persistent_storage),"Factory storage differs from request");
            require(upload.copies==(upload.mode==DeviceStorageMode::direct?0:upload.writes),"World bypassed its upload policy");
            require(glGetError()==GL_NO_ERROR,"OpenGL error in staged world path");
            report<<"world requested="<<device_storage_name(mode)<<" effective="<<device_storage_name(upload.mode)
                <<" state_error="<<error<<" writes="<<upload.writes<<" copies="<<upload.copies
                <<" device_bytes="<<upload.device_bytes<<" staging_bytes="<<upload.staging_bytes<<" passed\n";report.flush();
        }
        report<<"status=passed\n";return {};
    }catch(const std::exception& error){std::ofstream{output/"transport-error.txt"}<<error.what();return std::unexpected{error.what()};}
}
}
