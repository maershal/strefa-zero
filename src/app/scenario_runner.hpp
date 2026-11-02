#pragma once
#include "app/frame_profile.hpp"
#include "app/simulation_world.hpp"
#include "build_info.hpp"
#include "sim/recording.hpp"
#include "ui/gpu_laboratory.hpp"
#include "ui/laboratory.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <iomanip>
#include <vector>

namespace strefa::app {
// Uses the production lifecycle/actions and scheduler. Scripted input and
// synchronous validation/export stay outside the interactive frame controller.
class ScenarioRunner {
public:
    static constexpr std::uint64_t mission_smoke_min_ticks = 1700;
    // Internal limit below the CTest TIMEOUT of gpu.mission_ui (60 s), measured from SDL start.
    static constexpr std::uint64_t mission_smoke_deadline_ns = 55'000'000'000;
    // Hardware GPU: wall time the interactive clock may discard after the first second.
    static constexpr double mission_smoke_max_dropped = 0.5;
    // mission_quick: an injected 5 s stall must be dropped, not repaid as a fast-forward.
    static constexpr double quick_stall_seconds = 5, quick_max_speed = 1.1;
    static constexpr unsigned quick_stall_frame = 148, quick_speed_from = 150;
    static constexpr unsigned menu_smoke_frames = 306, mission_quick_frames = 180, fault_smoke_frames = 150;
    ScenarioRunner(bool smoke, bool laboratory, bool gpu, bool mission, bool profile, bool session)
        : session_smoke{session},
          smoke_test{smoke},
          lab_smoke{laboratory},
          gpu_smoke{gpu},
          mission_smoke{mission} {
        profile_frames = profile;
        if (profile) {
            frame_profiles.reserve(1024);
            mission_frames.reserve(1024);
            profile_anchor_ns = SDL_GetTicksNS();
            profile_anchor_utc_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
            profile_anchor_after_ns = SDL_GetTicksNS();
        }
    }
    void append(const FrameProfile& row) {
        if (frame_profiles.size() < frame_profile_capacity) {
            frame_profiles.push_back(row);
            frame_profiles.back().finish(SDL_GetTicksNS());
        } else
            ++frame_profiles_dropped;
    }
    template<class App> void before_frame(App& app, float frame_ms) {
        if (session_smoke) session_step(app);
        if (smoke_test) smoke_ok = smoke_step(app, frames) && smoke_ok;
        if (mission_quick) mission_quick_step(app, frame_ms);
        if (fault_smoke) fault_step(app);
        if (lab_smoke) {
            if (frames >= 30) ui_frame_times.push_back(frame_ms);
            if (frames == 120) app.lab_controls_.paused = true;
            if (frames == 150) app.lab_controls_.paused = false;
            if (frames == 200) app.perform(ui::Action::back);
            if (frames == 220) app.perform(ui::Action::open_laboratory);
        }
        if (mission_smoke && frames >= 30) ui_frame_times.push_back(frame_ms);
        if (gpu_smoke) {
            if (frames >= 30) ui_frame_times.push_back(frame_ms);
            if (!app.window_seconds_ && frames == 80) {
                app.gpu_controls_.paused = true;
                gpu_paused_step = app.simulation_->metrics().steps;
            }
            if (!app.window_seconds_ && frames == 100) {
                gpu_pause_ok = app.simulation_->metrics().steps == gpu_paused_step;
                app.gpu_controls_.paused = false;
            }
            if (!app.window_seconds_ && frames == 140) app.perform(ui::Action::back);
            if (!app.window_seconds_ && frames == 155) app.perform(ui::Action::open_gpu);
        }
    }
    template<class App> std::expected<void, std::string> write_frames(App& app) {
        if (profile_frames) {
            std::ofstream timing_out{app.store_.path().parent_path() / "frame-timings.csv"};
            std::ofstream info_out{app.store_.path().parent_path() / "frame-timings-info.txt"};
            if (!timing_out || !info_out) return std::unexpected{"Nie można zapisać pełnego profilu klatek."};
            timing_out << std::setprecision(12);
            write_frame_profile_header(timing_out);
            for (const auto& row : frame_profiles) write_frame_profile(timing_out, row);
            info_out << "render_width=960\nrender_height=576\nrender_quality="
                     << gpu::render_quality_name(app.simulation_ ? app.simulation_->render_options().quality
                                                                 : gpu::RenderQuality::basic)
                     << '\n';
            info_out
                    << "schema=1\nrows_capacity=" << frame_profile_capacity
                    << "\nrows_dropped=" << frame_profiles_dropped << "\nanchor_sdl_ns=" << profile_anchor_ns
                    << "\nanchor_utc_unix_ns=" << profile_anchor_utc_ns
                    << "\nanchor_sdl_after_ns=" << profile_anchor_after_ns
                    << "\nanchor_pair_order=sdl_before_utc_then_sdl_after\nanchor_precision=bracketed_cpu_samples\n"
                    << "mission_smoke=" << mission_smoke << "\nmission_smoke_min_ticks=" << mission_smoke_min_ticks
                    << "\nrendered_frames=" << frames << "\nloop_iterations=" << iterations
                    << "\nmission_submitted_ticks=" << (app.simulation_ ? app.simulation_->metrics().steps : 0) << '\n'
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
                    << "timer_mode=" << gpu::timer_mode_name(gpu::effective_timer_mode(app.gpu_profiling_))
                    << "\ngpu_timers_enabled=" << app.gpu_profiling_.gpu_timers
                    << "\nphase_gating=" << app.execution_.phase_gating << "\nphase_gating_effective="
                    << (app.execution_.phase_gating && app.execution_.neighbors != gpu::NeighborMode::linked)
                    << "\nneighbor_reads_requested=" << gpu::neighbor_reads_name(app.execution_.neighbor_reads)
                    << "\nneighbor_reads_effective="
                    << gpu::neighbor_reads_name(
                               gpu::effective_neighbor_reads(app.execution_.neighbor_reads, app.execution_.neighbors))
                    << "\nstorage_requested=" << gpu::device_storage_name(app.execution_.storage)
                    << "\nreadback_period=" << app.gpu_profiling_.readback_period
                    << "\nskip_zero_projection=" << app.execution_.skip_zero_projection << '\n';
            if (!timing_out || !info_out) return std::unexpected{"Niepełny zapis profilu klatek."};
        }
        return {};
    }
    template<class App> std::expected<void, std::string> finish(App& app) {
        if (session_smoke) {
            const auto cache = app.simulation_->shader_cache_stats();
            session_ok = session_ok && app.simulation_.creations() == 1 && !app.game_ && !app.lab_worker_
                      && cache.programs == session_programs;
            std::ofstream report{app.store_.path().parent_path() / "session-smoke.txt"};
            report << "frames=" << frames << "\nchecks=" << session_checks << "\npassed=" << session_ok
                   << "\nworld_creations=" << app.simulation_.creations() << "\nprograms=" << cache.programs
                   << "\ncpu_game_created=" << bool(app.game_) << "\ncpu_laboratory_created=" << bool(app.lab_worker_)
                   << '\n';
            const auto folder = app.store_.path().parent_path() / "recording-check";
            if (auto saved = app.simulation_.save_recording(folder); !saved) return saved;
            const auto tick = app.simulation_->metrics().steps;
            if (auto replayed = app.simulation_.replay_recording(folder / "session.txt"); !replayed) return replayed;
            if (app.simulation_->metrics().steps != tick || app.simulation_.scene().mission)
                return std::unexpected{"Odtwarzanie nie zachowało sceny i kroku."};
            if (!session_ok || session_checks != 12)
                return std::unexpected{"Wspólna scena: utrata stanu lub niepełny test przełączeń."};
        }
        if (lab_smoke) {
            const auto snapshot = app.lab_worker_->snapshot();
            std::ranges::sort(ui_frame_times);
            auto percentile = [&](double q) {
                return ui_frame_times[std::min(ui_frame_times.size() - 1,
                                               static_cast<std::size_t>(ui_frame_times.size() * q))];
            };
            std::ofstream report{app.store_.path().parent_path() / "laboratory-performance.txt"};
            if (!report) return std::unexpected{"Nie można zapisać raportu laboratorium."};
            report << "frames=" << frames << "\nui_median_ms=" << percentile(.5) << "\nui_p95_ms=" << percentile(.95)
                   << "\nui_p99_ms=" << percentile(.99) << "\nui_max_ms=" << ui_frame_times.back()
                   << "\nphysics_last_ms=" << snapshot->step_ms << "\nphysics_ticks=" << snapshot->report.tick
                   << "\nphysics_steps_per_second=" << snapshot->steps_per_second
                   << "\nphysics_threads=" << snapshot->report.worker_threads << "\nfinite=" << snapshot->report.finite
                   << '\n';
            if (!snapshot->ready || !snapshot->report.finite || snapshot->report.tick < 5 || percentile(.95) > 100
                || (app.physics_.worker_threads && snapshot->report.worker_threads != app.physics_.worker_threads))
                return std::unexpected{"Test reaktywności laboratorium nie przeszedł."};
        }
        if (gpu_smoke || mission_smoke) {
            std::ranges::sort(ui_frame_times);
            auto percentile = [&](double q) {
                return ui_frame_times[std::min(ui_frame_times.size() - 1,
                                               static_cast<std::size_t>(ui_frame_times.size() * q))];
            };
            std::ofstream report{app.store_.path().parent_path() / (mission_smoke ? "mission-ui.txt" : "gpu-ui.txt")};
            auto& measured = mission_smoke ? *app.simulation_ : *app.simulation_;
            const auto async_mission = measured.mission();
            // Final validation is outside the measured window, including readback-off ablations.
            const auto final_mission = mission_smoke ? measured.read_mission() : gpu::MissionSnapshot{};
            if (app.gpu_profiling_.collect) {
                measured.poll();
                gpu::write_profile_samples(measured, app.store_.path().parent_path());
            }
            if (mission_smoke) {
                std::ofstream frames_out{app.store_.path().parent_path() / "mission-frames.csv"};
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
            report << "scene=" << (mission_smoke ? "mission" : "sand_water") << "\nframes=" << frames
                   << "\nmedian_ms=" << percentile(.5) << "\np95_ms=" << percentile(.95)
                   << "\np99_ms=" << percentile(.99) << "\nmax_ms=" << ui_frame_times.back()
                   << "\nsteps=" << measured.metrics().steps << "\ngpu_step_ms=" << measured.metrics().gpu_ms
                   << "\ngpu_render_ms=" << measured.metrics().render_ms
                   << "\ncpu_submit_ms=" << measured.metrics().cpu_submit_ms << "\npaused_ok=" << gpu_pause_ok
                   << "\ndropped_seconds="
                   << (mission_smoke ? app.simulation_.clock().dropped_seconds() : app.gpu_controls_.lost_seconds)
                   << "\nrenderer=" << app.renderer_ << '\n';
            report << "phase_gating=" << app.execution_.phase_gating << "\nphase_gating_effective="
                   << (app.execution_.phase_gating && app.execution_.neighbors != gpu::NeighborMode::linked)
                   << "\nneighbor_reads_requested=" << gpu::neighbor_reads_name(app.execution_.neighbor_reads)
                   << "\nneighbor_reads_effective="
                   << gpu::neighbor_reads_name(
                              gpu::effective_neighbor_reads(app.execution_.neighbor_reads, app.execution_.neighbors))
                   << "\nstorage_requested=" << gpu::device_storage_name(app.execution_.storage)
                   << "\nreadback_period=" << app.gpu_profiling_.readback_period
                   << "\nskip_zero_projection=" << app.execution_.skip_zero_projection << '\n';
            report << "storage_effective=" << gpu::device_storage_name(measured.upload_stats().mode) << '\n';
            report << "conditional_work_requested=" << app.execution_.conditional_work
                   << "\nconditional_work_effective=" << measured.execution_options().conditional_work << '\n';
            report << "gpu_timer_mode=" << gpu::timer_mode_name(gpu::effective_timer_mode(app.gpu_profiling_))
                   << "\ngpu_timing_available="
                   << (gpu::effective_timer_mode(app.gpu_profiling_) != gpu::TimerMode::off
                       && measured.metrics().timing_samples > 0)
                   << "\ngpu_timing_samples=" << measured.metrics().timing_samples << '\n';
            const double accounting_error = mission_smoke ? mission_accounting_error(app) : 0;
            const double dropped_after_start = app.simulation_.clock().dropped_seconds() - mission_dropped_at_start;
            const bool software = software_renderer(app.renderer_);
            const bool dropped_ok = std::abs(accounting_error) <= SimulationWorld::tick_seconds + 1e-6
                                 && (software || dropped_after_start <= mission_smoke_max_dropped);
            if (mission_smoke)
                report << "mission_min_ticks=" << mission_smoke_min_ticks << "\nfed_seconds=" << mission_fed_seconds
                       << "\nclock_accounting_error_seconds=" << accounting_error
                       << "\ndropped_after_first_second=" << dropped_after_start
                       << "\ndropped_limit=" << (software ? "none_software_renderer" : "0.5")
                       << "\ndropped_ok=" << dropped_ok << "\nhud_epoch=" << async_mission.status[1]
                       << "\nhud_tick=" << async_mission.status[2] << "\nvalidation_tick=" << final_mission.status[2]
                       << "\nshots=" << final_mission.inventory[3] << "\nexplosions=" << final_mission.counts[0]
                       << "\nmission_complete=" << final_mission.status[3] << "\nscore=" << final_mission.inventory[2]
                       << '\n';
            if (mission_smoke
                && (measured.metrics().steps < mission_smoke_min_ticks || !dropped_ok
                    || (app.gpu_profiling_.body_coupling
                        && (final_mission.status[3] != 1 || final_mission.inventory[2] != 3))))
                return std::unexpected{"Misja GPU: niepełny przebieg lub pominięte kroki czasu."};
            if (!gpu_pause_ok || measured.metrics().steps < 100 || percentile(.95) > 100)
                return std::unexpected{"Nieudany test okna GPU."};
        }
        if (mission_quick) {
            const auto steps = app.simulation_ ? app.simulation_->metrics().steps : 0;
            const double speed = quick_speed(app);
            const bool stall_ok = stall_dropped >= quick_stall_seconds - sim::interactive_clock.max_backlog_seconds
                               && stall_dropped <= quick_stall_seconds + 1 && speed > 0 && speed <= quick_max_speed;
            const bool passed = quick_ok && quick_checks == 7 && stall_ok && steps > quick_ticks
                             && app.simulation_.creations() == 1;
            std::ofstream report{app.store_.path().parent_path() / "mission-ui-quick.txt"};
            report << "frames=" << frames << "\nchecks=" << quick_checks << "\nstall_seconds=" << quick_stall_seconds
                   << "\nstall_dropped_seconds=" << stall_dropped << "\nspeed_after_stall=" << speed
                   << "\nstall_ok=" << stall_ok << "\npassed=" << passed << "\nsubmitted_ticks=" << steps
                   << "\nworld_creations=" << app.simulation_.creations() << "\nrenderer=" << app.renderer_
                   << "\nopengl=" << app.gl_version_ << '\n';
            if (!passed)
                return std::unexpected{"Krótki test ekranu misji nie przeszedł. Sprawdź mission-ui-quick.txt."};
        }
        if (fault_smoke) {
            const unsigned expected = app.gpu_available_ ? 4 : 2;
            unsigned logged = 0;
            std::ifstream log{app.store_.path().parent_path() / "errors.log"};
            for (std::string line; std::getline(log, line);) logged += line.find("Wstrzyknięty") != std::string::npos;
            const bool passed = fault_ok && !app.fault_screen_ && fault_checks == (app.gpu_available_ ? 9 : 5)
                             && app.mode_failures_ == expected && logged == expected;
            std::ofstream report{app.store_.path().parent_path() / "mode-faults.txt"};
            report << "frames=" << frames << "\nchecks=" << fault_checks << "\npassed=" << passed
                   << "\nrecovered_modes=" << app.mode_failures_ << "\nexpected_modes=" << expected
                   << "\nlogged=" << logged << "\ngpu_modes=" << app.gpu_available_ << '\n';
            if (!passed) return std::unexpected{"Test błędów trybów nie przeszedł. Sprawdź mode-faults.txt."};
        }
        if (smoke_test) {
            // Without OpenGL 4.3 the two GPU screens must refuse to open and keep the menu.
            const std::size_t gpu_screens = 2;
            const bool cpu_screens = std::all_of(visited_screens.begin(), visited_screens.end() - gpu_screens,
                                                 [](bool value) { return value; });
            const auto visited = std::ranges::count(visited_screens, true);
            const bool all_screens = app.gpu_available_ ? visited == std::ssize(visited_screens)
                                                        : cpu_screens && gpu_unavailable_handled;
            const bool all_themes = std::ranges::all_of(visited_themes, [](bool value) { return value; });
            const bool all_images =
                    std::ranges::all_of(app.images_, [](const auto& image) { return image.handle != 0; });
            std::ofstream report{app.store_.path().parent_path() / "smoke-report.txt"};
            report << "frames=" << frames << "\nscreens=" << visited << '/' << visited_screens.size()
                   << "\ngpu_screens=" << (app.gpu_available_ ? "available" : "unavailable_handled")
                   << "\nexperimental_confirmation=" << (!app.gpu_available_ || experimental_ok)
                   << "\nall_screens=" << all_screens << "\nall_themes=" << all_themes
                   << "\nall_backgrounds=" << all_images << "\nsettings_applied=" << smoke_ok
                   << "\nrenderer=" << app.renderer_ << "\nopengl=" << app.gl_version_
                   << "\ncompiler=" << build::compiler << "\ncpp=" << __cplusplus
                   << "\nsimulation_uploads=" << app.uploads_ << "\nsimulation_pause_step=" << app.smoke_lab_ok_
                   << "\nactor_atlas=" << (app.soldier_.handle != 0) << "\ngame_uploads=" << app.game_uploads_
                   << "\ngame_ticks=" << app.game_->ticks() << "\nshots_fired=" << app.game_->shots_fired()
                   << "\ngrenade_explosions=" << app.game_->explosions() << '\n';
            report << "layered_parts=" << (app.parts_.handle != 0) << "\nweapon_atlas=" << (app.weapons_.handle != 0)
                   << "\nphysics_finite=" << app.world_->statistics().finite
                   << "\ngame_physics_finite=" << app.game_->world().statistics().finite << '\n';
            if (!all_screens || (app.gpu_available_ && !experimental_ok) || !all_themes || !all_images || !smoke_ok
                || app.uploads_ == 0 || !app.smoke_lab_ok_ || !app.soldier_.handle || !app.parts_.handle
                || !app.weapons_.handle || !app.world_->statistics().finite || !app.game_->world().statistics().finite
                || app.game_uploads_ == 0 || app.game_->shots_fired() == 0 || app.game_->explosions() != 1)
                return std::unexpected{"Niepełny wynik testu aplikacji. Sprawdź smoke-report.txt."};
        }
        return {};
    }
    template<class App> bool smoke_step(App& app, unsigned frame) {
        switch (frame) {
        case 16: return app.perform(ui::Action::open_settings);
        case 24:
            app.state_.draft.map_theme = core::MapTheme::industrial;
            app.state_.draft.width = 1100;
            app.state_.draft.height = 720;
            app.state_.draft.vsync = false;
            app.state_.draft.ui_scale = 1.15F;
            return app.perform(ui::Action::apply);
        case 32: return app.perform(ui::Action::open_preview);
        case 40: return app.perform(ui::Action::open_settings);
        case 41: app.state_.draft.map_theme = core::MapTheme::winter; return app.perform(ui::Action::apply);
        case 48: return app.perform(ui::Action::open_about);
        case 56: return app.perform(ui::Action::open_laboratory);
        case 57:
            app.world_->paint({105, 100}, {109, 115}, 3, sim::Material::fire);
            app.world_->paint({240, 120}, {255, 120}, 4, sim::Material::lava);
            break;
        case 58:
            app.lab_controls_.paused = true;
            app.smoke_paused_tick_ = app.world_->statistics().tick;
            break;
        case 59: app.smoke_lab_ok_ = app.world_->statistics().tick == app.smoke_paused_tick_; break;
        case 60: app.smoke_single_step_ = true; break;
        case 61:
            app.smoke_lab_ok_ = app.smoke_lab_ok_ && app.world_->statistics().tick == app.smoke_paused_tick_ + 1;
            break;
        case 62: app.lab_controls_.paused = false; break;
        case 78: return app.perform(ui::Action::back);
        case 80: return app.perform(ui::Action::open_arena);
        case 230: return app.perform(ui::Action::back);
        case 236: return open_gpu_screen(app, ui::Action::open_gpu);
        case 240:
            // Out-of-range slider values take the UI path (make_thermal_brush) into the world.
            if (app.gpu_available_) {
                auto controls = app.gpu_controls_;
                controls.thermal_tool = static_cast<int>(gpu::ThermalTool::heat);
                controls.thermal_rate = 1e9F;
                controls.gas_rate = -5;
                controls.radius = 99;
                injected_brush = ui::make_thermal_brush(controls, {40, 40, 60, 40});
            }
            break;
        case 242:
            // Raising lighting in the HUD stays in the session until "Zapisz na stałe".
            if (app.gpu_available_) app.gpu_controls_.lighting.quality = gpu::LightingQuality::low;
            break;
        case 244:
            if (app.gpu_available_) {
                experimental_ok = app.state_.applied.lighting_quality == 1 && app.state_.stored.lighting_quality == 0;
                experimental_ok = app.perform(ui::Action::confirm_settings) && experimental_ok
                               && app.state_.stored.lighting_quality == 1;
                app.gpu_controls_.lighting.quality = gpu::LightingQuality::off;
            }
            break;
        case 246:
            // Lowering is written without confirmation.
            if (app.gpu_available_) experimental_ok = experimental_ok && app.state_.stored.lighting_quality == 0;
            break;
        case 250: return !app.gpu_available_ || app.perform(ui::Action::back);
        case 256: return open_gpu_screen(app, ui::Action::open_mission);
        case 300: return !app.gpu_available_ || app.perform(ui::Action::back);
        default: return true;
        }
        return true;
    }
    template<class App> bool open_gpu_screen(App& app, ui::Action action) {
        if (app.gpu_available_) return app.perform(action);
        const bool refused =
                !app.perform(action) && app.state_.screen == ui::Screen::main_menu && !app.message_.empty();
        gpu_unavailable_handled = refused && (action == ui::Action::open_gpu || gpu_unavailable_handled);
        return true;
    }
    // Short CI check of the mission screen: open, leave, reopen through the GPU tools.
    template<class App> void mission_quick_step(App& app, float frame_ms) {
        auto check = [&](bool condition) {
            quick_ok = condition && quick_ok;
            ++quick_checks;
        };
        switch (frames) {
        case 60:
            quick_ticks = app.simulation_->metrics().steps;
            check(quick_ticks > 0 && app.state_.screen == ui::Screen::gpu_mission);
            check(app.perform(ui::Action::back) && app.state_.screen == ui::Screen::main_menu);
            break;
        case 70: check(app.perform(ui::Action::open_mission) && app.simulation_.creations() == 1); break;
        case 120:
            check(app.simulation_->metrics().steps > quick_ticks);
            check(app.perform(ui::Action::back));
            break;
        case 125: check(app.perform(ui::Action::open_gpu)); break;
        case 140: app.perform(ui::Action::back); break;
        case 145: check(app.perform(ui::Action::open_mission)); break;
        case quick_stall_frame:
            stall_dropped = app.simulation_.clock().dropped_seconds();
            SDL_Delay(static_cast<Uint32>(quick_stall_seconds * 1000));
            break;
        case quick_speed_from:
            stall_dropped = app.simulation_.clock().dropped_seconds() - stall_dropped;
            quick_ticks_after_stall = app.simulation_->metrics().steps;
            break;
        default: break;
        }
        if (frames >= quick_speed_from) quick_seconds_after_stall += frame_ms / 1000.0;
    }
    // Ticks run after the stall per second of frame time fed to the clock; ~1 when real time.
    template<class App> double quick_speed(const App& app) const {
        const auto ticks = app.simulation_ ? app.simulation_->metrics().steps - quick_ticks_after_stall : 0;
        return quick_seconds_after_stall > 0
                     ? static_cast<double>(ticks) * SimulationWorld::tick_seconds / quick_seconds_after_stall
                     : 0;
    }
    static bool software_renderer(std::string_view renderer) {
        std::string lower{renderer};
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.find("llvmpipe") != std::string::npos || lower.find("softpipe") != std::string::npos
            || lower.find("swiftshader") != std::string::npos || lower.find("software") != std::string::npos;
    }
    // Mission smoke: time fed to the clock = executed ticks + dropped + pending (one tick tolerance).
    template<class App> double mission_accounting_error(const App& app) const {
        const auto& clock = app.simulation_.clock();
        const double executed = static_cast<double>(app.simulation_->metrics().steps - mission_steps_at_epoch)
                              * SimulationWorld::tick_seconds;
        return mission_fed_seconds - (executed + clock.dropped_seconds() + clock.pending_seconds());
    }
    template<class App> void mission_clock_frame(const App& app, double sim_seconds, std::uint64_t frame_start) {
        if (app.state_.screen != ui::Screen::gpu_mission) return;
        if (!mission_epoch || *mission_epoch != app.simulation_.scene_epoch()) {
            mission_epoch = app.simulation_.scene_epoch();
            mission_fed_seconds = 0;
            // Called after this frame's update: its ticks belong to the new epoch.
            mission_steps_at_epoch = app.simulation_->metrics().steps - app.mission_ticks_this_frame_;
            mission_started_ns = frame_start;
            mission_dropped_at_start = 0;
            mission_start_recorded = false;
        }
        if (!app.gpu_controls_.paused) mission_fed_seconds += sim_seconds;
        if (!mission_start_recorded && frame_start - mission_started_ns >= 1'000'000'000) {
            mission_dropped_at_start = app.simulation_.clock().dropped_seconds();
            mission_start_recorded = true;
        }
        if (frames % 240 == 0) write_mission_progress(app, "w toku");
    }
    template<class App> void write_mission_progress(const App& app, std::string_view state) const {
        std::ofstream out{app.store_.path().parent_path() / "mission-progress.txt"};
        out << "state=" << state << "\nticks=" << (app.simulation_ ? app.simulation_->metrics().steps : 0)
            << "\nrequired_ticks=" << mission_smoke_min_ticks << "\nframes=" << frames
            << "\nelapsed_s=" << static_cast<double>(SDL_GetTicksNS()) / 1e9
            << "\ndropped_seconds=" << app.simulation_.clock().dropped_seconds()
            << "\nfed_seconds=" << mission_fed_seconds << "\nrenderer=" << app.renderer_ << '\n';
    }
    template<class App> std::string mission_deadline(const App& app) const {
        write_mission_progress(app, "limit_czasu");
        return std::format("Misja GPU: przekroczono limit czasu testu ({}/{} kroków). Postęp zapisano w "
                           "mission-progress.txt.",
                           app.simulation_ ? app.simulation_->metrics().steps : 0, mission_smoke_min_ticks);
    }
    // Each mode throws once inside its frame; the application must release it,
    // log the error, return to the menu and be able to open the mode again.
    template<class App> void fault_step(App& app) {
        auto check = [&](bool condition) {
            fault_ok = condition && fault_ok;
            ++fault_checks;
        };
        const bool menu = app.state_.screen == ui::Screen::main_menu && !app.message_.empty();
        switch (frames) {
        case 10: app.perform(ui::Action::open_laboratory); break;
        case 20: app.fault_screen_ = ui::Screen::laboratory; break;
        case 24: check(menu && !app.lab_worker_ && !app.world_); break;
        case 30: app.perform(ui::Action::open_arena); break;
        case 40: app.fault_screen_ = ui::Screen::arena; break;
        case 44: check(menu && !app.game_); break;
        case 50:
            if (app.gpu_available_) app.perform(ui::Action::open_gpu);
            break;
        case 60:
            if (app.gpu_available_) app.fault_screen_ = ui::Screen::gpu_laboratory;
            break;
        case 64:
            if (app.gpu_available_) check(menu && !app.simulation_);
            break;
        case 70:
            if (app.gpu_available_) app.perform(ui::Action::open_mission);
            break;
        case 80:
            if (app.gpu_available_) app.fault_screen_ = ui::Screen::gpu_mission;
            break;
        case 84:
            if (app.gpu_available_) check(menu && !app.simulation_);
            break;
        case 90:
            if (app.gpu_available_) check(app.perform(ui::Action::open_gpu));
            break;
        case 100:
            if (app.gpu_available_) check(app.perform(ui::Action::back));
            break;
        case 110: check(app.perform(ui::Action::open_laboratory)); break;
        case 120: check(app.perform(ui::Action::back)); break;
        case 130: check(app.perform(ui::Action::open_arena)); break;
        default: break;
        }
    }

    template<class App> void session_step(App& app) {
        auto check = [&](bool condition) {
            session_ok = condition && session_ok;
            ++session_checks;
        };
        auto snapshot = [&] {
            retained_mission = app.simulation_->read_mission();
            retained_grains = app.simulation_->readback();
            retained_terrain = app.simulation_->read_terrain();
            retained_tick = app.simulation_->metrics().steps;
        };
        auto unchanged = [&] {
            const auto mission = app.simulation_->read_mission();
            const auto grains = app.simulation_->readback();
            return app.simulation_->metrics().steps == retained_tick
                && std::memcmp(&mission, &retained_mission, sizeof(mission)) == 0
                && grains.size() == retained_grains.size()
                && std::memcmp(grains.data(), retained_grains.data(), grains.size() * sizeof(gpu::Grain)) == 0
                && app.simulation_->read_terrain() == retained_terrain;
        };
        switch (frames) {
        case 0:
            app.simulation_.begin_recording();
            session_programs = app.simulation_->shader_cache_stats().programs;
            check(session_programs > 0 && !app.game_ && !app.lab_worker_);
            break;
        case 30:
            app.gpu_controls_.paused = true;
            snapshot();
            app.perform(ui::Action::open_gpu);
            break;
        case 31: check(unchanged() && app.simulation_.scene().mission); break;
        case 32: injected_stroke = gpu::Stroke{8, 8, 8, 8, 2, gpu::Material::sand}; break;
        case 33:
            check(app.simulation_->metrics().steps == retained_tick
                  && app.simulation_->read_terrain() == retained_terrain);
            snapshot();
            break;
        case 34: app.perform(ui::Action::open_mission); break;
        case 35: check(unchanged()); break;
        case 36: app.gpu_controls_.render_quality = 2; break;
        case 37: check(unchanged()); break;
        case 38: app.gpu_controls_.render_quality = 0; break;
        case 39: check(unchanged()); break;
        case 40:
            app.perform(ui::Action::open_gpu);
            injected_step = true;
            break;
        case 41:
            check(app.simulation_->metrics().steps == retained_tick + 1);
            snapshot();
            break;
        case 50: app.perform(ui::Action::back); break;
        case 60:
            check(unchanged());
            app.perform(ui::Action::open_mission);
            break;
        case 61: check(unchanged()); break;
        case 70: app.gpu_controls_.paused = false; break;
        case 90:
            app.gpu_controls_.paused = true;
            app.perform(ui::Action::open_gpu);
            snapshot();
            break;
        case 91: check(unchanged()); break;
        case 100:
            app.simulation_.reset({false, gpu::Scene::empty});
            snapshot();
            break;
        case 101:
            check(!app.simulation_.scene().mission && retained_tick == 0);
            app.perform(ui::Action::open_mission);
            break;
        case 102: check(unchanged() && !app.simulation_.scene().mission); break;
        default: break;
        }
    }
    bool session_smoke{}, session_ok{true}, injected_step{};
    std::optional<gpu::Stroke> injected_stroke;
    unsigned session_checks{}, session_programs{};
    std::uint64_t retained_tick{};
    gpu::MissionSnapshot retained_mission;
    std::vector<gpu::Grain> retained_grains;
    std::vector<std::uint32_t> retained_terrain;
    bool smoke_test{}, lab_smoke{}, gpu_smoke{}, mission_smoke{};
    bool gpu_unavailable_handled{}, experimental_ok{};
    std::optional<gpu::ThermalBrush> injected_brush;
    bool mission_quick{}, quick_ok{true};
    unsigned quick_checks{};
    std::uint64_t quick_ticks{}, quick_ticks_after_stall{};
    double stall_dropped{}, quick_seconds_after_stall{};
    std::optional<unsigned> mission_epoch;
    double mission_fed_seconds{}, mission_dropped_at_start{};
    std::uint64_t mission_steps_at_epoch{}, mission_started_ns{};
    bool mission_start_recorded{};
    bool fault_smoke{}, fault_ok{true};
    unsigned fault_checks{};
    std::array<bool, static_cast<std::size_t>(ui::Screen::gpu_mission) + 1> visited_screens{};
    std::array<bool, 3> visited_themes{};
    bool smoke_ok = true;
    unsigned frames{};
    std::vector<double> ui_frame_times;
    std::vector<std::array<double, 12>> mission_frames;
    bool profile_frames{};
    std::vector<FrameProfile> frame_profiles;
    std::uint64_t profile_anchor_ns{};
    std::uint64_t profile_anchor_utc_ns{};
    std::uint64_t profile_anchor_after_ns{};
    std::uint64_t iterations{}, frame_profiles_dropped{};
    static constexpr std::size_t frame_profile_capacity = 250000;
    std::uint64_t gpu_paused_step{};
    bool gpu_pause_ok = true;
};
} // namespace strefa::app
