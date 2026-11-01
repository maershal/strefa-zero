#include "gpu/world.hpp"
#include "gpu/render_device.hpp"
#include "gpu/thermal_system.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/scene_renderer.hpp"
#include "gpu/readback_ring.hpp"
#include "gpu/event_ring.hpp"
#include "gpu/upload_ring.hpp"
#include "gpu/dispatch_layout.hpp"
#include "gpu/diagnostic_trace.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <bit>
#include <fstream>
#include <deque>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <limits>
#include <sstream>
#include <iomanip>
#include <locale>

namespace strefa::gpu {
namespace {
using detail::Api;
struct ParticleAliasError:std::logic_error {using std::logic_error::logic_error;};
void require_disjoint_particle_bases(const std::array<GLuint,8>& bases){
    for(unsigned particle=0;particle<2;++particle){
        if(!bases[particle])throw ParticleAliasError{"Restricted PBF has an unbound particle bank."};
        for(unsigned other=0;other<bases.size();++other)
            if(other!=particle&&bases[particle]==bases[other])throw ParticleAliasError{"Restricted PBF particle bank aliases another SSBO binding."};
    }
}
std::uint32_t hash(std::uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;return x^(x>>16);}
std::array<float,2> seeded_position(int x,int y){
    const auto id=static_cast<std::uint32_t>(y*width+x);
    return {x+.5F+(static_cast<float>(hash(2*id)&65535U)/65535.F-.5F)*.12F,
            y+.5F+(static_cast<float>(hash(2*id+1)&65535U)/65535.F-.5F)*.12F};
}
}
struct ParticleWorld::Impl {
    std::filesystem::path shader_folder;
    std::shared_ptr<RenderDevice> resources;
    explicit Impl(std::shared_ptr<RenderDevice> value):resources{std::move(value)}{}
    detail::DeviceContext& device{resources->context};
    Api& gl{device.gl};
    detail::ShaderLibrary& shader_library{resources->library};
    detail::SceneRenderer renderer{gl,device.state};
    std::unique_ptr<detail::ThermalSystem> thermal;ThermalOptions thermal_config;ThermalBrush thermal_brush;
    detail::UploadRing uploads{gl};
    detail::ReadbackRing readbacks{gl};
    detail::EventRing event_ring{gl};
    game::GameplaySession gameplay;
    detail::DiagnosticTrace trace{gl};
    bool trace_step{},hold_events{}; // hold_events is validation-only fault injection
    std::uint64_t readback_revision{1};
    bool readback_refresh{};
    bool debug_groups{};
    ProfileOptions profile_options;
    CallCounters calls;
    StepProfile last_profile;
    std::vector<StepProfile> profiles;
    std::vector<GroupProfile> group_profiles;
    struct GroupTimer {std::array<GLuint,6> ids{};bool pending{};std::uint64_t tick{};unsigned generation{};};
    std::array<GroupTimer,32> group_timers{};
    unsigned group_cursor{};
    std::uint64_t readback_age{};
    double poll_timer_ms{},poll_fence_ms{},poll_read_ms{};
    ExecutionOptions execution;
    struct Kernel {
        GLuint id{};unsigned local_size{};
        bool particle_restrict{};
        GLint operation{-1},dirty{-1},substep{-1},stroke{-1},radius{-1},material{-1},input{-1},coupling{-1},test_count{-1};
        GLint hot_read{-1},hot_write{-1},hot_valid{-1};
        int hot_read_value{-2},hot_write_value{-2},hot_valid_value{-2};
        unsigned family{};
        int dirty_value{-1};
    };
    std::array<Kernel,21> granular;
    std::array<Kernel,7> liquids;
    std::array<Kernel,8> neighbors;
    bool bins_valid{};
    NeighborReads read_mode()const{return effective_neighbor_reads(execution.neighbor_reads,execution.neighbors);}
    std::string fluid_defines()const{
        const auto& f=execution.fluid;std::ostringstream out;out.imbue(std::locale::classic());
        out<<std::scientific<<std::setprecision(std::numeric_limits<float>::max_digits10)
            <<"#define FLUID_BOUNDARY_VOLUME "<<(f.boundary==FluidBoundary::volume?1:0)<<'\n'
            <<"#define FLUID_SURFACE "<<(f.surface==FluidSurface::akinci2d?1:0)<<'\n'
            <<"#define FLUID_FIELDS "<<(f.fields()?1:0)<<'\n'
            <<"#define FLUID_SUBSTEPS "<<f.substeps<<'\n'
            <<"#define FLUID_SCORR "<<f.s_corr<<'\n'
            <<"#define FLUID_TENSION "<<f.tension_scale<<'\n'
            <<"#define FLUID_CURVATURE "<<f.curvature_scale<<'\n'
            <<"#define FLUID_ADHESION "<<f.adhesion<<'\n'
            <<"#define FLUID_VORTICITY "<<f.vorticity<<'\n';
        return out.str();
    }
    std::string layout_defines()const{return fluid_defines()+std::string{"#define PARTICLE_SOA "}+(execution.soa?"1\n":"0\n")+"#define SORTED_NEIGHBORS "+(execution.neighbors!=NeighborMode::linked?"1\n":"0\n")+"#define REUSE_NEIGHBORS "+(execution.neighbors==NeighborMode::reused?"1\n":"0\n")+"#define COUPLING_MODE "+std::to_string(static_cast<int>(execution.coupling))+"\n#define REDUCTION_LOCAL_SIZE "+std::to_string(execution.reduction_size)+"u\n#define PHASE_GATING "+(execution.phase_gating?"1\n":"0\n")+"#define SKIP_ZERO_PROJECTION "+(execution.skip_zero_projection?"1\n":"0\n")+"#define HOT_READS "+std::to_string(static_cast<int>(read_mode()))+"\n";}
    std::array<Kernel,9> mission_kernels;
    std::array<Kernel,3> coupling_kernels;
    GLuint& bound_program{device.state.program};GLuint& bound_indirect{device.state.indirect};
    std::array<GLuint,8>& bound_bases{device.state.bases};
    std::array<unsigned,3> presented_hp{3,3,3};std::array<std::uint64_t,3> hit_started{};
    unsigned pbf_fault_source{8},pbf_fault_binding{8}; // validation only; 8 means no fault
    Stroke brush;
    int half_step{};
    void invalidate_bindings(){device.state.invalidate();}
    void compile_kernels(const std::filesystem::path& shaders){
        const auto compile=[&](auto& kernels,const char* file,std::initializer_list<unsigned> operations){
            for(unsigned op:operations){
                if(std::string_view{file}=="liquids.comp"&&op>=5&&!execution.fluid.fields())continue;
                const bool separate=execution.specialized||(std::string_view{file}=="neighbors.comp"||std::string_view{file}=="coupling.comp");
                if(!separate&&op!=*operations.begin())continue;
                auto& k=kernels[separate?op:0];
                const bool scalar=(std::string_view{file}=="granular.comp"&&(op==10||op==12||op==20))
                    ||(std::string_view{file}=="liquids.comp"&&op==3)
                    ||(std::string_view{file}=="mission.comp"&&(op==0||op==1||op==4));
                k.local_size=execution.specialized&&scalar?1U:execution.local_size;
                if(std::string_view{file}=="liquids.comp"&&!(execution.specialized&&scalar))k.local_size=execution.effective_pbf_size();
                if(std::string_view{file}=="mission.comp"&&op==3&&execution.specialized)k.local_size=execution.reduction_size;
                if(std::string_view{file}=="coupling.comp")k.local_size=execution.reduction_size;
                auto defines="#define LOCAL_SIZE "+std::to_string(k.local_size)+"\n#define PARTICLE_LOCAL_SIZE "+std::to_string(execution.local_size)+"u\n#define GPU_CONDITIONAL_WORK "+(execution.conditional_work?"1\n":"0\n");
                defines+=layout_defines();
                defines+="#define PBF_LOCAL_SIZE "+std::to_string(execution.effective_pbf_size())+"u\n";
                if(separate)defines+="#define PASS_OPERATION "+std::to_string(op)+"\n#define PASS_"+std::string{file}.substr(0,std::string{file}.find('.'))+"_"+std::to_string(op)+" 1\n";
                if(std::string_view{file}=="neighbors.comp")k.local_size=256;
                k.particle_restrict=execution.effective_pbf_restrict()&&std::string_view{file}=="liquids.comp"&&(op==1||op==4);
                k.id=shader_library.program({{GL_COMPUTE_SHADER,shaders/file}},defines,k.particle_restrict);
                const auto location=[&](const char* name){return gl.GetUniformLocation(k.id,name);};
                k.operation=location("operation");k.dirty=location("dirtyOnly");k.substep=location("substep");
                k.stroke=location("stroke");k.radius=location("brushRadius");k.material=location("material");
                k.input=location("inputControl");k.coupling=location("disableBodyCoupling");k.test_count=location("bodyTestCount");
                k.hot_read=location("hotReadBank");k.hot_write=location("hotWriteBank");k.hot_valid=location("hotValid");
                k.family=std::string_view{file}=="granular.comp"?0U:std::string_view{file}=="mission.comp"?1U:2U;
                if(debug_groups){const auto label=std::string{file}+" / "+std::to_string(op)+" / "+std::to_string(k.local_size);gl.ObjectLabel(GL_PROGRAM,k.id,-1,label.c_str());}
            }
        };
        compile(granular,"granular.comp",{0,1,2,3,4,5,7,8,9,10,11,12,19,20});
        compile(liquids,"liquids.comp",{0,1,2,3,4,5,6});
        if(execution.neighbors!=NeighborMode::linked)compile(neighbors,"neighbors.comp",{0,1,2,3,4,5,6,7});
        compile(mission_kernels,"mission.comp",{0,1,3,4,5,6,7,8});
        if(execution.coupling!=CouplingMode::reference)compile(coupling_kernels,"coupling.comp",{0,1,2});
    }
    void configure_hot(Kernel& k,bool in_place,unsigned op){
        const auto set=[&](GLint location,int value,int& cached){if(location>=0&&value!=cached){gl.Uniform1i(location,value);cached=value;}};
        set(k.hot_read,static_cast<int>(current),k.hot_read_value);
        set(k.hot_write,static_cast<int>(in_place?current:1-current),k.hot_write_value);
        // New/free IDs have no reverse map until the next index build. Never
        // publish them into an old entry, including in-place spawning.
        const bool edits=(k.family==0&&(op==5||op==8||op==19))||(k.family==1&&op==6);
        set(k.hot_valid,bins_valid&&!edits?1:0,k.hot_valid_value);
    }
    Kernel& prepare(auto& kernels,int op,bool in_place=false,bool separate=false){
        auto& k=kernels[execution.specialized||separate?static_cast<unsigned>(op):0];
        if(!k.id)throw std::logic_error{"Missing compute pass"};
        auto bases=buffers;bases[0]=buffers[current];bases[1]=buffers[in_place?current:1-current];
        if(k.particle_restrict){
            if(pbf_fault_binding<bases.size())bases[pbf_fault_binding]=pbf_fault_source<bases.size()?bases[pbf_fault_source]:0;
            require_disjoint_particle_bases(bases);
        }
        // A regression in the guard must still never submit an invalid restrict
        // program to the GPU. The test accepts only ParticleAliasError above.
        if(pbf_fault_binding<bases.size())throw std::runtime_error{"PBF binding fault escaped the production preflight guard."};
        if(!execution.cached_bindings){gl.UseProgram(granular[0].id);if(k.id!=granular[0].id)gl.UseProgram(k.id);}
        else if(bound_program!=k.id){gl.UseProgram(k.id);bound_program=k.id;}
        for(unsigned i=0;i<buffers.size();++i){
            const auto buffer=bases[i];
            if(!execution.cached_bindings||bound_bases[i]!=buffer){gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,buffer);bound_bases[i]=buffer;}
        }
        if(k.operation>=0)gl.Uniform1i(k.operation,op);
        configure_hot(k,in_place,static_cast<unsigned>(op));
        return k;
    }
    void indirect(unsigned command){
        if(!execution.cached_bindings||bound_indirect!=buffers[5]){gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[5]);bound_indirect=buffers[5];}
        gl.DispatchComputeIndirect(detail::command_offset(command));
    }
    std::array<GLuint,8> buffers{}; // A/B, head, link, walls, free, counters, fluid terms
    bool mission_enabled{};unsigned epoch{};MissionSnapshot snapshot{};std::deque<Explosion> explosions;MissionInput controls{};
    void next_generation(){if(epoch==std::numeric_limits<unsigned>::max())throw std::overflow_error{"World generation exhausted"};++epoch;resources->vfx.reset(epoch);renderer.lighting.reset(epoch);}
    bool fluids_enabled{},grains_enabled{};
    struct Timer {
        GLuint id{},start{};
        bool pending{};unsigned generation{};
        TimerMode mode{TimerMode::elapsed};
    };
    std::array<Timer,32> timers{},render_timers{};unsigned render_cursor{};
    unsigned timer_cursor{},timer_head{},render_head{},group_head{},current{};
    Metrics metrics;
    std::vector<double> timings,render_timings,cpu_timings;
    void reset_timer_queries(TimerMode mode){
        // Query objects acquire an immutable target on first use. Recreate the
        // pools on mode changes, including pending results; deletion is deferred
        // by GL and needs no CPU/GPU wait. No active query spans this method.
        for(auto* queue:{&timers,&render_timers})for(auto& timer:*queue){
            if(timer.id)gl.DeleteQueries(1,&timer.id);
            if(timer.start)gl.DeleteQueries(1,&timer.start);
            timer={};timer.mode=mode;
            if(mode!=TimerMode::off)gl.GenQueries(1,&timer.id);
            if(mode==TimerMode::timestamp)gl.GenQueries(1,&timer.start);
        }
        timer_cursor=timer_head=render_cursor=render_head=0;
        metrics.gpu_ms=0;metrics.render_ms=0;metrics.timing_samples=0;
        timings.clear();render_timings.clear();
    }
    void reset_group_queries(bool enabled){
        for(auto& timer:group_timers){
            if(timer.ids[0])gl.DeleteQueries(6,timer.ids.data());
            timer={};if(enabled)gl.GenQueries(6,timer.ids.data());
        }
        group_cursor=group_head=0;group_profiles.clear();
    }
    void begin_timer(Timer& timer){
        if(timer.mode==TimerMode::timestamp)gl.QueryCounter(timer.start,GL_TIMESTAMP);
        else gl.BeginQuery(GL_TIME_ELAPSED,timer.id);
    }
    void end_timer(Timer& timer){
        if(timer.mode==TimerMode::timestamp)gl.QueryCounter(timer.id,GL_TIMESTAMP);
        else gl.EndQuery(GL_TIME_ELAPSED);
        timer.pending=true;timer.generation=epoch;
    }
    ~Impl(){
        gl.observe(nullptr);
        for(const auto* queue:{&timers,&render_timers})for(const auto& timer:*queue){
            if(timer.id)gl.DeleteQueries(1,&timer.id);
            if(timer.start)gl.DeleteQueries(1,&timer.start);
        }
        for(const auto& timer:group_timers)if(timer.ids[0])gl.DeleteQueries(6,timer.ids.data());
        if(buffers[0])gl.DeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
    }
    void dispatch(int op,unsigned count,bool swap=false,bool in_place=false,bool conditional=false){
        if(count==0)return;
        if(op==8){renderer.lighting.prepare_birth(buffers,current);invalidate_bindings();}
        auto& k=prepare(granular,op,in_place);
        if(k.dirty>=0&&k.dirty_value!=static_cast<int>(conditional)){gl.Uniform1i(k.dirty,conditional?1:0);k.dirty_value=conditional?1:0;}
        if(op==1&&k.substep>=0)gl.Uniform1i(k.substep,half_step);
        if(op==5||op==7||op==8){
            if(k.stroke>=0)gl.Uniform4f(k.stroke,brush.x0,brush.y0,brush.x1,brush.y1);
            if(k.radius>=0)gl.Uniform1f(k.radius,std::clamp(brush.radius,1.F,16.F));
            if(k.material>=0)gl.Uniform1i(k.material,static_cast<int>(brush.material));
        }
        if(op==9)gl.BindImageTexture(0,renderer.terrain_image,0,static_cast<GLboolean>(GL_FALSE),0,GL_WRITE_ONLY,GL_R16UI);
        if(op==9)renderer.begin_terrain_update();
        const auto token=trace.begin(PassFamily::granular,static_cast<unsigned>(op),epoch,metrics.steps+(trace_step?1:0));
        if(op>=1&&op<=4)indirect(detail::active_command);
        else if(execution.conditional_work&&conditional&&(op==11||op==19||op==9))
            indirect(op==11?detail::rebuild_command:op==19?detail::sync_command:detail::terrain_command);
        else gl.DispatchCompute((count+k.local_size-1)/k.local_size,1,1);
        GLbitfield barriers=GL_SHADER_STORAGE_BARRIER_BIT;
        if(op==9)barriers|=GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        if(op==12||(op==10&&execution.conditional_work)||(op==3&&execution.coupling!=CouplingMode::reference))barriers|=GL_COMMAND_BARRIER_BIT;
        gl.MemoryBarrier(barriers);
        if(op==9)renderer.end_terrain_update();
        if(swap)current=1-current;
        trace.end(token,bins_valid&&op!=5&&op!=8&&op!=19,execution.soa,buffers[current],buffers[2],buffers[3]);
    }
    void neighbor_dispatch(unsigned op,unsigned groups){
        auto& k=neighbors[op];
        if(!execution.cached_bindings||bound_program!=k.id){gl.UseProgram(k.id);bound_program=k.id;}
        for(unsigned i=0;i<buffers.size();++i){const auto buffer=i<2?buffers[i==0?current:1-current]:buffers[i];
            if(!execution.cached_bindings||bound_bases[i]!=buffer){gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,buffer);bound_bases[i]=buffer;}}
        configure_hot(k,false,op);
        const auto token=trace.begin(PassFamily::neighbors,op,epoch,metrics.steps+(trace_step?1:0));
        gl.DispatchCompute(groups,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        trace.end(token,bins_valid||op==4,execution.soa,buffers[current],buffers[2],buffers[3]);
    }
    void bins(){
        if(execution.neighbors==NeighborMode::reused&&bins_valid)return;
        if(profile_options.collect)++calls.bins;
        if(execution.neighbors==NeighborMode::linked){dispatch(0,width*height);dispatch(2,capacity);}
        else {
            neighbor_dispatch(0,240);
            // Fixed capacity launch avoids a CPU read of current membership;
            // inactive lanes return before touching particle state.
            neighbor_dispatch(1,128);neighbor_dispatch(2,240);neighbor_dispatch(3,1);neighbor_dispatch(4,128);
        }
        bins_valid=true;
    }
    void liquid_dispatch(int op,bool swap=false){
        prepare(liquids,op);
        const auto token=trace.begin(PassFamily::liquids,static_cast<unsigned>(op),epoch,metrics.steps+(trace_step?1:0));
        if(op==3)gl.DispatchCompute(1,1,1);else indirect(detail::pbf_command);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|(((op==1||(op==4&&execution.fluid.velocity_reaction()))&&execution.coupling!=CouplingMode::reference)?GL_COMMAND_BARRIER_BIT:0));if(swap)current=1-current;
        trace.end(token,bins_valid,execution.soa,buffers[current],buffers[2],buffers[3]);
    }
    void rebuild_active(bool conditional=false){
        bins_valid=false;
        if(profile_options.collect)++calls.active_rebuilds;
        dispatch(10,1,false,false,conditional);dispatch(11,capacity,false,false,conditional);dispatch(12,1,false,false,conditional);
        // Membership edits must reach both ping-pong copies before active-only work.
        dispatch(19,capacity,false,false,conditional);
        if(conditional)dispatch(9,width*height,false,false,true);
        dispatch(20,1,false,false,conditional);
    }
    void mission_dispatch(int op,unsigned count=1,bool swap=false,bool active=false,bool in_place=false){
        if(!profile_options.body_coupling&&(op==3||op==8))return;
        if(profile_options.collect&&op==3)++calls.body_reductions;
        if(op==3&&execution.coupling!=CouplingMode::reference){
            prepare(coupling_kernels,0,false,true);
            auto token=trace.begin(PassFamily::coupling,0,epoch,metrics.steps+(trace_step?1:0));indirect(detail::body_reduce_command);
            gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
            trace.end(token,bins_valid,execution.soa,buffers[current],buffers[2],buffers[3]);
            prepare(coupling_kernels,1,false,true);
            token=trace.begin(PassFamily::coupling,1,epoch,metrics.steps+(trace_step?1:0));indirect(detail::body_final_command);
            gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
            trace.end(token,bins_valid,execution.soa,buffers[current],buffers[2],buffers[3]);return;
        }
        // Blast impulses touch only their own IDs. In-place work can be gated
        // without switching to a stale ping-pong copy when zero groups execute.
        if(execution.conditional_work&&op==7){in_place=true;swap=false;}
        auto& k=prepare(mission_kernels,op,in_place);
        if(op==4&&k.substep>=0)gl.Uniform1i(k.substep,half_step);
        if(op==0)gl.Uniform4f(k.input,std::clamp(controls.move,-1.F,1.F),controls.aim_x,controls.aim_y,static_cast<float>(controls.buttons&31U));
        const auto token=trace.begin(PassFamily::mission,static_cast<unsigned>(op),epoch,metrics.steps+(trace_step?1:0));
        if(execution.conditional_work&&(op==3||op==5||op==6||op==7))
            indirect(op==3?detail::reduction_command:op==5?detail::blast_terrain_command:op==6?detail::debris_command:detail::blast_command);
        else if(active)indirect(detail::active_command);
        else gl.DispatchCompute((count+k.local_size-1)/k.local_size,1,1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|(((op==0&&execution.conditional_work)||(op==8&&execution.coupling!=CouplingMode::reference))?GL_COMMAND_BARRIER_BIT:0));
        if(op==9)renderer.end_terrain_update();
        if(swap)current=1-current;
        trace.end(token,bins_valid&&op!=6,execution.soa,buffers[current],buffers[2],buffers[3]);
    }
    std::size_t body_offset()const{return execution.neighbors==NeighborMode::linked?capacity*4:capacity*24+(read_mode()==NeighborReads::sorted?capacity*64:0);}
    std::size_t index_buffer_size()const{return body_offset()+(execution.coupling==CouplingMode::reference?0:detail::body_metadata_size);}
    std::size_t terms_buffer_size()const{return capacity*((execution.coupling==CouplingMode::reference?32U:48U)+(execution.fluid.fields()?32U:0U));}
    void reset_body(){
        const std::array<GLuint,16> zero{};const std::array<GLuint,6> commands{0,1,1,0,1,1};
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        if(execution.coupling!=CouplingMode::reference)upload(3,static_cast<GLintptr>(body_offset()),sizeof(zero),zero.data());
        upload(5,detail::command_offset(detail::body_reduce_command),sizeof(commands),commands.data());
    }
    void upload(unsigned index,GLintptr offset,GLsizeiptr bytes,const void* data){uploads.write(buffers[index],offset,bytes,data);}
    void stage_count(bool force=false){
        if(profile_options.collect)++calls.stage_requests;
        if(profile_options.readback_period==0)return;
        if(!force&&metrics.steps%profile_options.readback_period!=0)return;
        readback_refresh=!readbacks.submit(buffers[6],epoch,readback_revision,metrics.steps);
    }

};
ParticleWorld::ParticleWorld(std::shared_ptr<RenderDevice> device):impl_{std::make_unique<Impl>(std::move(device))}{}
ParticleWorld::~ParticleWorld()=default;
std::expected<std::unique_ptr<ParticleWorld>,std::string> ParticleWorld::create(const std::filesystem::path& shaders,const ExecutionOptions& execution,const ProfileOptions& profiling,const std::filesystem::path& shader_cache,std::shared_ptr<RenderDevice> device){
    GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    if(major<4||(major==4&&minor<3))return std::unexpected{"Laboratorium GPU wymaga OpenGL 4.3. Laboratorium CPU jest dostępne."};
    try{
        const bool standalone=!device;auto resources=device?std::move(device):std::make_shared<RenderDevice>(shaders,shader_cache);
        if(standalone)resources->vfx.set_options({VfxQuality::off,false}); // Legacy physics callers opt into presentation explicitly.
        auto world=std::unique_ptr<ParticleWorld>{new ParticleWorld{std::move(resources)}};auto& s=*world->impl_;s.device.require_current();
        // Apply transport/timer policy before the first reset or staging copy.
        // Group query objects are allocated by set_profile_options below.
        s.profile_options=profiling;s.profile_options.group_timers=false;
        s.gl.observe(profiling.collect?&s.calls:nullptr);
        s.uploads.configure(execution.storage);
        s.debug_groups=s.device.debug_groups;
        const auto valid_size=[](unsigned size){return size==64||size==128||size==256;};
        if(!valid_size(execution.local_size)||!valid_size(execution.reduction_size)||!valid_size(execution.effective_pbf_size())||(!execution.specialized&&execution.local_size!=execution.reduction_size))
            throw std::invalid_argument{"Compute group sizes must be 64, 128 or 256; ubershaders require matching sizes."};
        // The shared device configures its program cache before VFX and world programs.
        if(!execution.fluid.valid())throw std::invalid_argument{"Invalid I07 fluid configuration."};
        s.execution=execution;
        // Mesa D3D12 in WSL can skip a nonzero conditional blast dispatch.
        // Keep active-particle indirect dispatch, but use the existing direct
        // path for optional work. The same fixture passes on native GL/Mesa
        // llvmpipe and with conditional_work=false (see LINUX_WSL_QA.md).
        const auto* renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if(renderer&&std::string_view{renderer}.starts_with("D3D12 (")&&s.execution.conditional_work){
            s.execution.conditional_work=false;
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"Mesa D3D12: using direct dispatch for conditional GPU work.");
        }
        s.compile_kernels(shaders);
        s.shader_folder=shaders;s.renderer.initialize(s.shader_library,shaders,s.layout_defines());s.renderer.vfx=&s.resources->vfx;
        s.gl.GenBuffers(static_cast<GLsizei>(s.buffers.size()),s.buffers.data());
        const std::array<std::size_t,8> sizes{capacity*sizeof(Grain),capacity*sizeof(Grain),static_cast<std::size_t>(s.execution.neighbors==NeighborMode::linked?width*height*4:(16+3*width*height+480+(s.execution.phase_gating?240:0))*4),s.index_buffer_size(),static_cast<std::size_t>(width*height*(execution.fluid.boundary==FluidBoundary::volume?20:16)+(execution.fluid.boundary==FluidBoundary::volume?144*4:0)),capacity*8+detail::command_words*4,mission_buffer_size,s.terms_buffer_size()};
        for(unsigned i=0;i<s.buffers.size();++i)s.uploads.allocate(s.buffers[i],static_cast<GLsizeiptr>(sizes[i]));
        if(s.debug_groups){
            constexpr std::array names{"particles A","particles B","cell counts and prefixes","sorted IDs, reverse mapping and anchors","terrain","active/free IDs","mission state","fluid terms"};
            for(std::size_t i=0;i<names.size();++i)s.gl.ObjectLabel(GL_BUFFER,s.buffers[i],-1,names[i]);
        }
        // Capsule samples translate with the actor. Their shape/volumes are fixed.
        if(execution.fluid.boundary==FluidBoundary::volume){
            std::array<float,144> weights{};
            const auto inside=[](int x,int y){const double px=x+.5,py=y+.5;const double dy=py-std::clamp(py,-14.0,-4.0);return px*px+dy*dy<16;};
            for(int y=-18;y<0;++y)for(int x=-4;x<4;++x)if(inside(x,y)){
                double sum=0;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)if(inside(x+dx,y+dy)){
                    const double q=std::max(0.0,4.0-dx*dx-dy*dy);sum+=q*q*q/(64*3.141592653589793);
                }
                weights[(y+18)*8+x+4]=static_cast<float>(1.014612762210833/sum);
            }
            s.upload(4,width*height*20,sizeof(weights),weights.data());
        }
        s.readbacks.configure(profiling.readback_mode,s.debug_groups);s.event_ring.initialize();
        s.reset_timer_queries(effective_timer_mode(s.profile_options));
        world->set_profile_options(profiling);
        world->reset(Scene::hourglass);return world;
    }catch(const std::exception& e){return std::unexpected{e.what()};}
}
void ParticleWorld::load(std::span<const Grain> grains,std::span<const std::uint32_t> walls){
    if(grains.size()>capacity||walls.size()!=width*height)throw std::invalid_argument{"Nieprawidłowy rozmiar sceny GPU."};
    std::vector<Grain> all(capacity);std::ranges::copy(grains,all.begin());auto& s=*impl_;s.invalidate_bindings();
    for(auto& p:all){p.contact[2]=p.position[0];p.contact[3]=p.position[1];}
    ++s.readback_revision;s.readback_refresh=false;s.mission_enabled=false;s.snapshot={};s.explosions.clear();s.next_generation();s.gameplay.reset(s.epoch);
    const std::vector<std::byte> empty(mission_buffer_size);
    s.upload(6,0,mission_buffer_size,empty.data());
    s.reset_body();s.current=0;s.bins_valid=false;
    const std::array<GLuint,16> neighbor_zero{};s.upload(2,0,sizeof(neighbor_zero),neighbor_zero.data());
    s.renderer.reset_profiles();s.presented_hp={3,3,3};s.hit_started={};
    s.metrics.steps=0;s.readback_age=0;s.metrics.grains=static_cast<unsigned>(std::ranges::count_if(all,[](const Grain& p){return p.motion[3]>.5F;}));
    s.metrics.gpu_ms=0;s.metrics.render_ms=0;s.metrics.timing_samples=0;
    s.metrics.water=static_cast<unsigned>(std::ranges::count_if(all,[](const Grain& p){return p.motion[3]==5;}));
    s.metrics.oil=static_cast<unsigned>(std::ranges::count_if(all,[](const Grain& p){return p.motion[3]==6;}));
    s.metrics.max_compression=0;s.fluids_enabled=s.metrics.water+s.metrics.oil>0;
    s.grains_enabled=s.metrics.grains>s.metrics.water+s.metrics.oil;
    const std::array<GLuint,8> counters{0,0,s.metrics.grains,s.metrics.water,s.metrics.oil,0,0,0};
    s.upload(6,0,32,counters.data());
    std::vector<std::array<float,4>> packed(capacity*4);
    if(s.execution.soa)for(unsigned i=0;i<capacity;++i){packed[i]=all[i].position;packed[capacity+i]=all[i].motion;packed[2*capacity+i]=all[i].contact;packed[3*capacity+i]=all[i].residual;}
    for(unsigned i=0;i<2;++i)s.upload(i,0,static_cast<GLsizeiptr>(all.size()*sizeof(Grain)),s.execution.soa?static_cast<const void*>(packed.data()):static_cast<const void*>(all.data()));
    s.upload(4,0,static_cast<GLsizeiptr>(walls.size_bytes()),walls.data());
    std::vector<std::uint32_t> strength(walls.size());for(std::size_t i=0;i<walls.size();++i)strength[i]=walls[i]>=2?3U:0U;
    s.upload(4,width*height*12,static_cast<GLsizeiptr>(strength.size()*4),strength.data());
    s.dispatch(9,width*height);s.rebuild_active();
    if(s.thermal)s.thermal->reset(s.buffers,s.current,s.epoch);
    render(1);
}
void ParticleWorld::reset(Scene scene){
    std::vector<std::uint32_t> walls(width*height);std::vector<Grain> grains;
    auto box=[&](int x0,int y0,int x1,int y1){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)walls[y*width+x]=1;};
    box(0,187,319,191);box(0,0,0,191);box(319,0,319,191);
    if(scene==Scene::hourglass){
        for(int y=28;y<64;++y)for(int x=100;x<220;++x){
            if(x<92+(y-20)*.87+3||x>228-(y-20)*.87-3)continue;
            const auto [px,py]=seeded_position(x,y);Grain p;p.position={px,py,px,py};p.motion={0,0,1,1};grains.push_back(p);
        }
        load(grains,walls);
        // Exactly the public brush path; the regression exercises user-drawn
        // walls and their edits, not special analytic hourglass collision code.
        for(const auto stroke:std::array{
            Stroke{92,20,155,94,1,Material::wall},Stroke{228,20,165,94,1,Material::wall},
            Stroke{155,94,155,102,1,Material::wall},Stroke{165,94,165,102,1,Material::wall},
            Stroke{155,102,92,176,1,Material::wall},Stroke{165,102,228,176,1,Material::wall},
            Stroke{92,176,228,176,1,Material::wall}})paint(stroke);
        return;
    }
    if(scene==Scene::inverted_oil||scene==Scene::oil_drop){
        box(111,100,111,164);box(208,100,208,164);box(111,164,208,164);
        for(int y=124;y<164;++y)for(int x=112;x<208;++x){
            const auto [px,py]=seeded_position(x,y);
            const bool oil=scene==Scene::inverted_oil?py>=144+2*std::sin((px-112)*6.2831853F/96):std::hypot(px-160,py-150)<8;
            Grain p;p.position={px,py,px,py};p.motion={0,0,oil?3.3125F:2.65F,oil?6.F:5.F};grains.push_back(p);
        }
        load(grains,walls);return;
    }
    if(scene>=Scene::water_tank){
        auto fill=[&](int x0,int y0,int nx,int ny,int type){
            for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){
                const float px=x0+x+.5F,py=y0+y+.5F;
                if(walls[static_cast<int>(py)*width+static_cast<int>(px)])continue;
                Grain p;p.position={px,py,px,py};p.motion={0,0,type==6?3.3125F:(type==5?2.65F:1.F),static_cast<float>(type)};grains.push_back(p);
            }
        };
        box(40,85,40,186);box(279,85,279,186);
        if(scene==Scene::water_tank)fill(41,167,238,20,5);
        if(scene==Scene::dam_break){fill(42,100,54,86,5);box(210,158,211,186);}
        if(scene==Scene::connected_vessels){box(156,80,157,173);fill(41,133,115,54,5);fill(158,175,121,12,5);}
        if(scene==Scene::oil_water){fill(41,171,238,16,5);fill(41,159,238,12,6);}
        if(scene==Scene::sand_water){fill(41,169,238,18,5);fill(120,100,50,20,1);}
    }else if(scene!=Scene::empty){
        box(68,118,142,119);box(191,148,265,149);
        for(int x=30;x<92;++x){const int y=135+(x-30)/3;box(x,y,x,y+1);}
        const int columns=scene==Scene::rain?220:190,rows=scene==Scene::rain?20:50;
        for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
            const auto seed=hash(static_cast<unsigned>(y*columns+x+1));
            const float px=45.F+x*1.04F+static_cast<float>(seed&255U)/2550.F;
            const float py=8.F+y*1.04F+static_cast<float>((seed>>8)&255U)/2550.F;
            const int type=scene==Scene::mixed?1+(x/64)%3:1;
            Grain p;p.position={px,py,px,py};p.motion={0,0,type==1?1.F:(type==2?4.F:.35F),static_cast<float>(type)};
            grains.push_back(p);
        }
    }
    load(grains,walls);
}
void ParticleWorld::paint(const Stroke& stroke){
    auto& s=*impl_;
    if(s.thermal&&s.thermal_config.enabled&&(stroke.material==Material::erase||stroke.material==Material::wall))s.thermal->erase(s.buffers,s.current,{{stroke.x0,stroke.y0,stroke.x1,stroke.y1},stroke.radius,0,ThermalTool::none});
    s.invalidate_bindings();s.bins_valid=false;++s.readback_revision;s.brush=stroke;
    const std::array<GLuint,8> zero{};s.upload(6,0,32,zero.data());
    // Conservative flags: an asynchronous counter must never disable a solver
    // after a newer edit. Reset/load can clear them from known scene state.
    if(stroke.material==Material::water||stroke.material==Material::oil)s.fluids_enabled=true;
    if(stroke.material>=Material::sand&&stroke.material<=Material::metal)s.grains_enabled=true;
    s.dispatch(5,capacity,true);s.rebuild_active();
    if(stroke.material==Material::wall||stroke.material==Material::erase){s.dispatch(7,width*height);s.dispatch(9,width*height);}
    else{
        s.bins();s.dispatch(8,width*height,false,true);
    }
    s.rebuild_active();s.bins_valid=false;s.stage_count(true);
}
bool ParticleWorld::ready_for_step(){
    auto& s=*impl_;if(!s.hold_events)s.event_ring.poll(s.gameplay);if(!s.mission_enabled||s.event_ring.available())return true;glFlush();return false;
}
void ParticleWorld::cancel_mission_input(){impl_->controls={};impl_->gameplay.cancel_input();}
const game::GameplaySession& ParticleWorld::gameplay()const noexcept{return impl_->gameplay;}
std::array<std::uint64_t,7> ParticleWorld::event_stats()const noexcept{
    const auto& s=impl_->event_ring.stats();return {s.submitted,s.acknowledged,s.stale,s.pending,s.max_pending,s.backpressure,s.max_age_ticks};
}
bool ParticleWorld::step(const MissionInput& input){
    impl_->controls=input;return step();
}
bool ParticleWorld::step(){
    const auto started=SDL_GetTicksNS();auto& s=*impl_;const auto initial_calls=s.calls;
    const auto logic_before=s.event_ring.stats().logic_ms;
    if(!ready_for_step())return false;
    s.metrics.gameplay_ms=0;s.invalidate_bindings();
    s.trace_step=true;s.trace.begin_step(s.metrics.steps+1);
    if(s.debug_groups)s.gl.PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION,1,-1,"Physics tick");
    const auto stamp=[&](){return s.profile_options.collect?SDL_GetTicksNS():std::uint64_t{0};};
    const auto elapsed=[](std::uint64_t a,std::uint64_t b){return static_cast<double>(b-a)/1e6;};
    // Physics consumes GPU state directly. Only diagnostic legacy modes retain
    // per-tick HUD polling; normal HUD collection is once per UI frame.
    const auto mode=s.readbacks.stats().mode;
    const auto poll_start=stamp();poll(mode==ReadbackMode::legacy||mode==ReadbackMode::stream);const auto poll_end=stamp();
    const bool timers_enabled=effective_timer_mode(s.profile_options)!=TimerMode::off;
    auto& timer=s.timers[s.timer_cursor];const bool measure=timers_enabled&&!timer.pending;
    auto& group=s.group_timers[s.group_cursor];const bool grouped=timers_enabled&&s.profile_options.group_timers&&!group.pending;
    if(grouped){if(s.profile_options.collect)++s.calls.timer_submissions;group.tick=s.metrics.steps+1;group.generation=s.epoch;s.gl.QueryCounter(group.ids[0],GL_TIMESTAMP);}
    if(s.profile_options.collect&&timers_enabled){if(measure)++s.calls.timer_submissions;else ++s.calls.timer_skips;}
    if(measure)s.begin_timer(timer);
    double upload_ms=0;
    const auto prepare_start=stamp();
    if(s.mission_enabled){
        const auto logic_started=SDL_GetTicksNS();
        const auto commands=s.gameplay.prepare({s.controls.move,s.controls.aim_x,s.controls.aim_y,s.controls.buttons});
        s.metrics.gameplay_ms=static_cast<double>(SDL_GetTicksNS()-logic_started)/1e6;
        s.upload(6,command_offset,sizeof(commands),&commands);
        if(!s.explosions.empty()){
            const auto upload_start=stamp();
            const auto e=s.explosions.front();s.explosions.pop_front();const std::array<float,4> payload{e.x,e.y,e.radius,e.impulse};
            const std::array<std::uint32_t,4> id{e.id,0,0,0};
            s.upload(6,explosion_offset,16,payload.data());
            s.upload(6,explosion_offset+16,16,id.data());
            upload_ms=elapsed(upload_start,stamp());
        }
        s.mission_dispatch(0);s.mission_dispatch(7,capacity,true,true);s.mission_dispatch(5,width*height);
        // Pending cells keep the material until a free slot is available.
        s.renderer.lighting.prepare_birth(s.buffers,s.current,s.execution.conditional_work);s.invalidate_bindings();s.mission_dispatch(6,width*height,false,false,true);
        s.rebuild_active(true);
    }
    const auto prepare_end=stamp();
    if(grouped)s.gl.QueryCounter(group.ids[1],GL_TIMESTAMP);
    // At |v|<=80 each half-tick travels <=1/3 cell. Two approaching,
    // nonoverlapping radius-.46 grains cannot exchange centers undetected.
    for(int half=0;half<static_cast<int>(s.execution.fluid.substeps);++half){
        s.trace.context(half+1,-1);
        s.bins_valid=false;s.half_step=half;
        if(s.mission_enabled)s.mission_dispatch(1);
        s.dispatch(1,capacity,true);
        const int iterations=static_cast<int>(s.grains_enabled?s.execution.fluid.contact_iterations:s.execution.fluid.pbf_iterations);
        for(int iteration=0;iteration<iterations;++iteration){
            s.trace.context(half+1,iteration);
            s.bins();
            if(s.fluids_enabled&&iteration<static_cast<int>(s.execution.fluid.pbf_iterations)){s.liquid_dispatch(0);s.liquid_dispatch(1,true);if(s.mission_enabled&&(s.execution.coupling!=CouplingMode::combined||!s.grains_enabled))s.mission_dispatch(3);}
            if(s.grains_enabled){if(s.fluids_enabled&&iteration<static_cast<int>(s.execution.fluid.pbf_iterations))s.bins();s.dispatch(3,capacity,true);}
            if(s.mission_enabled)s.mission_dispatch(3);
        }
        s.trace.context(half+1,iterations);s.bins();s.dispatch(4,capacity,true);
        if(s.fluids_enabled){
            if(s.execution.fluid.fields()){s.liquid_dispatch(5);s.liquid_dispatch(6);}
            s.liquid_dispatch(4,true);
            // Apply adhesion before contact drag. The reference reducer shares
            // one reaction channel; consuming here prevents it being overwritten.
            if(s.mission_enabled&&s.execution.fluid.velocity_reaction())s.mission_dispatch(3);
        }
        if(s.mission_enabled){if(s.fluids_enabled){s.mission_dispatch(8,capacity,true,true);s.mission_dispatch(3);}s.mission_dispatch(4);}
        if(grouped&&(half==0||half==static_cast<int>(s.execution.fluid.substeps)-1))s.gl.QueryCounter(group.ids[half==0?2:3],GL_TIMESTAMP);
    }
    const auto solver_end=stamp();
    s.trace.context(3,-1);
    const bool diagnostic_due=s.profile_options.periodic_diagnostics&&s.fluids_enabled&&s.metrics.steps%30==0;
    if(diagnostic_due){s.bins();s.liquid_dispatch(3);s.liquid_dispatch(2);}
    if(s.thermal&&s.thermal_config.enabled){
        if(s.mission_enabled)s.thermal->consume_events(s.buffers,s.epoch,s.gameplay.tick());
        s.thermal->tick(s.buffers,s.current,static_cast<unsigned>(s.metrics.steps+1),s.thermal_brush);s.invalidate_bindings();
        s.rebuild_active();s.renderer.lighting.prepare_birth(s.buffers,s.current);s.invalidate_bindings();s.thermal->condense(s.buffers,s.current);s.invalidate_bindings();s.rebuild_active();s.dispatch(9,width*height);
        s.bins_valid=false;s.grains_enabled=s.fluids_enabled=true;
    }
    const auto diagnostics_end=stamp();
    if(grouped)s.gl.QueryCounter(group.ids[4],GL_TIMESTAMP);
    if(measure){s.end_timer(timer);s.timer_cursor=(s.timer_cursor+1)%static_cast<unsigned>(s.timers.size());}
    const auto timer_closed=stamp();
    if(s.thermal&&s.thermal_config.enabled)s.thermal->presentation(s.renderer.lighting.thermal_image);
    s.gl.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);s.renderer.lighting.tick(s.buffers,s.current,s.epoch,s.mission_enabled?s.gameplay.tick():static_cast<unsigned>(s.metrics.steps+1),s.mission_enabled,s.renderer.terrain_image,s.thermal&&s.thermal_config.enabled?s.renderer.lighting.thermal_image:0,s.thermal&&s.thermal_config.enabled?s.thermal->presentation_particles():0);s.invalidate_bindings();
    if(s.resources->vfx.enabled()){
        auto& vfx=s.resources->vfx;vfx.begin_tick(s.renderer.terrain_image,s.epoch,s.mission_enabled?s.gameplay.tick():static_cast<unsigned>(s.metrics.steps+1),1.F/120.F);
        s.renderer.build_vfx_field({s.buffers,s.current,s.fluids_enabled,s.mission_enabled});
        vfx.sand_sources(s.renderer.get_options().lighting.quality!=LightingQuality::off?s.renderer.lighting.field:0);vfx.end_tick(s.mission_enabled?s.buffers[6]:0,{},s.fluids_enabled);s.invalidate_bindings();
    }
    if(s.mission_enabled)s.event_ring.submit(s.buffers[6],event_offset,s.epoch,s.gameplay.tick());
    ++s.metrics.steps;if(s.mission_enabled||diagnostic_due)s.stage_count(!s.mission_enabled);
    if(grouped){s.gl.QueryCounter(group.ids[5],GL_TIMESTAMP);group.pending=true;s.group_cursor=(s.group_cursor+1)%static_cast<unsigned>(s.group_timers.size());}
    const auto finished=SDL_GetTicksNS();
    if(s.debug_groups)s.gl.PopDebugGroup();
    s.metrics.gameplay_ms+=s.event_ring.stats().logic_ms-logic_before;
    s.metrics.cpu_submit_ms=static_cast<double>(finished-started)/1e6;
    if(s.cpu_timings.size()<10000)s.cpu_timings.push_back(s.metrics.cpu_submit_ms);
    if(s.profile_options.collect){
        auto& p=s.last_profile;p={};p.tick=s.metrics.steps;p.generation=s.epoch;p.calls=s.calls-initial_calls;
        p.total_ms=s.metrics.cpu_submit_ms;p.poll_ms=elapsed(poll_start,poll_end);p.upload_ms=upload_ms;
        p.prepare_ms=elapsed(prepare_start,prepare_end)-upload_ms;p.solver_ms=elapsed(prepare_end,solver_end);
        p.diagnostics_ms=elapsed(solver_end,diagnostics_end);p.staging_ms=elapsed(timer_closed,finished);
        p.timer_begin_ms=elapsed(poll_end,prepare_start);p.timer_end_ms=elapsed(diagnostics_end,timer_closed);
        p.timer_mode=effective_timer_mode(s.profile_options);
        p.timer_poll_ms=s.poll_timer_ms;p.fence_poll_ms=s.poll_fence_ms;p.read_ms=s.poll_read_ms;p.readback_age_ticks=s.mission_enabled?s.metrics.steps-s.snapshot.status[2]:s.readback_age;
        if(s.profiles.size()<10000)s.profiles.push_back(p);
    }
    s.trace_step=false;s.trace.end_step();return true;
}
void ParticleWorld::poll(bool collect_hud){
    auto& s=*impl_;
    const auto stamp=[&](){return s.profile_options.collect?SDL_GetTicksNS():std::uint64_t{0};};
    if(collect_hud&&!s.hold_events)s.event_ring.poll(s.gameplay);
    const auto timer_start=stamp();s.poll_fence_ms=0;s.poll_read_ms=0;
    const auto collect=[&](auto& queue,unsigned& head,auto consume){
        const auto ready=[&](auto& timer,GLuint id){
            GLint available{};s.gl.GetQueryObjectiv(id,GL_QUERY_RESULT_AVAILABLE,&available);
            if(!available)return false;
            consume(timer);timer.pending=false;return true;
        };
        const auto id=[](const auto& timer){if constexpr(requires{timer.id;})return timer.id;else return timer.ids.back();};
        if(s.execution.fifo_queries){
            while(queue[head].pending){if(!ready(queue[head],id(queue[head])))break;head=(head+1)%static_cast<unsigned>(queue.size());}
        }else for(auto& timer:queue)if(timer.pending)ready(timer,id(timer));
    };
    const auto scalar=[&](auto& queue,unsigned& head,bool rendering){
        collect(queue,head,[&](auto& timer){
            GLuint64 ns{};s.gl.GetQueryObjectui64v(timer.id,GL_QUERY_RESULT,&ns);
            if(timer.mode==TimerMode::timestamp){
                GLuint64 began{};s.gl.GetQueryObjectui64v(timer.start,GL_QUERY_RESULT,&began);ns-=began;
            }
            if(timer.generation!=s.epoch){if(s.profile_options.collect)++s.calls.timer_stale;return;}
            const auto ms=static_cast<double>(ns)*1e-6;
            if(rendering){s.metrics.render_ms=ms;if(s.render_timings.size()<10000)s.render_timings.push_back(ms);}
            else{s.metrics.gpu_ms=ms;++s.metrics.timing_samples;if(s.timings.size()<10000)s.timings.push_back(ms);}
        });
    };
    scalar(s.timers,s.timer_head,false);scalar(s.render_timers,s.render_head,true);
    if(effective_timer_mode(s.profile_options)!=TimerMode::off&&s.profile_options.group_timers)collect(s.group_timers,s.group_head,[&](auto& timer){
        std::array<GLuint64,6> timestamps{};
        for(std::size_t i=0;i<timestamps.size();++i)s.gl.GetQueryObjectui64v(timer.ids[i],GL_QUERY_RESULT,&timestamps[i]);
        if(timer.generation==s.epoch&&s.group_profiles.size()<10000){
            GroupProfile result;result.tick=timer.tick;result.generation=timer.generation;
            for(std::size_t i=0;i<result.milliseconds.size();++i)result.milliseconds[i]=static_cast<double>(timestamps[i+1]-timestamps[i])*1e-6;
            s.group_profiles.push_back(result);
        }else if(s.profile_options.collect)++s.calls.timer_stale;
    });
    const auto timer_end=stamp();s.poll_timer_ms=static_cast<double>(timer_end-timer_start)/1e6;
    if(!collect_hud)return;
    if(const auto packet=s.readbacks.poll(s.epoch,s.readback_revision,s.metrics.steps)){
        const auto& values=packet->counters;
        s.metrics.grains=values[2];s.metrics.water=values[3];s.metrics.oil=values[4];s.metrics.max_compression=std::bit_cast<float>(values[5]);
        const auto& received=packet->mission;
        if(s.mission_enabled&&received.status[1]==s.epoch&&received.status[2]>=s.snapshot.status[2])s.snapshot=received;
        s.readback_age=s.readbacks.stats().last_latency_ticks;
    }
    const auto read_times=s.readbacks.timings();s.poll_fence_ms=read_times.fence_ms;s.poll_read_ms=read_times.read_ms;
    if(s.readback_refresh)s.stage_count(true);
}
void ParticleWorld::render(float alpha){
    auto& s=*impl_;s.device.require_current();s.resources->vfx.poll();if(s.thermal)s.thermal->poll();
    auto& timer=s.render_timers[s.render_cursor];
    const bool timers_enabled=effective_timer_mode(s.profile_options)!=TimerMode::off;
    const bool measure=timers_enabled&&!timer.pending;
    if(s.profile_options.collect&&timers_enabled&&!measure)++s.calls.timer_skips;
    const auto now=SDL_GetTicksNS();const auto& hp=s.gameplay.state().hp;
    for(unsigned i=0;i<3;++i){if(hp[i]<s.presented_hp[i])s.hit_started[i]=now;s.presented_hp[i]=hp[i];
        s.renderer.targets.hp[i]=hp[i];s.renderer.targets.hit[i]=s.hit_started[i]?std::max(0.F,1.F-static_cast<float>(now-s.hit_started[i])/150000000.F):0.F;}

    if(measure)s.begin_timer(timer);
    s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    s.renderer.lighting.impulse=s.mission_enabled?std::clamp(s.snapshot.flash[3]/.3F,0.F,1.F):0.F;
    s.renderer.render({s.buffers,s.current,s.fluids_enabled,s.mission_enabled},alpha);
    s.invalidate_bindings();
    if(measure){
        s.end_timer(timer);
        if(s.profile_options.collect)++s.calls.timer_submissions;
        s.render_cursor=(s.render_cursor+1)%static_cast<unsigned>(s.render_timers.size());
    }
}
std::shared_ptr<RenderDevice> ParticleWorld::render_device()const{return impl_->resources;}
void ParticleWorld::set_thermal_options(ThermalOptions value){
    if(value.pressure_iterations==0||value.pressure_iterations>128)throw std::invalid_argument{"Thermal pressure iterations: 1..128"};
    auto& s=*impl_;if(value.enabled&&!s.thermal){s.thermal=std::make_unique<detail::ThermalSystem>(s.gl,s.device.state);s.thermal->initialize(s.shader_library,s.shader_folder,s.layout_defines());s.thermal->reset(s.buffers,s.current,s.epoch);s.renderer.thermal=s.thermal.get();}
    s.thermal_config=value;if(s.thermal)s.thermal->options=value;s.thermal_brush={};s.invalidate_bindings();
}
ThermalOptions ParticleWorld::thermal_options()const{return impl_->thermal_config;}
void ParticleWorld::set_thermal_view(ThermalView view){if(impl_->thermal)impl_->thermal->view=view;}
void ParticleWorld::set_thermal_brush(const ThermalBrush& brush){
    if(!std::ranges::all_of(brush.segment,[](float v){return std::isfinite(v);})||!std::isfinite(brush.radius)||brush.radius<1||brush.radius>16||!std::isfinite(brush.rate)||brush.rate<0||brush.rate>10000||brush.tool>ThermalTool::steam)throw std::invalid_argument{"Invalid thermal brush"};
    impl_->thermal_brush=brush;
}
const ThermalTotals& ParticleWorld::thermal_totals()const{static const ThermalTotals empty;return impl_->thermal?impl_->thermal->totals():empty;}
ThermalSnapshot ParticleWorld::read_thermal(){return impl_->thermal?impl_->thermal->snapshot():ThermalSnapshot{};}
void ParticleWorld::request_thermal_inspection(int x,int y){if(impl_->thermal&&impl_->thermal_config.enabled)impl_->thermal->request_inspection(x,y);}
const ThermalInspection& ParticleWorld::thermal_inspection()const{static const ThermalInspection empty;return impl_->thermal?impl_->thermal->inspection():empty;}
std::uint64_t ParticleWorld::thermal_memory()const{return impl_->thermal?impl_->thermal->memory_bytes():0;}
void ParticleWorld::set_vfx_options(VfxOptions value){impl_->resources->vfx.set_options(value);}
VfxOptions ParticleWorld::vfx_options()const noexcept{return impl_->resources->vfx.options();}
const VfxCounters& ParticleWorld::vfx_stats()const{return impl_->resources->vfx.stats();}
void ParticleWorld::set_render_options(RenderOptions value){impl_->renderer.set_options(value);}
std::array<float,2> ParticleWorld::camera_offset()const{const auto& r=impl_->renderer;const auto c=r.lighting.camera(r.get_options().lighting);return r.get_options().lighting.quality==LightingQuality::off?std::array<float,2>{}:std::array<float,2>{c[0]/static_cast<float>(r.render_width())*320.F,c[1]/static_cast<float>(r.render_height())*192.F};}
RenderOptions ParticleWorld::render_options()const noexcept{return impl_->renderer.get_options();}
void ParticleWorld::set_background_texture(unsigned texture){impl_->renderer.background_texture=texture;}
void ParticleWorld::invalidate_gl_state(){impl_->invalidate_bindings();}
std::vector<RenderSample> ParticleWorld::take_render_samples(){return impl_->renderer.take_samples();}
unsigned ParticleWorld::texture()const noexcept{return impl_->renderer.image;}
std::vector<double> ParticleWorld::take_render_timings(){return std::exchange(impl_->render_timings,{});}
std::vector<double> ParticleWorld::take_cpu_timings(){return std::exchange(impl_->cpu_timings,{});}
const Metrics& ParticleWorld::metrics()const noexcept{return impl_->metrics;}
const MissionSnapshot& ParticleWorld::mission()const noexcept{
    auto& s=*impl_;if(s.mission_enabled)s.gameplay.present(s.snapshot.inventory,s.snapshot.timers,s.snapshot.targets,s.snapshot.status[3]);return s.snapshot;
}
void ParticleWorld::set_profile_options(const ProfileOptions& options){
    auto& s=*impl_;
    const auto previous_mode=effective_timer_mode(s.profile_options),next_mode=effective_timer_mode(options);
    const bool previous_groups=previous_mode!=TimerMode::off&&s.profile_options.group_timers;
    const bool next_groups=next_mode!=TimerMode::off&&options.group_timers;
    if(previous_mode!=next_mode)s.reset_timer_queries(next_mode);
    if(previous_mode!=next_mode||previous_groups!=next_groups)s.reset_group_queries(next_groups);
    s.profile_options=options;s.gl.observe(options.collect?&s.calls:nullptr);
    s.readbacks.configure(options.readback_mode,s.debug_groups);
    if(options.readback_period==0)s.readback_refresh=false;
    for(auto kernels:{std::span<Impl::Kernel>{s.granular},std::span<Impl::Kernel>{s.liquids},std::span<Impl::Kernel>{s.mission_kernels},std::span<Impl::Kernel>{s.coupling_kernels}})for(const auto& k:kernels)if(k.id&&k.coupling>=0){s.gl.UseProgram(k.id);s.gl.Uniform1i(k.coupling,options.body_coupling?0:1);}
    s.invalidate_bindings();

}
ProfileOptions ParticleWorld::profile_options()const noexcept{return impl_->profile_options;}
const StepProfile& ParticleWorld::step_profile()const noexcept{return impl_->last_profile;}
PollTimings ParticleWorld::poll_timings()const noexcept{return {impl_->poll_timer_ms,impl_->poll_fence_ms,impl_->poll_read_ms};}
ReadbackStats ParticleWorld::readback_stats()const noexcept{return impl_->readbacks.stats();}
UploadStats ParticleWorld::upload_stats()const noexcept{return impl_->uploads.stats();}
ShaderCacheStats ParticleWorld::shader_cache_stats()const noexcept{return impl_->shader_library.stats();}
ExecutionOptions ParticleWorld::execution_options()const noexcept{return impl_->execution;}
std::array<bool,5> ParticleWorld::pbf_restrict_programs()const{
    for(const auto kernels:{std::span<const Impl::Kernel>{impl_->granular},std::span<const Impl::Kernel>{impl_->mission_kernels},
                            std::span<const Impl::Kernel>{impl_->neighbors},std::span<const Impl::Kernel>{impl_->coupling_kernels}})
        for(const auto& kernel:kernels)if(kernel.particle_restrict)throw std::logic_error{"PBF qualifiers escaped their two-program scope."};
    std::array<bool,5> result{};for(unsigned i=0;i<result.size();++i)result[i]=impl_->liquids[i].particle_restrict;return result;
}
std::array<std::string,5> ParticleWorld::pbf_program_sources_for_test()const{
    auto& s=*impl_;std::array<std::string,5> result;
    for(unsigned i=0;i<result.size();++i){
        if(!s.liquids[i].id)continue;
        GLuint shader{};GLsizei attached{};s.gl.GetAttachedShaders(s.liquids[i].id,1,&attached,&shader);
        if(attached!=1)throw std::logic_error{"PBF program must have one attached compute shader."};
        GLint length{};s.gl.GetShaderiv(shader,GL_SHADER_SOURCE_LENGTH,&length);
        if(length<=1)throw std::logic_error{"PBF program has no retrievable shader source."};
        result[i].resize(static_cast<std::size_t>(length));GLsizei written{};
        s.gl.GetShaderSource(shader,length,&written,result[i].data());result[i].resize(static_cast<std::size_t>(written));
    }
    return result;
}
void ParticleWorld::probe_pbf_alias_for_test(unsigned operation,unsigned source,unsigned binding){
    auto& s=*impl_;
    if((operation!=1&&operation!=4)||!s.liquids[operation].particle_restrict||source>8||binding>=8||source==binding)
        throw std::invalid_argument{"Invalid PBF binding fault probe."};
    const auto before=s.calls;const auto current=s.current;
    s.pbf_fault_source=source;s.pbf_fault_binding=binding;
    try{s.liquid_dispatch(static_cast<int>(operation),true);}
    catch(const ParticleAliasError&){
        s.pbf_fault_source=s.pbf_fault_binding=8;
        if(s.calls.api_calls!=before.api_calls||s.current!=current)throw std::logic_error{"PBF alias rejection changed GL state or the current bank."};
        return;
    }catch(...){s.pbf_fault_source=s.pbf_fault_binding=8;throw;}
    s.pbf_fault_source=s.pbf_fault_binding=8;
    throw std::logic_error{"PBF alias probe was not rejected."};
}
std::array<std::uint32_t,37> ParticleWorld::read_dispatch_commands()const{
    auto& s=*impl_;std::array<std::uint32_t,37> result{};s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[5]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,detail::command_offset(0),sizeof(result),result.data());return result;
}
CallCounters ParticleWorld::counters()const noexcept{return impl_->calls;}
std::vector<StepProfile> ParticleWorld::take_step_profiles(){return std::exchange(impl_->profiles,{});}
std::vector<GroupProfile> ParticleWorld::take_group_profiles(){return std::exchange(impl_->group_profiles,{});}
MissionSnapshot ParticleWorld::read_mission()const{
    auto& s=*impl_;MissionSnapshot result{};s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,32,sizeof(result),&result);
    if(!s.hold_events)s.event_ring.poll(s.gameplay);
    if(s.mission_enabled)s.gameplay.present(result.inventory,result.timers,result.targets,result.status[3]);
    return result;
}
std::vector<std::uint32_t> ParticleWorld::read_terrain()const{
    auto& s=*impl_;std::vector<std::uint32_t> result(width*height*4);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[4]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,result.size()*4,result.data());return result;
}
void ParticleWorld::set_actor_textures(unsigned parts,unsigned weapons,int w,int h){
    impl_->renderer.parts_texture=parts;impl_->renderer.weapons_texture=weapons;impl_->renderer.weapon_width=w;impl_->renderer.weapon_height=h;
}
void ParticleWorld::queue_explosion(const Explosion& e){
    if(e.id==0||!std::isfinite(e.x)||!std::isfinite(e.y)||!std::isfinite(e.radius)||!std::isfinite(e.impulse)
        ||e.x<0||e.x>=width||e.y<0||e.y>=height||e.radius<=0||e.radius>32||e.impulse<0||e.impulse>80)
        throw std::invalid_argument{"Invalid GPU explosion"};
    if(impl_->explosions.size()>=32)throw std::runtime_error{"GPU explosion command queue full"};
    impl_->explosions.push_back(e);
}
void ParticleWorld::configure_mission(const MissionConfig& config){
    if(!std::isfinite(config.spawn_x)||!std::isfinite(config.spawn_y)||!std::isfinite(config.relative_density)
        ||config.spawn_x<4||config.spawn_x>width-4||config.spawn_y<18||config.spawn_y>height||config.relative_density<=0)
        throw std::invalid_argument{"Invalid GPU player configuration"};
    auto& s=*impl_;s.reset_body();MissionSnapshot g{};s.explosions.clear();
    s.next_generation();++s.readback_revision;s.readback_refresh=false;
    g.actor={config.spawn_x,config.spawn_y,0,0};g.previous={config.spawn_x,config.spawn_y,config.spawn_x,config.spawn_y};
    g.aim={1,0,1.5F,0};g.status={1,s.epoch,0,0};g.inventory={30,3,0,0};g.control={0,config.spawn_x+40,config.spawn_y-14,0};
    g.exit=config.exit;g.targets=config.targets;
    s.gameplay.reset(s.epoch,{static_cast<unsigned>(config.targets[0][2]),static_cast<unsigned>(config.targets[1][2]),static_cast<unsigned>(config.targets[2][2])});
    s.presented_hp=s.gameplay.state().hp;s.hit_started={};
    constexpr float area=80+16*3.14159265358979323846F;
    g.body={2.65F/(area*config.relative_density),config.relative_density,area,0};
    const std::vector<std::byte> empty(mission_buffer_size-32);
    s.upload(6,32,static_cast<GLsizeiptr>(empty.size()),empty.data());
    s.upload(6,32,sizeof(g),&g);s.snapshot=g;s.controls={0,g.control[1],g.control[2],0};s.mission_enabled=true;s.grains_enabled=true;
}
void ParticleWorld::restore_coupling_checkpoint(const MissionSnapshot& checkpoint,std::uint64_t tick){
    if(checkpoint.status[0]==0||checkpoint.events[0]!=0||checkpoint.counts[2]!=0)
        throw std::invalid_argument{"Coupling checkpoint requires an active scene without projectile/destruction events"};
    for(float value:checkpoint.actor)if(!std::isfinite(value))throw std::invalid_argument{"Nonfinite coupling checkpoint"};
    auto& s=*impl_;s.reset_body();s.explosions.clear();s.next_generation();s.gameplay.reset(s.epoch);++s.readback_revision;s.readback_refresh=false;
    auto state=checkpoint;state.status[1]=s.epoch;s.upload(6,32,sizeof(state),&state);s.snapshot=state;
    s.gameplay.reset(s.epoch,{static_cast<unsigned>(state.targets[0][2]),static_cast<unsigned>(state.targets[1][2]),static_cast<unsigned>(state.targets[2][2])},state.status[2]);
    s.presented_hp=s.gameplay.state().hp;s.hit_started={};
    s.metrics.steps=tick;s.controls={0,state.control[1],state.control[2],0};s.mission_enabled=true;s.grains_enabled=true;
}
std::expected<void,std::string> ParticleWorld::validate_gameplay_protocol(const std::filesystem::path& output){
    try{
        std::filesystem::create_directories(output);std::ofstream report{output/"gameplay-tests.txt"};
        std::ofstream timing{output/"gameplay-timing.csv"};timing<<"tick,cpu_submit_ms,cpu_logic_ms,pending_events,ack_tick\n";
        auto& s=*impl_;
        const auto require=[](bool ok,const char* text){if(!ok)throw std::runtime_error{text};};
        const auto drain=[&](){glFinish();poll();}; // diagnostic only
        const auto tick=[&](MissionInput input=MissionInput{}){require(step(input),"Unexpected event backpressure in bounded fixture");};
        // Check actual driver std430 offsets of fields used by the kernels.
        const auto resource=reinterpret_cast<PFNGLGETPROGRAMRESOURCEINDEXPROC>(SDL_GL_GetProcAddress("glGetProgramResourceIndex"));
        const auto properties=reinterpret_cast<PFNGLGETPROGRAMRESOURCEIVPROC>(SDL_GL_GetProcAddress("glGetProgramResourceiv"));
        require(resource&&properties,"GL 4.3 reflection unavailable");
        const auto program=s.mission_kernels[0].id;
        const std::array<std::pair<const char*,std::size_t>,8> offsets{{
            {"commandBatch.header",command_offset},{"commandBatch.commands[0].identity",command_offset+64},
            {"commandBatch.commands[0].detail",command_offset+80},{"commandBatch.commands[0].payload",command_offset+96},
            {"eventBatch.header",event_offset},{"eventBatch.events[0].identity",event_offset+16},
            {"eventBatch.events[0].detail",event_offset+32},{"eventBatch.events[0].payload",event_offset+48}}};
        for(const auto& [name,offset]:offsets){
            const auto id=resource(program,GL_BUFFER_VARIABLE,name);require(id!=GL_INVALID_INDEX,"Protocol reflection field missing");
            constexpr GLenum property=GL_OFFSET;GLint value{};properties(program,GL_BUFFER_VARIABLE,id,1,&property,1,nullptr,&value);
            report<<name<<"="<<value<<'\n';require(value==static_cast<GLint>(offset),"C++/GLSL protocol offset mismatch");
        }
        report<<"abi_reflection=PASS\n";
        report<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nversion="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<'\n';
        // The GL 4.3 fallback transports the same real payload without mapping.
        {
            const auto supported=s.gl.persistent_storage;s.gl.persistent_storage=false;
            detail::EventRing fallback{s.gl};fallback.initialize();s.gl.persistent_storage=supported;
            game::GameplaySession session;session.reset(42);const auto commands=session.prepare({0,54,177,fire});
            game::EventBatch batch;batch.header={1,1,42,0};batch.events[0]={42,1,1,1,commands.commands[0].id,0,0,0,{},{}};
            s.upload(6,event_offset,sizeof(batch),&batch);fallback.submit(s.buffers[6],event_offset,42,1);
            glFinish();fallback.poll(session);
            require(session.state().shots==1&&session.pending()==0&&fallback.stats().acknowledged==1,"GL 4.3 reliable fallback payload lost");
        }
        report<<"event_gl43_fallback=PASS\n";
        // Retransmitted command IDs never create another physical projectile.
        reset(Scene::empty);configure_mission({});s.controls={0,54,177,0};
        auto first=s.gameplay.prepare({0,54,177,fire});s.upload(6,command_offset,sizeof(first),&first);s.mission_dispatch(0);
        s.event_ring.submit(s.buffers[6],event_offset,s.epoch,1);
        auto duplicate=s.gameplay.prepare({});duplicate.header[2]=1;duplicate.commands[0]=first.commands[0];duplicate.commands[0].tick=2;
        s.upload(6,command_offset,sizeof(duplicate),&duplicate);s.mission_dispatch(0);s.event_ring.submit(s.buffers[6],event_offset,s.epoch,2);drain();
        std::array<std::array<float,8>,bullet_capacity> duplicate_bullets{};
        s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,32+sizeof(MissionSnapshot),sizeof(duplicate_bullets),duplicate_bullets.data());
        require(std::ranges::count_if(duplicate_bullets,[](const auto& b){return b[4]>0;})==1&&gameplay().state().shots==1,"Duplicate command executed twice");
        report<<"gpu_command_dedup=PASS\n";
        // All 128 bullets can report a hit in one tick, plus command acks.
        reset(Scene::empty);configure_mission({});
        std::array<std::array<float,8>,bullet_capacity> simultaneous{};
        for(auto& bullet:simultaneous){bullet[0]=50;bullet[1]=177;bullet[2]=600;bullet[4]=1;}
        s.upload(6,32+sizeof(MissionSnapshot),sizeof(simultaneous),simultaneous.data());tick({0,54,177,fire|grenade|jump});drain();
        require(gameplay().state().score==1&&gameplay().state().ammunition==30&&gameplay().pending()==0,"Maximum hit batch lost events/reservations");
        report<<"128_simultaneous_hits_and_acks=PASS\n";

        // Refuse admission with a full staging queue. Leave actual GPU fences
        // unsignalled to the consumer using a validation-only hold flag.
        reset(Scene::empty);configure_mission({});s.hold_events=true;
        for(unsigned i=0;i<game::event_slots;++i)tick({0,54,177,i==0?fire|grenade:0U});
        const auto before=metrics().steps;const auto ammo=gameplay().state().ammunition;
        require(!step({0,54,177,fire})&&metrics().steps==before&&gameplay().state().ammunition==ammo,"Full ring consumed tick or ammo");
        s.hold_events=false;drain();require(gameplay().acknowledged_tick()==game::event_slots,"Batched events incomplete");
        require(gameplay().state().shots==1&&gameplay().pending()==0,"Command acknowledgement incomplete");
        report<<"full_ring_admission_and_batched_ack=PASS\n";
        // Projectile pool rejection refunds the exact reservation.
        reset(Scene::empty);configure_mission({});
        std::array<std::array<float,8>,bullet_capacity> full{};
        for(auto& p:full){p[0]=100;p[1]=100;p[4]=1;}
        s.upload(6,32+sizeof(MissionSnapshot),sizeof(full),full.data());tick({0,54,177,fire});drain();
        require(gameplay().state().ammunition==30&&gameplay().state().shots==0&&gameplay().pending()==0,"Rejected rifle did not refund");
        report<<"projectile_rejection_refund=PASS\n";
        // Pending old-generation commands cannot affect a new mission.
        s.hold_events=true;tick({0,54,177,grenade});configure_mission({});s.hold_events=false;drain();
        require(gameplay().state().grenades==3&&gameplay().state().score==0&&gameplay().pending()==0,"Reset leaked reservation/event");
        report<<"reset_in_flight=PASS\n";
        // Simultaneous fuses are serialized, every detonation reaches the CPU.
        std::array<std::array<float,8>,grenade_capacity> grenades{};
        for(unsigned i=0;i<grenades.size();++i){grenades[i][0]=100+static_cast<float>(i);grenades[i][1]=100;grenades[i][4]=.001F;}
        s.upload(6,32+sizeof(MissionSnapshot)+bullet_capacity*32,sizeof(grenades),grenades.data());
        for(unsigned i=0;i<grenade_capacity+2;++i)tick();
        drain();
        require(gameplay().state().explosions==grenade_capacity,"Simultaneous detonations lost/duplicated");
        report<<"simultaneous_detonations=PASS\n";
        // Coyote jump on current GPU state; no HUD poll is used for admission.
        reset(Scene::empty);MissionConfig airborne;airborne.spawn_y=100;configure_mission(airborne);
        float air_time=.05F;s.upload(6,32+offsetof(MissionSnapshot,timers)+8,4,&air_time);
        tick({0,54,100,jump});drain();require(read_mission().actor[3]<-60,"Coyote jump failed");
        tick({0,54,100,jump});drain();require(gameplay().pending()==1,"Jump reused coyote allowance");
        cancel_mission_input();tick();drain();require(gameplay().pending()==0,"Focus cancellation left buffered jump");
        report<<"coyote_and_focus_cancel=PASS\n";
        // Press shortly before landing: contact from terrain/sand is consumed
        // once on the GPU, within the 12-tick input window.
        reset(Scene::empty);airborne.spawn_y=186;configure_mission(airborne);
        air_time=1;s.upload(6,32+offsetof(MissionSnapshot,timers)+8,4,&air_time);
        std::array<float,4> falling{24,186,0,50};s.upload(6,32+offsetof(MissionSnapshot,actor),16,falling.data());
        tick({0,54,177,jump});for(unsigned i=0;i<10;++i)tick();drain();
        require(read_mission().actor[3]<-30&&gameplay().pending()==0,"Landing jump buffer failed");
        report<<"landing_buffer=PASS\n";
        reset(Scene::empty);airborne.spawn_y=100;configure_mission(airborne);
        air_time=1;s.upload(6,32+offsetof(MissionSnapshot,timers)+8,4,&air_time);
        tick({0,54,177,jump});for(unsigned i=0;i<14;++i)tick();drain();
        require(gameplay().pending()==0&&read_mission().actor[3]>0,"Expired jump was executed in the air");
        report<<"airborne_buffer_expiry=PASS\n";

        // Three render frames of delayed consumption; same tick-indexed physical
        // script at 30/60/144 Hz. World trajectory must not authorize CPU rules.
        MissionSnapshot reference{};
        for(unsigned fps:{30U,60U,144U}){
            reset(Scene::empty);configure_mission({});
            unsigned submitted=0,frame=0;s.hold_events=true;
            double accumulator=0;
            while(submitted<240){
                accumulator+=120.0/fps;unsigned count=static_cast<unsigned>(accumulator+1e-9);accumulator-=count;
                for(unsigned j=0;j<count&&submitted<240;++j){
                    tick({0,54,177,submitted<60?fire:0U});++submitted;
                    const auto stats=event_stats();timing<<submitted<<','<<metrics().cpu_submit_ms<<','<<metrics().gameplay_ms<<','<<stats[3]<<','<<gameplay().acknowledged_tick()<<'\n';
                }
                if(++frame%3==0){s.hold_events=false;drain();s.hold_events=true;}
            }
            s.hold_events=false;drain();const auto result=read_mission();
            require(result.inventory[2]==1&&gameplay().pending()==0,"Delayed frame route score/ack");
            if(fps==30)reference=result;else require(result.inventory==reference.inventory&&std::hypot(result.actor[0]-reference.actor[0],result.actor[1]-reference.actor[1])<.01,"Frame-dependent physical route");
            report<<"delayed_"<<fps<<"_fps=PASS\n";
        }

        // Stair slopes and a moving granular support use live GPU contacts.
        std::vector<std::uint32_t> slope(width*height);
        for(int x=0;x<width;++x)for(int y=187-std::min(12,x/8);y<height;++y)slope[y*width+x]=1;
        load({},slope);MissionConfig on_slope;on_slope.spawn_x=72;on_slope.spawn_y=170;configure_mission(on_slope);
        for(unsigned i=0;i<80;++i){tick();if(i%8==7)drain();}drain();require(read_mission().locomotion[2]>.5F,"Slope support missing");
        tick({0,100,150,jump});drain();require(read_mission().actor[3]<-60,"Slope jump failed");report<<"slope_jump=PASS\n";
        std::vector<Grain> support;
        for(int y=174;y<187;++y)for(int x=15;x<60;++x){Grain grain;grain.position={x+.5F,y+.5F,x+.5F,y+.5F};grain.motion={0,0,1,1};support.push_back(grain);}
        std::vector<std::uint32_t> floor(width*height);for(int y=187;y<height;++y)for(int x=0;x<width;++x)floor[y*width+x]=1;
        load(support,floor);MissionConfig on_sand;on_sand.spawn_x=35;on_sand.spawn_y=165;configure_mission(on_sand);
        for(unsigned i=0;i<160;++i){tick({i>100?.3F:0.F,80,150,0});if(i%8==7)drain();}drain();require(read_mission().locomotion[2]>.5F,"Moving sand support missing");
        tick({.3F,80,150,jump});drain();require(read_mission().actor[3]<-40,"Moving sand jump failed");report<<"moving_sand_jump=PASS\n";
        // Completed previous GPU work is drained outside CPU timing. Each measured
        // step includes the previous packet's consumption, new commands and submit.
        std::ofstream contract_csv{output/"full-mission-contract.csv"};contract_csv<<"trial,tick,cpu_contract_ms,cpu_logic_ms,gpu_ms,event_depth\n";
        for(unsigned trial=1;trial<=3;++trial){
            reset_mission();
            for(unsigned i=0;i<120;++i){glFinish();tick();}glFinish();poll(false);static_cast<void>(take_timings());
            for(unsigned i=0;i<240;++i){
                glFinish();tick();contract_csv<<trial<<','<<i+1<<','<<metrics().cpu_submit_ms<<','<<metrics().gameplay_ms<<','<<metrics().gpu_ms<<','<<event_stats()[3]<<'\n';
            }drain();
        }
        report<<"full_mission_contract_trials=3\n";
        report.flush();return {};
    }catch(const std::exception& e){impl_->hold_events=false;return std::unexpected{e.what()};}
}
void ParticleWorld::reset_mission(){
    std::vector<std::uint32_t> walls(width*height);std::vector<Grain> grains;
    auto box=[&](int x0,int y0,int x1,int y1,unsigned kind=1){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)walls[y*width+x]=kind;};
    box(0,187,319,191);box(0,0,0,191);box(319,0,319,191);
    // Raised reservoir with a breakable outlet; accessible underpass to its right.
    box(81,111,82,186);box(83,158,132,160);box(132,111,133,157,2);
    // Suspended sand silo. The orange floor is the destructible support.
    box(210,80,211,126);box(248,80,249,126);box(212,125,247,126,2);
    auto fill=[&](int x0,int y0,int x1,int y1,int kind){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const auto [px,py]=seeded_position(x,y);Grain p;p.position={px,py,px,py};
        p.motion={0,0,kind==5?2.65F:kind==6?3.3125F:1.F,static_cast<float>(kind)};grains.push_back(p);}};
    fill(84,123,130,156,5);fill(84,115,130,122,6);fill(213,91,246,123,1);
    load(grains,walls);configure_mission({});render(1);
}
std::vector<Grain> ParticleWorld::readback()const{
    auto& s=*impl_;std::vector<Grain> result(capacity);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[s.current]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(result.size()*sizeof(Grain)),result.data());
    if(s.execution.soa){
        std::vector<std::array<float,4>> packed(capacity*4);
        std::memcpy(packed.data(),result.data(),capacity*sizeof(Grain));
        for(unsigned i=0;i<capacity;++i)result[i]={packed[i],packed[capacity+i],packed[2*capacity+i],packed[3*capacity+i]};
    }return result;
}
NeighborSnapshot ParticleWorld::read_neighbors(){
    auto& s=*impl_;if(s.execution.neighbors==NeighborMode::linked)throw std::logic_error{"Sorted index required"};
    s.invalidate_bindings();s.bins();NeighborSnapshot result;std::vector<GLuint> cells(16+3*width*height+480);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[2]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(cells.size()*4),cells.data());
    result.displacement=std::bit_cast<float>(cells[0]);result.builds=cells[1];
    result.starts.resize(width*height+1);result.counts.resize(width*height);
    for(unsigned i=0;i<width*height;++i){result.counts[i]=cells[16+i];result.starts[i]=cells[16+width*height+i]+cells[16+3*width*height+240+i/256];}
    result.starts.back()=result.starts[width*height-1]+result.counts.back();
    result.order.resize(capacity);result.indices.resize(capacity);result.anchors.resize(capacity);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[3]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,capacity*4,result.order.data());
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,capacity*4,capacity*4,result.indices.data());
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,capacity*8,capacity*16,result.anchors.data());return result;
}
std::array<std::uint32_t,240> ParticleWorld::read_phase_mask(){
    auto& s=*impl_;
    if(s.execution.neighbors==NeighborMode::linked||!s.execution.phase_gating)
        throw std::logic_error{"Sorted phase-gated index required"};
    // Keep the same anchor generation as the neighbor index. With reuse enabled
    // bins() preserves the cached index and mask after bounded particle motion.
    s.invalidate_bindings();s.bins();std::array<std::uint32_t,240> result{};
    constexpr auto offset=static_cast<GLintptr>((16+3*width*height+480)*4);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    GLuint staging{};s.gl.GenBuffers(1,&staging);s.gl.BindBuffer(GL_COPY_WRITE_BUFFER,staging);
    s.gl.BufferData(GL_COPY_WRITE_BUFFER,sizeof(result),nullptr,GL_STREAM_READ);
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,s.buffers[2]);
    s.gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,offset,0,sizeof(result));
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,staging);s.gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(result),result.data());
    s.gl.DeleteBuffers(1,&staging);return result;
}
void ParticleWorld::start_diagnostic_trace(const TraceConfig& config){
    auto& s=*impl_;
    if((config.mode==CostMode::bounds||config.mode==CostMode::snapshots)&&s.execution.neighbors!=NeighborMode::reused)
        throw std::invalid_argument{"Spatial trace requires the production reused index"};
    const auto cells=static_cast<std::size_t>(16+3*width*height+480+(s.execution.phase_gating?240:0))*4;
    s.trace.start(config,capacity*sizeof(Grain),cells,capacity*24);
}
TraceReport ParticleWorld::read_diagnostic_trace(){impl_->trace_step=false;return impl_->trace.read();}
void ParticleWorld::capture_diagnostic_snapshot(std::string label){
    auto& s=*impl_;
    if(!s.bins_valid||s.execution.neighbors!=NeighborMode::reused)throw std::logic_error{"Snapshot requires an existing valid reused index"};
    s.trace.capture(std::move(label),s.epoch,s.metrics.steps,0,s.execution.soa,s.buffers[s.current],s.buffers[2],s.buffers[3]);
}
HotAudit ParticleWorld::audit_hot_view(){
    auto& s=*impl_;HotAudit result;
    if(s.read_mode()!=NeighborReads::sorted)return result;
    result.indexed=s.bins_valid;if(!result.indexed)return result;
    struct alignas(16) HotEntry {std::array<float,4> geometry,motion;};static_assert(sizeof(HotEntry)==32);
    constexpr auto view_bytes=capacity*sizeof(HotEntry),index_bytes=capacity*sizeof(std::uint32_t);
    std::vector<HotEntry> view(capacity);std::vector<std::uint32_t> indices(capacity);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    GLuint staging{};s.gl.GenBuffers(1,&staging);s.gl.BindBuffer(GL_COPY_WRITE_BUFFER,staging);
    s.gl.BufferData(GL_COPY_WRITE_BUFFER,view_bytes+index_bytes,nullptr,GL_STREAM_READ);
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,s.buffers[3]);
    s.gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,static_cast<GLintptr>(capacity*24+s.current*view_bytes),0,view_bytes);
    s.gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,capacity*4,view_bytes,index_bytes);
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,staging);s.gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,view_bytes,view.data());
    s.gl.GetBufferSubData(GL_COPY_READ_BUFFER,view_bytes,index_bytes,indices.data());s.gl.DeleteBuffers(1,&staging);
    const auto actual=readback();std::vector<bool> used(capacity);
    const auto count=static_cast<unsigned>(std::ranges::count_if(actual,[](const auto& p){return p.motion[3]>.5F;}));
    for(unsigned id=0;id<capacity;++id)if(actual[id].motion[3]>.5F){
        ++result.active;const auto entry=indices[id];
        if(entry>=count||used[entry]){++result.mismatches;continue;}used[entry]=true;
        const auto& p=actual[id];const std::array<float,4> geometry{p.position[0],p.position[1],p.residual[0],p.residual[1]};
        if(std::bit_cast<std::array<std::uint32_t,4>>(geometry)!=std::bit_cast<std::array<std::uint32_t,4>>(view[entry].geometry)
            ||std::bit_cast<std::array<std::uint32_t,4>>(p.motion)!=std::bit_cast<std::array<std::uint32_t,4>>(view[entry].motion))++result.mismatches;
    }
    return result;
}
std::vector<std::array<float,4>> ParticleWorld::audit_neighbors(bool pairs){
    auto& s=*impl_;if(s.execution.neighbors==NeighborMode::linked)throw std::logic_error{"Sorted index required"};s.invalidate_bindings();s.bins();
    std::vector<std::array<float,4>> result(pairs?65536:capacity);
    if(pairs){s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);s.upload(7,0,static_cast<GLsizeiptr>(result.size()*16),result.data());}
    s.neighbor_dispatch(pairs?5U:7U,pairs?1U:128U);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[7]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(result.size()*16),result.data());return result;
}
void ParticleWorld::displace_for_test(std::span<const std::array<float,4>> deltas){
    if(deltas.size()>capacity)throw std::invalid_argument{"Too many test displacements"};
    auto& s=*impl_;if(s.execution.neighbors==NeighborMode::linked)throw std::logic_error{"Sorted index required"};s.invalidate_bindings();s.bins();std::vector<std::array<float,4>> packed(capacity);std::ranges::copy(deltas,packed.begin());
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);s.upload(7,0,capacity*16,packed.data());
    s.neighbor_dispatch(6,128);s.current=1-s.current;
}
BodySnapshot ParticleWorld::read_body(){
    auto& s=*impl_;if(s.execution.coupling==CouplingMode::reference)throw std::logic_error{"Compact coupling required"};BodySnapshot result;s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    // Copy both headers and command bytes to fresh staging. Validation must not
    // migrate the simulation SSBO to host memory or read consumed argument shadows.
    GLuint staging{};s.gl.GenBuffers(1,&staging);s.gl.BindBuffer(GL_COPY_WRITE_BUFFER,staging);s.gl.BufferData(GL_COPY_WRITE_BUFFER,sizeof(result),nullptr,GL_STREAM_READ);
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,s.buffers[3]);s.gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,static_cast<GLintptr>(s.body_offset()),0,64);
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,s.buffers[5]);s.gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,detail::command_offset(detail::body_reduce_command),64,24);
    s.gl.BindBuffer(GL_COPY_READ_BUFFER,staging);s.gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(result),&result);s.gl.DeleteBuffers(1,&staging);return result;
}
void ParticleWorld::inject_body_for_test(std::span<const std::array<float,4>> reactions,bool apply){
    auto& s=*impl_;if(s.execution.coupling==CouplingMode::reference||reactions.size()>2*capacity)throw std::invalid_argument{"Invalid compact body test batch"};
    s.invalidate_bindings();s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    if(!reactions.empty())s.upload(7,capacity*16,static_cast<GLsizeiptr>(reactions.size_bytes()),reactions.data());
    auto& k=s.prepare(s.coupling_kernels,2,false,true);s.gl.Uniform1i(k.test_count,static_cast<GLint>(reactions.size()));
    s.gl.DispatchCompute(static_cast<GLuint>(std::max<std::size_t>(1,(reactions.size()+k.local_size-1)/k.local_size)),1,1);
    s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
    if(apply)s.mission_dispatch(3);
}
#include "gpu/render_validation.inl"
#include "gpu/vfx_replay.inl"
#include "gpu/vfx_benchmark.inl"
#include "gpu/vfx_validation.inl"

