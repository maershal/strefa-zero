#include "gpu/lighting_system.hpp"
#include <algorithm>
#include <bit>
#include <utility>
namespace strefa::gpu::detail {
namespace {
void texture(GLuint& id,int w,int h,GLint format,GLenum channels,GLenum type,const void* pixels=nullptr,bool linear=false){
    glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,channels,type,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
}
LightingSystem::~LightingSystem(){
    for(auto& p:probes)if(p.queries[0])gl.DeleteQueries(8,p.queries.data());
    for(GLuint t:{moisture,field,light_image,thermal_image,shadow_mask,seeds[0],seeds[1],post_scratch,density})if(t)glDeleteTextures(1,&t);
    for(auto t:bloom_down)if(t)glDeleteTextures(1,&t);
    for(auto t:bloom_up)if(t)glDeleteTextures(1,&t);
    for(GLuint b:{pool,lamps,candidates,field_command})if(b)gl.DeleteBuffers(1,&b);
    if(vao)gl.DeleteVertexArrays(1,&vao);
    if(fbo)gl.DeleteFramebuffers(1,&fbo);
}
GLint LightingSystem::location(GLuint p,const char* n)const{return uniforms.at(p).at(n);}
void LightingSystem::initialize(ShaderLibrary& library,const std::filesystem::path& folder,const std::string& defines){
    auto compute=[&](const char* f,const std::string& d=std::string{}){return library.program({{GL_COMPUTE_SHADER,folder/f}},d);};
    auto graphics=[&](const char* v,const char* f,const std::string& d=std::string{}){return library.program({{GL_VERTEX_SHADER,folder/v},{GL_FRAGMENT_SHADER,folder/f}},d);};
    birth_program=compute("presentation_prepare_birth.comp",defines);field_program=compute("vfx_field_command.comp");field_vertex=graphics("presentation_field.vert","presentation_field.frag",defines);
    moisture_program=compute("presentation_moisture.comp",defines);event_program=compute("lighting_events.comp");material_program=compute("lighting_materials.comp");shadow_program=compute("lighting_shadow.comp");jfa_program=compute("lighting_jfa.comp");light_program=compute("lighting_field.comp");
    apply_program=graphics("liquid_surface.vert","lighting_apply.frag");bloom_program=graphics("liquid_surface.vert","lighting_bloom.frag");post_program=graphics("liquid_surface.vert","lighting_post.frag");
    for(GLuint p:{birth_program,field_vertex,moisture_program,event_program,shadow_program,jfa_program,light_program,apply_program,bloom_program,post_program})
        for(const char* n:{"heatEnabled","sourceGeneration","sourceTick","missionEnabled","mode","jump","quality","ambient","renderExtent","effects","camera"})uniforms[p][n]=gl.GetUniformLocation(p,n);
    gl.GenVertexArrays(1,&vao);gl.GenFramebuffers(1,&fbo);gl.GenBuffers(1,&pool);gl.GenBuffers(1,&candidates);gl.GenBuffers(1,&lamps);gl.GenBuffers(1,&field_command);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,candidates);gl.BufferData(GL_SHADER_STORAGE_BUFFER,960*sizeof(PointLight),nullptr,GL_DYNAMIC_COPY);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,pool);gl.BufferData(GL_SHADER_STORAGE_BUFFER,sizeof(LightingSnapshot),nullptr,GL_DYNAMIC_COPY);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,lamps);gl.BufferData(GL_SHADER_STORAGE_BUFFER,16+8*sizeof(PointLight),nullptr,GL_DYNAMIC_COPY);
    gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER,field_command);const std::array<unsigned,4> empty{0,1,0,0};gl.BufferData(GL_DRAW_INDIRECT_BUFFER,sizeof(empty),empty.data(),GL_DYNAMIC_COPY);
    texture(moisture,256,128,GL_R32F,GL_RED,GL_FLOAT);texture(field,320,192,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    texture(light_image,320,192,GL_RGBA16F,GL_RGBA,GL_FLOAT);texture(thermal_image,320,192,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    std::vector<std::uint16_t> mask(320*192,65535);texture(shadow_mask,320,192,GL_R16UI,GL_RED_INTEGER,GL_UNSIGNED_SHORT,mask.data());
    for(auto& seed:seeds)texture(seed,320,192,GL_RG16UI,GL_RG_INTEGER,GL_UNSIGNED_SHORT);
    std::vector<float> lut(64*64*4);for(int z=0;z<16;++z)for(int y=0;y<16;++y)for(int x=0;x<16;++x){const auto i=static_cast<std::size_t>(((z/4)*16+y)*64+(z%4)*16+x)*4;
        const float r=x/15.F,g=y/15.F,b=z/15.F;lut[i]=r*.99F;lut[i+1]=g;lut[i+2]=std::min(1.F,b*1.01F);lut[i+3]=1;}
    texture(density,64,64,GL_RGBA16F,GL_RGBA,GL_FLOAT,lut.data(),true);
    const std::array<PointLight,3> defaults{{{{52,48,48,1},{1.1F,.94F,.62F,1},{0,2,0,0}},{{155,32,52,1},{.60F,.78F,1.1F,1},{0,2,0,1}},{{278,117,38,1},{.74F,1.0F,.66F,1},{0,2,0,2}}}};
    attach(field,320,192);if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Incomplete I11 framebuffer"};state.framebuffer(0);set_lamps(defaults);reset(0);bind_birth();state.invalidate();
}
void LightingSystem::reset(unsigned gen){
    generation=gen;last_tick=0;tick_known=false;jfa_valid=false;impulse=0;LightingSnapshot zero;zero.clock[0]=gen;
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,pool);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(zero),&zero);
    std::vector<float> wet(32768);glBindTexture(GL_TEXTURE_2D,moisture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,128,GL_RED,GL_FLOAT,wet.data());
    std::vector<std::uint16_t> mask(320*192,65535);glBindTexture(GL_TEXTURE_2D,shadow_mask);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,320,192,GL_RED_INTEGER,GL_UNSIGNED_SHORT,mask.data());state.invalidate();
}
void LightingSystem::set_lamps(std::span<const PointLight> values){
    if(values.size()>8)throw std::invalid_argument{"At most eight static lamps"};
    for(const auto& p:values)for(float f:p.position)if(!std::isfinite(f))throw std::invalid_argument{"Invalid lamp position"};
    const std::array<unsigned,4> header{static_cast<unsigned>(values.size()),0,0,0};lamp_count=header[0];gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,lamps);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,16,header.data());
    if(!values.empty())gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,16,static_cast<GLsizeiptr>(values.size_bytes()),values.data());
    state.invalidate();
}
void LightingSystem::bind_birth(){image(7,moisture,GL_READ_WRITE,GL_R32F);}
void LightingSystem::prepare_birth(const std::array<GLuint,8>& buffers,unsigned current,bool conditional){PresentationCounterScope observation{gl};state.invalidate();state.base(0,buffers[current]);state.base(5,buffers[5]);state.use(birth_program);gl.Uniform1i(location(birth_program,"mode"),conditional?1:0);bind_birth();gl.DispatchCompute(256,1,1);gl.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);state.invalidate();}
void LightingSystem::image(unsigned unit,GLuint id,GLenum access,GLenum format){gl.BindImageTexture(unit,id,0,static_cast<GLboolean>(GL_FALSE),0,access,format);}
void LightingSystem::attach(GLuint target,int w,int h){state.raster(w,h);state.framebuffer(fbo);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);}
void LightingSystem::begin(bool tick){active=nullptr;poll();if(!profile||count==probes.size())return;auto& p=probes[(head+count)%probes.size()];if(!p.queries[0])gl.GenQueries(8,p.queries.data());p.tick=tick;p.serial=serial++;active=&p;gl.QueryCounter(p.queries[0],GL_TIMESTAMP);}
void LightingSystem::mark(unsigned i){if(active)gl.QueryCounter(active->queries[i],GL_TIMESTAMP);}
void LightingSystem::end(){if(active){mark(7);active->pending=true;++count;active=nullptr;}}
void LightingSystem::poll(){while(count){auto& p=probes[head];GLint ready{};gl.GetQueryObjectiv(p.queries[7],GL_QUERY_RESULT_AVAILABLE,&ready);if(!ready)break;
    std::array<GLuint64,8> t{};for(unsigned i=0;i<8;++i)if(i==0||i==7||(p.tick?i==1:i>=2&&i<=6))gl.GetQueryObjectui64v(p.queries[i],GL_QUERY_RESULT,&t[i]);
    LightingSample s;s.serial=p.serial;s.tick=p.tick;s.milliseconds[6]=static_cast<double>(t[7]-t[0])/1e6;
    if(p.tick)s.milliseconds[0]=static_cast<double>(t[1]-t[0])/1e6;
    else{ s.milliseconds[1]=static_cast<double>(t[2]-t[0])/1e6;for(unsigned i=2;i<6;++i)s.milliseconds[i]=static_cast<double>(t[i+1]-t[i])/1e6;}
    samples.push_back(s);p.pending=false;head=(head+1)%static_cast<unsigned>(probes.size());--count;}}
