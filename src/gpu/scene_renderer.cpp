#include "gpu/scene_renderer.hpp"
#include "gpu/world.hpp"
#include <algorithm>
#include <limits>
namespace strefa::gpu::detail {
SceneRenderer::~SceneRenderer(){
    for(auto& t:terrain_timings)if(t.ids[0])gl.DeleteQueries(2,t.ids.data());
    release_frozen();for(auto& t:timings)if(t.ids[0])gl.DeleteQueries(10,t.ids.data());delete_targets();
    if(field_command)gl.DeleteBuffers(1,&field_command);
    if(terrain_image)glDeleteTextures(1,&terrain_image);
    if(vao)gl.DeleteVertexArrays(1,&vao);
}
GLint SceneRenderer::location(GLuint p,const char* name)const{return uniforms.at(p).at(name);}
void SceneRenderer::delete_targets(){
    for(auto* texture:{&image,&scene_image,&receiver_image,&raw_image,&temporary_image,&smooth_image,&thickness_image,&hdr_scene,&hdr_world})if(*texture){glDeleteTextures(1,texture);*texture=0;}
    for(auto* fbo:{&world_fbo,&scene_fbo,&field_fbo,&hdr_scene_fbo,&hdr_world_fbo})if(*fbo){gl.DeleteFramebuffers(1,fbo);*fbo=0;}
    state.invalidate();
}
void SceneRenderer::allocate_targets(){
    auto texture=[&](GLuint& id,GLint format,GLenum channels,GLenum type){glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
        glTexImage2D(GL_TEXTURE_2D,0,format,rw,rh,0,channels,type,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);};
    texture(image,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);texture(scene_image,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    texture(hdr_scene,GL_RGBA16F,GL_RGBA,GL_FLOAT);texture(hdr_world,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    texture(receiver_image,GL_R8UI,GL_RED_INTEGER,GL_UNSIGNED_BYTE);
    for(auto* id:{&raw_image,&temporary_image,&smooth_image})texture(*id,GL_RGBA16F,GL_RGBA,GL_FLOAT);
    texture(thickness_image,GL_R16F,GL_RED,GL_FLOAT);
    const auto fbo=[&](GLuint& id,GLuint color){gl.GenFramebuffers(1,&id);gl.BindFramebuffer(GL_FRAMEBUFFER,id);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Niekompletny framebuffer I08."};};
    fbo(hdr_scene_fbo,hdr_scene);fbo(hdr_world_fbo,hdr_world);fbo(world_fbo,image);fbo(field_fbo,raw_image);fbo(scene_fbo,scene_image);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,receiver_image,0);
    const std::array<GLenum,2> attachments{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};gl.DrawBuffers(2,attachments.data());
    if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Niekompletny MRT sceny I08."};
    gl.BindFramebuffer(GL_FRAMEBUFFER,0);state.invalidate();
}
void SceneRenderer::initialize(ShaderLibrary& library,const std::filesystem::path& shaders,const std::string& defines){
    const auto graphics=[&](const char* vertex,const char* fragment,const std::string& config=std::string{}){return library.program({{GL_VERTEX_SHADER,shaders/vertex},{GL_FRAGMENT_SHADER,shaders/fragment}},config);};
    actors=graphics("mission.vert","mission.frag");grains=graphics("granular.vert","granular.frag",defines);
    background=graphics("liquid_surface.vert","scene_background.frag");terrain=graphics("liquid_surface.vert","scene_terrain.frag");
    splat=graphics("liquid_splat.vert","liquid_splat.frag",defines);filter=graphics("liquid_surface.vert","liquid_filter.frag");
    composition=graphics("liquid_surface.vert","liquid_surface.frag");
    thickness=library.program({{GL_COMPUTE_SHADER,shaders/"liquid_thickness.comp"}});
    field_prepare=library.program({{GL_COMPUTE_SHADER,shaders/"vfx_field_command.comp"}});
    field_splat=graphics("vfx_field.vert","liquid_splat.frag",defines+"\n#define VFX_FIELD 1\n");
    gl.GenBuffers(1,&field_command);gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER,field_command);const std::array<unsigned,4> field_draw{0,1,0,0};gl.BufferData(GL_DRAW_INDIRECT_BUFFER,sizeof(field_draw),field_draw.data(),GL_DYNAMIC_COPY);state.invalidate();
    reference_splat=graphics("reference_liquid_splat.vert","reference_liquid_splat.frag",defines);
    reference_surface=graphics("liquid_surface.vert","reference_liquid_surface.frag");
    gl.GenVertexArrays(1,&vao);glGenTextures(1,&terrain_image);glBindTexture(GL_TEXTURE_2D,terrain_image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R16UI,width,height,0,GL_RED_INTEGER,GL_UNSIGNED_SHORT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    for(GLuint program:{field_splat,actors,grains,background,terrain,splat,filter,thickness,composition,reference_splat,reference_surface})
        for(const char* name:{"alpha","partsTexture","weaponsTexture","weaponSize","instanceOffset","overlay","referencePass","vfxEnabled","targetHP","targetHit","hasBackground","renderExtent","terrainPass","bilateral","directionExtent","quality","hasFluid","stage","hdrOutput","sandStyle","cameraOffset"})
            uniforms[program][name]=gl.GetUniformLocation(program,name);
    allocate_targets();lighting.initialize(library,shaders,defines);
}
void SceneRenderer::extent(GLuint program,const char* uniform){gl.Uniform4f(location(program,uniform),static_cast<float>(rw),static_cast<float>(rh),0.F,0.F);}
void SceneRenderer::attachment(GLuint texture){state.framebuffer(field_fbo);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);}
void SceneRenderer::fullscreen(GLuint program){state.use(program);glDrawArrays(GL_TRIANGLES,0,3);}
void SceneRenderer::draw_active(const RenderState& input){state.base(5,input.buffers[5]);state.draw_indirect(input.buffers[5]);gl.DrawArraysIndirect(GL_TRIANGLE_STRIP,reinterpret_cast<void*>(capacity*8+12));}
void SceneRenderer::actor_pass(const RenderState& input,float interpolation,int offset,int count,bool overlay,bool reference){
    state.base(6,input.buffers[6]);state.use(actors);gl.Uniform1f(location(actors,"alpha"),interpolation);
    gl.Uniform1i(location(actors,"partsTexture"),0);gl.Uniform1i(location(actors,"weaponsTexture"),1);
    gl.Uniform4f(location(actors,"weaponSize"),static_cast<float>(weapon_width),static_cast<float>(weapon_height),0.F,0.F);
    gl.Uniform1i(location(actors,"instanceOffset"),offset);gl.Uniform1i(location(actors,"overlay"),overlay?1:0);const auto camera=lighting.camera(options.lighting);gl.Uniform4f(location(actors,"cameraOffset"),overlay&&options.lighting.quality!=LightingQuality::off?camera[0]/static_cast<float>(rw)*320.F:0.F,overlay&&options.lighting.quality!=LightingQuality::off?camera[1]/static_cast<float>(rh)*192.F:0.F,0.F,0.F);gl.Uniform1i(location(actors,"referencePass"),reference?1:0);
    gl.Uniform4f(location(actors,"targetHP"),static_cast<float>(targets.hp[0]),static_cast<float>(targets.hp[1]),static_cast<float>(targets.hp[2]),0.F);
    gl.Uniform4f(location(actors,"targetHit"),targets.hit[0],targets.hit[1],targets.hit[2],0.F);
    gl.Uniform1i(location(actors,"vfxEnabled"),vfx&&!reference?1:0);
    state.texture(0,parts_texture);state.texture(1,weapons_texture);glEnable(GL_BLEND);
    gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    gl.DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,count);glDisable(GL_BLEND);
}
void SceneRenderer::build_vfx_field(const RenderState& input){
    vfx->field_target();if(input.fluids_enabled){
        state.base(1,field_command);state.base(6,input.buffers[6]);state.use(field_prepare);gl.DispatchCompute(1,1,1);gl.MemoryBarrier(GL_COMMAND_BARRIER_BIT);
        state.base(0,input.buffers[input.current]);state.base(5,input.buffers[5]);state.texture(3,terrain_image);state.use(field_splat);gl.Uniform4f(location(field_splat,"renderExtent"),320.F,192.F,0.F,0.F);state.draw_indirect(field_command);
        glEnable(GL_PROGRAM_POINT_SIZE);glEnable(GL_BLEND);gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ONE);gl.DrawArraysIndirect(GL_POINTS,nullptr);glDisable(GL_BLEND);glDisable(GL_PROGRAM_POINT_SIZE);
    }state.framebuffer(0);state.invalidate();
}
void SceneRenderer::render(const RenderState& input,float interpolation){
    poll_samples();auto& timing=timings[timing_cursor];const bool measured=options.profile_passes&&!timing.pending;
    if(measured){if(!timing.ids[0])gl.GenQueries(10,timing.ids.data());timing.frame=frame;gl.QueryCounter(timing.ids[0],GL_TIMESTAMP);}
    auto mark=[&](unsigned i){if(measured)gl.QueryCounter(timing.ids[i],GL_TIMESTAMP);};
    ++frame;state.invalidate();state.raster(rw,rh);state.vertex_array(vao);
    state.base(0,input.buffers[input.current]);state.base(4,input.buffers[4]);state.texture(3,terrain_image);
    const float a=std::clamp(interpolation,0.F,1.F);const bool reference=stage==RenderStage::reference;const bool hdr=!reference&&options.lighting.quality!=LightingQuality::off;
    state.framebuffer(scene_fbo);state.texture(0,background_texture);state.use(background);
    gl.Uniform1i(location(background,"hasBackground"),input.mission_enabled&&background_texture?1:0);extent(background,"renderExtent");glDrawArrays(GL_TRIANGLES,0,3);if(!reference)mark(8);
    if(!reference){state.use(terrain);extent(terrain,"renderExtent");glDrawArrays(GL_TRIANGLES,0,3);mark(9);
        state.use(grains);state.texture(6,lighting.moisture);state.texture(7,lighting.field);gl.Uniform1i(location(grains,"sandStyle"),hdr?1:0);gl.Uniform1f(location(grains,"alpha"),a);gl.Uniform1i(location(grains,"referencePass"),0);gl.Uniform1i(location(grains,"terrainPass"),0);draw_active(input);
        if(input.mission_enabled)actor_pass(input,a,0,157,false);
    }
    if(!reference&&vfx&&vfx->enabled()){vfx->draw(scene_fbo,rw,rh,0,a,true);state.base(0,input.buffers[input.current]);state.base(4,input.buffers[4]);state.texture(3,terrain_image);}
    mark(1);
    if(hdr){lighting.illuminate(scene_image,receiver_image,terrain_image,rw,rh,hdr_scene_fbo,options.lighting);state.base(0,input.buffers[input.current]);state.base(4,input.buffers[4]);}
    attachment(raw_image);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    if(input.fluids_enabled){state.texture(3,terrain_image);state.use(reference?reference_splat:splat);gl.Uniform1f(location(reference?reference_splat:splat,"alpha"),a);
        if(!reference)extent(splat,"renderExtent");
        glEnable(GL_BLEND);gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ONE);draw_active(input);glDisable(GL_BLEND);
    }mark(2);
    const bool smooth=input.fluids_enabled&&!reference&&options.quality!=RenderQuality::basic&&stage!=RenderStage::opaque;
    if(smooth){attachment(temporary_image);state.texture(0,raw_image);state.texture(2,raw_image);state.texture(3,terrain_image);state.use(filter);
        gl.Uniform1i(location(filter,"bilateral"),bilateral?1:0);gl.Uniform4f(location(filter,"directionExtent"),1.F,0.F,0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);
    }mark(3);
    if(smooth){attachment(smooth_image);state.texture(0,temporary_image);gl.Uniform4f(location(filter,"directionExtent"),0.F,1.F,0.F,0.F);glDrawArrays(GL_TRIANGLES,0,3);}
    mark(4);
    if(smooth){state.texture(2,raw_image);state.use(thickness);gl.BindImageTexture(1,thickness_image,0,static_cast<GLboolean>(GL_FALSE),0,GL_WRITE_ONLY,GL_R16F);
        gl.DispatchCompute(static_cast<GLuint>((rw+63)/64),1,1);gl.MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }mark(5);
    state.framebuffer(hdr?hdr_world_fbo:world_fbo);
    if(reference){
        state.texture(0,scene_image);state.texture(1,raw_image);state.texture(2,raw_image);state.texture(3,terrain_image);state.use(composition);
        gl.Uniform1i(location(composition,"hdrOutput"),0);gl.Uniform1i(location(composition,"hasFluid"),0);gl.Uniform1i(location(composition,"stage"),1);gl.Uniform1i(location(composition,"quality"),0);glDrawArrays(GL_TRIANGLES,0,3);
        if(input.fluids_enabled){state.texture(0,raw_image);fullscreen(reference_surface);}
        state.use(grains);gl.Uniform1f(location(grains,"alpha"),a);gl.Uniform1i(location(grains,"sandStyle"),0);gl.Uniform1i(location(grains,"referencePass"),1);gl.Uniform1i(location(grains,"terrainPass"),1);
        mark(8);gl.DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,width*height);mark(9);gl.Uniform1i(location(grains,"terrainPass"),0);draw_active(input);
        if(input.mission_enabled)actor_pass(input,a,0,159,false,true);
    }else{
        state.texture(0,hdr?hdr_scene:scene_image);state.texture(1,smooth?smooth_image:raw_image);state.texture(2,raw_image);state.texture(3,terrain_image);state.texture(4,thickness_image);state.texture(5,receiver_image);state.texture(6,lighting.light_image);
        state.use(composition);gl.Uniform1i(location(composition,"hdrOutput"),hdr?1:0);gl.Uniform1i(location(composition,"quality"),static_cast<int>(options.quality));gl.Uniform1i(location(composition,"hasFluid"),input.fluids_enabled?1:0);
        gl.Uniform1i(location(composition,"stage"),static_cast<int>(stage));glDrawArrays(GL_TRIANGLES,0,3);
    }
    if(!reference&&thermal&&thermal->options.enabled)thermal->draw(hdr?hdr_world_fbo:world_fbo,rw,rh,hdr);
    if(!reference&&vfx&&vfx->enabled()){vfx->draw(hdr?hdr_world_fbo:world_fbo,rw,rh,1,a,hdr);vfx->distort(hdr?hdr_world:image,hdr?hdr_world_fbo:world_fbo,rw,rh,a,hdr);}
    if(hdr)lighting.finish(hdr_world,world_fbo,rw,rh,options.lighting);
    mark(6);state.raster(rw,rh);state.framebuffer(world_fbo);
    if(!reference&&input.mission_enabled){actor_pass(input,a,157,2,true);actor_pass(input,a,159,18,true);}
    mark(7);state.framebuffer(0);gl.ActiveTexture(GL_TEXTURE0);
    if(measured){timing.pending=true;timing_cursor=(timing_cursor+1)%static_cast<unsigned>(timings.size());}
}
void SceneRenderer::begin_terrain_update(){
    auto& t=terrain_timings[terrain_cursor];terrain_active=options.profile_passes&&!t.pending;
    if(terrain_active){if(!t.ids[0])gl.GenQueries(2,t.ids.data());gl.QueryCounter(t.ids[0],GL_TIMESTAMP);}
}
void SceneRenderer::end_terrain_update(){if(terrain_active){auto& t=terrain_timings[terrain_cursor];gl.QueryCounter(t.ids[1],GL_TIMESTAMP);t.pending=true;terrain_cursor=(terrain_cursor+1)%32;terrain_active=false;}}
void SceneRenderer::poll_samples(){
    for(unsigned i=0;i<32;++i){auto& t=terrain_timings[terrain_head];if(!t.pending)break;GLint ready{};gl.GetQueryObjectiv(t.ids[1],GL_QUERY_RESULT_AVAILABLE,&ready);if(!ready)break;
        GLuint64 a{},b{};gl.GetQueryObjectui64v(t.ids[0],GL_QUERY_RESULT,&a);gl.GetQueryObjectui64v(t.ids[1],GL_QUERY_RESULT,&b);
        if(terrain_samples.size()<8192)terrain_samples.push_back(static_cast<double>(b-a)/1e6);
        t.pending=false;terrain_head=(terrain_head+1)%32;
    }

    for(unsigned i=0;i<timings.size();++i){auto& t=timings[timing_head];if(!t.pending)break;GLint ready{};gl.GetQueryObjectiv(t.ids[7],GL_QUERY_RESULT_AVAILABLE,&ready);if(!ready)break;
        std::array<GLuint64,10> stamp{};for(unsigned k=0;k<10;++k)gl.GetQueryObjectui64v(t.ids[k],GL_QUERY_RESULT,&stamp[k]);
        RenderSample sample;sample.frame=t.frame;for(unsigned k=0;k<7;++k)sample.milliseconds[k]=static_cast<double>(stamp[k+1]-stamp[k])/1e6;
        sample.terrain_ms=static_cast<double>(stamp[9]-stamp[8])/1e6;sample.milliseconds[7]=static_cast<double>(stamp[7]-stamp[0])/1e6;if(samples.size()<8192)samples.push_back(sample);
        t.pending=false;timing_head=(timing_head+1)%static_cast<unsigned>(timings.size());
    }
}
std::vector<RenderSample> SceneRenderer::take_samples(){poll_samples();return std::exchange(samples,{});}
void SceneRenderer::reset_profiles(){
    for(auto& t:timings){if(t.ids[0])gl.DeleteQueries(10,t.ids.data());t={};}samples.clear();timing_cursor=timing_head=0;
    for(auto& t:terrain_timings){if(t.ids[0])gl.DeleteQueries(2,t.ids.data());t={};}terrain_head=terrain_cursor=0;terrain_active=false;terrain_samples.clear();
}
void SceneRenderer::resize_for_test(int scale){if(scale<1||scale>6)throw std::invalid_argument{"Render scale outside 1..6"};
    if(rw==width*scale)return;
    delete_targets();rw=width*scale;rh=height*scale;allocate_targets();reset_profiles();}
