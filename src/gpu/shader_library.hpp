#pragma once
#include "gpu/gl_api.hpp"
#include "gpu/shader_cache.hpp"
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace strefa::gpu::detail {
// Owns all linked programs for one device/configuration. Scene changes only
// replace data; they never destroy or compile programs.
class ShaderLibrary {
public:
    explicit ShaderLibrary(Api& gl):gl_{gl} {}
    ~ShaderLibrary();
    ShaderLibrary(const ShaderLibrary&)=delete;
    ShaderLibrary& operator=(const ShaderLibrary&)=delete;
    GLuint program(std::initializer_list<std::pair<GLenum,std::filesystem::path>> stages,
        const std::string& defines={},bool particle_restrict=false);
    void configure_cache(const std::filesystem::path&);
    ShaderCacheStats stats() const noexcept {return stats_;}
    std::size_t size() const noexcept { return programs_.size(); }
private:
    Api& gl_;
    std::vector<GLuint> programs_;
    std::filesystem::path cache_;
    std::string driver_;
    std::vector<GLint> formats_;
    PFNGLPROGRAMBINARYPROC program_binary_{};
    PFNGLGETPROGRAMBINARYPROC get_binary_{};
    PFNGLPROGRAMPARAMETERIPROC program_parameter_{};
    ShaderCacheStats stats_;
    bool load_binary(GLuint,const std::string& key);
    void save_binary(GLuint,const std::string& key);
};
}
