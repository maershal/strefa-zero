#include "gpu/synthetic_passes.hpp"

#include "gpu/gl_api.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace strefa::gpu {
namespace {
constexpr unsigned group_size = 256, prefix_threads = 32768;
constexpr const char* empty_source = R"(#version 430
layout(local_size_x = 64) in;
void main() {}
)";
// Each thread walks its own position along one random cycle: every read depends on the previous one, so the walk
// cannot be overlapped within a thread; only other threads hide its latency.
constexpr const char* chain_source = R"(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Cycle { uint next_index[]; };
layout(std430, binding = 1) buffer State { uint state[]; };
uniform int chain_length;
uniform int thread_count;
void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= uint(thread_count)) return;
    uint i = state[id];
    for (int k = 0; k < chain_length; ++k) i = next_index[i];
    state[id] = i;
}
)";
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}
std::uint32_t initial_state(std::uint32_t id) {
    return (id * 2654435761U) & (SyntheticPasses::cycle_size - 1);
}
template<class T> double median(std::vector<T> values) {
    std::ranges::sort(values);
    return values[values.size() / 2];
}
} // namespace

struct SyntheticPasses::Impl {
    detail::Api gl;
    PFNGLGETINTEGERI_VPROC get_integer_i{};
    PFNGLGETINTEGER64I_VPROC get_integer64_i{};
    PFNGLBINDBUFFERRANGEPROC bind_buffer_range{};
    GLuint empty{}, chain{}, cycle{}, state{}, indirect{};
    GLint chain_length{-1}, thread_count{-1};
    std::array<GLuint, 4> queries{};
    unsigned prefix{256};
    ~Impl() {
        if (queries[0]) gl.DeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        for (GLuint* buffer : {&cycle, &state, &indirect})
            if (*buffer) gl.DeleteBuffers(1, buffer);
        if (empty) gl.DeleteProgram(empty);
        if (chain) gl.DeleteProgram(chain);
    }
    GLuint program(const char* source) {
        const GLuint shader = gl.CreateShader(GL_COMPUTE_SHADER);
        gl.ShaderSource(shader, 1, &source, nullptr);
        gl.CompileShader(shader);
        GLint ok{};
        gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        std::array<char, 2048> log{};
        if (!ok) {
            gl.GetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            gl.DeleteShader(shader);
            throw std::runtime_error{std::string{"Shader syntetyczny: "} + log.data()};
        }
        const GLuint result = gl.CreateProgram();
        gl.AttachShader(result, shader);
        gl.LinkProgram(result);
        gl.DeleteShader(shader);
        gl.GetProgramiv(result, GL_LINK_STATUS, &ok);
        if (!ok) {
            gl.GetProgramInfoLog(result, static_cast<GLsizei>(log.size()), nullptr, log.data());
            gl.DeleteProgram(result);
            throw std::runtime_error{std::string{"Program syntetyczny: "} + log.data()};
        }
        return result;
    }
    GLuint buffer(GLenum target, GLsizeiptr bytes, const void* data) {
        GLuint id{};
        gl.GenBuffers(1, &id);
        gl.BindBuffer(target, id);
        gl.BufferData(target, bytes, data, GL_STATIC_DRAW);
        return id;
    }
    // Saves what a ParticleWorld may rely on: program, SSBO 0 and 1 (with range), generic SSBO and indirect buffers.
    struct Bindings {
        GLint program{}, generic{}, indirect{};
        std::array<GLint, 2> buffer{};
        std::array<GLint64, 2> start{}, size{};
    };
    Bindings save() const {
        Bindings b;
        glGetIntegerv(GL_CURRENT_PROGRAM, &b.program);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &b.generic);
        glGetIntegerv(GL_DISPATCH_INDIRECT_BUFFER_BINDING, &b.indirect);
        for (GLuint i = 0; i < 2; ++i) {
            get_integer_i(GL_SHADER_STORAGE_BUFFER_BINDING, i, &b.buffer[i]);
            get_integer64_i(GL_SHADER_STORAGE_BUFFER_START, i, &b.start[i]);
            get_integer64_i(GL_SHADER_STORAGE_BUFFER_SIZE, i, &b.size[i]);
        }
        return b;
    }
    void restore(const Bindings& b) {
        for (GLuint i = 0; i < 2; ++i) {
            const auto id = static_cast<GLuint>(b.buffer[i]);
            if (id && b.size[i] > 0)
                bind_buffer_range(GL_SHADER_STORAGE_BUFFER, i, id, static_cast<GLintptr>(b.start[i]),
                                  static_cast<GLsizeiptr>(b.size[i]));
            else
                gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, i, id);
        }
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(b.generic));
        gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, static_cast<GLuint>(b.indirect));
        gl.UseProgram(static_cast<GLuint>(b.program));
    }
    void use_chain(unsigned threads, unsigned length) {
        gl.UseProgram(chain);
        gl.Uniform1i(thread_count, static_cast<GLint>(threads));
        gl.Uniform1i(chain_length, static_cast<GLint>(length));
    }
    void bind_storage() {
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cycle);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, state);
        gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, indirect);
    }
    double span(GLuint begin, GLuint end) {
        GLuint64 a{}, b{};
        gl.GetQueryObjectui64v(begin, GL_QUERY_RESULT, &a);
        gl.GetQueryObjectui64v(end, GL_QUERY_RESULT, &b);
        return static_cast<double>(b - a) / 1e6;
    }
};

