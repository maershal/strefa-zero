#include "app/window_test.hpp"

#include "app/frame_observer.hpp"
#include "app/simulation_world.hpp"
#include "build_info.hpp"
#include "sim/fixed_clock.hpp"
#include "sim/laboratory.hpp"
#include "sim/world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>

namespace strefa::app {

bool software_renderer(std::string_view renderer) {
    std::string lower{renderer};
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("llvmpipe") != std::string::npos || lower.find("softpipe") != std::string::npos
        || lower.find("swiftshader") != std::string::npos || lower.find("software") != std::string::npos;
}
bool p95_ok(std::string_view renderer, double p95_ms) {
    return software_renderer(renderer) || p95_ms <= ui_p95_limit_ms;
}
const char* p95_limit_name(std::string_view renderer) {
    return software_renderer(renderer) ? "none_software_renderer" : "100";
}
std::expected<std::vector<double>, std::string> sorted_frame_times(std::vector<double> times) {
    if (times.empty())
        return std::unexpected{"Za mało klatek do oceny czasu klatki (pomiar zaczyna się od klatki 30)."};
    std::ranges::sort(times);
    return times;
}
double percentile(std::span<const double> sorted, double q) {
    return sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(static_cast<double>(sorted.size()) * q))];
}

void write_gpu_window_report(std::ostream& report, const ScenarioContext& context, const GpuWindowRun& run) {
    const auto& measured = *context.simulation;
    const auto& execution = context.execution;
    const auto& profiling = context.profiling;
    const auto times = run.sorted_frame_times;
    report << "scene=" << run.scene << "\nframes=" << run.frames << "\nmedian_ms=" << percentile(times, .5)
           << "\np95_ms=" << percentile(times, .95) << "\np99_ms=" << percentile(times, .99)
           << "\nmax_ms=" << times.back() << "\nsteps=" << measured.metrics().steps
           << "\ngpu_step_ms=" << measured.metrics().physics_ms
           << "\ngpu_presentation_ms=" << measured.metrics().presentation_ms
           << "\ngpu_tick_ms=" << measured.metrics().gpu_ms << "\ngpu_render_ms=" << measured.metrics().render_ms
           << "\ncpu_submit_ms=" << measured.metrics().cpu_submit_ms << "\npaused_ok=" << run.pause_ok
           << "\ndropped_seconds=" << run.dropped_seconds << "\nrenderer=" << context.renderer
           << "\np95_limit=" << p95_limit_name(context.renderer) << '\n';
    report << "phase_gating=" << execution.phase_gating << "\nphase_gating_effective="
           << (execution.phase_gating && execution.neighbors != gpu::NeighborMode::linked)
           << "\nneighbor_reads_requested=" << gpu::neighbor_reads_name(execution.neighbor_reads)
           << "\nneighbor_reads_effective="
           << gpu::neighbor_reads_name(gpu::effective_neighbor_reads(execution.neighbor_reads, execution.neighbors))
           << "\nstorage_requested=" << gpu::device_storage_name(execution.storage)
           << "\nreadback_period=" << profiling.readback_period
           << "\nskip_zero_projection=" << execution.skip_zero_projection << '\n';
    report << "storage_effective=" << gpu::device_storage_name(measured.upload_stats().mode) << '\n';
    report << "conditional_work_requested=" << execution.conditional_work
           << "\nconditional_work_effective=" << measured.execution_options().conditional_work << '\n';
    report << "gpu_timer_mode=" << gpu::timer_mode_name(gpu::effective_timer_mode(profiling))
           << "\ngpu_timing_available="
           << (gpu::effective_timer_mode(profiling) != gpu::TimerMode::off && measured.metrics().timing_samples > 0)
           << "\ngpu_timing_samples=" << measured.metrics().timing_samples << '\n';
}

WindowTest::WindowTest(SmokeScenario scenario, bool profile)
    : mission_deadline_ns{mission_smoke_deadline_ns * gpu_timeout_scale(SDL_getenv("STREFA_GPU_TIMEOUT_SCALE"))},
      scenario_{scenario},
      smoke_test_{scenario == SmokeScenario::menu},
      mission_smoke_{scenario == SmokeScenario::mission} {
    if (profile) mission_frames.reserve(1024);
}

void WindowTest::before_frame(ScenarioContext& context, float frame_ms) {
    if (smoke_test_) settings_ok_ = smoke_step(context) && settings_ok_;
    if (mission_smoke_ && frames >= 30) ui_frame_times_.push_back(frame_ms);
    if (static_cast<std::size_t>(context.state.screen) < visited_screens_.size())
        visited_screens_[static_cast<std::size_t>(context.state.screen)] = true;
    visited_themes_[static_cast<std::size_t>(context.state.applied.map_theme)] = true;
}

