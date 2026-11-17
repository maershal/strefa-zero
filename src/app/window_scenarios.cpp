#include "app/window_scenarios.hpp"

#include "app/frame_observer.hpp"
#include "app/launch_config.hpp"
#include "app/scenario_runner.hpp"
#include "app/simulation_world.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <utility>
#include <vector>

namespace strefa::app {
namespace {

// frame-timings.csv: one row per loop iteration (FrameProfile), plus the clock anchors and the effective GPU options.
class FrameProfileRecorder final : public FrameObserver {
public:
    FrameProfileRecorder(const LaunchConfig& config, std::filesystem::path profile)
        : profiling_{config.profiling},
          execution_{config.execution},
          mission_smoke_{config.smoke == SmokeScenario::mission},
          profile_{std::move(profile)} {
        rows_.reserve(1024);
        anchor_ns_ = SDL_GetTicksNS();
        anchor_utc_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        anchor_after_ns_ = SDL_GetTicksNS();
    }
    void frame_end(const FrameProfile& row) override {
        if (rows_.size() < capacity) {
            rows_.push_back(row);
            rows_.back().finish(SDL_GetTicksNS());
        } else
            ++dropped_;
    }
    std::expected<void, std::string> finish(const FrameView& view) override {
        const auto& simulation = view.simulation;
        std::ofstream timing_out{profile_ / "frame-timings.csv"};
        std::ofstream info_out{profile_ / "frame-timings-info.txt"};
        if (!timing_out || !info_out) return std::unexpected{"Nie można zapisać pełnego profilu klatek."};
        timing_out << std::setprecision(12);
        write_frame_profile_header(timing_out);
        for (const auto& row : rows_) write_frame_profile(timing_out, row);
        info_out << "render_width=960\nrender_height=576\nrender_quality="
                 << gpu::render_quality_name(simulation ? simulation->render_options().quality
                                                        : gpu::RenderQuality::basic)
                 << '\n';
        info_out
                << "schema=1\nrows_capacity=" << capacity << "\nrows_dropped=" << dropped_
                << "\nanchor_sdl_ns=" << anchor_ns_ << "\nanchor_utc_unix_ns=" << anchor_utc_ns_
                << "\nanchor_sdl_after_ns=" << anchor_after_ns_
                << "\nanchor_pair_order=sdl_before_utc_then_sdl_after\nanchor_precision=bracketed_cpu_samples\n"
                << "mission_smoke=" << mission_smoke_
                << "\nmission_smoke_min_ticks=" << ScenarioRunner::mission_smoke_min_ticks
                << "\nrendered_frames=" << view.frames << "\nloop_iterations=" << view.iterations
                << "\nmission_submitted_ticks=" << (simulation ? simulation->metrics().steps : 0) << '\n'
                << "durations=current_loop_iteration_after_present_and_limiter\n"
                << "previous_interval_ms=preceding_frame_start_to_current_frame_start\n"
                << "phase_partition=input+metadata+scenario+ui_prepare+upload+poll+ui_build+update+world_render+gui_render+capture+validation+present+actions+sleep+other\n"
                << "rendered_0=minimized_loop_no_swap_or_simulation\n"
                << "input_ms=SDL_event_poll_and_gui_event_dispatch\n"
                << "ui_build_ms=UI_widgets_and_game_input_collection\n"
                << "world_render_ms=CPU_submission_in_update_not_GPU_execution\n"
                << "capture_ms=synchronous_test_capture_excluded_from_runtime_comparisons\n"
                << "capture_performed=explicit_capture_marker_not_a_duration_threshold\n"
                << "validation_ms=per_frame_glGetError_not_full_snapshot_validation\n"
                << "full_snapshot_validation=after_loop_not_in_frame_timings_csv\n"
                << "presentation_snapshot=after_event_poll_before_UI_actions\n"
                << "frame_limit_snapshot=before_UI_actions_may_change_limiter_for_same_iteration\n"
                << "mission_tick=last_submitted_tick_not_GPU_completion\n"
                << "mission_pending_seconds=clock_accumulator_including_fractional_tick\n"
                << "timer_mode=" << gpu::timer_mode_name(gpu::effective_timer_mode(profiling_))
                << "\ngpu_timers_enabled=" << profiling_.gpu_timers << "\nphase_gating=" << execution_.phase_gating
                << "\nphase_gating_effective="
                << (execution_.phase_gating && execution_.neighbors != gpu::NeighborMode::linked)
                << "\nneighbor_reads_requested=" << gpu::neighbor_reads_name(execution_.neighbor_reads)
                << "\nneighbor_reads_effective="
                << gpu::neighbor_reads_name(
                           gpu::effective_neighbor_reads(execution_.neighbor_reads, execution_.neighbors))
                << "\nstorage_requested=" << gpu::device_storage_name(execution_.storage)
                << "\nreadback_period=" << profiling_.readback_period
                << "\nskip_zero_projection=" << execution_.skip_zero_projection << '\n';
        if (!timing_out || !info_out) return std::unexpected{"Niepełny zapis profilu klatek."};
        return {};
    }

private:
    static constexpr std::size_t capacity = 250000;
    gpu::ProfileOptions profiling_;
    gpu::ExecutionOptions execution_;
    bool mission_smoke_{};
    std::filesystem::path profile_;
    std::vector<FrameProfile> rows_;
    std::uint64_t dropped_{}, anchor_ns_{}, anchor_after_ns_{};
    std::int64_t anchor_utc_ns_{};
};

} // namespace

ApplicationRunner with_frame_profile(ApplicationRunner run, std::filesystem::path profile) {
    return [run = std::move(run), profile = std::move(profile)](const LaunchConfig& config) {
        if (!config.profiling.collect) return run(config);
        FrameProfileRecorder recorder{config, profile};
        auto profiled = config;
        profiled.observers.push_back(&recorder);
        return run(profiled);
    };
}

std::optional<std::expected<int, std::string>>
run_window_scenario(const LaunchOptions& options, const LaunchConfig& base, const platform::Window& window,
                    const std::filesystem::path& profile, const ApplicationRunner& run_application) {
    const auto run = with_frame_profile(run_application, profile);
    if (!options.benchmark_scenes.empty()) return run_scene_benchmark(options, base, window, profile, run);
    if (base.profiling.collect) return run(base);
    return std::nullopt;
}

} // namespace strefa::app
