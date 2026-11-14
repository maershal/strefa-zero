#include "gpu/shader_library.hpp"

#include <algorithm>
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
            throw std::logic_error{"Zmieniły się deklaracje banków PBF; sprawdź specjalizację restrict dla AoS i SoA."};
    }
    return text;
}
} // namespace
ShaderLibrary::~ShaderLibrary() {
    for (auto& pending : pending_)
        for (auto [kind, shader] : pending.shaders) gl_.DeleteShader(shader);
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
void ShaderLibrary::set_progress(ShaderProgress progress) {
    progress_ = std::move(progress);
    batch_done_ = batch_total_ = 0;
    last_report_ = {};
}
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
namespace {
const char* stage_name(GLenum kind) {
    switch (kind) {
    case GL_COMPUTE_SHADER: return "obliczeniowy";
    case GL_VERTEX_SHADER: return "wierzchołków";
    case GL_FRAGMENT_SHADER: return "fragmentów";
    case GL_GEOMETRY_SHADER: return "geometrii";
    default: return "nieznany";
    }
}
// Driver logs count lines after the defines inserted below line 1 and after #include expansion.
std::string describe(std::string_view headline, const std::string& defines, bool particle_restrict,
                     const std::string& log) {
    std::string text{headline};
    const auto shift = std::count(defines.begin(), defines.end(), '\n');
    if (defines.empty())
        text += "\nDefinicje: brak.";
    else
        text += "\nDefinicje (wstawione po 1. linii, numery linii w logu są przesunięte o " + std::to_string(shift)
              + "):\n" + defines;
    if (particle_restrict) text += "\nSpecjalizacja restrict banków PBF: tak.";
    if (!text.ends_with('\n')) text += '\n';
    return text + "Log sterownika:\n" + log;
}
} // namespace
void ShaderLibrary::probe_parallel() {
    parallel_probed_ = true;
    if (!parallel_allowed_) return;
    const char* name = SDL_GL_ExtensionSupported("GL_KHR_parallel_shader_compile") ? "glMaxShaderCompilerThreadsKHR"
                     : SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile") ? "glMaxShaderCompilerThreadsARB"
                                                                                   : nullptr;
    if (!name) return;
    const auto threads = reinterpret_cast<PFNGLMAXSHADERCOMPILERTHREADSKHRPROC>(SDL_GL_GetProcAddress(name));
    if (!threads) return;
    threads(0xFFFFFFFFU); // the implementation-defined maximum
    parallel_ = stats_.parallel = true;
}
GLuint ShaderLibrary::program(Stages stages, const std::string& defines, bool particle_restrict) {
    const auto id = submit(stages, defines, particle_restrict);
    finish();
    return id;
}
GLuint ShaderLibrary::submit(Stages stages, const std::string& defines, bool particle_restrict) {
    if (!parallel_probed_) probe_parallel();
    auto& gl = gl_;
    Pending pending;
    pending.defines = defines;
    pending.particle_restrict = particle_restrict;
    std::vector<std::pair<GLenum, std::string>> sources;
    std::string key = "strefa-program-v1\n" + driver_;
    for (const auto& [kind, path] : stages) {
        auto text = read_text(path);
        if (particle_restrict) text = restricted_particle_storage(std::move(text));
        if (!defines.empty()) text.insert(text.find('\n') + 1, defines);
        key += '\n' + std::to_string(kind) + ':' + std::to_string(text.size()) + ':' + text;
        sources.emplace_back(kind, std::move(text));
        pending.files.push_back(path);
    }
    pending.key = std::move(key);
    // With parallel compilation glCreateProgram can wait for the driver's busy compiler
    // threads (about 5 ms each on GTX 1650), so it counts as compilation time.
    const auto created = Clock::now();
    pending.program = gl.CreateProgram();
    const double create_ms = milliseconds(created);
    const bool use_cache = stats_.enabled && stats_.supported;
    try {
        if (use_cache && load_binary(pending.program, pending.key)) {
            pending.cached = true;
            stats_.load_ms += create_ms;
        } else {
            stats_.compile_ms += create_ms;
            stats_.submit_ms += create_ms;
            const auto began = Clock::now();
            if (use_cache) program_parameter_(pending.program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
            for (const auto& [kind, text] : sources) {
                const auto shader = gl.CreateShader(kind);
                pending.shaders.emplace_back(kind, shader);
                const auto* pointer = text.c_str();
                gl.ShaderSource(shader, 1, &pointer, nullptr);
                gl.CompileShader(shader);
                gl.AttachShader(pending.program, shader);
            }
            gl.LinkProgram(pending.program);
            stats_.compile_ms += milliseconds(began);
            stats_.submit_ms += milliseconds(began);
        }
        programs_.push_back(pending.program);
        pending_.push_back(std::move(pending));
    } catch (...) {
        for (auto [kind, shader] : pending.shaders) gl.DeleteShader(shader);
        gl.DeleteProgram(pending.program);
        throw;
    }
    ++batch_total_;
    const auto id = pending_.back().program;
    if (!parallel_) {
        finish(); // without the extension: built and checked one by one, as before
        return id;
    }
    // A long batch submission still services the window between programs.
    try {
        report();
    } catch (...) {
        discard_pending();
        throw;
    }
    return id;
}
bool ShaderLibrary::complete(const Pending& pending) const {
    if (pending.cached) return true;
    GLint done{};
    gl_.GetProgramiv(pending.program, GL_COMPLETION_STATUS_KHR, &done);
    return done != 0;
}
void ShaderLibrary::check(Pending& pending) {
    auto& gl = gl_;
    if (!pending.cached) {
        GLint linked{};
        gl.GetProgramiv(pending.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            const auto info = [&](GLuint id, bool shader) {
                GLint length{};
                if (shader)
                    gl.GetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
                else
                    gl.GetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
                if (shader)
                    gl.GetShaderInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
                else
                    gl.GetProgramInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
                log.resize(std::strlen(log.c_str()));
                return log;
            };
            for (std::size_t i = 0; i < pending.shaders.size(); ++i) {
                const auto [kind, shader] = pending.shaders[i];
                GLint compiled{};
                gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
                if (!compiled) {
                    const auto& file = pending.files[i];
                    throw std::runtime_error{describe("Błąd kompilacji shadera „" + file.filename().string()
                                                              + "” (etap " + stage_name(kind)
                                                              + ").\nPlik: " + file.generic_string(),
                                                      pending.defines, pending.particle_restrict, info(shader, true))};
                }
            }
            std::string files;
            for (std::size_t i = 0; i < pending.files.size(); ++i)
                files += (i ? ", " : "") + pending.files[i].filename().string() + " ("
                       + stage_name(pending.shaders[i].first) + ")";
            throw std::runtime_error{describe("Błąd linkowania programu: " + files + ".", pending.defines,
                                              pending.particle_restrict, info(pending.program, false))};
        }
    }
    for (auto [kind, shader] : pending.shaders) gl.DeleteShader(shader); // freed with the program
    pending.shaders.clear();
}
void ShaderLibrary::report() {
    if (!progress_) return;
    const auto began = Clock::now();
    if (last_report_ != Clock::time_point{})
        stats_.progress_gap_ms = std::max(stats_.progress_gap_ms, milliseconds(last_report_));
    progress_(batch_done_, batch_total_);
    last_report_ = Clock::now();
    stats_.progress_ms += std::chrono::duration<double, std::milli>(last_report_ - began).count();
}
void ShaderLibrary::finish() {
    const auto began = Clock::now();
    const double progress_before = stats_.progress_ms;
    bool built{};
    try {
        for (auto& pending : pending_) {
            built = built || !pending.cached;
            // Poll so the callback keeps the window alive; without one the status query blocks.
            while (parallel_ && progress_ && !complete(pending)) {
                report();
                SDL_Delay(1);
            }
            check(pending);
            ++batch_done_;
            report();
        }
    } catch (...) {
        discard_pending();
        throw;
    }
    // Counted and cached only once the whole batch is kept, so a failed batch leaves no binaries behind.
    for (const auto& pending : pending_) {
        if (!pending.cached) {
            ++stats_.source_builds;
            if (stats_.enabled && stats_.supported) save_binary(pending.program, pending.key);
        }
        ++stats_.programs;
    }
    pending_.clear();
    if (built) stats_.compile_ms += milliseconds(began) - (stats_.progress_ms - progress_before);
}
void ShaderLibrary::discard_pending() {
    for (auto& pending : pending_) {
        for (auto [kind, shader] : pending.shaders) gl_.DeleteShader(shader);
        gl_.DeleteProgram(pending.program);
        std::erase(programs_, pending.program);
    }
    pending_.clear();
}
} // namespace strefa::gpu::detail
