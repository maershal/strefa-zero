#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <cmath>
#include <bit>
#include <fstream>
#include <deque>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace strefa::gpu {
namespace {
#define GL_FUNCTIONS(X) \
 X(PFNGLACTIVETEXTUREPROC,ActiveTexture) X(PFNGLBLENDFUNCSEPARATEPROC,BlendFuncSeparate) X(PFNGLBLENDEQUATIONSEPARATEPROC,BlendEquationSeparate) \
 X(PFNGLGENBUFFERSPROC,GenBuffers) X(PFNGLDELETEBUFFERSPROC,DeleteBuffers) \
 X(PFNGLBINDBUFFERPROC,BindBuffer) X(PFNGLBUFFERDATAPROC,BufferData) X(PFNGLBUFFERSUBDATAPROC,BufferSubData) \
 X(PFNGLBINDBUFFERBASEPROC,BindBufferBase) X(PFNGLGETBUFFERSUBDATAPROC,GetBufferSubData) X(PFNGLCOPYBUFFERSUBDATAPROC,CopyBufferSubData) \
 X(PFNGLCREATESHADERPROC,CreateShader) X(PFNGLSHADERSOURCEPROC,ShaderSource) X(PFNGLCOMPILESHADERPROC,CompileShader) \
 X(PFNGLGETSHADERIVPROC,GetShaderiv) X(PFNGLGETSHADERINFOLOGPROC,GetShaderInfoLog) X(PFNGLDELETESHADERPROC,DeleteShader) \
 X(PFNGLCREATEPROGRAMPROC,CreateProgram) X(PFNGLATTACHSHADERPROC,AttachShader) X(PFNGLLINKPROGRAMPROC,LinkProgram) \
 X(PFNGLGETPROGRAMIVPROC,GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC,GetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC,DeleteProgram) \
 X(PFNGLUSEPROGRAMPROC,UseProgram) X(PFNGLGETUNIFORMLOCATIONPROC,GetUniformLocation) \
 X(PFNGLUNIFORM1IPROC,Uniform1i) X(PFNGLUNIFORM1FPROC,Uniform1f) X(PFNGLUNIFORM4FPROC,Uniform4f) \
 X(PFNGLDISPATCHCOMPUTEINDIRECTPROC,DispatchComputeIndirect) X(PFNGLDRAWARRAYSINDIRECTPROC,DrawArraysIndirect) X(PFNGLDISPATCHCOMPUTEPROC,DispatchCompute) X(PFNGLMEMORYBARRIERPROC,MemoryBarrier) \
 X(PFNGLGENFRAMEBUFFERSPROC,GenFramebuffers) X(PFNGLDELETEFRAMEBUFFERSPROC,DeleteFramebuffers) \
 X(PFNGLBINDFRAMEBUFFERPROC,BindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC,FramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,CheckFramebufferStatus) \
 X(PFNGLGENVERTEXARRAYSPROC,GenVertexArrays) X(PFNGLDELETEVERTEXARRAYSPROC,DeleteVertexArrays) X(PFNGLBINDVERTEXARRAYPROC,BindVertexArray) \
 X(PFNGLDRAWARRAYSINSTANCEDPROC,DrawArraysInstanced) X(PFNGLGENQUERIESPROC,GenQueries) X(PFNGLDELETEQUERIESPROC,DeleteQueries) \
 X(PFNGLBEGINQUERYPROC,BeginQuery) X(PFNGLENDQUERYPROC,EndQuery) X(PFNGLGETQUERYOBJECTIVPROC,GetQueryObjectiv) X(PFNGLGETQUERYOBJECTUI64VPROC,GetQueryObjectui64v) \
 X(PFNGLFENCESYNCPROC,FenceSync) X(PFNGLCLIENTWAITSYNCPROC,ClientWaitSync) X(PFNGLDELETESYNCPROC,DeleteSync)
struct Api {
#define DECLARE(type,name) type name{};
    GL_FUNCTIONS(DECLARE)
#undef DECLARE
    void load(){
#define LOAD(type,name) name=reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name));if(!name)throw std::runtime_error{"Brak OpenGL: gl" #name};
        GL_FUNCTIONS(LOAD)
#undef LOAD
    }
};
std::string read_text(const std::filesystem::path& path){
    std::ifstream input{path,std::ios::binary};if(!input)throw std::runtime_error{"Brak shadera: "+path.string()};
    std::string text{std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
    // Fixed, local shader dependencies; recursive expansion supports shared ABI.
    for(const std::string name:{"particle_common.glsl","mission_state.glsl"}){
        const std::string directive="#include \""+name+"\"";
        if(const auto at=text.find(directive);at!=std::string::npos)
            text.replace(at,directive.size(),read_text(path.parent_path()/name));
    }
    return text;
}
GLuint program(Api& gl,std::initializer_list<std::pair<GLenum,std::filesystem::path>> stages){
    const auto result=gl.CreateProgram();std::vector<GLuint> shaders;
    try {
        for(const auto& [kind,path]:stages){
            const auto shader=gl.CreateShader(kind);shaders.push_back(shader);
            const auto text=read_text(path);const auto* pointer=text.c_str();
            gl.ShaderSource(shader,1,&pointer,nullptr);gl.CompileShader(shader);
            GLint valid{};gl.GetShaderiv(shader,GL_COMPILE_STATUS,&valid);
            if(!valid){std::array<char,8192> log{};gl.GetShaderInfoLog(shader,static_cast<GLsizei>(log.size()),nullptr,log.data());throw std::runtime_error{path.string()+": "+log.data()};}
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
    std::array<GLuint,9> buffers{}; // A/B, head, link, walls, free, counters, staging, fluid terms
    GLuint compute{},fluid{},draw{},fbo{},image{},vao{},mission_program{},actor_program{};
    GLint input_control{},mission_operation{},dirty_only{},actor_alpha{},actor_parts{},actor_weapons{},actor_weapon_size{};
    unsigned parts_texture{},weapons_texture{};int weapon_width{256},weapon_height{128};
    bool mission_enabled{};unsigned epoch{};MissionSnapshot snapshot{};std::deque<Explosion> explosions;MissionInput controls{};
    GLuint splat{},surface{},fluid_fbo{},fluid_image{};
    GLint fluid_operation{},splat_alpha{};
    bool fluids_enabled{},grains_enabled{};
    GLint operation{},substep{},stroke{},radius{},material{},alpha{},terrain_pass{};
    struct Timer {GLuint id{};bool pending{};};
    std::array<Timer,32> timers{},render_timers{};unsigned render_cursor{};
    unsigned timer_cursor{},current{};
    GLsync read_fence{};
    bool count_dirty{};
    Metrics metrics;
    std::vector<double> timings,render_timings,cpu_timings;
    ~Impl(){
        if(read_fence)gl.DeleteSync(read_fence);
        for(const auto& timer:timers)if(timer.id)gl.DeleteQueries(1,&timer.id);
        for(const auto& timer:render_timers)if(timer.id)gl.DeleteQueries(1,&timer.id);
        if(mission_program)gl.DeleteProgram(mission_program);if(actor_program)gl.DeleteProgram(actor_program);
        if(compute)gl.DeleteProgram(compute);if(draw)gl.DeleteProgram(draw);
        if(fluid)gl.DeleteProgram(fluid);if(splat)gl.DeleteProgram(splat);if(surface)gl.DeleteProgram(surface);
        if(fluid_fbo)gl.DeleteFramebuffers(1,&fluid_fbo);if(fluid_image)glDeleteTextures(1,&fluid_image);
        if(vao)gl.DeleteVertexArrays(1,&vao);if(fbo)gl.DeleteFramebuffers(1,&fbo);
        if(image)glDeleteTextures(1,&image);
        if(buffers[0])gl.DeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
    }
    void bind(){
        gl.UseProgram(compute);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[current]);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[1-current]);
        for(unsigned i=2;i<=6;++i)gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,buffers[i]);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,7,buffers[8]);
    }
    void dispatch(int op,unsigned count,bool swap=false){
        const bool active=op>=1&&op<=4;
        if(count==0)return;
        bind();gl.Uniform1i(operation,op);
        if(active){gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[5]);gl.DispatchComputeIndirect(capacity*8);}
        else gl.DispatchCompute((count+255)/256,1,1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        if(swap)current=1-current;
    }
    void bins(){dispatch(0,width*height);dispatch(2,capacity);}
    void liquid_dispatch(int op,bool swap=false){
        bind();gl.UseProgram(fluid);gl.Uniform1i(fluid_operation,op);
        if(op==3)gl.DispatchCompute(1,1,1);
        else{gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[5]);gl.DispatchComputeIndirect(capacity*8);}
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);if(swap)current=1-current;
    }
    void rebuild_active(bool conditional=false){
        bind();gl.Uniform1i(dirty_only,conditional?1:0);
        dispatch(10,1);dispatch(11,capacity);dispatch(12,1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
        // An inactive ID is skipped by simulation, so both ping-pong copies
        // must be synchronized on membership edits (otherwise IDs resurrect).
        dispatch(19,capacity);
        if(conditional)dispatch(9,width*height);
        dispatch(20,1);bind();gl.Uniform1i(dirty_only,0);
    }
    void mission_dispatch(int op,unsigned count=1,bool swap=false,bool active=false){
        bind();gl.UseProgram(mission_program);gl.Uniform1i(mission_operation,op);
        if(op==0)gl.Uniform4f(input_control,std::clamp(controls.move,-1.F,1.F),controls.aim_x,controls.aim_y,static_cast<float>(controls.buttons&31U));
        if(active){gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[5]);gl.DispatchComputeIndirect(capacity*8);}
        else gl.DispatchCompute((count+255)/256,1,1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);if(swap)current=1-current;
    }
    void active_draw(){
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,5,buffers[5]);
        gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER,buffers[5]);gl.DrawArraysIndirect(GL_TRIANGLE_STRIP,reinterpret_cast<void*>(capacity*8+12));
    }
    void stage_count(){
        count_dirty=true;
        if(read_fence)return;
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[6]);gl.BindBuffer(GL_COPY_WRITE_BUFFER,buffers[7]);
        gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,32+sizeof(MissionSnapshot));
        read_fence=gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        count_dirty=false;
    }
};
ParticleWorld::ParticleWorld():impl_{std::make_unique<Impl>()}{}
ParticleWorld::~ParticleWorld()=default;
std::expected<std::unique_ptr<ParticleWorld>,std::string> ParticleWorld::create(const std::filesystem::path& shaders){
    GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    if(major<4||(major==4&&minor<3))return std::unexpected{"Laboratorium GPU wymaga OpenGL 4.3. Laboratorium CPU jest dostępne."};
    try{
        auto world=std::unique_ptr<ParticleWorld>{new ParticleWorld};auto& s=*world->impl_;s.gl.load();
        s.compute=program(s.gl,{{GL_COMPUTE_SHADER,shaders/"granular.comp"}});
        s.fluid=program(s.gl,{{GL_COMPUTE_SHADER,shaders/"liquids.comp"}});
        s.fluid_operation=s.gl.GetUniformLocation(s.fluid,"operation");
        s.mission_program=program(s.gl,{{GL_COMPUTE_SHADER,shaders/"mission.comp"}});
        s.mission_operation=s.gl.GetUniformLocation(s.mission_program,"operation");s.input_control=s.gl.GetUniformLocation(s.mission_program,"inputControl");
        s.dirty_only=s.gl.GetUniformLocation(s.compute,"dirtyOnly");
        s.actor_program=program(s.gl,{{GL_VERTEX_SHADER,shaders/"mission.vert"},{GL_FRAGMENT_SHADER,shaders/"mission.frag"}});
        s.actor_alpha=s.gl.GetUniformLocation(s.actor_program,"alpha");s.actor_parts=s.gl.GetUniformLocation(s.actor_program,"partsTexture");
        s.actor_weapons=s.gl.GetUniformLocation(s.actor_program,"weaponsTexture");s.actor_weapon_size=s.gl.GetUniformLocation(s.actor_program,"weaponSize");
        s.splat=program(s.gl,{{GL_VERTEX_SHADER,shaders/"liquid_splat.vert"},{GL_FRAGMENT_SHADER,shaders/"liquid_splat.frag"}});
        s.surface=program(s.gl,{{GL_VERTEX_SHADER,shaders/"liquid_surface.vert"},{GL_FRAGMENT_SHADER,shaders/"liquid_surface.frag"}});
        s.splat_alpha=s.gl.GetUniformLocation(s.splat,"alpha");
        s.draw=program(s.gl,{{GL_VERTEX_SHADER,shaders/"granular.vert"},{GL_FRAGMENT_SHADER,shaders/"granular.frag"}});
        s.operation=s.gl.GetUniformLocation(s.compute,"operation");s.stroke=s.gl.GetUniformLocation(s.compute,"stroke");
        s.substep=s.gl.GetUniformLocation(s.compute,"substep");
        s.radius=s.gl.GetUniformLocation(s.compute,"brushRadius");s.material=s.gl.GetUniformLocation(s.compute,"material");
        s.alpha=s.gl.GetUniformLocation(s.draw,"alpha");s.terrain_pass=s.gl.GetUniformLocation(s.draw,"terrainPass");
        s.gl.GenBuffers(static_cast<GLsizei>(s.buffers.size()),s.buffers.data());
        const std::array<std::size_t,9> sizes{capacity*sizeof(Grain),capacity*sizeof(Grain),width*height*4,capacity*4,width*height*16,capacity*8+28,mission_buffer_size,32+sizeof(MissionSnapshot),capacity*32};
        for(unsigned i=0;i<s.buffers.size();++i){s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[i]);s.gl.BufferData(GL_SHADER_STORAGE_BUFFER,static_cast<GLsizeiptr>(sizes[i]),nullptr,GL_DYNAMIC_COPY);}
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
        for(auto& timer:s.timers)s.gl.GenQueries(1,&timer.id);
        for(auto& timer:s.render_timers)s.gl.GenQueries(1,&timer.id);
        world->reset(Scene::hourglass);return world;
    }catch(const std::exception& e){return std::unexpected{e.what()};}
}
void ParticleWorld::load(std::span<const Grain> grains,std::span<const std::uint32_t> walls){
    if(grains.size()>capacity||walls.size()!=width*height)throw std::invalid_argument{"Nieprawidłowy rozmiar sceny GPU."};
    std::vector<Grain> all(capacity);std::ranges::copy(grains,all.begin());auto& s=*impl_;
    for(auto& p:all){p.contact[2]=p.position[0];p.contact[3]=p.position[1];}
    if(s.read_fence){s.gl.DeleteSync(s.read_fence);s.read_fence=nullptr;}
    s.count_dirty=false;s.mission_enabled=false;s.snapshot={};s.explosions.clear();++s.epoch;
    const std::vector<std::byte> empty(mission_buffer_size);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,mission_buffer_size,empty.data());
    s.current=0;s.metrics.steps=0;s.metrics.grains=static_cast<unsigned>(std::ranges::count_if(all,[](const Grain& p){return p.motion[3]>.5F;}));
    s.metrics.water=static_cast<unsigned>(std::ranges::count_if(all,[](const Grain& p){return p.motion[3]==5;}));
    s.metrics.oil=static_cast<unsigned>(std::ranges::count_if(all,[](const Grain& p){return p.motion[3]==6;}));
    s.metrics.max_compression=0;s.fluids_enabled=s.metrics.water+s.metrics.oil>0;
    s.grains_enabled=s.metrics.grains>s.metrics.water+s.metrics.oil;
    const std::array<GLuint,8> counters{0,0,s.metrics.grains,s.metrics.water,s.metrics.oil,0,0,0};
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,32,counters.data());
    for(int i=0;i<2;++i){s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[i]);s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(all.size()*sizeof(Grain)),all.data());}
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[4]);s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(walls.size_bytes()),walls.data());
    std::vector<std::uint32_t> strength(walls.size());for(std::size_t i=0;i<walls.size();++i)strength[i]=walls[i]>=2?3U:0U;
    s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,width*height*12,static_cast<GLsizeiptr>(strength.size()*4),strength.data());
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
    auto& s=*impl_;s.bind();s.gl.Uniform4f(s.stroke,stroke.x0,stroke.y0,stroke.x1,stroke.y1);
    s.gl.Uniform1f(s.radius,std::clamp(stroke.radius,1.F,16.F));s.gl.Uniform1i(s.material,static_cast<int>(stroke.material));
    const std::array<GLuint,8> zero{};s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,32,zero.data());
    // Conservative flags: an asynchronous counter must never disable a solver
    // after a newer edit. Reset/load can clear them from known scene state.
    if(stroke.material==Material::water||stroke.material==Material::oil)s.fluids_enabled=true;
    if(stroke.material>=Material::sand&&stroke.material<=Material::metal)s.grains_enabled=true;
    s.dispatch(5,capacity,true);s.rebuild_active();
    if(stroke.material==Material::wall||stroke.material==Material::erase){s.dispatch(7,width*height);s.dispatch(9,width*height);}
    else{
        s.bins();s.bind();s.gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,s.buffers[s.current]);
        s.gl.Uniform1i(s.operation,8);s.gl.DispatchCompute((width*height+255)/256,1,1);s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    s.rebuild_active();s.stage_count();
}
void ParticleWorld::step(const MissionInput& input){
    impl_->controls=input;step();
}
void ParticleWorld::step(){
    const auto started=SDL_GetTicksNS();
    auto& s=*impl_;poll();auto& timer=s.timers[s.timer_cursor];const bool measure=!timer.pending;
    if(measure)s.gl.BeginQuery(GL_TIME_ELAPSED,timer.id);
    if(s.mission_enabled){
        if(!s.explosions.empty()){
            const auto e=s.explosions.front();s.explosions.pop_front();const std::array<float,4> payload{e.x,e.y,e.radius,e.impulse};
            const std::array<std::uint32_t,4> id{e.id,0,0,0};s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);
            s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,mission_buffer_size-32,16,payload.data());
            s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,mission_buffer_size-16,16,id.data());
        }
        s.mission_dispatch(0);s.mission_dispatch(7,capacity,true,true);s.mission_dispatch(5,width*height);
        // Pending cells keep the material until a free slot is available.
        s.bind();s.gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,s.buffers[s.current]);
        s.gl.UseProgram(s.mission_program);s.gl.Uniform1i(s.mission_operation,6);s.gl.DispatchCompute((width*height+255)/256,1,1);s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        s.rebuild_active(true);
    }
    // At |v|<=80 each half-tick travels <=1/3 cell. Two approaching,
    // nonoverlapping radius-.46 grains cannot exchange centers undetected.
    for(int half=0;half<2;++half){
        if(s.mission_enabled)s.mission_dispatch(1);
        s.bind();s.gl.Uniform1i(s.substep,half);s.dispatch(1,capacity,true);
        const int iterations=s.grains_enabled?12:8;
        for(int iteration=0;iteration<iterations;++iteration){
            s.bins();
            if(s.fluids_enabled&&iteration<8){s.liquid_dispatch(0);s.liquid_dispatch(1,true);if(s.mission_enabled)s.mission_dispatch(3);}
            if(s.grains_enabled){if(s.fluids_enabled&&iteration<8)s.bins();s.dispatch(3,capacity,true);}
            if(s.mission_enabled)s.mission_dispatch(3);
        }
        s.bins();s.dispatch(4,capacity,true);
        if(s.fluids_enabled)s.liquid_dispatch(4,true);
        if(s.mission_enabled){if(s.fluids_enabled){s.mission_dispatch(8,capacity,true,true);s.mission_dispatch(3);}s.mission_dispatch(4);}
    }
    if(s.fluids_enabled&&s.metrics.steps%30==0){s.bins();s.liquid_dispatch(3);s.liquid_dispatch(2);s.stage_count();}
    if(measure){s.gl.EndQuery(GL_TIME_ELAPSED);timer.pending=true;s.timer_cursor=(s.timer_cursor+1)%static_cast<unsigned>(s.timers.size());}
    ++s.metrics.steps;if(s.mission_enabled)s.stage_count();
    s.metrics.cpu_submit_ms=static_cast<double>(SDL_GetTicksNS()-started)/1e6;
    if(s.cpu_timings.size()<10000)s.cpu_timings.push_back(s.metrics.cpu_submit_ms);
}
void ParticleWorld::poll(){
    auto& s=*impl_;
    for(auto& timer:s.timers)if(timer.pending){
        GLint ready{};s.gl.GetQueryObjectiv(timer.id,GL_QUERY_RESULT_AVAILABLE,&ready);
        if(ready){GLuint64 ns{};s.gl.GetQueryObjectui64v(timer.id,GL_QUERY_RESULT,&ns);timer.pending=false;
            s.metrics.gpu_ms=static_cast<double>(ns)*1e-6;++s.metrics.timing_samples;
            // Bounded diagnostic history; validation drains it between scenes.
            if(s.timings.size()<10000)s.timings.push_back(s.metrics.gpu_ms);}
    }
    for(auto& timer:s.render_timers)if(timer.pending){
        GLint ready{};s.gl.GetQueryObjectiv(timer.id,GL_QUERY_RESULT_AVAILABLE,&ready);
        if(ready){GLuint64 ns{};s.gl.GetQueryObjectui64v(timer.id,GL_QUERY_RESULT,&ns);timer.pending=false;
            s.metrics.render_ms=static_cast<double>(ns)*1e-6;if(s.render_timings.size()<10000)s.render_timings.push_back(s.metrics.render_ms);}
    }
    if(s.read_fence){const auto status=s.gl.ClientWaitSync(s.read_fence,0,0);
        if(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED){
            std::array<GLuint,8> values{};s.gl.BindBuffer(GL_COPY_WRITE_BUFFER,s.buffers[7]);s.gl.GetBufferSubData(GL_COPY_WRITE_BUFFER,0,32,values.data());
            s.metrics.grains=values[2];s.metrics.water=values[3];s.metrics.oil=values[4];s.metrics.max_compression=std::bit_cast<float>(values[5]);
            MissionSnapshot received{};s.gl.GetBufferSubData(GL_COPY_WRITE_BUFFER,32,sizeof(received),&received);
            if(received.status[1]==s.epoch&&received.status[2]>=s.snapshot.status[2])s.snapshot=received;
            s.gl.DeleteSync(s.read_fence);s.read_fence=nullptr;
        }
    }
    if(s.count_dirty&&!s.read_fence)s.stage_count();
}
void ParticleWorld::render(float alpha){
    auto& s=*impl_;auto& render_timer=s.render_timers[s.render_cursor];const bool measure_render=!render_timer.pending;
    if(measure_render)s.gl.BeginQuery(GL_TIME_ELAPSED,render_timer.id);s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
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
        s.gl.Uniform4f(s.actor_weapon_size,static_cast<float>(s.weapon_width),static_cast<float>(s.weapon_height),0,0);
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
    if(measure_render){s.gl.EndQuery(GL_TIME_ELAPSED);render_timer.pending=true;s.render_cursor=(s.render_cursor+1)%static_cast<unsigned>(s.render_timers.size());}
    if(blend)glEnable(GL_BLEND);if(scissor)glEnable(GL_SCISSOR_TEST);if(depth)glEnable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);
}
unsigned ParticleWorld::texture()const noexcept{return impl_->image;}
std::vector<double> ParticleWorld::take_render_timings(){return std::exchange(impl_->render_timings,{});}
std::vector<double> ParticleWorld::take_cpu_timings(){return std::exchange(impl_->cpu_timings,{});}
const Metrics& ParticleWorld::metrics()const noexcept{return impl_->metrics;}
const MissionSnapshot& ParticleWorld::mission()const noexcept{return impl_->snapshot;}
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
    auto& s=*impl_;MissionSnapshot g{};s.explosions.clear();
    if(s.read_fence){s.gl.DeleteSync(s.read_fence);s.read_fence=nullptr;}++s.epoch;
    g.actor={config.spawn_x,config.spawn_y,0,0};g.previous={config.spawn_x,config.spawn_y,config.spawn_x,config.spawn_y};
    g.aim={1,0,1.5F,0};g.status={1,s.epoch,0,0};g.inventory={30,3,0,0};g.control={0,config.spawn_x+40,config.spawn_y-14,0};
    g.exit=config.exit;g.targets=config.targets;
    constexpr float area=80+16*3.14159265358979323846F;
    g.body={2.65F/(area*config.relative_density),config.relative_density,area,0};
    const std::vector<std::byte> empty(mission_buffer_size-32);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[6]);s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,32,empty.size(),empty.data());
    s.gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,32,sizeof(g),&g);s.snapshot=g;s.controls={0,g.control[1],g.control[2],0};s.mission_enabled=true;s.grains_enabled=true;
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
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[s.current]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(result.size()*sizeof(Grain)),result.data());return result;
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
    auto& s=*impl_;s.bins();s.liquid_dispatch(3);s.liquid_dispatch(2);
    std::vector<std::array<float,4>> result(capacity);s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,s.buffers[8]);s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,capacity*16,result.data());return result;
}
void ParticleWorld::project_liquids_for_test(unsigned iterations){
    auto& s=*impl_;for(unsigned i=0;i<iterations;++i){s.bins();s.liquid_dispatch(0);s.liquid_dispatch(1,true);if(s.mission_enabled)s.mission_dispatch(3);}
}
}
