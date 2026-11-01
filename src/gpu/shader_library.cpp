#include "gpu/shader_library.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace strefa::gpu::detail {
namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error{"Brak shadera: " + path.string()};
    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    // Fixed, local shader dependencies; recursive expansion supports shared ABI.
    for (const std::string name :
         {"lighting_common.glsl", "lighting_visibility.glsl", "presentation_birth.glsl", "thermal_common.glsl",
          "vfx_common.glsl", "scene_common.glsl", "render_terrain.glsl", "liquid_sampling.glsl", "particle_common.glsl",
          "mission_state.glsl", "particle_storage.glsl", "body_terrain.glsl", "world_protocol.glsl",
          "soldier_pose.glsl"}) {
        const std::string directive = "#include \"" + name + "\"";
        if (const auto at = text.find(directive); at != std::string::npos)
            text.replace(at, directive.size(), read_text(path.parent_path() / name));
    }
    return text;
}
std::string restricted_particle_storage(std::string text) {
    // Specialize only the two canonical bank declarations, keeping one ABI.
    // OFF passes the original source bytes through, including old cache keys.
    for (const auto& [from, to] : {std::pair{"readonly buffer Input", "readonly restrict buffer Input"},
                                   std::pair{"buffer Output", "writeonly restrict buffer Output"}}) {
        std::size_t at = 0;
        unsigned count = 0;
        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, std::strlen(from), to);
            at += std::strlen(to);
            ++count;
        }
        if (count != 2)
            throw std::logic_error{
                    "PBF bank declarations changed; review the restrict specialization for both AoS and SoA."};
    }
    return text;
}
} // namespace
ShaderLibrary::~ShaderLibrary() {
    for (auto id : programs_) gl_.DeleteProgram(id);
}
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point began) {
    return std::chrono::duration<double, std::milli>(Clock::now() - began).count();
}
std::uint64_t digest(std::string_view bytes, std::uint64_t seed = 14695981039346656037ULL) {
    for (unsigned char b : bytes) {
        seed ^= b;
        seed *= 1099511628211ULL;
    }
    return seed;
}
std::string cache_name(const std::string& source, GLenum format) {
    const auto key = source + "\nformat=" + std::to_string(format);
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << digest(key) << std::setw(16)
         << digest(key, 7809847782465536322ULL) << ".glbin";
    return name.str();
}
constexpr std::uint64_t cache_magic = 0x314e4942425a5353ULL; // SZ program binary, schema 1
constexpr std::uint64_t max_binary_bytes = 64 * 1024 * 1024;
} // namespace
void ShaderLibrary::configure_cache(const std::filesystem::path& path) {
    cache_ = path;
    stats_.enabled = !path.empty();
    if (path.empty()) return;
    program_binary_ = reinterpret_cast<PFNGLPROGRAMBINARYPROC>(SDL_GL_GetProcAddress("glProgramBinary"));
    get_binary_ = reinterpret_cast<PFNGLGETPROGRAMBINARYPROC>(SDL_GL_GetProcAddress("glGetProgramBinary"));
    program_parameter_ = reinterpret_cast<PFNGLPROGRAMPARAMETERIPROC>(SDL_GL_GetProcAddress("glProgramParameteri"));
    GLint count{};
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &count);
    if (count <= 0 || count > 256 || !program_binary_ || !get_binary_ || !program_parameter_) return;
    formats_.resize(static_cast<std::size_t>(count));
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, formats_.data());
    for (auto field : {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION}) {
        const auto* value = glGetString(field);
        driver_ += value ? reinterpret_cast<const char*>(value) : "unknown";
        driver_ += '\n';
    }
    GLint profile{};
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    driver_ += std::to_string(profile);
    stats_.supported = true;
}
bool ShaderLibrary::load_binary(GLuint program, const std::string& key) {
    const auto began = Clock::now();
    for (auto format : formats_) {
        std::ifstream in{cache_ / cache_name(key, static_cast<GLenum>(format)), std::ios::binary | std::ios::ate};
        if (!in) continue;
        const auto file_size = in.tellg();
        in.seekg(0);
        std::array<std::uint64_t, 4> header{};
        in.read(reinterpret_cast<char*>(header.data()), sizeof(header));
        if (!in || header[0] != cache_magic || header[1] != static_cast<std::uint64_t>(format) || header[2] == 0
            || header[2] > max_binary_bytes || file_size != static_cast<std::streamoff>(sizeof(header) + header[2])) {
            ++stats_.rejected;
            continue;
        }
        std::string bytes(static_cast<std::size_t>(header[2]), '\0');
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!in || digest(bytes) != header[3]) {
            ++stats_.rejected;
            continue;
        }
        program_binary_(program, static_cast<GLenum>(format), bytes.data(), static_cast<GLsizei>(bytes.size()));
        GLint linked{};
        gl_.GetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked) {
            ++stats_.hits;
            stats_.load_ms += milliseconds(began);
            return true;
        }
        ++stats_.rejected;
        // A driver may reject its own previously valid binary. Consume that
        // operation's error before compiling the canonical source fallback.
        while (glGetError() != GL_NO_ERROR) {
        }
    }
    ++stats_.misses;
    stats_.load_ms += milliseconds(began);
    return false;
}
void ShaderLibrary::save_binary(GLuint program, const std::string& key) {
    GLint size{};
    gl_.GetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &size);
    if (size <= 0 || static_cast<std::uint64_t>(size) > max_binary_bytes) return;
    std::string bytes(static_cast<std::size_t>(size), '\0');
    GLsizei written{};
    GLenum format{};
    get_binary_(program, size, &written, &format, bytes.data());
    if (written <= 0 || written > size) return;
    bytes.resize(static_cast<std::size_t>(written));
    const auto destination = cache_ / cache_name(key, format);
    auto temporary = destination;
    temporary += "." + std::to_string(SDL_GetTicksNS()) + ".tmp";
    std::error_code error;
    std::filesystem::create_directories(cache_, error);
    if (error) {
        ++stats_.write_failures;
        return;
    }
    const std::array<std::uint64_t, 4> header{cache_magic, format, bytes.size(), digest(bytes)};
    std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        ++stats_.write_failures;
        std::filesystem::remove(temporary, error);
        return;
    }
    // Cache contents are disposable. On Windows rename cannot replace a file;
    // removing an older rejected entry still leaves a safe source fallback.
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        ++stats_.write_failures;
        std::filesystem::remove(temporary, error);
    }
}
GLuint ShaderLibrary::program(std::initializer_list<std::pair<GLenum, std::filesystem::path>> stages,
                              const std::string& defines, bool particle_restrict) {
    auto& gl = gl_;
    std::vector<std::pair<GLenum, std::string>> sources;
    std::string key = "strefa-program-v1\n" + driver_;
    for (const auto& [kind, path] : stages) {
        auto text = read_text(path);
        if (particle_restrict) text = restricted_particle_storage(std::move(text));
        if (!defines.empty()) text.insert(text.find('\n') + 1, defines);
        key += '\n' + std::to_string(kind) + ':' + std::to_string(text.size()) + ':' + text;
        sources.emplace_back(kind, std::move(text));
    }
    const auto result = gl.CreateProgram();
    std::vector<GLuint> shaders;
    const bool use_cache = stats_.enabled && stats_.supported;
    try {
        if (use_cache && load_binary(result, key)) {
            programs_.push_back(result);
            ++stats_.programs;
            return result;
        }
        const auto began = Clock::now();
        if (use_cache) program_parameter_(result, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        for (const auto& [kind, text] : sources) {
            const auto shader = gl.CreateShader(kind);
            shaders.push_back(shader);
            const auto* pointer = text.c_str();
            gl.ShaderSource(shader, 1, &pointer, nullptr);
            gl.CompileShader(shader);
            GLint valid{};
            gl.GetShaderiv(shader, GL_COMPILE_STATUS, &valid);
            if (!valid) {
                std::array<char, 8192> log{};
                gl.GetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
                throw std::runtime_error{log.data()};
            }
            gl.AttachShader(result, shader);
        }
        gl.LinkProgram(result);
        GLint valid{};
        gl.GetProgramiv(result, GL_LINK_STATUS, &valid);
        if (!valid) {
            std::array<char, 8192> log{};
            gl.GetProgramInfoLog(result, static_cast<GLsizei>(log.size()), nullptr, log.data());
            throw std::runtime_error{log.data()};
        }
        ++stats_.source_builds;
        stats_.compile_ms += milliseconds(began);
        if (use_cache) save_binary(result, key);
        programs_.push_back(result);
        ++stats_.programs;
    } catch (...) {
        for (auto shader : shaders) gl.DeleteShader(shader);
        gl.DeleteProgram(result);
        throw;
    }
    for (auto shader : shaders) gl.DeleteShader(shader);
    return result;
}
} // namespace strefa::gpu::detail
