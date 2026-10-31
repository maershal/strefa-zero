#include "app/diagnostic_scenarios.hpp"
#include "gpu/shader_library.hpp"
#include <fstream>
#include <chrono>

namespace strefa::app {
std::expected<int,std::string> validate_shader_cache(const std::filesystem::path& output){
    try{
        const auto root=output/"cache-fixture";
        std::filesystem::remove_all(root);std::filesystem::create_directories(root/"sources");
        const auto shader=root/"sources"/"test.comp";
        std::ofstream{shader}<<"#version 430\n#include \"particle_common.glsl\"\nlayout(local_size_x=1) in;\nlayout(std430,binding=0) buffer Result {uint value;};\nvoid main(){value=VALUE+EXTRA;}\n";
        auto include=[&](unsigned value){std::ofstream{root/"sources"/"particle_common.glsl"}<<"#define VALUE "<<value<<"u\n";};
        include(7);
        gpu::detail::Api api;api.load();GLuint buffer{};api.GenBuffers(1,&buffer);api.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
        api.BufferData(GL_SHADER_STORAGE_BUFFER,sizeof(unsigned),nullptr,GL_DYNAMIC_COPY);
        struct Cleanup{gpu::detail::Api& gl;GLuint id;~Cleanup(){gl.DeleteBuffers(1,&id);}} cleanup{api,buffer};
        unsigned cases{};
        auto run=[&](const std::string& defines,unsigned expected,bool hit,bool reject){
            gpu::detail::ShaderLibrary library{api};library.configure_cache(root/"binaries");
            const auto program=library.program({{GL_COMPUTE_SHADER,shader}},defines);
            api.UseProgram(program);api.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);api.DispatchCompute(1,1,1);
            api.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);unsigned value{};
            api.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);api.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(value),&value);
            const auto stats=library.stats();
            if(value!=expected||stats.hits!=(hit?1U:0U)||stats.rejected!=(reject?1U:0U)||glGetError()!=GL_NO_ERROR)
                throw std::runtime_error{"Cache shaderów: niepoprawny wynik, licznik lub błąd GL."};
            ++cases;return stats.supported;
        };
        const std::string defines="#define EXTRA 0u\n";
        if(!run(defines,7,false,false)){std::ofstream{output/"shader-cache-check.txt"}<<"supported=0\nsource_fallback=1\n";return 0;}
        run(defines,7,true,false);
        for(const auto& entry:std::filesystem::directory_iterator(root/"binaries"))std::ofstream{entry.path(),std::ios::binary|std::ios::trunc}<<"corrupt";
        run(defines,7,false,true);run(defines,7,true,false);
        include(11);run(defines,11,false,false);run(defines,11,true,false);
        run("#define EXTRA 2u\n",13,false,false);run("#define EXTRA 2u\n",13,true,false);
        api.UseProgram(0);
        std::ofstream report{output/"shader-cache-check.txt"};report<<"supported=1\ncases="<<cases<<"\npassed=1\n";
        return 0;
    }catch(const std::exception& error){return std::unexpected{error.what()};}
}
}
