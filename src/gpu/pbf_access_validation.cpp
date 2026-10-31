#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error{message};}
}
std::expected<void,std::string> validate_pbf_restrict(const std::filesystem::path& output){
    try{
        std::filesystem::create_directories(output);std::ofstream report{output/"pbf-restrict-tests.txt"};
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        ProfileOptions quiet;quiet.collect=true;quiet.gpu_timers=false;quiet.readback_period=0;
        const auto create=[&](const ExecutionOptions& execution){auto made=ParticleWorld::create(shaders,execution,quiet);require(made.has_value(),made?"":made.error().c_str());return std::move(*made);};
        unsigned rejected=0;
        for(bool soa:{false,true}){
            ExecutionOptions execution;execution.soa=soa;execution.pbf_restrict=true;auto world=create(execution);
            require(world->pbf_restrict_programs()==std::array{false,true,false,false,true},"Restrict did not select exactly correction and XSPH");
            auto base_execution=execution;base_execution.pbf_restrict=false;auto base=create(base_execution);
            const auto sources=world->pbf_program_sources_for_test(),base_sources=base->pbf_program_sources_for_test();
            for(unsigned op=0;op<sources.size();++op){
                auto normalized=sources[op];
                for(const auto& [from,to]:{std::pair{"readonly restrict buffer Input","readonly buffer Input"},
                                          std::pair{"writeonly restrict buffer Output","buffer Output"}}){
                    std::size_t at=0;unsigned count=0;
                    while((at=normalized.find(from,at))!=std::string::npos){normalized.replace(at,std::string_view{from}.size(),to);at+=std::string_view{to}.size();++count;}
                    require(count==((op==1||op==4)?2U:0U),"Actual GL shader qualifiers disagree with the declared scope");
                }
                require(normalized==base_sources[op],"Restrict changed more than the particle-bank qualifiers");
                if(op==1||op==4){
                    const auto prefix=std::string{soa?"soa":"aos"}+"-liquids-"+std::to_string(op);
                    std::ofstream original{output/(prefix+"-off.glsl")},qualified{output/(prefix+"-on.glsl")};
                    original<<base_sources[op];qualified<<sources[op];require(original.good()&&qualified.good(),"Cannot archive actual PBF shader source");
                }
            }
            std::vector<Grain> initial(258);std::vector<std::uint32_t> walls(width*height);
            for(unsigned i=0;i<initial.size();++i){const float x=80.F+static_cast<float>(i%20),y=50.F+static_cast<float>(i/20);
                initial[i].position={x,y,x,y};initial[i].motion={0,0,2.65F,5};}
            world->load(initial,walls);const auto before=world->readback();
            const auto faults=[&](){
                for(unsigned op:{1U,4U}){
                    for(unsigned source:{0U,1U})for(unsigned binding=0;binding<8;++binding)if(binding!=source){world->probe_pbf_alias_for_test(op,source,binding);++rejected;}
                    for(unsigned binding:{0U,1U}){world->probe_pbf_alias_for_test(op,8,binding);++rejected;}
                }
            };
            faults();
            const auto after=world->readback();
            for(unsigned id=0;id<capacity;++id)require(before[id].position==after[id].position&&before[id].motion==after[id].motion
                &&before[id].contact==after[id].contact&&before[id].residual==after[id].residual,"Rejected PBF fault changed authoritative particle data");
            world->project_liquids_for_test(1);faults(); // One Jacobi swap exercises the other physical bank.
            world->step();
            unsigned alive=0;for(const auto& p:world->readback())if(p.motion[3]>.5F){++alive;for(float value:p.position)require(std::isfinite(value),"Valid restricted PBF became nonfinite");}
            require(alive==initial.size(),"Restricted PBF lost a stable ID");
            world->reset_mission();world->queue_explosion({1,128,147,22,70});world->step();
            world->paint({200,60,204,60,3,Material::water});world->step();world->paint({200,60,204,60,7,Material::erase});world->step();
            require(world->pbf_restrict_programs()==std::array{false,true,false,false,true},"Editing changed program scope");
            report<<"layout="<<(soa?"soa":"aos")<<" scope=liquids_1,liquids_4 actual_GL_sources=qualifiers_only faults=64 rejected_before_GL; state_unchanged; valid_step,blast,spawn,erase=passed"<<std::endl;
        }
        auto disabled=create(ExecutionOptions{});require(disabled->pbf_restrict_programs()==std::array{false,false,false,false,false},"Default shaders gained qualifiers");
        auto fallback=ExecutionOptions::reference();fallback.pbf_restrict=true;auto reference=create(fallback);
        require(!reference->execution_options().effective_pbf_restrict()&&reference->pbf_restrict_programs()==std::array{false,false,false,false,false},"Ubershader request did not fall back to unqualified storage");
        reference->reset_mission();reference->step();
        require(rejected==128,"Missing PBF binding fault cases");require(glGetError()==GL_NO_ERROR,"OpenGL error in PBF access validation");
        report<<"rejected="<<rejected<<" default_off=passed ubershader_fallback=passed\nstatus=passed\n";
        require(report.good(),"Cannot write PBF access checks");return {};
    }catch(const std::exception& error){std::ofstream{output/"pbf-restrict-error.txt"}<<error.what();return std::unexpected{error.what()};}
}
}
