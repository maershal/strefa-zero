#pragma once
// Window tests of StrefaZero (--smoke-test, --mission-smoke-test) and the progress.txt heartbeat of every window
// test. The StrefaQA-only scripts (CPU and GPU laboratory, session, mission_quick, mode faults) are FrameObservers in
// app/qa_window_scripts.cpp and are not linked into StrefaZero.
#include "app/application.hpp"
#include "app/scenario_context.hpp"
#include "gpu/world.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::app {
class FrameObservers;

class WindowTest {
public:
    static constexpr std::uint64_t mission_smoke_min_ticks = 1700;
    // Internal limit, measured from SDL start, that ends gpu.mission_ui with a progress report well before its
    // CTest TIMEOUT (120 s) kills it. Grows with STREFA_GPU_TIMEOUT_SCALE (see mission_deadline_ns).
    static constexpr std::uint64_t mission_smoke_deadline_ns = 55'000'000'000;
    // Hardware GPU: wall time the interactive clock may discard after the first second.
    static constexpr double mission_smoke_max_dropped = 0.5;
    static constexpr unsigned menu_smoke_frames = 306;

    WindowTest(SmokeScenario scenario, bool profile);
    // Scripted input of the menu smoke test and frame times of the mission smoke test.
    void before_frame(ScenarioContext&, float frame_ms);
    // Mission smoke test, after the frame's update: clock accounting of the current mission epoch.
    void mission_clock_frame(const ScenarioContext&, double sim_seconds, std::uint64_t frame_start,
                             unsigned mission_ticks_this_frame);
    // Mission smoke test past mission_deadline_ns: writes the progress files and returns the error text.
    [[nodiscard]] std::string mission_deadline(const ScenarioContext&, const FrameObservers&) const;
    // Rewrites progress.txt about once per second, so a CTest TIMEOUT kill leaves the last state.
    void heartbeat(const ScenarioContext&, std::uint64_t now_ns, const FrameObservers&);
    void write_heartbeat(const ScenarioContext&, std::string_view state, const FrameObservers&) const;
    // After the loop: reports and verdict of the menu or mission smoke test.
    [[nodiscard]] std::expected<void, std::string> finish(const ScenarioContext&);

    unsigned frames{};
    std::vector<std::array<double, 12>> mission_frames; // mission-frames.csv rows
    std::uint64_t mission_deadline_ns{};

private:
    bool smoke_step(ScenarioContext&);
    bool open_gpu_screen(ScenarioContext&, ui::Action);
    [[nodiscard]] double mission_accounting_error(const ScenarioContext&) const;
    void write_mission_progress(const ScenarioContext&, std::string_view state) const;
    [[nodiscard]] std::expected<void, std::string> finish_menu(const ScenarioContext&) const;
    [[nodiscard]] std::expected<void, std::string> finish_mission(const ScenarioContext&);

    SmokeScenario scenario_;
    bool smoke_test_{}, mission_smoke_{};
    bool gpu_unavailable_handled_{}, experimental_ok_{}, preview_keeps_draft_{}, settings_ok_{true};
    bool lab_ok_{true};
    std::uint64_t paused_tick_{};
    std::array<bool, static_cast<std::size_t>(ui::Screen::gpu_mission) + 1> visited_screens_{};
    std::array<bool, 3> visited_themes_{};
    std::optional<unsigned> mission_epoch_;
    double mission_fed_seconds_{}, mission_dropped_at_start_{};
    std::uint64_t mission_steps_at_epoch_{}, mission_started_ns_{};
    bool mission_start_recorded_{};
    std::vector<double> ui_frame_times_;
    std::uint64_t last_heartbeat_ns_{};
};

// UI frame p95 is judged only on hardware renderers. A software renderer (llvmpipe in the nightly run) misses it by
// design; its p95 is still measured and reported, and every functional check stays.
inline constexpr double ui_p95_limit_ms = 100;
[[nodiscard]] bool software_renderer(std::string_view renderer);
[[nodiscard]] bool p95_ok(std::string_view renderer, double p95_ms);
[[nodiscard]] const char* p95_limit_name(std::string_view renderer);
// Sorted frame times of a window test (from frame 30 on); an error when there are none, e.g. a run of fewer than 30
// frames.
[[nodiscard]] std::expected<std::vector<double>, std::string> sorted_frame_times(std::vector<double> times);
// q-quantile of sorted, non-empty frame times.
[[nodiscard]] double percentile(std::span<const double> sorted, double q);

// The lines of gpu-ui.txt and mission-ui.txt shared by the GPU laboratory (StrefaQA) and mission window tests.
struct GpuWindowRun {
    std::string_view scene; // sand_water or mission
    unsigned frames{};
    std::span<const double> sorted_frame_times;
    bool pause_ok{};
    double dropped_seconds{};
};
void write_gpu_window_report(std::ostream&, const ScenarioContext&, const GpuWindowRun&);

} // namespace strefa::app