bool WindowTest::smoke_step(ScenarioContext& context) {
    auto& state = context.state;
    auto& world = *context.cpu_world;
    switch (frames) {
    case 16: return context.perform(ui::Action::open_settings);
    case 24:
        state.draft.map_theme = core::MapTheme::industrial;
        state.draft.width = 1100;
        state.draft.height = 720;
        state.draft.vsync = false;
        state.draft.ui_scale = 1.15F;
        return context.perform(ui::Action::apply);
    case 32:
        // The theme preview shows the unsaved choice and returns to Settings without losing it.
        state.draft.map_theme = core::MapTheme::winter;
        return context.perform(ui::Action::open_preview);
    case 40: {
        const bool opened = context.perform(ui::Action::open_settings);
        preview_keeps_draft_ = opened && state.draft.map_theme == core::MapTheme::winter
                            && state.applied.map_theme == core::MapTheme::industrial;
        return opened;
    }
    case 41: state.draft.map_theme = core::MapTheme::winter; return context.perform(ui::Action::apply);
    case 48: return context.perform(ui::Action::open_about);
    case 56: return context.perform(ui::Action::open_laboratory);
    case 57:
        world.paint({105, 100}, {109, 115}, 3, sim::Material::fire);
        world.paint({240, 120}, {255, 120}, 4, sim::Material::lava);
        break;
    case 58:
        context.lab_controls.paused = true;
        paused_tick_ = world.statistics().tick;
        break;
    case 59: lab_ok_ = world.statistics().tick == paused_tick_; break;
    case 60: context.cpu_single_step = true; break;
    case 61: lab_ok_ = lab_ok_ && world.statistics().tick == paused_tick_ + 1; break;
    case 62: context.lab_controls.paused = false; break;
    case 78: return context.perform(ui::Action::back);
    case 236: return open_gpu_screen(context, ui::Action::open_gpu);
    case 240:
        // Out-of-range slider values take the UI path (make_thermal_brush) into the world.
        if (context.gpu_available) {
            auto controls = context.gpu_controls;
            controls.thermal_tool = static_cast<int>(gpu::ThermalTool::heat);
            controls.thermal_rate = 1e9F;
            controls.gas_rate = -5;
            controls.radius = 99;
            context.inject.thermal_brush = ui::make_thermal_brush(controls, {40, 40, 60, 40});
        }
        break;
    case 242:
        // Raising lighting in the HUD stays in the session until "Zapisz na stałe".
        if (context.gpu_available) context.gpu_controls.lighting.quality = gpu::LightingQuality::low;
        break;
    case 244:
        if (context.gpu_available) {
            experimental_ok_ = state.applied.lighting_quality == 1 && state.stored.lighting_quality == 0;
            experimental_ok_ = context.perform(ui::Action::confirm_settings) && experimental_ok_
                            && state.stored.lighting_quality == 1;
            context.gpu_controls.lighting.quality = gpu::LightingQuality::off;
        }
        break;
    case 246:
        // Lowering is written without confirmation.
        if (context.gpu_available) experimental_ok_ = experimental_ok_ && state.stored.lighting_quality == 0;
        break;
    case 250: return !context.gpu_available || context.perform(ui::Action::back);
    case 256: return open_gpu_screen(context, ui::Action::open_mission);
    case 300: return !context.gpu_available || context.perform(ui::Action::back);
    default: return true;
    }
    return true;
}

bool WindowTest::open_gpu_screen(ScenarioContext& context, ui::Action action) {
    if (context.gpu_available) return context.perform(action);
    const bool refused =
            !context.perform(action) && context.state.screen == ui::Screen::main_menu && !context.message.empty();
    gpu_unavailable_handled_ = refused && (action == ui::Action::open_gpu || gpu_unavailable_handled_);
    return true;
}

// Mission smoke: time fed to the clock = executed ticks + dropped + pending (one tick tolerance).
double WindowTest::mission_accounting_error(const ScenarioContext& context) const {
    const auto& clock = context.simulation.clock();
    const double executed = static_cast<double>(context.simulation->metrics().steps - mission_steps_at_epoch_)
                          * SimulationWorld::tick_seconds;
    return mission_fed_seconds_ - (executed + clock.dropped_seconds() + clock.pending_seconds());
}