void ParticleWorld::save_image(const std::filesystem::path& path)const{impl_->renderer.save_image(path);}
std::vector<double> ParticleWorld::take_timings(){return std::exchange(impl_->timings,{});}
std::vector<std::array<float,4>> ParticleWorld::read_fluid_terms(){
    auto& s=*impl_;s.invalidate_bindings();s.bins();s.liquid_dispatch(3);s.liquid_dispatch(2);
    std::vector<std::array<float,4>> result(capacity);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[7]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,capacity*16,result.data());return result;
}
std::vector<float> ParticleWorld::read_boundary_weights()const{
    auto& s=*impl_;std::vector<float> result(width*height,1);
    if(s.execution.fluid.boundary==FluidBoundary::volume){
        s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[4]);
        s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,width*height*16,static_cast<GLsizeiptr>(result.size()*sizeof(float)),result.data());s.invalidate_bindings();
    }
    return result;
}
std::vector<std::array<float,4>> ParticleWorld::fluid_fields_for_test(){
    auto& s=*impl_;if(!s.execution.fluid.fields())throw std::logic_error{"Fluid fields disabled."};
    s.invalidate_bindings();s.bins();s.liquid_dispatch(5);s.liquid_dispatch(6);
    std::vector<std::array<float,4>> result(capacity*2);
    const auto offset=capacity*(s.execution.coupling==CouplingMode::reference?32U:48U);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[7]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,offset,static_cast<GLsizeiptr>(result.size()*sizeof(result[0])),result.data());s.invalidate_bindings();return result;
}
void ParticleWorld::apply_fluid_velocities_for_test(){
    auto& s=*impl_;s.invalidate_bindings();s.bins();
    if(s.execution.fluid.fields()){s.liquid_dispatch(5);s.liquid_dispatch(6);}
    s.liquid_dispatch(4,true);if(s.mission_enabled&&s.execution.fluid.velocity_reaction())s.mission_dispatch(3);
}
void ParticleWorld::project_liquids_for_test(unsigned iterations){
    auto& s=*impl_;s.invalidate_bindings();for(unsigned i=0;i<iterations;++i){s.bins();s.liquid_dispatch(0);s.liquid_dispatch(1,true);if(s.mission_enabled)s.mission_dispatch(3);}
}
}


#include "gpu/thermal_validation.inl"

#include "gpu/thermal_scenes.inl"

#include "gpu/thermal_benchmark.inl"

#include "gpu/lighting_validation.inl"
