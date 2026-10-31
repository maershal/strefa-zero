#include "gpu/lighting_system.hpp"
#include "gpu/device_context.hpp"
#include "gpu/world.hpp"
#include "platform/window.hpp"
#include "platform/gl_debug.hpp"
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=3)return 2;const std::filesystem::path output{argv[2]};std::filesystem::create_directories(output);
    try{
        auto sdl=strefa::platform::SdlSystem::create();if(!sdl)throw std::runtime_error{sdl.error()};
        strefa::core::Settings settings;settings.vsync=false;auto window=strefa::platform::Window::create(settings,true,true);if(!window)throw std::runtime_error{window.error()};
        GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);if(major<4||(major==4&&minor<3))return 77;
        auto debug=strefa::platform::GlDebugLog::create(output/"gl-debug.txt");if(debug)debug->set_phase(strefa::platform::DebugPhase::validation);
        strefa::gpu::detail::DeviceContext device;auto& gl=device.gl;strefa::gpu::detail::ShaderLibrary library{gl};strefa::gpu::detail::LightingSystem light{gl,device.state};light.initialize(library,argv[1],"#define PARTICLE_SOA 0\n");
        std::ofstream report{output/"lighting-pressure-tests.txt"};const auto require=[&](bool ok,const char* name){report<<name<<'='<<(ok?"PASS":"FAIL")<<'\n';report.flush();if(!ok)throw std::runtime_error{name};};
        std::array<GLuint,8> buffers{};gl.GenBuffers(8,buffers.data());std::vector<strefa::gpu::Grain> grains(strefa::gpu::capacity);for(auto& p:grains){p.position={160.5F,100.5F,160.5F,100.5F};p.motion={80,0,1,1};}
        const auto upload=[&](GLuint id,GLsizeiptr bytes,const void* data){gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,id);gl.BufferData(GL_SHADER_STORAGE_BUFFER,bytes,data,GL_DYNAMIC_COPY);};
        upload(buffers[0],static_cast<GLsizeiptr>(grains.size()*sizeof(grains[0])),grains.data());
        std::vector<unsigned> slots(strefa::gpu::capacity*2+37);for(unsigned i=0;i<strefa::gpu::capacity;++i)slots[strefa::gpu::capacity+i]=i;upload(buffers[5],static_cast<GLsizeiptr>(slots.size()*4),slots.data());
        std::vector<std::byte> mission(strefa::gpu::mission_buffer_size);const unsigned count=strefa::gpu::capacity;std::memcpy(mission.data()+8,&count,4);upload(buffers[6],static_cast<GLsizeiptr>(mission.size()),mission.data());
        GLuint terrain{};glGenTextures(1,&terrain);glBindTexture(GL_TEXTURE_2D,terrain);std::vector<unsigned short> walls(320*192);glTexImage2D(GL_TEXTURE_2D,0,GL_R16UI,320,192,0,GL_RED_INTEGER,GL_UNSIGNED_SHORT,walls.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        device.state.invalidate();light.tick(buffers,0,1,1,false,terrain);glFinish();std::vector<float> field(320*192*4);glBindTexture(GL_TEXTURE_2D,light.field);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,field.data());
        require(std::all_of(field.begin(),field.end(),[](float v){return std::isfinite(v)&&v>=0;}),"full_pool_single_cell_field_finite");require(field[(100*320+160)*4+2]>0,"full_pool_field_not_empty");
        std::vector<strefa::gpu::Grain> after(grains.size());gl.BindBuffer(GL_COPY_READ_BUFFER,buffers[0]);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,static_cast<GLsizeiptr>(after.size()*sizeof(after[0])),after.data());require(std::memcmp(grains.data(),after.data(),grains.size()*sizeof(grains[0]))==0,"presentation_does_not_write_physical_state");
        std::vector<float> heat(320*192*4);for(std::size_t i=0;i<heat.size();i+=4){heat[i]=1093.15F;heat[i+1]=1;}glBindTexture(GL_TEXTURE_2D,light.thermal_image);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,320,192,GL_RGBA,GL_FLOAT,heat.data());device.state.invalidate();light.tick(buffers,0,1,2,false,terrain,light.thermal_image);glFinish();const auto sources=light.snapshot();
        require(sources.counts[0]==32&&sources.counts[1]>0,"material_source_overflow_is_bounded_and_counted");report<<"stored_sources="<<sources.counts[0]<<"\nrejected_candidates="<<sources.counts[1]<<'\n';
        require(glGetError()==GL_NO_ERROR,"opengl_no_error");gl.DeleteBuffers(8,buffers.data());glDeleteTextures(1,&terrain);report<<"result=PASS\n";return 0;
    }catch(const std::exception& e){std::ofstream{output/"lighting-pressure-error.txt"}<<e.what()<<'\n';std::cerr<<e.what()<<'\n';return 1;}
}
