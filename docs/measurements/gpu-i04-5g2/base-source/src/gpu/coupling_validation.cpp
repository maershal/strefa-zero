#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error{message};}
void drain(ParticleWorld& world){glFinish();world.poll();}
double percentile(std::vector<double> values,double q){
    std::ranges::sort(values);return values.empty()?0:values[std::min(values.size()-1,static_cast<std::size_t>(q*static_cast<double>(values.size())))];
}
void clear(ParticleWorld& world){
    static_cast<void>(world.take_timings());static_cast<void>(world.take_cpu_timings());
    static_cast<void>(world.take_step_profiles());static_cast<void>(world.take_group_profiles());
}
std::vector<std::uint32_t> floor_at(int y){std::vector<std::uint32_t> walls(width*height);for(int j=y;j<height;++j)for(int x=0;x<width;++x)walls[j*width+x]=1;return walls;}
Grain particle(float x,float y,unsigned kind){Grain p;p.position={x,y,x,y};p.motion={0,0,kind==5?2.65F:1.F,static_cast<float>(kind)};return p;}
void scene(ParticleWorld& world,unsigned count){
    if(count==3096){world.reset_mission();return;}
    if(count==1443){
        std::vector<Grain> particles;for(int y=140;y<179;++y)for(int x=112;x<149;++x)particles.push_back(particle(x+.53F,y+.47F,y<160?5U:1U));
        world.load(particles,floor_at(180));MissionConfig config;config.spawn_x=130;config.spawn_y=145;world.configure_mission(config);return;
    }
    std::vector<Grain> particles(count);for(unsigned i=0;i<count;++i)particles[i]=particle(2.5F+static_cast<float>(i%300),2.5F+static_cast<float>(i/300),1);
    world.load(particles,floor_at(187));MissionConfig config;config.spawn_x=310;world.configure_mission(config);
}
}
std::expected<void,std::string> validate_coupling(const std::filesystem::path& output,bool benchmark,bool reference_control,bool legacy_check,const ExecutionOptions& layout){
    try {
        std::filesystem::create_directories(output);
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        const auto create=[&](const ExecutionOptions& options){auto made=ParticleWorld::create(shaders,options);if(!made)throw std::runtime_error{made.error()};return std::move(*made);};
        if(!benchmark){
        std::ofstream checks{output/"coupling-tests.txt"};
        checks<<"pbf_size="<<layout.effective_pbf_size()<<" particle_size="<<layout.local_size<<" reduction_size="<<layout.reduction_size<<std::endl;
        std::unique_ptr<ParticleWorld> retained;
        for(auto mode:{CouplingMode::local,CouplingMode::combined})for(unsigned local:{64U,128U,256U}){
            if(mode==CouplingMode::local&&local!=256)continue;
            const auto started=SDL_GetTicksNS();auto options=layout;options.coupling=mode;options.reduction_size=local;auto world=create(options);
            checks<<"world_ready mode="<<static_cast<int>(mode)<<" group="<<local<<" seconds="<<static_cast<double>(SDL_GetTicksNS()-started)/1e9<<std::endl;
            for(unsigned count:{0U,1U,local-1,local,local+1,static_cast<unsigned>(capacity),static_cast<unsigned>(2*capacity)}){
                world->load({},floor_at(187));MissionConfig config;config.spawn_x=160;config.spawn_y=100;world->configure_mission(config);
                std::vector<std::array<float,4>> input(count);std::array<double,4> expected{};
                for(unsigned i=0;i<count;++i){input[i]={i%2?.000244140625F:-.0001220703125F,-.0001220703125F,i%11==0?1.F:0.F,0};for(unsigned k=0;k<4;++k)expected[k]+=input[i][k];}
                const auto actor_before=world->read_mission().actor;
                checks<<"case mode="<<static_cast<int>(mode)<<" group="<<local<<" count="<<count<<std::endl;world->inject_body_for_test(input,false);const auto queued=world->read_body();
                require(queued.queue[0]==count&&queued.queue[2]==0,"Compact queue lost a reaction or overflowed");
                require(queued.commands[0]==(count+local-1)/local&&queued.commands[3]==(count>local?1U:0U),"Actual indirect reduction arguments are incomplete");
                require(queued.commands[1]==1&&queued.commands[2]==1&&queued.commands[4]==1&&queued.commands[5]==1,"Indirect reduction has invalid Y/Z dimensions");
                world->inject_body_for_test({});const auto result=world->read_body();checks<<"queue="<<result.queue[0]<<" commands="<<result.commands[0]<<","<<result.commands[3]<<" stats="<<result.stats[0]<<","<<result.stats[1]<<","<<result.stats[2]<<","<<result.stats[3]<<std::endl;
                if(result.commands[0]||result.commands[3]){world->inject_body_for_test({});const auto replay=world->read_body();checks<<"replay count="<<replay.queue[0]<<" commands="<<replay.commands[0]<<","<<replay.commands[3]<<" batches="<<replay.stats[1]<<std::endl;}
                require(result.queue[0]==0&&result.queue[2]==0&&result.commands[0]==0&&result.commands[3]==0,"Reduction did not release pending work");
                require(result.stats[0]==count&&result.stats[1]==(count?1U:0U),"Reduction duplicated or discarded a committed batch");
                require(result.stats[2]==(count+local-1)/local&&result.stats[3]==(count>local?1U:0U),"Hierarchical reduction did not execute the required groups");
                for(unsigned k=0;k<4;++k)require(std::abs(static_cast<double>(result.last_sum[k])-expected[k])<.00001,"GPU reduction differs from independent double-precision sum");
                const auto actor_after=world->read_mission().actor;
                for(unsigned k=0;k<2;++k)require(std::abs((static_cast<double>(actor_after[k])-actor_before[k])-expected[k])<.00001,"Committed reaction was not applied exactly to the actor");
                if(count)require(world->read_mission().locomotion[2]>.5F,"Support-only reaction was lost");
                world->inject_body_for_test({});require(world->read_body().stats==result.stats,"Empty replay applied a previous reaction batch");
            }
            world->load({},floor_at(187));MissionConfig air;air.spawn_y=80;world->configure_mission(air);
            const std::array<std::array<float,4>,2> support{{{0,0,0,0},{0,0,1,0}}};world->inject_body_for_test(support);
            require(world->read_body().stats[0]==1&&world->read_mission().locomotion[2]>.5F,"Zero XY or support-only reaction was handled incorrectly");
            const std::array<std::array<float,4>,1> pending{{{.25F,-.125F,1,0}}};world->inject_body_for_test(pending,false);
            require(world->read_body().queue[0]==1,"Reset test did not seed an in-flight reaction");world->reset_mission();world->step();
            require(world->read_body().queue[0]==0&&world->read_body().stats[0]==0,"Old-generation reaction affected a reset world");
            bool rejected=false;try {std::vector<std::array<float,4>> too_many(2*capacity+1);world->inject_body_for_test(too_many);}catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"Oversized validation batch was silently accepted");
            checks<<"mode="<<static_cast<int>(mode)<<" group="<<local<<" empty,tails,full_two_channels,hierarchy,support,replay,reset=passed\n";checks.flush();
            if(mode==CouplingMode::local&&local==256)retained=std::move(world);
        }
        // Both producers must see the same current pose in the local sequential
        // variant. Relocating the capsule also checks that no old candidate cache
        // can suppress a newly entered contact.
        std::vector<std::array<float,4>> reference_actor;
        std::vector<Grain> reference_particles;
        double control_mean=0,control_actor=0,control_center=0;
        checks<<"geometry_target="<<(reference_control?"identical_reference_control":"local")<<std::endl;
        checks<<"geometry_acceptance="<<(reference_control||legacy_check?"legacy_ratio":"paired_checkpoint_replay; legacy90 metrics diagnostic")<<std::endl;
        for(unsigned run=0;run<3;++run){
            const bool local=run==2&&!reference_control;auto options=layout;options.coupling=local?CouplingMode::local:CouplingMode::reference;
            auto world=local?std::move(retained):create(options);
            std::vector<Grain> particles;for(int y=140;y<179;++y)for(int x=112;x<149;++x)particles.push_back(particle(x+.53F,y+.47F,y<160?5U:1U));
            world->load(particles,floor_at(180));MissionConfig config;config.spawn_x=130;config.spawn_y=145;world->configure_mission(config);
            double actor_error=0;
            for(unsigned tick=0;tick<90;++tick){
                if(tick==45){config.spawn_x=139;config.spawn_y=151;world->configure_mission(config);}
                world->step({tick<45?.4F:-.3F,170,140,0});if(tick%16==15)drain(*world);
                const auto actor=world->read_mission().actor;
                for(float value:actor)require(std::isfinite(value),"Continuous coupling route produced a nonfinite actor");
                if(run==0)reference_actor.push_back(actor);
                else actor_error=std::max(actor_error,static_cast<double>(std::hypot(actor[0]-reference_actor[tick][0],actor[1]-reference_actor[tick][1])));
            }
            const auto state=world->readback();if(run==0)reference_particles=state;
            else {
                double sum=0,dx=0,dy=0;unsigned alive=0;
                for(unsigned i=0;i<capacity;++i){require(state[i].motion[3]==reference_particles[i].motion[3],"Coupling changed stable membership");if(state[i].motion[3]>.5F){++alive;
                    for(const auto& values:{state[i].position,state[i].motion,state[i].contact,state[i].residual})for(float value:values)require(std::isfinite(value),"Continuous coupling route produced nonfinite particles");
                    const double x=state[i].position[0]-reference_particles[i].position[0],y=state[i].position[1]-reference_particles[i].position[1];sum+=std::hypot(x,y);dx+=x;dy+=y;}}
                const double mean=sum/std::max(1U,alive),center=std::hypot(dx,dy)/std::max(1U,alive);
                checks<<"geometry_run="<<run<<" mean="<<mean<<" center="<<center<<" actor_max="<<actor_error<<std::endl;
                if(run==1){control_mean=mean;control_actor=actor_error;control_center=center;}
                else {
                    const bool legacy_pass=mean<std::max(.05,2*control_mean)&&actor_error<std::max(.15,2*control_actor)&&center<std::max(.02,2*control_center);
                    checks<<"legacy90_pass="<<legacy_pass<<" mean_limit="<<std::max(.05,2*control_mean)<<" center_limit="<<std::max(.02,2*control_center)<<" actor_limit="<<std::max(.15,2*control_actor)<<std::endl;
                    if(reference_control||legacy_check){
                        require(mean<std::max(.05,2*control_mean),"Local particle difference exceeds reference repeat variation");
                        require(actor_error<std::max(.15,2*control_actor),"Local actor trajectory exceeds reference repeat variation");
                        require(center<std::max(.02,2*control_center),"Local aggregate particle displacement exceeds reference variation");
                    }
                    if(local){const auto stats=world->read_body();require(stats.stats[0]>0&&stats.queue[2]==0,"Geometry comparison exercised no local reactions");}
                }
            }
        }
        if(!reference_control&&!legacy_check)validate_coupling_replay(output,checks,layout);
        require(glGetError()==GL_NO_ERROR,"OpenGL error during coupling validation");checks<<"status=passed\n";checks.close();
        return {};
        }
        struct Variant {const char* name;CouplingMode mode;};
        std::vector<Variant> variants{{"i03_reference",CouplingMode::reference},{"local",CouplingMode::local},{"combined",CouplingMode::combined}};
        struct Case {const char* name;unsigned count;bool route;};
        const std::array cases{Case{"mission",3096,false},Case{"stress_16k",16000,false},Case{"stress_32k",32768,false},Case{"mission_route",3096,true},Case{"coupled_tank",1443,false}};
        std::ofstream summary{output/"summary.csv"},steps{output/"steps.csv"},progress{output/"progress.txt"},environment{output/"environment.txt"};
        environment<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nversion="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<"\nrepeats=3\nwarmup=120\nmeasured=240\nfresh_world_per_variant=1\nreadback=auto,period=2\ngroup_timers=1\nlayout=soa\nneighbors=reused\nmode=unpaced; glFinish and HUD poll every 16 ticks outside CPU step timer\n";
        summary<<"scene,variant,repeat,metric,samples,median,p95,p99,max\n";
        steps<<"scene,variant,repeat,tick,total_ms,poll_ms,solver_ms";
#define HEADER(name) steps<<"," #name;
        STREFA_GPU_COUNTERS(HEADER)
#undef HEADER
        steps<<'\n';
        const auto metric=[&](const char* name,const char* variant,unsigned repeat,const char* metric_name,const std::vector<double>& values){
            if(values.empty())return;summary<<name<<','<<variant<<','<<repeat<<','<<metric_name<<','<<values.size()<<','<<percentile(values,.5)<<','<<percentile(values,.95)<<','<<percentile(values,.99)<<','<<*std::ranges::max_element(values)<<'\n';
        };
        for(unsigned repeat=1;repeat<=3;++repeat){
            if(repeat%2==0)std::ranges::reverse(variants);
            for(const auto& variant:variants)for(const auto& test:cases){
                progress<<"repeat="<<repeat<<" variant="<<variant.name<<" scene="<<test.name<<'\n';progress.flush();
                const auto create_started=SDL_GetTicksNS();auto options=layout;options.coupling=variant.mode;auto world=create(options);progress<<"world_ready_seconds="<<static_cast<double>(SDL_GetTicksNS()-create_started)/1e9<<std::endl;ProfileOptions profile;profile.collect=true;profile.group_timers=true;world->set_profile_options(profile);scene(*world,test.count);
                std::vector<double> external;
                const auto run=[&](unsigned ticks){for(unsigned i=0;i<ticks;++i){world->step(test.route?scripted_mission_input(static_cast<unsigned>(world->metrics().steps)):MissionInput{});if(i%16==15){glFinish();const auto began=SDL_GetTicksNS();world->poll();external.push_back(static_cast<double>(SDL_GetTicksNS()-began)/1e6);}}drain(*world);};
                run(120);BodySnapshot initial_body;if(variant.mode!=CouplingMode::reference)initial_body=world->read_body();clear(*world);external.clear();const auto began=SDL_GetTicksNS();run(240);
                metric(test.name,variant.name,repeat,"batch_mean_step_wall_ms",{static_cast<double>(SDL_GetTicksNS()-began)/1e6/240});
                metric(test.name,variant.name,repeat,"external_poll_ms",external);metric(test.name,variant.name,repeat,"gpu_step_ms",world->take_timings());metric(test.name,variant.name,repeat,"cpu_submit_ms",world->take_cpu_timings());
                const auto samples=world->take_step_profiles();require(samples.size()==240,"Benchmark lost CPU samples");
                const auto groups=world->take_group_profiles();require(groups.size()==240,"Benchmark lost GPU group samples");
                for(std::size_t i=0;i<gpu_group_names.size();++i){std::vector<double> values;for(const auto& group:groups)values.push_back(group.milliseconds[i]);metric(test.name,variant.name,repeat,((std::string{"gpu_"}+std::string{gpu_group_names[i]})+"_ms").c_str(),values);}
                for(const auto& p:samples){steps<<test.name<<','<<variant.name<<','<<repeat<<','<<p.tick<<','<<p.total_ms<<','<<p.poll_ms<<','<<p.solver_ms;
#define VALUE(name) steps<<','<<p.calls.name;
                    STREFA_GPU_COUNTERS(VALUE)
#undef VALUE
                    steps<<'\n';}
                if(variant.mode!=CouplingMode::reference){const auto state=world->read_body();metric(test.name,variant.name,repeat,"body_measured_reactions",{static_cast<double>(state.stats[0]-initial_body.stats[0])});if(std::string_view{test.name}=="coupled_tank")require(state.stats[0]>initial_body.stats[0],"Coupled benchmark measured no body reactions");require(state.queue[0]==0&&state.queue[2]==0,"Benchmark left reactions pending or overflowed");metric(test.name,variant.name,repeat,"body_peak_reactions",{static_cast<double>(state.queue[1])});metric(test.name,variant.name,repeat,"body_total_reactions",{static_cast<double>(state.stats[0])});metric(test.name,variant.name,repeat,"body_applied_batches",{static_cast<double>(state.stats[1])});metric(test.name,variant.name,repeat,"body_groups",{static_cast<double>(state.stats[2])});metric(test.name,variant.name,repeat,"body_final_passes",{static_cast<double>(state.stats[3])});}
                summary.flush();steps.flush();
            }
            if(repeat%2==0)std::ranges::reverse(variants);
        }
        return {};
    }catch(const std::exception& e){std::ofstream{output/"coupling-error.txt"}<<e.what();return std::unexpected{e.what()};}
}
}
