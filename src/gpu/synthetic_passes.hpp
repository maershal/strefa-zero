#pragma once
// Synthetic compute passes and calibration probes (Z01). Measurement code without ParticleWorld: it compiles its own
// two programs and buffers in the current OpenGL 4.3 context and restores the bindings it touches, so a benchmark can
// call it between ticks of a world. Never used by the frame loop; the game does not reference it.
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace strefa::gpu {
enum class SyntheticKind {
    empty,         // dispatch of one 64-thread group that does nothing, then a storage barrier
    indirect_zero, // DispatchComputeIndirect of {0, 1, 1} groups, then a storage barrier
    chain          // T threads, each follows L dependent SSBO reads through a random cycle, then a storage barrier
};
enum class SubmitMode {
    cold,       // GPU idle before the series; the GL_TIMESTAMP span includes any gap in delivering the passes
    primed,     // a busy prefix keeps the GPU occupied while the series is submitted: the span is GPU-side cost only
    finish_each // glFinish after every pass: one full CPU-GPU round trip per pass
};
inline constexpr std::string_view synthetic_kind_name(SyntheticKind kind) {
    return kind == SyntheticKind::empty ? "empty" : kind == SyntheticKind::indirect_zero ? "indirect_zero" : "chain";
}
inline constexpr std::string_view submit_mode_name(SubmitMode mode) {
    return mode == SubmitMode::cold ? "cold" : mode == SubmitMode::primed ? "primed" : "finish_each";
}
struct SyntheticSeries {
    SyntheticKind kind{SyntheticKind::empty};
    unsigned threads{256}; // T, chain only (multiple of 256, at most max_threads)
    unsigned chain{16};    // L, dependent reads per thread, chain only
    unsigned passes{1};    // M
    SubmitMode mode{SubmitMode::cold};
};
struct SeriesTiming {
    double gpu_ms{};    // GL_TIMESTAMP pair: before the first pass, after the last pass
    double submit_ms{}; // CPU time to issue the series (finish_each: includes the waits)
    double wall_ms{};   // CPU time from the first call of the series to the return of glFinish
    double prefix_ms{}; // primed: GPU time of the busy prefix; the span is clean when submit_ms < prefix_ms
};
// One calibration reading: a fixed CPU loop and a fixed GPU series (chain, T = 4096, L = 16, M = 64, cold), each the
// median of a few runs. A slow CPU reading means the CPU (clock, scheduler) slowed down, a slow GPU reading the GPU
// (clock, thermal or power limit); both together usually mean the whole machine.
struct ProbeSample {
    std::int64_t utc_unix_ns{}; // system_clock, same epoch as GpuTelemetry.psm1 and the scene benchmark CSV
    double cpu_ms{};
    double gpu_ms{}, gpu_submit_ms{}, gpu_wall_ms{};
};

class SyntheticPasses {
public:
    static constexpr unsigned max_threads = 1U << 23; // state buffer, 32 MiB
    static constexpr unsigned cycle_size = 1U << 20;  // random cycle of 4 MiB, larger than the L2 of a TU117
    static std::expected<std::unique_ptr<SyntheticPasses>, std::string> create(); // needs a current GL 4.3 context
    SyntheticPasses(const SyntheticPasses&) = delete;
    SyntheticPasses& operator=(const SyntheticPasses&) = delete;
    ~SyntheticPasses();
    SeriesTiming run(const SyntheticSeries& series);
    // Doubles the prefix chain until the busy prefix takes at least target_ms on the GPU; returns its length.
    unsigned calibrate_prefix(double target_ms);
    unsigned prefix_chain() const noexcept;
    // Correctness of the chain kernel: GPU result of a small series against a CPU walk of the same cycle.
    bool verify_chain();

private:
    struct Impl;
    explicit SyntheticPasses(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

// Fixed CPU work (xorshift loop); the median of three runs in milliseconds.
double cpu_reference_ms();

// Part 3 of Z01: call sample() at the start and at the end of every measured segment. Reusable by the thermal and
// scene benchmarks (needs only a current GL 4.3 context; restores program and buffer bindings).
class CalibrationProbe {
public:
    static std::expected<CalibrationProbe, std::string> create();
    ProbeSample sample();
    SyntheticPasses& passes() noexcept { return *passes_; }

private:
    explicit CalibrationProbe(std::unique_ptr<SyntheticPasses> passes) : passes_{std::move(passes)} {}
    std::unique_ptr<SyntheticPasses> passes_;
};
} // namespace strefa::gpu
