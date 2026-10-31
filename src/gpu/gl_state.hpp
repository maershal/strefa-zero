#pragma once
#include "gpu/gl_api.hpp"
#include <array>
#include <limits>
namespace strefa::gpu::detail {
// Shared by compute and graphics; external GL (ImGui/uploads/captures) invalidates it.
class GlStateTracker {
public:
    explicit GlStateTracker(Api& api):gl{api}{invalidate();}
    GLuint program{},indirect{};
    std::array<GLuint,8> bases{};
    void invalidate(){raster_known=false;program=indirect=unknown;fbo=vao=unknown;bases.fill(unknown);textures.fill(unknown);}
    void use(GLuint value){if(program!=value){gl.UseProgram(value);program=value;}}
    void base(unsigned index,GLuint value){if(bases[index]!=value){gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,index,value);bases[index]=value;}}
    void draw_indirect(GLuint value){if(indirect!=value){gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER,value);indirect=value;}}
    void framebuffer(GLuint value){if(fbo!=value){gl.BindFramebuffer(GL_FRAMEBUFFER,value);fbo=value;}}
    void vertex_array(GLuint value){if(vao!=value){gl.BindVertexArray(value);vao=value;}}
    void texture(unsigned unit,GLuint value){gl.ActiveTexture(GL_TEXTURE0+unit);if(textures[unit]!=value){glBindTexture(GL_TEXTURE_2D,value);textures[unit]=value;}}
    void raster(int render_width,int render_height){
        if(raster_known&&raster_width==render_width&&raster_height==render_height){glDisable(GL_BLEND);return;}
        raster_known=true;raster_width=render_width;raster_height=render_height;
        glViewport(0,0,render_width,render_height);for(GLuint unit=0;unit<8;++unit)gl.BindSampler(unit,0);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_COLOR_LOGIC_OP);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glDisable(GL_BLEND);gl.BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    }
private:
    static constexpr GLuint unknown=std::numeric_limits<GLuint>::max();
    bool raster_known{};int raster_width{},raster_height{};
    Api& gl;GLuint fbo{},vao{};std::array<GLuint,8> textures{};
};
}