SyntheticPasses::SyntheticPasses(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}
SyntheticPasses::~SyntheticPasses() = default;

std::expected<std::unique_ptr<SyntheticPasses>, std::string> SyntheticPasses::create() {
    try {
        auto impl = std::make_unique<Impl>();
        auto& s = *impl;
        s.gl.load();
        s.get_integer_i = reinterpret_cast<PFNGLGETINTEGERI_VPROC>(SDL_GL_GetProcAddress("glGetIntegeri_v"));
        s.get_integer64_i = reinterpret_cast<PFNGLGETINTEGER64I_VPROC>(SDL_GL_GetProcAddress("glGetInteger64i_v"));
        s.bind_buffer_range = reinterpret_cast<PFNGLBINDBUFFERRANGEPROC>(SDL_GL_GetProcAddress("glBindBufferRange"));
        if (!s.get_integer_i || !s.get_integer64_i || !s.bind_buffer_range)
            return std::unexpected{"Brak OpenGL: glGetIntegeri_v/glGetInteger64i_v/glBindBufferRange"};
        const auto saved = s.save();
        s.empty = s.program(empty_source);
        s.chain = s.program(chain_source);
        s.chain_length = s.gl.GetUniformLocation(s.chain, "chain_length");
        s.thread_count = s.gl.GetUniformLocation(s.chain, "thread_count");
        // Sattolo's algorithm: one cycle through every index, so no walk falls into a short loop.
        std::vector<std::uint32_t> cycle(cycle_size);
        std::iota(cycle.begin(), cycle.end(), 0U);
        std::uint64_t random = 0x5DEE'CE66'DULL;
        for (std::uint32_t i = cycle_size - 1; i > 0; --i) {
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;
            std::swap(cycle[i], cycle[static_cast<std::uint32_t>((random >> 33U) % i)]);
        }
        std::vector<std::uint32_t> state(max_threads);
        for (std::uint32_t i = 0; i < max_threads; ++i) state[i] = initial_state(i);
        s.cycle = s.buffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(cycle.size() * 4), cycle.data());
        s.state = s.buffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(state.size() * 4), state.data());
        constexpr std::array<std::uint32_t, 3> zero_groups{0, 1, 1};
        s.indirect = s.buffer(GL_DISPATCH_INDIRECT_BUFFER, sizeof(zero_groups), zero_groups.data());
        s.gl.GenQueries(static_cast<GLsizei>(s.queries.size()), s.queries.data());
        s.restore(saved);
        glFinish();
        if (glGetError() != GL_NO_ERROR) return std::unexpected{"Błąd OpenGL przy tworzeniu przebiegów syntetycznych"};
        return std::unique_ptr<SyntheticPasses>{new SyntheticPasses{std::move(impl)}};
    } catch (const std::exception& error) {
        return std::unexpected{error.what()};
    }
}

