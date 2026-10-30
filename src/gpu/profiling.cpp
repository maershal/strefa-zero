#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error{message};}
double percentile(std::vector<double> values,double q){
    if(values.empty())return 0;
    std::ranges::sort(values);return values[std::min(values.size()-1,static_cast<std::size_t>(q*static_cast<double>(values.size())))];
}
void drain(ParticleWorld& world){glFinish();world.poll();}
void clear_samples(ParticleWorld& world){
    static_cast<void>(world.take_timings());static_cast<void>(world.take_cpu_timings());
    static_cast<void>(world.take_render_timings());static_cast<void>(world.take_step_profiles());static_cast<void>(world.take_group_profiles());
}
void ticks(ParticleWorld& world,unsigned count){
    for(unsigned i=0;i<count;++i){world.step();if(i%16==15)drain(world);}drain(world);
}
struct Variant {const char* name;ProfileOptions options;};
std::vector<Variant> variants(){
    ProfileOptions base;base.collect=true;base.readback_period=1;base.readback_mode=ReadbackMode::legacy;
    auto no_read=base;no_read.readback_period=0;
    auto rare=base;rare.readback_period=4;
    auto no_body=base;no_body.body_coupling=false;
    auto no_diag=base;no_diag.periodic_diagnostics=false;no_diag.gpu_timers=false;
    auto no_timers=base;no_timers.gpu_timers=false;
    auto no_counters=base;no_counters.collect=false;
    auto groups=base;groups.group_timers=true;
    return {{"baseline",base},{"no_readback",no_read},{"readback_4",rare},{"no_coupling",no_body},
        {"no_diagnostics",no_diag},{"no_timers",no_timers},{"no_counters",no_counters},{"group_timers",groups}};
}
void check_timer_modes(ParticleWorld& world,std::ostream& report){
    {
        ProfileOptions quiet;quiet.collect=true;quiet.gpu_timers=false;
        quiet.group_timers=true;quiet.readback_period=0;
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        auto created=ParticleWorld::create(shaders,world.execution_options(),quiet);
        require(created.has_value(),"Cannot create a world with initial profiling policy");
        auto& fresh=**created;
        require(fresh.readback_stats().submitted==0,"Readback-off performed copies during world creation");
        fresh.reset_mission();for(unsigned tick=0;tick<4;++tick){fresh.step();fresh.render(1);}drain(fresh);
        require(fresh.readback_stats().submitted==0,"Readback-off performed reset/runtime copies");
        require(fresh.counters().timer_submissions==0&&fresh.counters().query_polls==0&&fresh.counters().query_results==0,"Timer-off submitted queries during creation/reset/runtime");
        require(fresh.take_timings().empty()&&fresh.take_render_timings().empty()&&fresh.take_group_profiles().empty(),"Timer-off published startup samples");
        report<<"initial_profile_policy=passed (creation,reset,runtime: zero readbacks and queries)\n";
    }
    for(const auto mode:{TimerMode::elapsed,TimerMode::timestamp,TimerMode::off}){
        ProfileOptions options;options.collect=true;options.group_timers=true;options.timer_mode=mode;
        world.set_profile_options(options);world.reset_mission();drain(world);clear_samples(world);
        const auto before=world.counters();
        for(unsigned tick=0;tick<4;++tick){world.step();world.render(1);}
        drain(world);
        const auto calls=world.counters()-before;
        const auto physics=world.take_timings(),render=world.take_render_timings();
        const auto groups=world.take_group_profiles();
        if(mode==TimerMode::off){
            require(physics.empty()&&render.empty()&&groups.empty(),"Disabled timers produced GPU samples");
            require(calls.timer_submissions==0&&calls.query_polls==0&&calls.query_results==0,"Disabled timers still submit or poll queries");
        }else{
            require(physics.size()==4&&render.size()==4&&groups.size()==4,"Timer mode lost completed queries");
            for(const auto* samples:{&physics,&render})for(double ms:*samples)
                require(std::isfinite(ms)&&ms>=0,"Timer mode returned invalid elapsed time");
            // Leave queries in flight. Reconfiguration must neither reuse an
            // incompatible query target nor publish the previous mode's sample.
            world.step();world.render(1);options.gpu_timers=false;world.set_profile_options(options);
            const auto disabled_before=world.counters();world.step();drain(world);
            const auto disabled=world.counters()-disabled_before;
            require(disabled.timer_submissions==0&&disabled.query_polls==0&&disabled.query_results==0,"Legacy timer disable did not override group timers");
            require(world.take_timings().empty()&&world.take_render_timings().empty()&&world.take_group_profiles().empty(),"Old timer mode survived disable");
            world.set_profile_options(ProfileOptions{.collect=true,.group_timers=true,.timer_mode=mode});
            world.step();world.reset_mission();
            require(world.metrics().gpu_ms==0&&world.metrics().render_ms==0&&world.metrics().timing_samples==0,"Previous scene timing gauge survived reset");
            drain(world);
            require(world.take_timings().empty()&&world.take_group_profiles().empty(),"Old-generation query survived reset");
        }
        require(glGetError()==GL_NO_ERROR,"OpenGL query target error while changing timer modes");
        report<<"timer_mode_"<<timer_mode_name(mode)<<"=passed (physics, render, groups, off override, pending-mode change, reset)\n";
    }
}
void check_instrumentation(ParticleWorld& world,std::ostream& report){
    for(const auto& variant:variants()){
        drain(world);world.set_profile_options(variant.options);world.reset_mission();drain(world);clear_samples(world);
        for(unsigned i=0;i<4;++i){
            world.step();const auto p=world.step_profile();
            if(variant.options.collect){
                const bool diagnostic_due=i==0&&variant.options.periodic_diagnostics;
                const auto mode=world.execution_options().neighbors;
                const unsigned builds=mode==NeighborMode::reused?2U:(42U+(diagnostic_due?1U:0U));
                const unsigned bin_passes=mode==NeighborMode::linked?2U:5U;
                const auto coupling=world.execution_options().coupling;
                const unsigned reductions=coupling==CouplingMode::combined?26U:42U;
                const unsigned passes=coupling==CouplingMode::reference?1U:2U;
                const unsigned body_work=variant.options.body_coupling?2U+reductions*passes:0U;
                require(p.calls.dispatches==76U+body_work+builds*bin_passes+(diagnostic_due?2U:0U),"Incorrect measured dispatch count");
                require(p.calls.bins==builds,"Incorrect measured index rebuild count");
                require(p.calls.body_reductions==(variant.options.body_coupling?reductions:0U),"Incorrect measured body reduction count");
                require(p.calls.active_rebuilds==1,"Missing active-list dispatch accounting");
                require(p.calls.copy_bytes==p.calls.stage_copies*(32+sizeof(MissionSnapshot)),"Incorrect staging byte accounting");
                if(variant.options.readback_period==0)require(p.calls.copy_bytes==0&&p.calls.read_bytes==0,"Disabled readback still transfers data");
                require(p.total_ms>=p.poll_ms&&p.total_ms>=p.solver_ms,"Invalid CPU phase timings");
            }
            drain(world);
        }
        if(variant.options.group_timers){
            const auto groups=world.take_group_profiles();require(groups.size()==4,"Grouped GPU timestamps lost ticks");
            for(const auto& group:groups)for(double ms:group.milliseconds)require(std::isfinite(ms)&&ms>=0,"Invalid GPU group time");
        }
        const auto particles=world.readback();unsigned count=0;
        for(const auto& p:particles)if(p.motion[3]>.5F){++count;for(float x:p.position)require(std::isfinite(x),"Profiling changed state to nonfinite");}
        require(count==3096,"Profiling lost or created particles");
        if(variant.options.readback_period!=0){
            const auto epoch=world.mission().status[1];world.step();world.reset_mission();drain(world);
            require(world.mission().status[1]>epoch&&world.mission().status[2]==0,"Old profiling readback survived reset");
        }
        report<<variant.name<<"=passed\n";
    }
    check_timer_modes(world,report);
    world.set_profile_options({});world.reset_mission();drain(world);clear_samples(world);
    require(glGetError()==GL_NO_ERROR,"OpenGL error during instrumentation checks");
}
void setup_scene(ParticleWorld& world,unsigned count){
    if(count==3096){world.reset_mission();return;}
    std::vector<Grain> particles(count);std::vector<std::uint32_t> walls(width*height);
    for(int y=187;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
    for(unsigned i=0;i<count;++i){const float x=1.5F+static_cast<float>(i%300),y=1.5F+static_cast<float>(i/300);
        particles[i].position={x,y,x,y};particles[i].motion={0,0,1,1};}
    world.load(particles,walls);world.configure_mission({});
}
}