std::uint64_t SceneRenderer::memory_bytes()const{return static_cast<std::uint64_t>(rw)*rh*(4+8+1+3*8+2+16)+width*height*2+16+lighting.memory_bytes();}
std::vector<std::uint8_t> SceneRenderer::read_image()const{std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rw)*rh*4);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,world_fbo);glReadPixels(0,0,rw,rh,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());gl.BindFramebuffer(GL_READ_FRAMEBUFFER,0);state.invalidate();return pixels;}
std::vector<std::uint16_t> SceneRenderer::read_terrain_texture()const{std::vector<std::uint16_t> result(width*height);glBindTexture(GL_TEXTURE_2D,terrain_image);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RED_INTEGER,GL_UNSIGNED_SHORT,result.data());state.invalidate();return result;}
std::vector<float> SceneRenderer::read_field(unsigned which)const{GLuint source=which==0?raw_image:which==1?smooth_image:thickness_image;
    const unsigned channels=which==2?1:4;std::vector<float> pixels(static_cast<std::size_t>(rw)*rh*channels);
    gl.BindFramebuffer(GL_FRAMEBUFFER,field_fbo);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
    glReadPixels(0,0,rw,rh,channels==1?GL_RED:GL_RGBA,GL_FLOAT,pixels.data());gl.BindFramebuffer(GL_FRAMEBUFFER,0);state.invalidate();return pixels;}
