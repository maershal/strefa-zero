#include "gpu/vfx_pool.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <numeric>
#include <utility>
namespace strefa::gpu::detail {
namespace {
void texture(GLuint& id,int w,int h,GLint format,GLenum channels,GLenum type){
    glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,channels,type,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
}
VfxPool::~VfxPool(){
    for(auto& p:probes){if(p.fence)gl.DeleteSync(p.fence);if(p.buffer)gl.DeleteBuffers(1,&p.buffer);if(p.queries[0])gl.DeleteQueries(4,p.queries.data());}
    if(buffers[0])gl.DeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
    for(auto id:{field,cpu_terrain,scratch,cpu_images[0],cpu_images[1]})if(id)glDeleteTextures(1,&id);
    for(auto id:{fbo,scratch_fbo,cpu_fbos[0],cpu_fbos[1]})if(id)gl.DeleteFramebuffers(1,&id);
    if(vao)gl.DeleteVertexArrays(1,&vao);
}
GLint VfxPool::location(GLuint program,const char* name)const{
    const auto p=std::find_if(uniforms.begin(),uniforms.end(),[&](const auto& x){return x.id==program;});
    const auto n=std::find_if(uniform_names.begin(),uniform_names.end(),[&](const auto* x){return std::strcmp(x,name)==0;});
    if(p==uniforms.end()||n==uniform_names.end())throw std::logic_error{"Uncached VFX uniform"};
    return p->locations[static_cast<std::size_t>(n-uniform_names.begin())];
}
void VfxPool::initialize(ShaderLibrary& library,const std::filesystem::path& folder){
    if(initialized)return;
    const auto compute=[&](const char* file){return library.program({{GL_COMPUTE_SHADER,folder/file}});};
    init=compute("vfx_init.comp");prepare=compute("vfx_prepare.comp");update=compute("vfx_update.comp");events_program=compute("vfx_events.comp");water_program=compute("vfx_water.comp");finish=compute("vfx_finish.comp");
    graphics=library.program({{GL_VERTEX_SHADER,folder/"vfx_draw.vert"},{GL_FRAGMENT_SHADER,folder/"vfx_draw.frag"}});
    copy=library.program({{GL_VERTEX_SHADER,folder/"liquid_surface.vert"},{GL_FRAGMENT_SHADER,folder/"vfx_canvas.frag"}});canvas_program=copy;
    for(GLuint program:{init,prepare,update,events_program,water_program,finish,graphics,copy}){ProgramUniforms u;u.id=program;
        for(unsigned i=0;i<uniform_names.size();++i)u.locations[i]=gl.GetUniformLocation(program,uniform_names[i]);
        uniforms.push_back(u);}
    gl.GenBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
    const std::array<std::size_t,7> sizes{vfx_capacity*sizeof(VfxParticle),vfx_capacity*4,vfx_capacity*4,vfx_capacity*4,sizeof(VfxCounters),16+vfx_event_capacity*sizeof(VfxEvent),vfx_capacity*12};
    for(unsigned i=0;i<sizes.size();++i){gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[i]);gl.BufferData(GL_SHADER_STORAGE_BUFFER,static_cast<GLsizeiptr>(sizes[i]),nullptr,GL_DYNAMIC_COPY);}
    texture(field,320,192,GL_RGBA16F,GL_RGBA,GL_FLOAT);texture(cpu_terrain,320,192,GL_R16UI,GL_RED_INTEGER,GL_UNSIGNED_SHORT);
    gl.GenFramebuffers(1,&fbo);gl.GenVertexArrays(1,&vao);
    for(unsigned i=0;i<2;++i){texture(cpu_images[i],960,576,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);gl.GenFramebuffers(1,&cpu_fbos[i]);state.framebuffer(cpu_fbos[i]);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,cpu_images[i],0);
        if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Incomplete CPU VFX framebuffer"};}
    initialized=true;reset();state.framebuffer(0);
}
void VfxPool::bind(){state.base(0,buffers[0]);state.base(1,buffers[1+bank]);state.base(2,buffers[2-bank]);state.base(3,buffers[3]);state.base(4,buffers[4]);state.base(5,buffers[5]);state.base(7,buffers[6]);state.texture(2,field);state.texture(3,terrain);}
void VfxPool::run(GLuint program,unsigned groups){
    state.use(program);auto& u=*std::find_if(uniforms.begin(),uniforms.end(),[&](const auto& x){return x.id==program;});
    if(u.locations[0]>=0&&u.last_dt!=dt){gl.Uniform4f(u.locations[0],0.F,0.F,dt,0.F);u.last_dt=dt;}
    if(u.locations[12]>=0&&u.last_normalization!=normalization){gl.Uniform1f(u.locations[12],normalization);u.last_normalization=normalization;}
    if(u.locations[1]>=0&&u.last_quality!=static_cast<int>(config.quality)){gl.Uniform1i(u.locations[1],static_cast<int>(config.quality));u.last_quality=static_cast<int>(config.quality);}
    if(u.locations[10]>=0&&u.last_generation!=generation){gl.Uniform1i(u.locations[10],std::bit_cast<int>(generation));u.last_generation=generation;}
    if(u.locations[11]>=0&&u.last_tick!=tick){gl.Uniform1i(u.locations[11],std::bit_cast<int>(tick));u.last_tick=tick;}
    gl.DispatchCompute(groups,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
void VfxPool::reset(unsigned gen){
    if(!initialized)return;
    generation=gen;tick=0;bank=0;++serial;latest={};latest.counts[1]=vfx_capacity;latest.clock[0]=gen;latest.draw={4,0,0,0};active_probe=nullptr;
    state.invalidate();gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[4]);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(latest),&latest);bind();run(init,(vfx_capacity+127)/128);state.invalidate();
}
void VfxPool::set_options(VfxOptions value){if(value.quality==VfxQuality::off&&config.quality!=VfxQuality::off)reset(generation);config=value;}
VfxPool::Probe* VfxPool::begin_probe(bool draw,bool metadata){
    if((!config.profile&&!metadata)||probe_count==probes.size())return nullptr;
    auto& p=probes[(probe_head+probe_count)%probes.size()];
    if(!p.buffer){gl.GenBuffers(1,&p.buffer);gl.BindBuffer(GL_COPY_WRITE_BUFFER,p.buffer);gl.BufferData(GL_COPY_WRITE_BUFFER,sizeof(VfxCounters),nullptr,GL_STREAM_READ);}
    if(config.profile&&!p.queries[0])gl.GenQueries(4,p.queries.data());
    p.draw=draw;p.measured=config.profile;p.serial=serial;p.tick=tick;if(p.measured)gl.QueryCounter(p.queries[0],GL_TIMESTAMP);return &p;
}
void VfxPool::close_probe(Probe* p){if(!p)return;if(p->measured)gl.QueryCounter(p->queries[3],GL_TIMESTAMP);
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[4]);gl.BindBuffer(GL_COPY_WRITE_BUFFER,p->buffer);gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,sizeof(VfxCounters));p->fence=gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);++probe_count;}
void VfxPool::begin_tick(GLuint terrain_id,unsigned gen,unsigned source_tick,float seconds){
    if(!enabled())return;
    sand_field=0;if(gen!=generation)reset(gen);generation=gen;tick=source_tick;dt=seconds;normalization=1.F/(1.F+4.F*(1.F-1.F/(1.35F*1.35F))*(1.F-1.F/(1.35F*1.35F)));terrain=terrain_id;state.invalidate();bind();active_probe=begin_probe(false);
}
void VfxPool::field_target(){
    state.raster(320,192);state.vertex_array(vao);state.framebuffer(fbo);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,field,0);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
}
void VfxPool::upload_field(std::span<const float> values,std::span<const std::uint16_t> walls){
    if(values.size()!=320*192*4||walls.size()!=320*192)throw std::invalid_argument{"CPU VFX field size"};
    normalization=1.F;state.texture(2,field);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,320,192,GL_RGBA,GL_FLOAT,values.data());state.texture(3,cpu_terrain);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,320,192,GL_RED_INTEGER,GL_UNSIGNED_SHORT,walls.data());terrain=cpu_terrain;
}
void VfxPool::end_tick(GLuint mission,std::span<const VfxEvent> events,bool water){
    if(!enabled())return;
    const auto refused=events.size()>vfx_event_capacity?static_cast<unsigned>(events.size()-vfx_event_capacity):0u;events=events.first(std::min<std::size_t>(events.size(),vfx_event_capacity));
    if(active_probe)gl.QueryCounter(active_probe->queries[1],GL_TIMESTAMP);
    state.invalidate();bind();run(prepare);run(update,(vfx_capacity+127)/128);
    if(active_probe)gl.QueryCounter(active_probe->queries[2],GL_TIMESTAMP);
    if(!mission&&!events.empty()){const std::array<unsigned,4> header{generation,tick,static_cast<unsigned>(events.size()),refused};gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[5]);gl.BufferData(GL_SHADER_STORAGE_BUFFER,16+vfx_event_capacity*sizeof(VfxEvent),nullptr,GL_STREAM_DRAW);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(header),header.data());if(!events.empty())gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,16,static_cast<GLsizeiptr>(events.size_bytes()),events.data());}
    state.base(6,mission?mission:buffers[5]);state.use(events_program);gl.Uniform1i(location(events_program,"cpuSource"),mission?0:events.empty()?2:1);run(events_program);
    if(water)for(int kind=5;kind<=7;++kind){state.use(water_program);gl.Uniform1i(location(water_program,"layer"),kind);run(water_program,((320*192)/(config.quality==VfxQuality::low?4:1)+127)/128);}
    if(sand_field){state.texture(1,sand_field);state.use(water_program);gl.Uniform1i(location(water_program,"layer"),9);run(water_program,((320*192)/(config.quality==VfxQuality::low?4:1)+127)/128);}run(finish);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);bank=1-bank;close_probe(active_probe);active_probe=nullptr;
    if(!config.profile&&tick%30==0&&probe_count<probes.size()){
        auto* p=begin_probe(true,true);close_probe(p);
    }
    state.invalidate();
}
void VfxPool::draw(GLuint target,int w,int h,int layer,float interpolation,bool linear){
    if(!enabled())return;
    state.raster(w,h);state.framebuffer(target);state.vertex_array(vao);bind();
    auto* p=begin_probe(true);state.use(graphics);gl.Uniform1f(location(graphics,"fieldNormalization"),normalization);gl.Uniform1i(location(graphics,"layer"),layer);gl.Uniform1f(location(graphics,"alpha"),std::clamp(interpolation,0.F,1.F));gl.Uniform1i(location(graphics,"linearOutput"),linear?1:0);gl.Uniform4f(location(graphics,"renderExtent"),static_cast<float>(w),static_cast<float>(h),0.F,0.F);
    state.draw_indirect(buffers[4]);glEnable(GL_BLEND);gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);gl.DrawArraysIndirect(GL_TRIANGLE_STRIP,reinterpret_cast<void*>(layer==2?offsetof(VfxCounters,wave):layer==0?offsetof(VfxCounters,pre):offsetof(VfxCounters,post)));glDisable(GL_BLEND);close_probe(p);
}
void VfxPool::allocate_scratch(int w,int h,bool linear){
    if(scratch&&w==scratch_width&&h==scratch_height&&scratch_linear==linear)return;
    if(scratch)glDeleteTextures(1,&scratch);
    texture(scratch,w,h,linear?GL_RGBA16F:GL_RGBA8,GL_RGBA,GL_FLOAT);scratch_linear=linear;if(!scratch_fbo)gl.GenFramebuffers(1,&scratch_fbo);state.framebuffer(scratch_fbo);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,scratch,0);scratch_width=w;scratch_height=h;
    if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Incomplete distortion framebuffer"};
}
void VfxPool::distort(GLuint source,GLuint target,int w,int h,float interpolation,bool linear){
    if(!enabled())return;
    state.invalidate();allocate_scratch(w,h,linear);state.raster(w,h);state.vertex_array(vao);state.framebuffer(scratch_fbo);state.use(copy);state.texture(4,source);gl.Uniform1i(location(copy,"copyMode"),0);gl.Uniform4f(location(copy,"renderExtent"),static_cast<float>(w),static_cast<float>(h),0.F,0.F);
    auto* p=begin_probe(true);state.draw_indirect(buffers[4]);gl.DrawArraysIndirect(GL_TRIANGLES,reinterpret_cast<void*>(offsetof(VfxCounters,copy)));close_probe(p);state.texture(4,scratch);draw(target,w,h,2,interpolation,linear);
}
void VfxPool::cpu_canvas(GLuint source,GLuint background,float interpolation){
    if(!enabled())return;
    state.invalidate();state.raster(960,576);state.vertex_array(vao);state.framebuffer(cpu_fbos[0]);state.texture(4,source);state.texture(1,background);state.use(canvas_program);gl.Uniform1i(location(copy,"copyMode"),1);gl.Uniform4f(location(copy,"renderExtent"),960.F,576.F,0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);
    draw(cpu_fbos[0],960,576,0,interpolation);distort(cpu_images[0],cpu_fbos[0],960,576,interpolation);state.framebuffer(cpu_fbos[1]);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);draw(cpu_fbos[1],960,576,1,interpolation);state.framebuffer(0);state.invalidate();
}
void VfxPool::poll(){
    while(probe_count){auto& p=probes[probe_head];const auto ready=gl.ClientWaitSync(p.fence,0,0);if(ready==GL_WAIT_FAILED)throw std::runtime_error{"VFX telemetry fence failed"};if(ready!=GL_ALREADY_SIGNALED&&ready!=GL_CONDITION_SATISFIED)break;
        if(p.serial==serial){gl.BindBuffer(GL_COPY_READ_BUFFER,p.buffer);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(latest),&latest);if(config.profile&&p.measured){std::array<GLuint64,4> stamp{};gl.GetQueryObjectui64v(p.queries[0],GL_QUERY_RESULT,&stamp[0]);gl.GetQueryObjectui64v(p.queries[3],GL_QUERY_RESULT,&stamp[3]);VfxSample result;result.tick=p.tick;
            if(p.draw)result.draw_ms=static_cast<double>(stamp[3]-stamp[0])/1e6;else{gl.GetQueryObjectui64v(p.queries[1],GL_QUERY_RESULT,&stamp[1]);gl.GetQueryObjectui64v(p.queries[2],GL_QUERY_RESULT,&stamp[2]);result.field_ms=static_cast<double>(stamp[1]-stamp[0])/1e6;result.update_ms=static_cast<double>(stamp[2]-stamp[1])/1e6;result.emit_ms=static_cast<double>(stamp[3]-stamp[2])/1e6;}samples.push_back(result);}}
        gl.DeleteSync(p.fence);p.fence=nullptr;probe_head=(probe_head+1)%static_cast<unsigned>(probes.size());--probe_count;
    }state.invalidate();
}
std::vector<VfxSample> VfxPool::take_samples(){poll();return std::exchange(samples,{});}
std::uint64_t VfxPool::memory_bytes()const{std::uint64_t staging=0;for(const auto& p:probes)if(p.buffer)staging+=sizeof(VfxCounters);return staging+vfx_capacity*(sizeof(VfxParticle)+24)+sizeof(VfxCounters)+16+vfx_event_capacity*sizeof(VfxEvent)+320*192*10+960*576*8+static_cast<std::uint64_t>(scratch_width)*scratch_height*(scratch_linear?8:4);}
VfxSnapshot VfxPool::snapshot(){
    VfxSnapshot result;gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[4]);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(result.counters),&result.counters);
    if(result.counters.counts[0]>vfx_capacity||result.counters.counts[1]>vfx_capacity)throw std::runtime_error{"VFX counter overflow"};
    result.particles.resize(vfx_capacity);result.alive.resize(result.counters.counts[0]);result.free.resize(result.counters.counts[1]);
    gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[0]);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,static_cast<GLsizeiptr>(result.particles.size()*sizeof(VfxParticle)),result.particles.data());
    gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[1+bank]);if(!result.alive.empty())gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,static_cast<GLsizeiptr>(result.alive.size()*4),result.alive.data());
    gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[3]);if(!result.free.empty())gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,static_cast<GLsizeiptr>(result.free.size()*4),result.free.data());state.invalidate();return result;
}
void VfxPool::seed_for_test(std::span<const VfxParticle> input){
    if(input.size()>vfx_capacity)throw std::invalid_argument{"VFX diagnostic seed capacity"};
    reset(generation);std::vector<unsigned> ids(vfx_capacity);std::iota(ids.begin(),ids.end(),0u);latest.counts={static_cast<unsigned>(input.size()),vfx_capacity-static_cast<unsigned>(input.size()),0,static_cast<unsigned>(input.size())};latest.draw={4,static_cast<unsigned>(input.size()),0,0};
    std::vector<unsigned> draw_ids(vfx_capacity*3);for(unsigned i=0;i<input.size();++i){const auto kind=input[i].appearance[0];const auto layer=(kind==7||kind==9||kind==10)?0u:kind==11?2u:1u;draw_ids[layer*vfx_capacity+latest.layers[layer]++]=i;}latest.pre={4,latest.layers[0],0,0};latest.post={4,latest.layers[1],0,0};latest.wave={4,latest.layers[2],0,0};latest.copy={3,latest.layers[2]?1u:0u,0,0};
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);const auto put=[&](GLuint buffer,GLsizeiptr bytes,const void* data){gl.BindBuffer(GL_COPY_WRITE_BUFFER,buffer);if(bytes)gl.BufferSubData(GL_COPY_WRITE_BUFFER,0,bytes,data);};
    put(buffers[0],static_cast<GLsizeiptr>(input.size_bytes()),input.data());put(buffers[1],static_cast<GLsizeiptr>(input.size()*4),ids.data());put(buffers[3],static_cast<GLsizeiptr>((vfx_capacity-input.size())*4),ids.data()+input.size());put(buffers[4],sizeof(latest),&latest);put(buffers[6],static_cast<GLsizeiptr>(draw_ids.size()*4),draw_ids.data());gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);state.invalidate();
}
}