std::vector<LightingSample> LightingSystem::take_samples(){poll();return std::exchange(samples,{});}
void LightingSystem::tick(const std::array<GLuint,8>& buffers,unsigned current,unsigned gen,unsigned tick_id,bool mission,GLuint terrain,GLuint heat,GLuint thermal_particles){
    if(gen!=generation)reset(gen);
    if(tick_known&&tick_id<=last_tick)return;
    last_tick=tick_id;tick_known=true;PresentationCounterScope observation{gl};begin(true);state.invalidate();state.vertex_array(vao);
    state.base(1,field_command);state.base(6,buffers[6]);state.use(field_program);gl.DispatchCompute(1,1,1);gl.MemoryBarrier(GL_COMMAND_BARRIER_BIT);
    attach(field,320,192);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);state.base(0,buffers[current]);state.base(5,buffers[5]);state.texture(3,terrain);state.base(2,thermal_particles?thermal_particles:buffers[current]);state.use(field_vertex);gl.Uniform1i(location(field_vertex,"heatEnabled"),thermal_particles?1:0);state.draw_indirect(field_command);
    glEnable(GL_PROGRAM_POINT_SIZE);glEnable(GL_BLEND);gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ONE);gl.DrawArraysIndirect(GL_POINTS,nullptr);glDisable(GL_BLEND);glDisable(GL_PROGRAM_POINT_SIZE);
    state.texture(0,field);state.texture(1,heat?heat:thermal_image);state.use(moisture_program);gl.Uniform1i(location(moisture_program,"heatEnabled"),heat?1:0);bind_birth();gl.DispatchCompute(256,1,1);gl.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    state.base(0,pool);state.base(1,lamps);state.base(2,candidates);if(heat){state.use(material_program);gl.DispatchCompute(15,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);}
    state.base(6,buffers[6]);state.texture(1,heat?heat:thermal_image);state.use(event_program);
    gl.Uniform1i(location(event_program,"sourceGeneration"),std::bit_cast<int>(gen));gl.Uniform1i(location(event_program,"sourceTick"),std::bit_cast<int>(tick_id));gl.Uniform1i(location(event_program,"missionEnabled"),mission?1:0);gl.Uniform1i(location(event_program,"heatEnabled"),heat?1:0);gl.DispatchCompute(1,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    mark(1);end();state.framebuffer(0);state.invalidate();
}
void LightingSystem::resize(int w,int h){if(rw==w&&rh==h)return;rw=w;rh=h;for(auto& t:bloom_down){if(t)glDeleteTextures(1,&t);t=0;}for(auto& t:bloom_up){if(t)glDeleteTextures(1,&t);t=0;}
    if(post_scratch)glDeleteTextures(1,&post_scratch);
    texture(post_scratch,w,h,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    for(int i=0;i<3;++i){texture(bloom_down[i],std::max(1,w>>(i+1)),std::max(1,h>>(i+1)),GL_RGBA16F,GL_RGBA,GL_FLOAT,nullptr,true);texture(bloom_up[i],std::max(1,w>>(i+1)),std::max(1,h>>(i+1)),GL_RGBA16F,GL_RGBA,GL_FLOAT,nullptr,true);}state.invalidate();}
void LightingSystem::illuminate(GLuint scene,GLuint,GLuint terrain,int w,int h,GLuint target,const LightingOptions& options){
    PresentationCounterScope observation{gl};resize(w,h);begin(false);state.invalidate();state.vertex_array(vao);state.base(0,pool);state.base(1,lamps);state.texture(3,terrain);
    state.use(shadow_program);image(0,shadow_mask,GL_READ_WRITE,GL_R16UI);image(1,seeds[0],GL_WRITE_ONLY,GL_RG16UI);
    gl.Uniform1i(location(shadow_program,"mode"),0);gl.DispatchCompute(1,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    gl.Uniform1i(location(shadow_program,"mode"),1);gl.DispatchCompute(480,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    if(options.quality==LightingQuality::high&&!jfa_valid){gl.Uniform1i(location(shadow_program,"mode"),3);gl.DispatchCompute(1,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);}
    gl.Uniform1i(location(shadow_program,"mode"),2);gl.DispatchCompute(480,1,1);gl.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    if(options.quality==LightingQuality::high){unsigned bank=0;state.use(jfa_program);for(int jump:{256,128,64,32,16,8,4,2,1,1}){image(1,seeds[bank],GL_READ_ONLY,GL_RG16UI);image(2,seeds[bank^1],GL_WRITE_ONLY,GL_RG16UI);gl.Uniform1i(location(jfa_program,"jump"),jump);gl.DispatchCompute(40,24,1);gl.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);bank^=1;}jfa_valid=true;}else jfa_valid=false;
    mark(2);gl.MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);state.texture(2,seeds[0]);state.use(light_program);image(0,light_image,GL_WRITE_ONLY,GL_RGBA16F);gl.Uniform1i(location(light_program,"quality"),static_cast<int>(options.quality));gl.Uniform1f(location(light_program,"ambient"),options.ambient);gl.DispatchCompute(40,24,1);gl.MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    state.raster(w,h);state.framebuffer(target);state.texture(0,scene);state.texture(1,light_image);state.use(apply_program);gl.Uniform4f(location(apply_program,"renderExtent"),static_cast<float>(w),static_cast<float>(h),0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);mark(3);state.invalidate();
}
std::array<float,2> LightingSystem::camera(const LightingOptions& o)const{const float t=static_cast<float>(last_tick)/120.F;return {std::round(std::sin(t*71.F)*o.shake*impulse*3.F)*3.F,std::round(std::sin(t*93.F)*o.shake*impulse*2.F)*3.F};}
void LightingSystem::finish(GLuint hdr,GLuint target,int w,int h,const LightingOptions& options){
    mark(4);state.invalidate();state.vertex_array(vao);GLuint result=hdr;
    if(options.bloom>0){const int first=options.quality==LightingQuality::high?0:1;GLuint source=hdr;
        for(int i=first;i<3;++i){attach(bloom_down[i],std::max(1,w>>(i+1)),std::max(1,h>>(i+1)));state.texture(0,source);state.use(bloom_program);gl.Uniform1i(location(bloom_program,"mode"),i==first?0:1);gl.Uniform4f(location(bloom_program,"renderExtent"),static_cast<float>(std::max(1,w>>(i+1))),static_cast<float>(std::max(1,h>>(i+1))),0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);source=bloom_down[i];}
        for(int i=1;i>=first;--i){attach(bloom_up[i],std::max(1,w>>(i+1)),std::max(1,h>>(i+1)));state.texture(0,source);state.texture(1,bloom_down[i]);state.use(bloom_program);gl.Uniform1i(location(bloom_program,"mode"),2);gl.Uniform4f(location(bloom_program,"renderExtent"),static_cast<float>(std::max(1,w>>(i+1))),static_cast<float>(std::max(1,h>>(i+1))),0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);source=bloom_up[i];}result=source;
    }mark(5);state.raster(w,h);state.framebuffer(target);state.texture(0,hdr);state.texture(1,result);state.texture(2,density);state.use(post_program);const auto offset=camera(options);
    gl.Uniform4f(location(post_program,"renderExtent"),static_cast<float>(w),static_cast<float>(h),0.F,0.F);gl.Uniform4f(location(post_program,"effects"),options.bloom,options.vignette,options.aberration*impulse,options.exposure);gl.Uniform4f(location(post_program,"camera"),offset[0],offset[1],0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);mark(6);end();state.framebuffer(0);state.invalidate();
}
std::vector<float> LightingSystem::read_moisture()const{std::vector<float> values(32768);glBindTexture(GL_TEXTURE_2D,moisture);glGetTexImage(GL_TEXTURE_2D,0,GL_RED,GL_FLOAT,values.data());return values;}
std::vector<float> LightingSystem::read_light()const{std::vector<float> values(320*192*4);glBindTexture(GL_TEXTURE_2D,light_image);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,values.data());return values;}
std::vector<std::uint16_t> LightingSystem::read_seeds()const{std::vector<std::uint16_t> values(320*192*2);glBindTexture(GL_TEXTURE_2D,seeds[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RG_INTEGER,GL_UNSIGNED_SHORT,values.data());return values;}
LightingSnapshot LightingSystem::snapshot()const{LightingSnapshot s;gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.BindBuffer(GL_COPY_READ_BUFFER,pool);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(s),&s);return s;}
std::uint64_t LightingSystem::memory_bytes()const{std::uint64_t total=32768*4+320*192*(8*3+2+4*2)+64*64*8+sizeof(LightingSnapshot)+960*sizeof(PointLight)+16+8*sizeof(PointLight)+16+static_cast<std::uint64_t>(rw)*rh*8;
    for(int i=0;i<3;++i)total+=static_cast<std::uint64_t>(std::max(1,rw>>(i+1)))*std::max(1,rh>>(i+1))*16;
    return total;}
}