void SceneRenderer::save_image(const std::filesystem::path& path)const{auto pixels=read_image();
    auto* bitmap=SDL_CreateSurfaceFrom(rw,rh,SDL_PIXELFORMAT_RGBA32,pixels.data(),rw*4);if(!bitmap)throw std::runtime_error{SDL_GetError()};
    const auto utf8=path.u8string();const bool saved=SDL_SaveBMP(bitmap,reinterpret_cast<const char*>(utf8.c_str()));SDL_DestroySurface(bitmap);if(!saved)throw std::runtime_error{SDL_GetError()};}
RenderState SceneRenderer::freeze(const RenderState& input){release_frozen();gl.GenBuffers(8,frozen_buffers.data());
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);for(unsigned i=0;i<8;++i){gl.BindBuffer(GL_COPY_READ_BUFFER,input.buffers[i]);GLint size{};gl.GetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&size);
        gl.BindBuffer(GL_COPY_WRITE_BUFFER,frozen_buffers[i]);gl.BufferData(GL_COPY_WRITE_BUFFER,size,nullptr,GL_STATIC_COPY);gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,size);}
    gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);state.invalidate();auto result=input;result.buffers=frozen_buffers;return result;}
void SceneRenderer::release_frozen(){if(frozen_buffers[0])gl.DeleteBuffers(8,frozen_buffers.data());frozen_buffers={};state.invalidate();}
}
