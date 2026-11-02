#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::app {

// Thermal scene ids of --gpu-thermal-scene and --scene-benchmark; scene number = index + 1.
inline constexpr std::array thermal_scene_ids{"fire", "oil", "quench", "phases"};

// One scene measured by --scene-benchmark (StrefaQA).
struct BenchmarkScene {
    enum class Kind { gpu, mission, thermal, lab };
    Kind kind{Kind::gpu};
    unsigned index{}; // gpu::Scene, thermal scene number (1-4) or sim::Scene; 0 for the mission
    // gpu:<id>, mission, thermal:<id> or lab:<id>
    [[nodiscard]] std::string id() const;
    bool operator==(const BenchmarkScene&) const = default;
};
// GPU laboratory scenes, the mission, thermal scenes and CPU laboratory scenes, in that order.
[[nodiscard]] std::vector<BenchmarkScene> all_benchmark_scenes();
// "all" or a comma-separated list of ids; duplicates are rejected.
[[nodiscard]] std::expected<std::vector<BenchmarkScene>, std::string> parse_benchmark_scenes(std::string_view list);
// Index of name in ids, for the --gpu-scene / --lab-scene / --gpu-thermal-scene values.
[[nodiscard]] std::expected<unsigned, std::string> find_scene_id(std::span<const char* const> ids,
                                                                 std::string_view name, std::string_view what);

// Nearest-rank percentiles with the semantics of gpu/profiling.cpp: sorted[min(n-1, q*n)].
struct Distribution {
    std::size_t samples{};
    double median{}, p95{}, p99{}, max{};
};
[[nodiscard]] Distribution distribution(std::vector<double> values);

// One presented frame inside the measurement window.
struct BenchmarkFrame {
    std::int64_t utc_unix_ns{}; // system_clock after the frame's update; same epoch as GpuTelemetry.psm1
    std::uint64_t sdl_ns{};     // SDL_GetTicksNS at the start of the frame
    double frame_ms{};
    unsigned ticks{};         // simulation ticks advanced by this frame
    double dropped_seconds{}; // simulation time discarded by the clock during this frame
    double pending_seconds{}; // clock backlog after the frame
    double physics_gpu_ms{};  // latest available GPU result (GPU scenes), not necessarily this frame
    double presentation_gpu_ms{};
    double render_gpu_ms{};
    double cpu_step_ms{}; // CPU laboratory: last tick on the worker thread
};
struct BenchmarkSummary {
    std::string scene;
    std::size_t frames{};
    Distribution frame_ms, physics_gpu_ms, presentation_gpu_ms, render_gpu_ms, cpu_step_ms, pending_seconds;
    double ticks_mean{}, dropped_seconds{};
    unsigned ticks_max{};
    std::int64_t start_utc_unix_ns{}, end_utc_unix_ns{};
};
// GPU distributions come from every completed timer query of the window (take_timings and friends),
// not from the per-frame copies; empty vectors leave them at zero samples.
[[nodiscard]] BenchmarkSummary summarize(std::string scene, std::span<const BenchmarkFrame> frames,
                                         std::vector<double> physics_gpu_ms, std::vector<double> presentation_gpu_ms,
                                         std::vector<double> render_gpu_ms);

inline constexpr std::string_view benchmark_frames_header =
        "scene,utc_unix_ns,sdl_ns,frame_ms,ticks,dropped_seconds,pending_seconds,physics_gpu_ms,presentation_gpu_ms,"
        "render_gpu_ms,cpu_step_ms";
inline constexpr std::string_view benchmark_summary_header =
        "scene,frames,frame_median_ms,frame_p95_ms,frame_p99_ms,frame_max_ms,gpu_samples,physics_median_ms,"
        "physics_p95_ms,physics_p99_ms,presentation_median_ms,presentation_p95_ms,presentation_p99_ms,"
        "render_median_ms,render_p95_ms,render_p99_ms,cpu_step_median_ms,cpu_step_p95_ms,ticks_per_frame_mean,"
        "ticks_per_frame_max,dropped_seconds,backlog_p95_seconds,backlog_max_seconds,start_utc_unix_ns,"
        "end_utc_unix_ns";
[[nodiscard]] std::string benchmark_frame_row(std::string_view scene, const BenchmarkFrame&);
[[nodiscard]] std::string benchmark_summary_row(const BenchmarkSummary&);
// One human-readable line per scene for stdout and the PR table.
[[nodiscard]] std::string benchmark_summary_line(const BenchmarkSummary&);

} // namespace strefa::app