SeriesTiming SyntheticPasses::run(const SyntheticSeries& series) {
    auto& s = *impl_;
    if (series.passes == 0 || series.threads == 0 || series.threads > max_threads)
        throw std::invalid_argument{"Nieprawidłowa seria syntetyczna"};
    const auto saved = s.save();
    s.bind_storage();
    glFinish(); // the series starts on an idle GPU and an empty command queue
    SeriesTiming result;
    const auto groups = (series.threads + group_size - 1) / group_size;
    if (series.kind == SyntheticKind::chain)
        s.use_chain(series.threads, series.chain);
    else
        s.gl.UseProgram(s.empty);
    if (series.mode == SubmitMode::primed) {
        s.use_chain(prefix_threads, s.prefix);
        s.gl.QueryCounter(s.queries[2], GL_TIMESTAMP);
        s.gl.DispatchCompute(prefix_threads / group_size, 1, 1);
        s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glFlush();
        if (series.kind == SyntheticKind::chain)
            s.use_chain(series.threads, series.chain);
        else
            s.gl.UseProgram(s.empty);
    }
    const auto started = Clock::now();
    s.gl.QueryCounter(s.queries[0], GL_TIMESTAMP);
    for (unsigned pass = 0; pass < series.passes; ++pass) {
        if (series.kind == SyntheticKind::indirect_zero)
            s.gl.DispatchComputeIndirect(0);
        else
            s.gl.DispatchCompute(series.kind == SyntheticKind::chain ? groups : 1, 1, 1);
        s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        if (series.mode == SubmitMode::finish_each) glFinish();
    }
    s.gl.QueryCounter(s.queries[1], GL_TIMESTAMP);
    const auto submitted = Clock::now();
    glFinish();
    const auto finished = Clock::now();
    result.submit_ms = milliseconds(submitted - started);
    result.wall_ms = milliseconds(finished - started);
    result.gpu_ms = s.span(s.queries[0], s.queries[1]);
    if (series.mode == SubmitMode::primed) result.prefix_ms = s.span(s.queries[2], s.queries[0]);
    s.restore(saved);
    return result;
}

unsigned SyntheticPasses::calibrate_prefix(double target_ms) {
    auto& s = *impl_;
    for (s.prefix = 256; s.prefix < (1U << 20); s.prefix *= 2) {
        const auto t = run({SyntheticKind::chain, prefix_threads, s.prefix, 1, SubmitMode::cold});
        if (t.gpu_ms >= target_ms) break;
    }
    return s.prefix;
}

unsigned SyntheticPasses::prefix_chain() const noexcept {
    return impl_->prefix;
}

bool SyntheticPasses::verify_chain() {
    auto& s = *impl_;
    constexpr unsigned threads = 512, length = 16, passes = 4;
    const auto saved = s.save();
    s.bind_storage();
    std::vector<std::uint32_t> before(threads), after(threads), cycle(cycle_size);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.state);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, threads * 4, before.data());
    s.restore(saved);
    run({SyntheticKind::chain, threads, length, passes, SubmitMode::cold});
    s.bind_storage();
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.state);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, threads * 4, after.data());
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.cycle);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cycle_size * 4, cycle.data());
    s.restore(saved);
    for (unsigned id = 0; id < threads; ++id) {
        auto i = before[id];
        for (unsigned k = 0; k < length * passes; ++k) i = cycle[i];
        if (i != after[id]) return false;
    }
    return true;
}

double cpu_reference_ms() {
    std::vector<double> runs;
    for (int run = 0; run < 3; ++run) {
        const auto started = Clock::now();
        std::uint64_t x = 0x9E37'79B9'7F4A'7C15ULL, sum = 0;
        for (int i = 0; i < (1 << 23); ++i) {
            x ^= x << 13U;
            x ^= x >> 7U;
            x ^= x << 17U;
            sum += x >> 60U;
        }
        volatile std::uint64_t sink = sum;
        static_cast<void>(sink);
        runs.push_back(milliseconds(Clock::now() - started));
    }
    return median(runs);
}

std::expected<CalibrationProbe, std::string> CalibrationProbe::create() {
    auto passes = SyntheticPasses::create();
    if (!passes) return std::unexpected{passes.error()};
    return CalibrationProbe{std::move(*passes)};
}

ProbeSample CalibrationProbe::sample() {
    ProbeSample result;
    result.utc_unix_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
    result.cpu_ms = cpu_reference_ms();
    std::vector<double> gpu, submit, wall;
    for (int run = 0; run < 5; ++run) {
        const auto t = passes_->run({SyntheticKind::chain, 4096, 16, 64, SubmitMode::cold});
        gpu.push_back(t.gpu_ms);
        submit.push_back(t.submit_ms);
        wall.push_back(t.wall_ms);
    }
    result.gpu_ms = median(gpu);
    result.gpu_submit_ms = median(submit);
    result.gpu_wall_ms = median(wall);
    return result;
}
} // namespace strefa::gpu
