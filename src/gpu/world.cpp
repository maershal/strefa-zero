#include "gpu/world.hpp"
#include "gpu/gl_api.hpp"
#include "gpu/readback_ring.hpp"
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

namespace strefa::gpu {
namespace {
using detail::Api;
std::string read_text(const std::filesystem::path& path){
    std::ifstream input{path,std::ios::binary};if(!input)throw std::runtime_error{"Brak shadera: "+path.string()};
    std::string text{std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
    // Fixed, local shader dependencies; recursive expansion supports shared ABI.
    for(const std::string name:{"particle_common.glsl","mission_state.glsl","particle_storage.glsl","body_terrain.glsl"}){
        const std::string directive="#include \""+name+"\"";
        if(const auto at=text.find(directive);at!=std::string::npos)
            text.replace(at,directive.size(),read_text(path.parent_path()/name));
    }
    return text;
}
GLuint program(Api& gl,std::initializer_list<std::pair<GLenum,std::filesystem::path>> stages,const std::string& defines={}){
    const auto result=gl.CreateProgram();std::vector<GLuint> shaders;
    try {
        for(const auto& [kind,path]:stages){
            const auto shader=gl.CreateShader(kind);shaders.push_back(shader);
            auto text=read_text(path);
            if(!defines.empty())text.insert(text.find('\n')+1,defines);
            const auto* pointer=text.c_str();
            gl.ShaderSource(shader,1,&pointer,nullptr);gl.CompileShader(shader);
            GLint valid{};gl.GetShaderiv(shader,GL_COMPILE_STATUS,&valid);
            if(!valid){std::array<char,8192> log{};gl.GetShaderInfoLog(shader,static_cast<GLsizei>(log.size()),nullptr,log.data());throw std::runtime_error{path.string()+"\n"+defines+": "+log.data()};}
            gl.AttachShader(result,shader);
        }
        gl.LinkProgram(result);GLint valid{};gl.GetProgramiv(result,GL_LINK_STATUS,&valid);
        if(!valid){std::array<char,8192> log{};gl.GetProgramInfoLog(result,static_cast<GLsizei>(log.size()),nullptr,log.data());throw std::runtime_error{log.data()};}
    }catch(...){for(auto shader:shaders)gl.DeleteShader(shader);gl.DeleteProgram(result);throw;}
    for(auto shader:shaders)gl.DeleteShader(shader);return result;
}
std::uint32_t hash(std::uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;return x^(x>>16);}
std::array<float,2> seeded_position(int x,int y){
    const auto id=static_cast<std::uint32_t>(y*width+x);
    return {x+.5F+(static_cast<float>(hash(2*id)&65535U)/65535.F-.5F)*.12F,
            y+.5F+(static_cast<float>(hash(2*id+1)&65535U)/65535.F-.5F)*.12F};
}
}
struct ParticleWorld::Impl {
    Api gl;
    detail::UploadRing uploads{gl};
    detail::ReadbackRing readbacks{gl};
    detail::DiagnosticTrace trace{gl};
    bool trace_step{};
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
        GLint operation{-1},dirty{-1},substep{-1},stroke{-1},radius{-1},material{-1},input{-1},coupling{-1},test_count{-1};
        GLint hot_read{-1},hot_write{-1},hot_valid{-1};
        int hot_read_value{-2},hot_write_value{-2},hot_valid_value{-2};
        unsigned family{};
        int dirty_value{-1};
    };
    std::array<Kernel,21> granular;
    std::array<Kernel,5> liquids;
    std::array<Kernel,8> neighbors;
    bool bins_valid{};
    NeighborReads read_mode()const{return effective_neighbor_reads(execution.neighbor_reads,execution.neighbors);}
    std::string layout_defines()const{return std::string{"#define PARTICLE_SOA "}+(execution.soa?"1\n":"0\n")+"#define SORTED_NEIGHBORS "+(execution.neighbors!=NeighborMode::linked?"1\n":"0\n")+"#define REUSE_NEIGHBORS "+(execution.neighbors==NeighborMode::reused?"1\n":"0\n")+"#define COUPLING_MODE "+std::to_string(static_cast<int>(execution.coupling))+"\n#define REDUCTION_LOCAL_SIZE "+std::to_string(execution.reduction_size)+"u\n#define PHASE_GATING "+(execution.phase_gating?"1\n":"0\n")+"#define SKIP_ZERO_PROJECTION "+(execution.skip_zero_projection?"1\n":"0\n")+"#define HOT_READS "+std::to_string(static_cast<int>(read_mode()))+"\n";}
    std::array<Kernel,9> mission_kernels;
    std::array<Kernel,3> coupling_kernels;
    GLuint bound_program{},bound_indirect{};
    std::array<GLuint,8> bound_bases{};
    Stroke brush;
    int half_step{};
    void invalidate_bindings(){bound_program=0;bound_indirect=0;bound_bases.fill(0);}
    void compile_kernels(const std::filesystem::path& shaders){
        const auto compile=[&](auto& kernels,const char* file,std::initializer_list<unsigned> operations){
            for(unsigned op:operations){
                const bool separate=execution.specialized||(std::string_view{file}=="neighbors.comp"||std::string_view{file}=="coupling.comp");
                if(!separate&&op!=*operations.begin())continue;
                auto& k=kernels[separate?op:0];
                const bool scalar=(std::string_view{file}=="granular.comp"&&(op==10||op==12||op==20))
                    ||(std::string_view{file}=="liquids.comp"&&op==3)
                    ||(std::string_view{file}=="mission.comp"&&(op==0||op==1||op==4));
                k.local_size=execution.specialized&&scalar?1U:execution.local_size;
                if(std::string_view{file}=="mission.comp"&&op==3&&execution.specialized)k.local_size=execution.reduction_size;
                if(std::string_view{file}=="coupling.comp")k.local_size=execution.reduction_size;
                auto defines="#define LOCAL_SIZE "+std::to_string(k.local_size)+"\n#define PARTICLE_LOCAL_SIZE "+std::to_string(execution.local_size)+"u\n#define GPU_CONDITIONAL_WORK "+(execution.conditional_work?"1\n":"0\n");
                defines+=layout_defines();
                if(separate)defines+="#define PASS_OPERATION "+std::to_string(op)+"\n#define PASS_"+std::string{file}.substr(0,std::string{file}.find('.'))+"_"+std::to_string(op)+" 1\n";
                if(std::string_view{file}=="neighbors.comp")k.local_size=256;
                k.id=program(gl,{{GL_COMPUTE_SHADER,shaders/file}},defines);
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
        compile(liquids,"liquids.comp",{0,1,2,3,4});
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
        if(!execution.cached_bindings){gl.UseProgram(granular[0].id);if(k.id!=granular[0].id)gl.UseProgram(k.id);}
        else if(bound_program!=k.id){gl.UseProgram(k.id);bound_program=k.id;}
        for(unsigned i=0;i<buffers.size();++i){
            const auto buffer=i==0?buffers[current]:(i==1?buffers[in_place?current:1-current]:buffers[i]);
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
    GLuint draw{},fbo{},image{},vao{},actor_program{};
    GLint actor_alpha{},actor_parts{},actor_weapons{},actor_weapon_size{};
    unsigned parts_texture{},weapons_texture{};int weapon_width{256},weapon_height{128};
    bool mission_enabled{};unsigned epoch{};MissionSnapshot snapshot{};std::deque<Explosion> explosions;MissionInput controls{};
    GLuint splat{},surface{},fluid_fbo{},fluid_image{};
    GLint splat_alpha{};
    bool fluids_enabled{},grains_enabled{};
    GLint alpha{},terrain_pass{};
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
        for(auto& kernels:{std::span<Kernel>{granular},std::span<Kernel>{liquids},std::span<Kernel>{mission_kernels},std::span<Kernel>{neighbors},std::span<Kernel>{coupling_kernels}})for(const auto& k:kernels)if(k.id)gl.DeleteProgram(k.id);
        if(actor_program)gl.DeleteProgram(actor_program);if(draw)gl.DeleteProgram(draw);
        if(splat)gl.DeleteProgram(splat);if(surface)gl.DeleteProgram(surface);
        if(fluid_fbo)gl.DeleteFramebuffers(1,&fluid_fbo);if(fluid_image)glDeleteTextures(1,&fluid_image);
        if(vao)gl.DeleteVertexArrays(1,&vao);if(fbo)gl.DeleteFramebuffers(1,&fbo);
        if(image)glDeleteTextures(1,&image);
        if(buffers[0])gl.DeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
    }
    void dispatch(int op,unsigned count,bool swap=false,bool in_place=false,bool conditional=false){
        if(count==0)return;
        auto& k=prepare(granular,op,in_place);
        if(k.dirty>=0&&k.dirty_value!=static_cast<int>(conditional)){gl.Uniform1i(k.dirty,conditional?1:0);k.dirty_value=conditional?1:0;}
        if(op==1&&k.substep>=0)gl.Uniform1i(k.substep,half_step);
        if(op==5||op==7||op==8){
            if(k.stroke>=0)gl.Uniform4f(k.stroke,brush.x0,brush.y0,brush.x1,brush.y1);
            if(k.radius>=0)gl.Uniform1f(k.radius,std::clamp(brush.radius,1.F,16.F));
            if(k.material>=0)gl.Uniform1i(k.material,static_cast<int>(brush.material));
        }
        const auto token=trace.begin(PassFamily::granular,static_cast<unsigned>(op),epoch,metrics.steps+(trace_step?1:0));
        if(op>=1&&op<=4)indirect(detail::active_command);
        else if(execution.conditional_work&&conditional&&(op==11||op==19||op==9))
            indirect(op==11?detail::rebuild_command:op==19?detail::sync_command:detail::terrain_command);
        else gl.DispatchCompute((count+k.local_size-1)/k.local_size,1,1);
        GLbitfield barriers=GL_SHADER_STORAGE_BARRIER_BIT;
        if(op==12||(op==10&&execution.conditional_work)||(op==3&&execution.coupling!=CouplingMode::reference))barriers|=GL_COMMAND_BARRIER_BIT;
        gl.MemoryBarrier(barriers);
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
        if(op==3)gl.DispatchCompute(1,1,1);else indirect(detail::active_command);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|((op==1&&execution.coupling!=CouplingMode::reference)?GL_COMMAND_BARRIER_BIT:0));if(swap)current=1-current;
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
        if(op==0)gl.Uniform4f(k.input,std::clamp(controls.move,-1.F,1.F),controls.aim_x,controls.aim_y,static_cast<float>(controls.buttons&31U));
        const auto token=trace.begin(PassFamily::mission,static_cast<unsigned>(op),epoch,metrics.steps+(trace_step?1:0));
        if(execution.conditional_work&&(op==3||op==5||op==6||op==7))
            indirect(op==3?detail::reduction_command:op==5?detail::blast_terrain_command:op==6?detail::debris_command:detail::blast_command);
        else if(active)indirect(detail::active_command);
        else gl.DispatchCompute((count+k.local_size-1)/k.local_size,1,1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|(((op==0&&execution.conditional_work)||(op==8&&execution.coupling!=CouplingMode::reference))?GL_COMMAND_BARRIER_BIT:0));
        if(swap)current=1-current;
        trace.end(token,bins_valid&&op!=6,execution.soa,buffers[current],buffers[2],buffers[3]);
    }
    std::size_t body_offset()const{return execution.neighbors==NeighborMode::linked?capacity*4:capacity*24+(read_mode()==NeighborReads::sorted?capacity*64:0);}
    std::size_t index_buffer_size()const{return body_offset()+(execution.coupling==CouplingMode::reference?0:detail::body_metadata_size);}
    std::size_t terms_buffer_size()const{return capacity*(execution.coupling==CouplingMode::reference?32U:48U);}
    void reset_body(){
        const std::array<GLuint,16> zero{};const std::array<GLuint,6> commands{0,1,1,0,1,1};
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        if(execution.coupling!=CouplingMode::reference)upload(3,static_cast<GLintptr>(body_offset()),sizeof(zero),zero.data());
        upload(5,detail::command_offset(detail::body_reduce_command),sizeof(commands),commands.data());
    }
    void upload(unsigned index,GLintptr offset,GLsizeiptr bytes,const void* data){uploads.write(buffers[index],offset,bytes,data);}
    void active_draw(){
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,5,buffers[5]);
        gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER,buffers[5]);gl.DrawArraysIndirect(GL_TRIANGLE_STRIP,reinterpret_cast<void*>(capacity*8+12));
    }
    void stage_count(bool force=false){
        if(profile_options.collect)++calls.stage_requests;
        if(profile_options.readback_period==0)return;
        if(!force&&metrics.steps%profile_options.readback_period!=0)return;
        readback_refresh=!readbacks.submit(buffers[6],epoch,readback_revision,metrics.steps);
    }

};
ParticleWorld::ParticleWorld():impl_{std::make_unique<Impl>()}{}
ParticleWorld::~ParticleWorld()=default;
std::expected<std::unique_ptr<ParticleWorld>,std::string> ParticleWorld::create(const std::filesystem::path& shaders,const ExecutionOptions& execution,const ProfileOptions& profiling){
    GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    if(major<4||(major==4&&minor<3))return std::unexpected{"Laboratorium GPU wymaga OpenGL 4.3. Laboratorium CPU jest dostępne."};
    try{
        auto world=std::unique_ptr<ParticleWorld>{new ParticleWorld};auto& s=*world->impl_;s.gl.load();
        // Apply transport/timer policy before the first reset or staging copy.
        // Group query objects are allocated by set_profile_options below.
        s.profile_options=profiling;s.profile_options.group_timers=false;
        s.gl.observe(profiling.collect?&s.calls:nullptr);
        s.uploads.configure(execution.storage);
        GLint flags{};glGetIntegerv(GL_CONTEXT_FLAGS,&flags);s.debug_groups=(flags&GL_CONTEXT_FLAG_DEBUG_BIT)!=0;
        const auto valid_size=[](unsigned size){return size==64||size==128||size==256;};
        if(!valid_size(execution.local_size)||!valid_size(execution.reduction_size)||(!execution.specialized&&execution.local_size!=execution.reduction_size))
            throw std::invalid_argument{"Compute group sizes must be 64, 128 or 256; ubershaders require matching sizes."};
        s.execution=execution;s.compile_kernels(shaders);
        s.actor_program=program(s.gl,{{GL_VERTEX_SHADER,shaders/"mission.vert"},{GL_FRAGMENT_SHADER,shaders/"mission.frag"}});
        s.actor_alpha=s.gl.GetUniformLocation(s.actor_program,"alpha");s.actor_parts=s.gl.GetUniformLocation(s.actor_program,"partsTexture");
        s.actor_weapons=s.gl.GetUniformLocation(s.actor_program,"weaponsTexture");s.actor_weapon_size=s.gl.GetUniformLocation(s.actor_program,"weaponSize");
        s.splat=program(s.gl,{{GL_VERTEX_SHADER,shaders/"liquid_splat.vert"},{GL_FRAGMENT_SHADER,shaders/"liquid_splat.frag"}},s.layout_defines());
        s.surface=program(s.gl,{{GL_VERTEX_SHADER,shaders/"liquid_surface.vert"},{GL_FRAGMENT_SHADER,shaders/"liquid_surface.frag"}});
        s.splat_alpha=s.gl.GetUniformLocation(s.splat,"alpha");
        s.draw=program(s.gl,{{GL_VERTEX_SHADER,shaders/"granular.vert"},{GL_FRAGMENT_SHADER,shaders/"granular.frag"}},s.layout_defines());
        s.alpha=s.gl.GetUniformLocation(s.draw,"alpha");s.terrain_pass=s.gl.GetUniformLocation(s.draw,"terrainPass");
        s.gl.GenBuffers(static_cast<GLsizei>(s.buffers.size()),s.buffers.data());
        const std::array<std::size_t,8> sizes{capacity*sizeof(Grain),capacity*sizeof(Grain),static_cast<std::size_t>(s.execution.neighbors==NeighborMode::linked?width*height*4:(16+3*width*height+480+(s.execution.phase_gating?240:0))*4),s.index_buffer_size(),width*height*16,capacity*8+detail::command_words*4,mission_buffer_size,s.terms_buffer_size()};
        for(unsigned i=0;i<s.buffers.size();++i)s.uploads.allocate(s.buffers[i],static_cast<GLsizeiptr>(sizes[i]));
        if(s.debug_groups){
            constexpr std::array names{"particles A","particles B","cell counts and prefixes","sorted IDs, reverse mapping and anchors","terrain","active/free IDs","mission state","fluid terms"};
            for(std::size_t i=0;i<names.size();++i)s.gl.ObjectLabel(GL_BUFFER,s.buffers[i],-1,names[i]);
        }
        s.readbacks.configure(profiling.readback_mode,s.debug_groups);
        glGenTextures(1,&s.image);glBindTexture(GL_TEXTURE_2D,s.image);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width*3,height*3,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        s.gl.GenFramebuffers(1,&s.fbo);s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.fbo);
        s.gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.image,0);
        if(s.gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Niekompletny framebuffer GPU."};
        glGenTextures(1,&s.fluid_image);glBindTexture(GL_TEXTURE_2D,s.fluid_image);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,width*3,height*3,0,GL_RG,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        s.gl.GenFramebuffers(1,&s.fluid_fbo);s.gl.BindFramebuffer(GL_FRAMEBUFFER,s.fluid_fbo);
        s.gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.fluid_image,0);
        if(s.gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Niekompletny framebuffer cieczy."};
        s.gl.BindFramebuffer(GL_FRAMEBUFFER,0);s.gl.GenVertexArrays(1,&s.vao);
        s.reset_timer_queries(effective_timer_mode(s.profile_options));
        world->set_profile_options(profiling);
        world->reset(Scene::hourglass);return world;
    }catch(const std::exception& e){return std::unexpected{e.what()};}
}
void ParticleWorld::load(std::span<const Grain> grains,std::span<const std::uint32_t> walls){
    if(grains.size()>capacity||walls.size()!=width*height)throw std::invalid_argument{"Nieprawidłowy rozmiar sceny GPU."};
    std::vector<Grain> all(capacity);std::ranges::copy(grains,all.begin());auto& s=*impl_;s.invalidate_bindings();
    for(auto& p:all){p.contact[2]=p.position[0];p.contact[3]=p.position[1];}
    ++s.readback_revision;s.readback_refresh=false;s.mission_enabled=false;s.snapshot={};s.explosions.clear();++s.epoch;
    const std::vector<std::byte> empty(mission_buffer_size);
    s.upload(6,0,mission_buffer_size,empty.data());
    s.reset_body();s.current=0;s.bins_valid=false;
    const std::array<GLuint,16> neighbor_zero{};s.upload(2,0,sizeof(neighbor_zero),neighbor_zero.data());
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
    auto& s=*impl_;s.invalidate_bindings();s.bins_valid=false;++s.readback_revision;s.brush=stroke;
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
void ParticleWorld::step(const MissionInput& input){
    impl_->controls=input;step();
}
void ParticleWorld::step(){
    const auto started=SDL_GetTicksNS();
    auto& s=*impl_;s.invalidate_bindings();const auto initial_calls=s.calls;
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
        if(!s.explosions.empty()){
            const auto upload_start=stamp();
            const auto e=s.explosions.front();s.explosions.pop_front();const std::array<float,4> payload{e.x,e.y,e.radius,e.impulse};
            const std::array<std::uint32_t,4> id{e.id,0,0,0};
            s.upload(6,mission_buffer_size-32,16,payload.data());
            s.upload(6,mission_buffer_size-16,16,id.data());
            upload_ms=elapsed(upload_start,stamp());
        }
        s.mission_dispatch(0);s.mission_dispatch(7,capacity,true,true);s.mission_dispatch(5,width*height);
        // Pending cells keep the material until a free slot is available.
        s.mission_dispatch(6,width*height,false,false,true);
        s.rebuild_active(true);
    }
    const auto prepare_end=stamp();
    if(grouped)s.gl.QueryCounter(group.ids[1],GL_TIMESTAMP);
    // At |v|<=80 each half-tick travels <=1/3 cell. Two approaching,
    // nonoverlapping radius-.46 grains cannot exchange centers undetected.
    for(int half=0;half<2;++half){
        s.trace.context(half+1,-1);
        s.bins_valid=false;
        if(s.mission_enabled)s.mission_dispatch(1);
        s.half_step=half;s.dispatch(1,capacity,true);
        const int iterations=s.grains_enabled?12:8;
        for(int iteration=0;iteration<iterations;++iteration){
            s.trace.context(half+1,iteration);
            s.bins();
            if(s.fluids_enabled&&iteration<8){s.liquid_dispatch(0);s.liquid_dispatch(1,true);if(s.mission_enabled&&(s.execution.coupling!=CouplingMode::combined||!s.grains_enabled))s.mission_dispatch(3);}
            if(s.grains_enabled){if(s.fluids_enabled&&iteration<8)s.bins();s.dispatch(3,capacity,true);}
            if(s.mission_enabled)s.mission_dispatch(3);
        }
        s.trace.context(half+1,iterations);s.bins();s.dispatch(4,capacity,true);
        if(s.fluids_enabled)s.liquid_dispatch(4,true);
        if(s.mission_enabled){if(s.fluids_enabled){s.mission_dispatch(8,capacity,true,true);s.mission_dispatch(3);}s.mission_dispatch(4);}
        if(grouped)s.gl.QueryCounter(group.ids[2+half],GL_TIMESTAMP);
    }
    const auto solver_end=stamp();
    s.trace.context(3,-1);
    const bool diagnostic_due=s.profile_options.periodic_diagnostics&&s.fluids_enabled&&s.metrics.steps%30==0;
    if(diagnostic_due){s.bins();s.liquid_dispatch(3);s.liquid_dispatch(2);}
    const auto diagnostics_end=stamp();
    if(grouped)s.gl.QueryCounter(group.ids[4],GL_TIMESTAMP);
    if(measure){s.end_timer(timer);s.timer_cursor=(s.timer_cursor+1)%static_cast<unsigned>(s.timers.size());}
    const auto timer_closed=stamp();
    ++s.metrics.steps;if(s.mission_enabled||diagnostic_due)s.stage_count(!s.mission_enabled);
    if(grouped){s.gl.QueryCounter(group.ids[5],GL_TIMESTAMP);group.pending=true;s.group_cursor=(s.group_cursor+1)%static_cast<unsigned>(s.group_timers.size());}
    const auto finished=SDL_GetTicksNS();
    if(s.debug_groups)s.gl.PopDebugGroup();
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
    s.trace_step=false;s.trace.end_step();
}
void ParticleWorld::poll(bool collect_hud){
    auto& s=*impl_;
    const auto stamp=[&](){return s.profile_options.collect?SDL_GetTicksNS():std::uint64_t{0};};
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
    auto& s=*impl_;auto& render_timer=s.render_timers[s.render_cursor];
    const bool timers_enabled=effective_timer_mode(s.profile_options)!=TimerMode::off;
    const bool measure_render=timers_enabled&&!render_timer.pending;
    if(s.profile_options.collect&&timers_enabled&&!measure_render)++s.calls.timer_skips;
    if(s.profile_options.collect)s.calls.render_state_queries+=18;
    if(measure_render)s.begin_timer(render_timer);s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    GLint previous_fbo{},previous_vao{},previous_program{},viewport[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_fbo);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previous_vao);
    glGetIntegerv(GL_CURRENT_PROGRAM,&previous_program);glGetIntegerv(GL_VIEWPORT,viewport);
    GLint saved_active{};glGetIntegerv(GL_ACTIVE_TEXTURE,&saved_active);
    GLint texture1{};s.gl.ActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture1);s.gl.ActiveTexture(GL_TEXTURE0);
    GLint active_texture{},texture{},blend_src{},blend_dst{},alpha_src{},alpha_dst{},eq_rgb{},eq_alpha{};
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active_texture);s.gl.ActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glGetIntegerv(GL_BLEND_SRC_RGB,&blend_src);glGetIntegerv(GL_BLEND_DST_RGB,&blend_dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&alpha_src);glGetIntegerv(GL_BLEND_DST_ALPHA,&alpha_dst);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,&eq_rgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eq_alpha);
    const auto blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST),depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    s.gl.BindVertexArray(s.vao);s.gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,s.buffers[s.current]);
    if(s.fluids_enabled){
        s.gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,s.fluid_fbo);glViewport(0,0,width*3,height*3);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_BLEND);
        s.gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ONE);s.gl.BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        s.gl.UseProgram(s.splat);s.gl.Uniform1f(s.splat_alpha,std::clamp(alpha,0.F,1.F));
        s.active_draw();glDisable(GL_BLEND);
    }
    s.gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,s.fbo);glViewport(0,0,width*3,height*3);glClearColor(.035F,.055F,.065F,s.mission_enabled?0.F:1.F);glClear(GL_COLOR_BUFFER_BIT);
    if(s.fluids_enabled){
        s.gl.UseProgram(s.surface);glBindTexture(GL_TEXTURE_2D,s.fluid_image);glDrawArrays(GL_TRIANGLES,0,3);
    }
    s.gl.UseProgram(s.draw);s.gl.BindVertexArray(s.vao);
    s.gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,s.buffers[s.current]);s.gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,4,s.buffers[4]);
    s.gl.Uniform1f(s.alpha,std::clamp(alpha,0.F,1.F));s.gl.Uniform1i(s.terrain_pass,1);
    s.gl.DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,width*height);
    s.gl.Uniform1i(s.terrain_pass,0);s.active_draw();
    if(s.mission_enabled){
        s.gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,6,s.buffers[6]);s.gl.UseProgram(s.actor_program);
        s.gl.Uniform1f(s.actor_alpha,std::clamp(alpha,0.F,1.F));s.gl.Uniform1i(s.actor_parts,0);s.gl.Uniform1i(s.actor_weapons,1);
        s.gl.Uniform4f(s.actor_weapon_size,static_cast<float>(s.weapon_width),static_cast<float>(s.weapon_height),0.F,0.F);
        s.gl.ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s.parts_texture);s.gl.ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,s.weapons_texture);
        glEnable(GL_BLEND);s.gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        s.gl.DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,159);s.gl.ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture1));s.gl.ActiveTexture(GL_TEXTURE0);
    }
    s.gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(previous_fbo));s.gl.BindVertexArray(static_cast<GLuint>(previous_vao));s.gl.UseProgram(static_cast<GLuint>(previous_program));
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture));s.gl.ActiveTexture(static_cast<GLenum>(saved_active));
    s.gl.BlendFuncSeparate(static_cast<GLenum>(blend_src),static_cast<GLenum>(blend_dst),static_cast<GLenum>(alpha_src),static_cast<GLenum>(alpha_dst));
    s.gl.BlendEquationSeparate(static_cast<GLenum>(eq_rgb),static_cast<GLenum>(eq_alpha));
    if(!blend)glDisable(GL_BLEND);
    if(measure_render){s.end_timer(render_timer);if(s.profile_options.collect)++s.calls.timer_submissions;s.render_cursor=(s.render_cursor+1)%static_cast<unsigned>(s.render_timers.size());}
    if(blend)glEnable(GL_BLEND);if(scissor)glEnable(GL_SCISSOR_TEST);if(depth)glEnable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);
}
unsigned ParticleWorld::texture()const noexcept{return impl_->image;}
std::vector<double> ParticleWorld::take_render_timings(){return std::exchange(impl_->render_timings,{});}
std::vector<double> ParticleWorld::take_cpu_timings(){return std::exchange(impl_->cpu_timings,{});}
const Metrics& ParticleWorld::metrics()const noexcept{return impl_->metrics;}
const MissionSnapshot& ParticleWorld::mission()const noexcept{return impl_->snapshot;}
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
ExecutionOptions ParticleWorld::execution_options()const noexcept{return impl_->execution;}
std::array<std::uint32_t,28> ParticleWorld::read_dispatch_commands()const{
    auto& s=*impl_;std::array<std::uint32_t,28> result{};s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[5]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,detail::command_offset(0),sizeof(result),result.data());return result;
}
CallCounters ParticleWorld::counters()const noexcept{return impl_->calls;}
std::vector<StepProfile> ParticleWorld::take_step_profiles(){return std::exchange(impl_->profiles,{});}
std::vector<GroupProfile> ParticleWorld::take_group_profiles(){return std::exchange(impl_->group_profiles,{});}
MissionSnapshot ParticleWorld::read_mission()const{
    auto& s=*impl_;MissionSnapshot result{};s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,32,sizeof(result),&result);return result;
}
std::vector<std::uint32_t> ParticleWorld::read_terrain()const{
    auto& s=*impl_;std::vector<std::uint32_t> result(width*height*4);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[4]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,result.size()*4,result.data());return result;
}
void ParticleWorld::set_actor_textures(unsigned parts,unsigned weapons,int w,int h){
    impl_->parts_texture=parts;impl_->weapons_texture=weapons;impl_->weapon_width=w;impl_->weapon_height=h;
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
    ++s.epoch;++s.readback_revision;s.readback_refresh=false;
    g.actor={config.spawn_x,config.spawn_y,0,0};g.previous={config.spawn_x,config.spawn_y,config.spawn_x,config.spawn_y};
    g.aim={1,0,1.5F,0};g.status={1,s.epoch,0,0};g.inventory={30,3,0,0};g.control={0,config.spawn_x+40,config.spawn_y-14,0};
    g.exit=config.exit;g.targets=config.targets;
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
    auto& s=*impl_;s.reset_body();s.explosions.clear();++s.epoch;++s.readback_revision;s.readback_refresh=false;
    auto state=checkpoint;state.status[1]=s.epoch;s.upload(6,32,sizeof(state),&state);s.snapshot=state;
    s.metrics.steps=tick;s.controls={0,state.control[1],state.control[2],0};s.mission_enabled=true;s.grains_enabled=true;
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
void ParticleWorld::save_image(const std::filesystem::path& path)const{
    auto& s=*impl_;std::vector<std::uint32_t> pixels(width*height*9);GLint old{};glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old);
    s.gl.BindFramebuffer(GL_READ_FRAMEBUFFER,s.fbo);glReadPixels(0,0,width*3,height*3,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());s.gl.BindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(old));
    auto* surface=SDL_CreateSurfaceFrom(width*3,height*3,SDL_PIXELFORMAT_RGBA32,pixels.data(),width*3*4);
    if(!surface)throw std::runtime_error{SDL_GetError()};const auto utf8=path.u8string();const bool saved=SDL_SaveBMP(surface,reinterpret_cast<const char*>(utf8.c_str()));SDL_DestroySurface(surface);
    if(!saved)throw std::runtime_error{SDL_GetError()};
}
std::vector<double> ParticleWorld::take_timings(){return std::exchange(impl_->timings,{});}
std::vector<std::array<float,4>> ParticleWorld::read_fluid_terms(){
    auto& s=*impl_;s.invalidate_bindings();s.bins();s.liquid_dispatch(3);s.liquid_dispatch(2);
    std::vector<std::array<float,4>> result(capacity);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[7]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,capacity*16,result.data());return result;
}
void ParticleWorld::project_liquids_for_test(unsigned iterations){
    auto& s=*impl_;s.invalidate_bindings();for(unsigned i=0;i<iterations;++i){s.bins();s.liquid_dispatch(0);s.liquid_dispatch(1,true);if(s.mission_enabled)s.mission_dispatch(3);}
}
}
