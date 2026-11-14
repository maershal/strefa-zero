#pragma once
#include "gpu/gl_api.hpp"
#include "gpu/shader_cache.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace strefa::gpu::detail {
// Called while programs build: finished and requested programs of the current batch. It may render a frame
// (the context is current and no program is bound by the library) and may throw to cancel the batch.
using ShaderProgress = std::function<void(unsigned done, unsigned total)>;
// Owns all linked programs for one device/configuration. Scene changes only
// replace data; they never destroy or compile programs.
class ShaderLibrary {
public:
    using Stages = std::initializer_list<std::pair<GLenum, std::filesystem::path>>;
    explicit ShaderLibrary(Api& gl) : gl_{gl} {}
    ~ShaderLibrary();
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;
    // Builds one program and checks it (submit + finish).
    GLuint program(Stages stages, const std::string& defines = {}, bool particle_restrict = false);
    // Queues compilation and linking without asking for any status, so a driver with
    // GL_KHR_parallel_shader_compile builds the batch on its own threads. Without the
    // extension the program is built and checked here, as program() always did.
    // Uniform locations of a submitted program are valid only after finish().
    GLuint submit(Stages stages, const std::string& defines = {}, bool particle_restrict = false);
    // Checks every submitted program in submission order; on failure deletes the whole
    // batch and throws a message naming the file, the stage and the defines.
    void finish();
    void configure_cache(const std::filesystem::path&);
    // Restarts the done/total counters reported to the callback.
    void set_progress(ShaderProgress progress);
    // Validation only: forces the sequential path on a driver with the extension.
    void allow_parallel(bool allowed) { parallel_allowed_ = allowed; }
    bool parallel() const noexcept { return parallel_; }
    ShaderCacheStats stats() const noexcept { return stats_; }
    std::size_t size() const noexcept { return programs_.size(); }

private:
    struct Pending {
        GLuint program{};
        std::vector<std::pair<GLenum, GLuint>> shaders;
        std::vector<std::filesystem::path> files;
        std::string defines, key;
        bool particle_restrict{}, cached{}, checked{};
    };
    Api& gl_;
    std::vector<GLuint> programs_;
    std::vector<Pending> pending_;
    unsigned batch_done_{}, batch_total_{};
    std::filesystem::path cache_;
    std::string driver_;
    std::vector<GLint> formats_;
    PFNGLPROGRAMBINARYPROC program_binary_{};
    PFNGLGETPROGRAMBINARYPROC get_binary_{};
    PFNGLPROGRAMPARAMETERIPROC program_parameter_{};
    ShaderCacheStats stats_;
    ShaderProgress progress_;
    std::chrono::steady_clock::time_point last_report_{};
    bool parallel_probed_{}, parallel_{}, parallel_allowed_{true};
    bool load_binary(GLuint, const std::string& key);
    void save_binary(GLuint, const std::string& key);
    void probe_parallel();
    bool complete(const Pending&) const;
    void check(Pending&);
    void discard_pending();
    void report();
};
} // namespace strefa::gpu::detail
