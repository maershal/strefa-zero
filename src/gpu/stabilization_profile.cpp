#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error{message};}
double percentile(std::vector<double> values,double fraction){
    std::ranges::sort(values);
    return values[std::min(values.size()-1,static_cast<std::size_t>(fraction*static_cast<double>(values.size())))];
}
void drain(ParticleWorld& world){glFinish();world.poll();}
void clear(ParticleWorld& world){
    static_cast<void>(world.take_timings());static_cast<void>(world.take_render_timings());
    static_cast<void>(world.take_cpu_timings());static_cast<void>(world.take_step_profiles());
    static_cast<void>(world.take_group_profiles());
}
void setup(ParticleWorld& world,unsigned count){
    if(count==3096){world.reset_mission();return;}
    // Match I04 coupling benchmarks: dry grains, radius/.5 centered lattice,
    // floor at y=187, capsule away from the pile at x=310.
    std::vector<Grain> particles(count);std::vector<std::uint32_t> walls(width*height);
    for(int y=187;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
    for(unsigned i=0;i<count;++i){
        const float x=2.5F+static_cast<float>(i%300),y=2.5F+static_cast<float>(i/300);
        particles[i].position={x,y,x,y};particles[i].motion={0,0,1,1};
    }
    world.load(particles,walls);MissionConfig config;config.spawn_x=310;world.configure_mission(config);
}
}
std::expected<void,std::string> profile_stabilization(ParticleWorld& source,const std::filesystem::path& output,const ProfileOptions& options){
    try{
        std::filesystem::create_directories(output);
        std::ofstream metadata{output/"environment.txt"},summary{output/"summary.csv"},validation{output/"validation.csv"},timeline{output/"phase-timeline.csv"};
        require(metadata&&summary&&validation&&timeline,"Cannot create stabilization profile output");
        timeline<<"scene,phase,sdl_before_ns,utc_unix_ns,sdl_after_ns\n";
        const auto phase=[&](const std::string& scene,const char* label){
            const auto before=SDL_GetTicksNS();
            const auto utc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            const auto after=SDL_GetTicksNS();
            timeline<<scene<<','<<label<<','<<before<<','<<utc<<','<<after<<'\n';timeline.flush();
        };
        const auto execution=source.execution_options();const auto mode=effective_timer_mode(options);
        for(const auto& [label,value]:{std::pair{"vendor",GL_VENDOR},std::pair{"renderer",GL_RENDERER},std::pair{"version",GL_VERSION},std::pair{"glsl",GL_SHADING_LANGUAGE_VERSION}}){
            const auto* text=glGetString(static_cast<GLenum>(value));metadata<<label<<'='<<(text?reinterpret_cast<const char*>(text):"unknown")<<'\n';
        }
        GLint flags{};glGetIntegerv(GL_CONTEXT_FLAGS,&flags);
        metadata<<"context_flags="<<flags<<"\nrender_width=960\nrender_height=576\ntick_seconds="<<std::setprecision(12)<<step_seconds
            <<"\nwarmup_ticks=120\nmeasured_ticks=240\nrender_samples=120\nrepetitions=1\nfresh_world_per_scene=1"
            <<"\nshader_compilation_excluded=1\nmode=unpaced; glFinish + HUD poll every 16 ticks outside cpu_submit timer, included in wall throughput"
            <<"\ngpu_timer_mode="<<timer_mode_name(mode)<<"\ngpu_timer_mode_requested="<<timer_mode_name(options.timer_mode)
            <<"\ngpu_timers_requested="<<options.gpu_timers<<"\ngroup_timers_requested="<<options.group_timers
            <<"\ngroup_timers_effective="<<(mode!=TimerMode::off&&options.group_timers)<<"\ncounters="<<options.collect
            <<"\nperiodic_diagnostics="<<options.periodic_diagnostics<<"\nbody_coupling="<<options.body_coupling
            <<"\nreadback_period="<<options.readback_period<<"\nreadback_mode_requested="<<readback_mode_name(options.readback_mode)
            <<"\nspecialized="<<execution.specialized<<"\ncached_bindings="<<execution.cached_bindings<<"\nfifo_queries="<<execution.fifo_queries
            <<"\nconditional_work="<<execution.conditional_work<<"\nlocal_size="<<execution.local_size<<"\nreduction_size="<<execution.reduction_size<<"\npbf_size_requested="<<execution.pbf_size<<"\npbf_size_effective="<<execution.effective_pbf_size()<<"\npbf_restrict_requested="<<execution.pbf_restrict<<"\npbf_restrict_effective="<<execution.effective_pbf_restrict()
            <<"\nneighbor_mode="<<static_cast<int>(execution.neighbors)<<"\ncoupling_mode="<<static_cast<int>(execution.coupling)<<"\nsoa="<<execution.soa
            <<"\nphase_gating="<<execution.phase_gating
            <<"\nphase_gating_effective="<<(execution.phase_gating&&execution.neighbors!=NeighborMode::linked)
            <<"\nskip_zero_projection="<<execution.skip_zero_projection<<'\n';
        metadata<<"neighbor_reads_requested="<<neighbor_reads_name(execution.neighbor_reads)<<"\nneighbor_reads_effective="<<neighbor_reads_name(effective_neighbor_reads(execution.neighbor_reads,execution.neighbors))<<'\n';
        metadata<<"storage_requested="<<device_storage_name(execution.storage)
            <<"\nstorage_effective="<<device_storage_name(source.upload_stats().mode)<<'\n';
#ifdef NDEBUG
        metadata<<"build=Release\n";
#else
        metadata<<"build=Debug; not a Release baseline\n";
#endif
        metadata<<"phase_note=timer_begin/end_ms are disjoint CPU query submission phases; staging_ms starts after timer_end_ms\n";
        metadata<<"timeline_note=phase boundaries sampled outside measured batches; UTC bracketed by SDL timestamps, driver callbacks may arrive asynchronously\n";
        metadata.flush();
        summary<<"scene,variant,repeat,metric,samples,available,median,p95,p99,max\n"<<std::setprecision(10);
        validation<<"scene,expected_particles,actual_particles,finite,steps,timer_mode\n";
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        for(const auto count:{3096U,16000U,32768U}){
            const std::string name=count==3096?"mission":(count==16000?"stress_16k":"stress_32k");
            phase(name,"initialization_begin");
            const auto create_started=SDL_GetTicksNS();
            auto made=ParticleWorld::create(shaders,execution,options);require(made.has_value(),made?"":made.error().c_str());
            auto measured=std::move(*made);
            metadata<<"scene="<<name<<" world_create_ms="<<static_cast<double>(SDL_GetTicksNS()-create_started)/1e6
                <<" initial_upload_writes="<<measured->upload_stats().writes<<'\n';
            const auto initial_reads=measured->readback_stats().submitted;
            metadata<<"scene="<<name<<" initial_readbacks_submitted="<<initial_reads<<'\n';
            if(options.readback_period==0)require(initial_reads==0,"Disabled readback performed startup copies");
            setup(*measured,count);drain(*measured);
            const auto ticks=[&](unsigned number){
                for(unsigned tick=0;tick<number;++tick){measured->step();if(tick%16==15)drain(*measured);}
                drain(*measured);
            };
            phase(name,"warmup_begin");ticks(120);phase(name,"warmup_end");clear(*measured);
            phase(name,"measurement_begin");
            const auto began=SDL_GetTicksNS();ticks(240);
            const double wall_ms=static_cast<double>(SDL_GetTicksNS()-began)/1e6;
            phase(name,"measurement_end");
            const auto metric=[&](const char* label,const std::vector<double>& values){
                summary<<name<<','<<timer_mode_name(mode)<<",1,"<<label<<','<<values.size()<<','<<(!values.empty());
                if(values.empty())summary<<",,,,\n";
                else summary<<','<<percentile(values,.5)<<','<<percentile(values,.95)<<','<<percentile(values,.99)<<','<<*std::ranges::max_element(values)<<'\n';
            };
            const auto physics=measured->take_timings(),cpu=measured->take_cpu_timings();
            require(cpu.size()==240,"Stabilization profile lost measured CPU ticks");
            if(mode==TimerMode::off)require(physics.empty(),"Off timer mode unexpectedly returned GPU timings");
            metric("gpu_step_ms",physics);metric("cpu_submit_ms",cpu);
            metric("batch_wall_ms",{wall_ms});metric("batch_mean_step_wall_ms",{wall_ms/240});
            metric("batch_ticks_per_second",{240000/wall_ms});
            const auto scene_output=output/name;std::filesystem::create_directories(scene_output);
            write_profile_samples(*measured,scene_output);
            static_cast<void>(measured->take_render_timings());
            phase(name,"render_begin");
            for(unsigned frame=0;frame<120;++frame){measured->render(1);if(frame%16==15)drain(*measured);}drain(*measured);
            phase(name,"render_end");
            metric("gpu_render_ms",measured->take_render_timings());
            // Full reads validate the result after every measurement, never
            // inside a timed batch. The debug log identifies this boundary.
            metadata<<"scene="<<name<<" phase=full_validation_after_measurement\n";metadata.flush();
            phase(name,"validation_begin");
            unsigned alive{};bool finite=true;
            for(const auto& particle:measured->readback())if(particle.motion[3]>.5F){
                ++alive;for(float value:particle.position)finite=finite&&std::isfinite(value);
                for(float value:particle.motion)finite=finite&&std::isfinite(value);
            }
            validation<<name<<','<<count<<','<<alive<<','<<finite<<','<<measured->metrics().steps<<','<<timer_mode_name(mode)<<'\n';validation.flush();summary.flush();
            require(alive==count&&finite,"Stabilization profile changed particle membership or produced nonfinite state");
            require(glGetError()==GL_NO_ERROR,"OpenGL error during stabilization profile");
            phase(name,"validation_end");
        }
        require(metadata.good()&&summary.good()&&validation.good()&&timeline.good(),"Cannot finish stabilization profile output");
        return {};
    }catch(const std::exception& error){
        std::ofstream{output/"stabilization-error.txt"}<<error.what();return std::unexpected{error.what()};
    }
}
}
