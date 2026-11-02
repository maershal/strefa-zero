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
// Index of name in ids, for the --scene-benchmark and --gpu-thermal-scene values.
[[nodiscard]] std::expected<unsigned, std::string> find_scene_id(std::span<const char* const> ids,
                                                                 std::string_view name, std::string_view what);

// Nearest-rank percentiles with the semantics of gpu/profiling.cpp: sorted[min(n-1, q*n)].
struct Distribution {
    std::size_t samples{};
    double mean{}, median{}, p95{}, p99{}, max{};
};
[[nodiscard]] Distribution distribution(std::vector<double> values);

// SDL window state sampled once per frame (SDL_GetWindowFlags).
struct WindowState {
    bool visible{}, focused{}, minimized{}, occluded{};
    // A frame that was not on screen measures a different presentation path.
    [[nodiscard]] bool presented() const noexcept { return visible && !minimized && !occluded; }
};

// One presented frame. GPU samples are the timer queries that completed during this frame
// (take_*_timings drained every frame), so they are attributed to the frame's time.
struct BenchmarkFrame {
    std::int64_t utc_unix_ns{}; // system_clock after the frame's update; same epoch as GpuTelemetry.psm1
    std::uint64_t sdl_ns{};     // SDL_GetTicksNS at the start of the frame
    double elapsed_seconds{};   // since the start of the measurement window
    double frame_ms{};
    unsigned ticks{};         // simulation ticks advanced by this frame
    double dropped_seconds{}; // simulation time discarded by the clock during this frame
    double pending_seconds{}; // clock backlog after the frame
    unsigned physics_samples{}, presentation_samples{}, render_samples{};
    double physics_gpu_ms{}, presentation_gpu_ms{}, render_gpu_ms{}; // sums of this frame's samples
    double cpu_step_ms{};                                            // CPU laboratory: last tick on the worker thread
    WindowState window;
    bool route_reset{}; // this frame contains a mission route restart (excluded from frame-time statistics)
};

// Everything one scene produced: per-frame rows and every GPU sample of the measurement window.
struct BenchmarkRecord {
    std::vector<BenchmarkFrame> frames;
    std::vector<double> physics_gpu_ms, presentation_gpu_ms, render_gpu_ms;
    bool physics_truncated{}, presentation_truncated{}, render_truncated{}; // samples lost to a full queue
    unsigned route_resets{};
    std::string error; // empty unless the scene failed
};

enum class BenchmarkStatus { ok, invalid, failed };
[[nodiscard]] std::string_view status_name(BenchmarkStatus);

// Frame time of the frames that advanced 0, 1 or 2+ simulation ticks.
struct TickGroup {
    std::size_t frames{};
    double frame_median_ms{};
};
// Measurement split at early_phase_seconds (transient start vs. the rest of the window).
struct BenchmarkPhase {
    std::size_t frames{};
    double frame_median_ms{}, ticks_per_second{}, gpu_tick_mean_ms{};
};
inline constexpr double early_phase_seconds = 10;

struct BenchmarkSummary {
    std::string scene;
    unsigned pass{1}; // --repeat / --loop-minutes: 1-based pass over the scene list
    BenchmarkStatus status{BenchmarkStatus::ok};
    std::string status_reason;
    std::size_t frames{};
    double window_seconds{}; // measured wall time
    Distribution frame_ms, physics_gpu_ms, presentation_gpu_ms, render_gpu_ms, cpu_step_ms, pending_seconds;
    bool physics_truncated{}, presentation_truncated{}, render_truncated{};
    std::array<TickGroup, 3> by_ticks; // 0, 1, 2+ ticks per frame
    double ticks_mean{}, dropped_seconds{}, ticks_per_second{}, frames_per_second{};
    unsigned ticks_max{};
    // GPU milliseconds per second of simulated time ((physics + presentation) mean x tick rate).
    double gpu_ms_per_sim_second{};
    // GPU milliseconds per wall second as measured: (physics + presentation) x ticks/s + render x fps.
    double gpu_ms_per_wall_second{};
    // Share of a 60 FPS real-time budget: ((physics + presentation) x tick rate + render x 60) / 1000 ms.
    double budget_60fps{};
    BenchmarkPhase early, late;
    std::size_t hidden_frames{}, minimized_frames{}, occluded_frames{}, unfocused_frames{};
    unsigned route_resets{};
    std::int64_t start_utc_unix_ns{}, end_utc_unix_ns{};
};
// tick_rate = simulation ticks per simulated second (GPU world: 120); 0 for scenes without GPU physics.
[[nodiscard]] BenchmarkSummary summarize(std::string scene, BenchmarkRecord record, double tick_rate);

inline constexpr std::string_view benchmark_frames_header =
        "scene,pass,utc_unix_ns,sdl_ns,elapsed_seconds,frame_ms,ticks,dropped_seconds,pending_seconds,physics_samples,"
        "physics_gpu_ms_sum,presentation_samples,presentation_gpu_ms_sum,render_samples,render_gpu_ms_sum,"
        "cpu_step_ms,visible,focused,minimized,occluded,route_reset";
inline constexpr std::string_view benchmark_summary_header =
        "scene,pass,status,status_reason,frames,window_seconds,frame_median_ms,frame_p95_ms,frame_p99_ms,frame_max_ms,"
        "frames_0_ticks,frame_median_0_ticks_ms,frames_1_tick,frame_median_1_tick_ms,frames_2plus_ticks,"
        "frame_median_2plus_ticks_ms,physics_samples,physics_truncated,physics_mean_ms,physics_median_ms,"
        "physics_p95_ms,physics_p99_ms,presentation_samples,presentation_truncated,presentation_mean_ms,"
        "presentation_median_ms,presentation_p95_ms,presentation_p99_ms,render_samples,render_truncated,"
        "render_mean_ms,render_median_ms,render_p95_ms,render_p99_ms,gpu_ms_per_sim_second,"
        "gpu_ms_per_wall_second,budget_60fps,cpu_step_median_ms,cpu_step_p95_ms,ticks_per_frame_mean,"
        "ticks_per_frame_max,ticks_per_second,frames_per_second,dropped_seconds,backlog_p95_seconds,"
        "backlog_max_seconds,early_frames,early_frame_median_ms,early_ticks_per_second,early_gpu_tick_mean_ms,"
        "late_frames,late_frame_median_ms,late_ticks_per_second,late_gpu_tick_mean_ms,hidden_frames,"
        "minimized_frames,occluded_frames,unfocused_frames,route_resets,start_utc_unix_ns,end_utc_unix_ns";
[[nodiscard]] std::string benchmark_frame_row(std::string_view scene, unsigned pass, const BenchmarkFrame&);
[[nodiscard]] std::string benchmark_summary_row(const BenchmarkSummary&);
// One human-readable line per scene for stdout and the PR table.
[[nodiscard]] std::string benchmark_summary_line(const BenchmarkSummary&);

} // namespace strefa::app