void WindowTest::mission_clock_frame(const ScenarioContext& context, double sim_seconds, std::uint64_t frame_start,
                                     unsigned mission_ticks_this_frame) {
    const auto& simulation = context.simulation;
    if (context.state.screen != ui::Screen::gpu_mission) return;
    if (!mission_epoch_ || *mission_epoch_ != simulation.scene_epoch()) {
        mission_epoch_ = simulation.scene_epoch();
        mission_fed_seconds_ = 0;
        // Called after this frame's update: its ticks belong to the new epoch.
        mission_steps_at_epoch_ = simulation->metrics().steps - mission_ticks_this_frame;
        mission_started_ns_ = frame_start;
        mission_dropped_at_start_ = 0;
        mission_start_recorded_ = false;
    }
    if (!context.gpu_controls.paused) mission_fed_seconds_ += sim_seconds;
    if (!mission_start_recorded_ && frame_start - mission_started_ns_ >= 1'000'000'000) {
        mission_dropped_at_start_ = simulation.clock().dropped_seconds();
        mission_start_recorded_ = true;
    }
    if (frames % 240 == 0) write_mission_progress(context, "w toku");
}

void WindowTest::write_mission_progress(const ScenarioContext& context, std::string_view state) const {
    std::ofstream out{context.profile / "mission-progress.txt"};
    out << "state=" << state << "\nticks=" << (context.simulation ? context.simulation->metrics().steps : 0)
        << "\nrequired_ticks=" << mission_smoke_min_ticks << "\nframes=" << frames
        << "\ndeadline_s=" << static_cast<double>(mission_deadline_ns) / 1e9
        << "\nelapsed_s=" << static_cast<double>(SDL_GetTicksNS()) / 1e9
        << "\ndropped_seconds=" << context.simulation.clock().dropped_seconds()
        << "\nfed_seconds=" << mission_fed_seconds_ << "\nrenderer=" << context.renderer << '\n';
}

void WindowTest::heartbeat(const ScenarioContext& context, std::uint64_t now_ns, const FrameObservers& observers) {
    if (now_ns - last_heartbeat_ns_ < 1'000'000'000) return;
    last_heartbeat_ns_ = now_ns;
    write_heartbeat(context, "w toku", observers);
}

void WindowTest::write_heartbeat(const ScenarioContext& context, std::string_view state,
                                 const FrameObservers& observers) const {
    const char* scenario = "none";
    switch (scenario_) {
    case SmokeScenario::menu: scenario = "menu"; break;
    case SmokeScenario::cpu_laboratory: scenario = "cpu_laboratory"; break;
    case SmokeScenario::gpu_laboratory: scenario = "gpu_laboratory"; break;
    case SmokeScenario::mission: scenario = "mission"; break;
    case SmokeScenario::session: scenario = "session"; break;
    case SmokeScenario::mission_quick: scenario = "mission_quick"; break;
    case SmokeScenario::mode_faults: scenario = "mode_faults"; break;
    case SmokeScenario::none:
    case SmokeScenario::scene_benchmark: break;
    }
    const std::uint64_t ticks = context.simulation ? context.simulation->metrics().steps
                              : context.laboratory ? context.laboratory->snapshot()->report.tick
                                                   : 0;
    std::string stage = observers.progress_stage();
    if (stage.empty())
        stage = mission_smoke_ ? std::format("kroki {}/{}", ticks, mission_smoke_min_ticks)
                               : std::format("klatka {}", frames);
    std::ofstream out{context.profile / "progress.txt"};
    out << "state=" << state << "\nscenario=" << scenario
        << "\nelapsed_s=" << static_cast<double>(SDL_GetTicksNS()) / 1e9 << "\nframes=" << frames << "\nticks=" << ticks
        << "\nscreen=" << static_cast<int>(context.state.screen) << "\nstage=" << stage
        << "\nrenderer=" << context.renderer << '\n';
}

std::string WindowTest::mission_deadline(const ScenarioContext& context, const FrameObservers& observers) const {
    write_heartbeat(context, "limit_czasu", observers);
    write_mission_progress(context, "limit_czasu");
    return std::format("Misja GPU: przekroczono limit czasu testu ({}/{} kroków). Postęp zapisano w "
                       "mission-progress.txt.",
                       context.simulation ? context.simulation->metrics().steps : 0, mission_smoke_min_ticks);
}

std::expected<void, std::string> WindowTest::finish(const ScenarioContext& context) {
    if (mission_smoke_) return finish_mission(context);
    if (smoke_test_) return finish_menu(context);
    return {};
}

std::expected<void, std::string> WindowTest::finish_mission(const ScenarioContext& context) {
    const auto times = sorted_frame_times(std::move(ui_frame_times_));
    if (!times) return std::unexpected{times.error()};
    std::ofstream report{context.profile / "mission-ui.txt"};
    auto& measured = *context.simulation;
    const auto async_mission = measured.mission();
    // Final validation is outside the measured window, including readback-off ablations.
    const auto final_mission = measured.read_mission();
    {
        std::ofstream frames_out{context.profile / "mission-frames.csv"};
        frames_out
                << "frame,frame_ms,gpu_physics_ms,cpu_submit_ms,dropped_seconds,ui_poll_ms,ui_timer_poll_ms,ui_fence_poll_ms,ui_read_ms,frame_step_submit_ms,gpu_timing_available,gpu_timing_samples\n";
        for (const auto& row : mission_frames) {
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (i) frames_out << ',';
                frames_out << row[i];
            }
            frames_out << '\n';
        }
    }
    write_gpu_window_report(report, context,
                            {"mission", frames, *times, true, context.simulation.clock().dropped_seconds()});
    const double accounting_error = mission_accounting_error(context);
    const double dropped_after_start = context.simulation.clock().dropped_seconds() - mission_dropped_at_start_;
    const bool software = software_renderer(context.renderer);
    const bool dropped_ok = std::abs(accounting_error) <= SimulationWorld::tick_seconds + 1e-6
                         && (software || dropped_after_start <= mission_smoke_max_dropped);
    report << "mission_min_ticks=" << mission_smoke_min_ticks << "\nfed_seconds=" << mission_fed_seconds_
           << "\nclock_accounting_error_seconds=" << accounting_error
           << "\ndropped_after_first_second=" << dropped_after_start
           << "\ndropped_limit=" << (software ? "none_software_renderer" : "0.5") << "\ndropped_ok=" << dropped_ok
           << "\nhud_epoch=" << async_mission.status[1] << "\nhud_tick=" << async_mission.status[2]
           << "\nvalidation_tick=" << final_mission.status[2] << "\nshots=" << final_mission.inventory[3]
           << "\nexplosions=" << final_mission.counts[0] << "\nmission_complete=" << final_mission.status[3]
           << "\nscore=" << final_mission.inventory[2] << '\n';
    if (measured.metrics().steps < mission_smoke_min_ticks || !dropped_ok
        || (context.profiling.body_coupling && (final_mission.status[3] != 1 || final_mission.inventory[2] != 3)))
        return std::unexpected{"Misja GPU: niepełny przebieg lub pominięte kroki czasu."};
    if (measured.metrics().steps < 100 || !p95_ok(context.renderer, percentile(*times, .95)))
        return std::unexpected{"Nieudany test okna GPU."};
    return {};
}