std::expected<void,std::string> profile_gpu(ParticleWorld& world,const std::filesystem::path& output,bool check_only){
    try{
        std::filesystem::create_directories(output);
        std::ofstream checks{output/"instrumentation-tests.txt"};check_instrumentation(world,checks);checks<<"status=passed\n";checks.close();
        if(check_only)return {};
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets"/"shaders";
        const auto fresh_world=[&](const ProfileOptions& options){auto fresh=ParticleWorld::create(shaders,world.execution_options(),options);if(!fresh)throw std::runtime_error{fresh.error()};return std::move(*fresh);};
        std::ofstream metadata{output/"environment.txt"};
        for(const auto& [label,value]:{std::pair{"vendor",GL_VENDOR},std::pair{"renderer",GL_RENDERER},std::pair{"version",GL_VERSION},std::pair{"glsl",GL_SHADING_LANGUAGE_VERSION}})
            metadata<<label<<'='<<reinterpret_cast<const char*>(glGetString(static_cast<GLenum>(value)))<<'\n';
        GLint flags{};glGetIntegerv(GL_CONTEXT_FLAGS,&flags);
        metadata<<"context_flags="<<flags<<"\nrender_width=960\nrender_height=576\ntick_seconds="<<step_seconds
            <<"\nwarmup_ticks=120\nmeasured_ticks=240\nrepetitions=3\nfresh_buffers_per_measurement=1\nmode=unpaced; validation drain every 16 ticks outside step timer\n";
#ifdef NDEBUG
        metadata<<"build=Release\n";
#else
        metadata<<"build=Debug; not a Release performance baseline\n";
#endif
        std::ofstream summary{output/"summary.csv"},steps{output/"steps.csv"},groups{output/"gpu-groups.csv"};
        summary<<"scene,variant,repeat,metric,samples,median,p95,p99,max\n";
        steps<<"scene,variant,repeat,generation,tick,total_ms,poll_ms,upload_ms,prepare_ms,solver_ms,diagnostics_ms,staging_ms,timer_poll_ms,fence_poll_ms,read_ms,readback_age_ticks";
#define COUNTER_HEADER(name) steps<<"," #name;
        STREFA_GPU_COUNTERS(COUNTER_HEADER)
#undef COUNTER_HEADER
        steps<<",timer_begin_ms,timer_end_ms,timer_mode\n";groups<<"scene,variant,repeat,generation,tick";for(auto name:gpu_group_names)groups<<','<<name<<"_ms";groups<<'\n';
        const auto write_metric=[&](unsigned count,const char* variant,unsigned repeat,const char* metric,const std::vector<double>& values){
            if(values.empty())return;
            summary<<count<<','<<variant<<','<<repeat<<','<<metric<<','<<values.size()<<','<<percentile(values,.5)<<','<<percentile(values,.95)<<','<<percentile(values,.99)<<','<<*std::ranges::max_element(values)<<'\n';
        };
        for(unsigned repeat=1;repeat<=3;++repeat){
            const auto choices=variants();
            // Reverse every other repetition to reduce systematic warm-up/order bias.
            for(std::size_t v=0;v<choices.size();++v){
                const auto& variant=choices[repeat%2==0?choices.size()-1-v:v];
                auto isolated=fresh_world(variant.options);auto& measured=*isolated;
                setup_scene(measured,3096);ticks(measured,120);clear_samples(measured);
                const auto batch_started=SDL_GetTicksNS();ticks(measured,240);
                const double batch_step_wall_ms=static_cast<double>(SDL_GetTicksNS()-batch_started)/1e6/240;
                write_metric(3096,variant.name,repeat,"batch_mean_step_wall_ms",{batch_step_wall_ms});
                const auto samples=measured.take_step_profiles();const auto gpu_groups=measured.take_group_profiles();
                write_metric(3096,variant.name,repeat,"gpu_step_ms",measured.take_timings());
                write_metric(3096,variant.name,repeat,"cpu_submit_ms",measured.take_cpu_timings());
                const auto phase=[&](const char* name,auto member){std::vector<double> values;for(const auto& sample:samples)values.push_back(sample.*member);write_metric(3096,variant.name,repeat,name,values);};
                phase("poll_ms",&StepProfile::poll_ms);phase("prepare_ms",&StepProfile::prepare_ms);phase("solver_submit_ms",&StepProfile::solver_ms);
                phase("diagnostics_submit_ms",&StepProfile::diagnostics_ms);phase("staging_submit_ms",&StepProfile::staging_ms);
                phase("timer_poll_ms",&StepProfile::timer_poll_ms);phase("fence_poll_ms",&StepProfile::fence_poll_ms);phase("read_ms",&StepProfile::read_ms);
                for(const auto& p:samples){
                    steps<<3096<<','<<variant.name<<','<<repeat<<','<<p.generation<<','<<p.tick<<','<<p.total_ms<<','<<p.poll_ms<<','<<p.upload_ms<<','<<p.prepare_ms<<','<<p.solver_ms<<','<<p.diagnostics_ms<<','<<p.staging_ms<<','<<p.timer_poll_ms<<','<<p.fence_poll_ms<<','<<p.read_ms<<','<<p.readback_age_ticks;
#define COUNTER_VALUE(name) steps<<','<<p.calls.name;
                    STREFA_GPU_COUNTERS(COUNTER_VALUE)
#undef COUNTER_VALUE
                    steps<<','<<p.timer_begin_ms<<','<<p.timer_end_ms<<','<<timer_mode_name(p.timer_mode)<<'\n';
                }
                for(const auto& p:gpu_groups){groups<<3096<<','<<variant.name<<','<<repeat<<','<<p.generation<<','<<p.tick;for(double ms:p.milliseconds)groups<<','<<ms;groups<<'\n';}
                static_cast<void>(measured.take_render_timings());
                for(unsigned frame=0;frame<120;++frame){measured.render(1);if(frame%16==15)drain(measured);}drain(measured);
                write_metric(3096,variant.name,repeat,"gpu_render_ms",measured.take_render_timings());
                summary.flush();steps.flush();groups.flush();
            }
            for(unsigned count:{16000U,32768U}){
                auto isolated=fresh_world(variants().front().options);auto& measured=*isolated;
                setup_scene(measured,count);ticks(measured,120);clear_samples(measured);ticks(measured,240);
                write_metric(count,"baseline",repeat,"gpu_step_ms",measured.take_timings());write_metric(count,"baseline",repeat,"cpu_submit_ms",measured.take_cpu_timings());
            }
        }
        world.set_profile_options({});return {};
    }catch(const std::exception& e){std::ofstream{output/"profiling-error.txt"}<<e.what();return std::unexpected{e.what()};}
}
void write_profile_samples(ParticleWorld& world,const std::filesystem::path& output){
    std::ofstream steps{output/"window-steps.csv"},groups{output/"window-gpu-groups.csv"};
    steps<<"generation,tick,total_ms,poll_ms,upload_ms,prepare_ms,solver_ms,diagnostics_ms,staging_ms,timer_poll_ms,fence_poll_ms,read_ms,readback_age_ticks";
#define WRITE_HEADER(name) steps<<"," #name;
    STREFA_GPU_COUNTERS(WRITE_HEADER)
#undef WRITE_HEADER
    steps<<",timer_begin_ms,timer_end_ms,timer_mode\n";
    for(const auto& p:world.take_step_profiles()){
        steps<<p.generation<<','<<p.tick<<','<<p.total_ms<<','<<p.poll_ms<<','<<p.upload_ms<<','<<p.prepare_ms<<','<<p.solver_ms<<','<<p.diagnostics_ms<<','<<p.staging_ms<<','<<p.timer_poll_ms<<','<<p.fence_poll_ms<<','<<p.read_ms<<','<<p.readback_age_ticks;
#define WRITE_VALUE(name) steps<<','<<p.calls.name;
        STREFA_GPU_COUNTERS(WRITE_VALUE)
#undef WRITE_VALUE
        steps<<','<<p.timer_begin_ms<<','<<p.timer_end_ms<<','<<timer_mode_name(p.timer_mode)<<'\n';
    }
    groups<<"generation,tick";for(auto name:gpu_group_names)groups<<','<<name<<"_ms";groups<<'\n';
    for(const auto& p:world.take_group_profiles()){groups<<p.generation<<','<<p.tick;for(double ms:p.milliseconds)groups<<','<<ms;groups<<'\n';}
    const auto calls=world.counters();std::ofstream totals{output/"window-counters.txt"};
#define WRITE_TOTAL(name) totals<<#name "="<<calls.name<<'\n';
    STREFA_GPU_COUNTERS(WRITE_TOTAL)
#undef WRITE_TOTAL
    const auto options=world.profile_options();
    totals<<"gpu_timer_mode="<<timer_mode_name(effective_timer_mode(options))<<"\ngpu_timers_requested="<<options.gpu_timers
        <<"\ngroup_timers_requested="<<options.group_timers<<"\nreadback_period="<<options.readback_period<<'\n';
    const auto execution=world.execution_options();
    totals<<"specialized="<<execution.specialized<<"\ncached_bindings="<<execution.cached_bindings<<"\nfifo_queries="<<execution.fifo_queries
        <<"\ncoupling_mode="<<static_cast<int>(execution.coupling)<<"\nneighbor_mode="<<static_cast<int>(execution.neighbors)<<"\nsoa="<<execution.soa
        <<"\nconditional_work="<<execution.conditional_work<<"\nlocal_size="<<execution.local_size<<"\nreduction_size="<<execution.reduction_size<<'\n';
    const auto stats=world.readback_stats();
    totals<<"readback_mode="<<readback_mode_name(stats.mode)<<"\nreadback_capacity="<<stats.capacity<<"\nreadback_pending="<<stats.pending
        <<"\nreadback_max_pending="<<stats.max_pending<<"\nreadback_submitted="<<stats.submitted<<"\nreadback_completed="<<stats.completed
        <<"\nreadback_dropped="<<stats.dropped<<"\nreadback_stale="<<stats.stale<<"\nreadback_latency_ticks="<<stats.last_latency_ticks<<'\n';
    const auto upload=world.upload_stats();
    totals<<"neighbor_reads_requested="<<neighbor_reads_name(execution.neighbor_reads)<<"\nneighbor_reads_effective="<<neighbor_reads_name(effective_neighbor_reads(execution.neighbor_reads,execution.neighbors))<<"\nhot_cache_bytes="<<(effective_neighbor_reads(execution.neighbor_reads,execution.neighbors)==NeighborReads::sorted?capacity*64:0)<<'\n';
    totals<<"storage_requested="<<device_storage_name(execution.storage)<<"\nstorage_effective="<<device_storage_name(upload.mode)
        <<"\nupload_writes="<<upload.writes<<"\nupload_payload_bytes="<<upload.bytes<<"\nupload_copies="<<upload.copies
        <<"\nupload_fence_polls="<<upload.fence_polls<<"\nupload_busy_orphans="<<upload.busy_orphans
        <<"\nupload_allocations="<<upload.allocations<<"\nupload_staging_bytes="<<upload.staging_bytes
        <<"\nupload_staging_peak_bytes="<<upload.staging_peak_bytes<<"\ndevice_buffer_bytes="<<upload.device_bytes<<'\n';
}
}
