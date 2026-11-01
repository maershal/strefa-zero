#include "gpu/scene_renderer.hpp"
#include "gpu/world.hpp"
#include <algorithm>

namespace strefa::gpu::detail {
SceneRenderer::~SceneRenderer(){
    if(fluid_fbo)gl.DeleteFramebuffers(1,&fluid_fbo);
    if(fluid_image)glDeleteTextures(1,&fluid_image);
    if(vao)gl.DeleteVertexArrays(1,&vao);
    if(fbo)gl.DeleteFramebuffers(1,&fbo);
    if(image)glDeleteTextures(1,&image);
}
void SceneRenderer::initialize(ShaderLibrary& library,const std::filesystem::path& shaders,const std::string& defines){
        actor_program=library.program({{GL_VERTEX_SHADER,shaders/"mission.vert"},{GL_FRAGMENT_SHADER,shaders/"mission.frag"}});
        actor_alpha=gl.GetUniformLocation(actor_program,"alpha");actor_parts=gl.GetUniformLocation(actor_program,"partsTexture");
        actor_weapons=gl.GetUniformLocation(actor_program,"weaponsTexture");actor_weapon_size=gl.GetUniformLocation(actor_program,"weaponSize");
        splat=library.program({{GL_VERTEX_SHADER,shaders/"liquid_splat.vert"},{GL_FRAGMENT_SHADER,shaders/"liquid_splat.frag"}},defines);
        surface=library.program({{GL_VERTEX_SHADER,shaders/"liquid_surface.vert"},{GL_FRAGMENT_SHADER,shaders/"liquid_surface.frag"}});
        splat_alpha=gl.GetUniformLocation(splat,"alpha");
        draw=library.program({{GL_VERTEX_SHADER,shaders/"granular.vert"},{GL_FRAGMENT_SHADER,shaders/"granular.frag"}},defines);
        alpha=gl.GetUniformLocation(draw,"alpha");terrain_pass=gl.GetUniformLocation(draw,"terrainPass");
        glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width*3,height*3,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl.GenFramebuffers(1,&fbo);gl.BindFramebuffer(GL_FRAMEBUFFER,fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
        if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Niekompletny framebuffer GPU."};
        glGenTextures(1,&fluid_image);glBindTexture(GL_TEXTURE_2D,fluid_image);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,width*3,height*3,0,GL_RG,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl.GenFramebuffers(1,&fluid_fbo);gl.BindFramebuffer(GL_FRAMEBUFFER,fluid_fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,fluid_image,0);
        if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error{"Niekompletny framebuffer cieczy."};
        gl.BindFramebuffer(GL_FRAMEBUFFER,0);gl.GenVertexArrays(1,&vao);
}
void SceneRenderer::render(const RenderState& state,float interpolation){
    auto active_draw=[&]{
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,5,state.buffers[5]);
        gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER,state.buffers[5]);
        gl.DrawArraysIndirect(GL_TRIANGLE_STRIP,reinterpret_cast<void*>(capacity*8+12));
    };
    GLint previous_fbo{},previous_vao{},previous_program{},viewport[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_fbo);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previous_vao);
    glGetIntegerv(GL_CURRENT_PROGRAM,&previous_program);glGetIntegerv(GL_VIEWPORT,viewport);
    GLint saved_active{};glGetIntegerv(GL_ACTIVE_TEXTURE,&saved_active);
    GLint texture1{};gl.ActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture1);gl.ActiveTexture(GL_TEXTURE0);
    GLint active_texture{},texture{},blend_src{},blend_dst{},alpha_src{},alpha_dst{},eq_rgb{},eq_alpha{};
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active_texture);gl.ActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glGetIntegerv(GL_BLEND_SRC_RGB,&blend_src);glGetIntegerv(GL_BLEND_DST_RGB,&blend_dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&alpha_src);glGetIntegerv(GL_BLEND_DST_ALPHA,&alpha_dst);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,&eq_rgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eq_alpha);
    const auto blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST),depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    gl.BindVertexArray(vao);gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,state.buffers[state.current]);
    if(state.fluids_enabled){
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,fluid_fbo);glViewport(0,0,width*3,height*3);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_BLEND);
        gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ONE);gl.BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        gl.UseProgram(splat);gl.Uniform1f(splat_alpha,std::clamp(interpolation,0.F,1.F));
        active_draw();glDisable(GL_BLEND);
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);glViewport(0,0,width*3,height*3);glClearColor(.035F,.055F,.065F,state.mission_enabled?0.F:1.F);glClear(GL_COLOR_BUFFER_BIT);
    if(state.fluids_enabled){
        gl.UseProgram(surface);glBindTexture(GL_TEXTURE_2D,fluid_image);glDrawArrays(GL_TRIANGLES,0,3);
    }
    gl.UseProgram(draw);gl.BindVertexArray(vao);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,state.buffers[state.current]);gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,4,state.buffers[4]);
    gl.Uniform1f(alpha,std::clamp(interpolation,0.F,1.F));gl.Uniform1i(terrain_pass,1);
    gl.DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,width*height);
    gl.Uniform1i(terrain_pass,0);active_draw();
    if(state.mission_enabled){
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,6,state.buffers[6]);gl.UseProgram(actor_program);
        gl.Uniform1f(actor_alpha,std::clamp(interpolation,0.F,1.F));gl.Uniform1i(actor_parts,0);gl.Uniform1i(actor_weapons,1);
        gl.Uniform4f(actor_weapon_size,static_cast<float>(weapon_width),static_cast<float>(weapon_height),0.F,0.F);
        gl.ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,parts_texture);gl.ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,weapons_texture);
        glEnable(GL_BLEND);gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        gl.DrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,159);gl.ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture1));gl.ActiveTexture(GL_TEXTURE0);
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(previous_fbo));gl.BindVertexArray(static_cast<GLuint>(previous_vao));gl.UseProgram(static_cast<GLuint>(previous_program));
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture));gl.ActiveTexture(static_cast<GLenum>(saved_active));
    gl.BlendFuncSeparate(static_cast<GLenum>(blend_src),static_cast<GLenum>(blend_dst),static_cast<GLenum>(alpha_src),static_cast<GLenum>(alpha_dst));
    gl.BlendEquationSeparate(static_cast<GLenum>(eq_rgb),static_cast<GLenum>(eq_alpha));
    if(!blend)glDisable(GL_BLEND);
    if(blend)glEnable(GL_BLEND);
    if(scissor)glEnable(GL_SCISSOR_TEST);
    if(depth)glEnable(GL_DEPTH_TEST);
    if(cull)glEnable(GL_CULL_FACE);
}
void SceneRenderer::save_image(const std::filesystem::path& path)const{
    std::vector<std::uint32_t> pixels(width*height*9);GLint old{};glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glReadPixels(0,0,width*3,height*3,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());gl.BindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(old));
    auto* bitmap=SDL_CreateSurfaceFrom(width*3,height*3,SDL_PIXELFORMAT_RGBA32,pixels.data(),width*3*4);
    if(!bitmap)throw std::runtime_error{SDL_GetError()};const auto utf8=path.u8string();const bool saved=SDL_SaveBMP(bitmap,reinterpret_cast<const char*>(utf8.c_str()));SDL_DestroySurface(bitmap);
    if(!saved)throw std::runtime_error{SDL_GetError()};
}
}