std::expected<void, std::string> WindowTest::finish_menu(const ScenarioContext& context) const {
    // Without OpenGL 4.3 the two GPU screens must refuse to open and keep the menu.
    const std::size_t gpu_screens = 2;
    const bool cpu_screens = std::all_of(visited_screens_.begin(), visited_screens_.end() - gpu_screens,
                                         [](bool value) { return value; });
    const auto visited = std::ranges::count(visited_screens_, true);
    const bool all_screens =
            context.gpu_available ? visited == std::ssize(visited_screens_) : cpu_screens && gpu_unavailable_handled_;
    const bool all_themes = std::ranges::all_of(visited_themes_, [](bool value) { return value; });
    const bool all_images = std::ranges::all_of(context.images, [](const auto& image) { return image.handle != 0; });
    const bool finite = context.cpu_world->statistics().finite;
    std::ofstream report{context.profile / "smoke-report.txt"};
    report << "frames=" << frames << "\nscreens=" << visited << '/' << visited_screens_.size()
           << "\ngpu_screens=" << (context.gpu_available ? "available" : "unavailable_handled")
           << "\nexperimental_confirmation=" << (!context.gpu_available || experimental_ok_)
           << "\npreview_keeps_draft=" << preview_keeps_draft_ << "\nall_screens=" << all_screens
           << "\nall_themes=" << all_themes << "\nall_backgrounds=" << all_images
           << "\nsettings_applied=" << settings_ok_ << "\nrenderer=" << context.renderer
           << "\nopengl=" << context.gl_version << "\ncompiler=" << build::compiler << "\ncpp=" << __cplusplus
           << "\nsimulation_uploads=" << context.uploads << "\nsimulation_pause_step=" << lab_ok_ << '\n';
    report << "layered_parts=" << (context.parts.handle != 0) << "\nweapon_atlas=" << (context.weapons.handle != 0)
           << "\nphysics_finite=" << finite << '\n';
    if (!all_screens || (context.gpu_available && !experimental_ok_) || !preview_keeps_draft_ || !all_themes
        || !all_images || !settings_ok_ || context.uploads == 0 || !lab_ok_ || !context.parts.handle
        || !context.weapons.handle || !finite)
        return std::unexpected{"Niepełny wynik testu aplikacji. Sprawdź smoke-report.txt."};
    return {};
}

} // namespace strefa::app
