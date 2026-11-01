#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool v,const char* text){if(!v)throw std::runtime_error{text};}
Grain particle(float x,float y,unsigned kind=1){Grain p;p.position={x,y,x,y};p.motion={0,0,kind==5?2.65F:kind==6?3.3125F:1.F,static_cast<float>(kind)};return p;}
std::vector<std::uint32_t> floor_at(){std::vector<std::uint32_t> walls(width*height);for(int y=180;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;return walls;}
double compare(const std::vector<Grain>& a,const std::vector<Grain>& b){
    double error=0;
    for(unsigned i=0;i<capacity;++i){
        require(a[i].motion[3]==b[i].motion[3],"Hot reads changed stable IDs/materials");if(a[i].motion[3]<.5F)continue;
        for(const auto& pair:{std::pair{a[i].position,b[i].position},std::pair{a[i].motion,b[i].motion},std::pair{a[i].contact,b[i].contact},std::pair{a[i].residual,b[i].residual}})
            for(unsigned k=0;k<4;++k){require(std::isfinite(pair.second[k]),"Hot reads produced nonfinite state");error=std::max(error,std::abs(static_cast<double>(pair.first[k])-pair.second[k]));}
    }
    require(error<.0001,"Hot reads differ from authoritative full-state reference");return error;
}
void audit(ParticleWorld& world,unsigned& audits){
    if(effective_neighbor_reads(world.execution_options().neighbor_reads,world.execution_options().neighbors)!=NeighborReads::sorted)return;
    const auto result=world.audit_hot_view();require(result.indexed,"Expected an existing cache generation; audit must not build it");
    require(result.mismatches==0,"Current hot bank differs bitwise from authoritative geometry/motion");++audits;
}
}
std::expected<void,std::string> validate_hot_reads(const std::filesystem::path& output,const ExecutionOptions& layout){
    try{
        std::filesystem::create_directories(output);std::ofstream report{output/"hot-tests.txt"},errors{output/"hot-errors.csv"};
        errors<<"mode,phase,certificate,fixture,max_state_error\n";
        require(layout.neighbors!=NeighborMode::linked,"Select sorted/reused index; linked fallback is checked internally");
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        const auto create=[&](const ExecutionOptions& options){ProfileOptions quiet;quiet.collect=true;quiet.gpu_timers=false;quiet.readback_period=0;auto made=ParticleWorld::create(shaders,options,quiet);require(made.has_value(),made?"":made.error().c_str());return std::move(*made);};
        auto base=layout;base.neighbor_reads=NeighborReads::full;base.phase_gating=false;base.skip_zero_projection=false;
        auto reference=create(base);unsigned audits=0;
        for(unsigned variant=0;variant<3;++variant){
            auto options=base;options.neighbor_reads=variant==0?NeighborReads::narrow:NeighborReads::sorted;
            if(variant==2){options.phase_gating=true;options.skip_zero_projection=true;}
            report<<"create mode="<<neighbor_reads_name(options.neighbor_reads)<<" compatibility_C="<<(variant==2)<<std::endl;
            auto candidate=create(options);
            for(unsigned fixture=0;fixture<6;++fixture){
                auto walls=floor_at();std::vector<Grain> state(256);
                if(fixture==0){state[0]=particle(20,181.9F);state[255]=particle(220,80,5);}
                if(fixture==1){state[0]=particle(15.85F,80.5F);state[200]=particle(16.5F,80.5F,5);}
                if(fixture==2){state[0]=particle(31.3F,79.95F);state[255]=particle(128.2F,80.1F,6);}
                if(fixture==3){state[0]=particle(60,90,5);state[1]=particle(61.1F,90,5);state[0].residual={.0000004F,-.0000002F,0,0};state[1].motion[0]=3;state[1].motion[1]=-2;}
                if(fixture==4){state[0]=particle(.1F,40.5F);state[200]=particle(1.3F,40.6F,5);walls[40*width+1]=1;}
                if(fixture==5){state[0]=particle(40.4F,80.4F);state[255]=particle(41.4F,81.4F,5);walls[80*width+41]=1;walls[81*width+40]=1;}
                reference->load(state,walls);candidate->load(state,walls);static_cast<void>(candidate->read_neighbors());audit(*candidate,audits);
                if(fixture==2){std::vector<std::array<float,4>> delta(256);delta[255]={-96.2F,0,0,0};reference->displace_for_test(delta);candidate->displace_for_test(delta);audit(*candidate,audits);}
                reference->project_liquids_for_test(1);candidate->project_liquids_for_test(1);audit(*candidate,audits);
                double error=compare(reference->readback(),candidate->readback());
                for(unsigned tick=0;tick<4;++tick){reference->step();candidate->step();audit(*candidate,audits);error=std::max(error,compare(reference->readback(),candidate->readback()));}
                errors<<neighbor_reads_name(options.neighbor_reads)<<','<<options.phase_gating<<','<<options.skip_zero_projection<<','<<fixture<<','<<error<<'\n';errors.flush();
            }
            auto walls=floor_at();candidate->load(std::array{particle(30,80),particle(120,90,5)},walls);static_cast<void>(candidate->read_neighbors());audit(*candidate,audits);
            candidate->paint({180,100,184,100,3,Material::water});
            if(options.neighbor_reads==NeighborReads::sorted)require(!candidate->audit_hot_view().indexed,"Membership edit retained a valid old reverse map");
            candidate->step();audit(*candidate,audits);candidate->paint({180,100,184,100,7,Material::erase});candidate->step();audit(*candidate,audits);
            std::vector<Grain> full(capacity);for(unsigned i=0;i<capacity;++i)full[i]=particle(2.5F+static_cast<float>(i%300),2.5F+static_cast<float>(i/300));full.back().motion[3]=6;
            candidate->load(full,walls);candidate->step();audit(*candidate,audits);
            MissionConfig config;config.spawn_x=310;config.spawn_y=175;candidate->configure_mission(config);
            candidate->queue_explosion({1,160,60,10,10});candidate->step();audit(*candidate,audits);
            if(options.coupling!=CouplingMode::reference){
                const std::array<std::array<float,4>,3> reactions{{{.125F,0,0,0},{0,-.0625F,1,0},{0,0,0,0}}};candidate->inject_body_for_test(reactions);
                const auto body=candidate->read_body();require(body.stats[0]>=2&&body.queue[0]==0&&body.queue[2]==0,"Hot fields overlap compact body metadata");
                audit(*candidate,audits);
            }
            candidate->reset(Scene::empty);candidate->step();audit(*candidate,audits);
            require(glGetError()==GL_NO_ERROR,"OpenGL error in hot-view validation");
            report<<"mode="<<neighbor_reads_name(options.neighbor_reads)<<" compatibility_C="<<(variant==2)<<" passed"<<std::endl;
        }
        auto linked=base;linked.neighbors=NeighborMode::linked;linked.neighbor_reads=NeighborReads::sorted;auto fallback=create(linked);
        fallback->load(std::array{particle(20,175),particle(21,175,5)},floor_at());for(unsigned i=0;i<4;++i)fallback->step();
        for(const auto& p:fallback->readback())if(p.motion[3]>.5F)for(float value:p.position)require(std::isfinite(value),"Linked narrow fallback became nonfinite");
        require(glGetError()==GL_NO_ERROR,"OpenGL error in linked fallback");
        report<<"cache_bitwise_audits="<<audits<<"\nlinked_sorted_request=narrow_fallback_passed\nstatus=passed\n";return {};
    }catch(const std::exception& error){std::ofstream{output/"hot-error.txt"}<<error.what();return std::unexpected{error.what()};}
}
}
