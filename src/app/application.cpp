#include "app/application.hpp"

#include "app/clock_stats.hpp"
#include "app/frame_profile.hpp"
#include "app/scenario_runner.hpp"
#include "app/scene_benchmark.hpp"
#include "app/simulation_world.hpp"
#include "build_info.hpp"
#include "gpu/render_device.hpp"
#include "platform/gl_debug.hpp"
#include "platform/settings_store.hpp"
#include "platform/texture.hpp"
#include "platform/window.hpp"
#include "sim/fixed_clock.hpp"
#include "sim/recording.hpp"
#include "ui/arena.hpp"
#include "ui/gpu_laboratory.hpp"
#include "ui/laboratory.hpp"
#include "ui/session.hpp"
#include "ui/view.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace strefa::app {
namespace {

std::filesystem::path utf8_path(const char* text) {
    return std::filesystem::path{reinterpret_cast<const char8_t*>(text)};
}
std::string gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unknown";
}
const char* mode_name(ui::Screen screen) {
    switch (screen) {
    case ui::Screen::laboratory: return "Laboratorium CPU";
    case ui::Screen::gpu_laboratory: return "Laboratorium GPU";
    case ui::Screen::gpu_mission: return "Misja GPU";
    default: return "Menu";
    }
}
ui::Screen target_screen(ui::Action action, ui::Screen current) {
    switch (action) {
    case ui::Action::open_laboratory: return ui::Screen::laboratory;
    case ui::Action::open_gpu: return ui::Screen::gpu_laboratory;
    case ui::Action::open_mission: return ui::Screen::gpu_mission;
    default: return current;
    }
}

struct ApplicationAssets {
    std::array<ui::Image, 3> backgrounds;
    platform::Texture& canvas;
    ui::Image soldier, parts, weapons;
};

// One --scene-benchmark scene: settings in, collected frames and GPU samples out.
struct BenchmarkRun {
    BenchmarkScene scene;
    double warmup_seconds{}, measure_seconds{};
    BenchmarkRecord record;
    std::uint64_t measure_started_ns{};
    std::array<std::uint64_t, 3> drops_at_start{}; // Metrics::*_timings_dropped when the window opened
    bool measuring{}, completed{};
};

struct LaunchConfig {
    sim::PhysicsConfig physics;
    gpu::ProfileOptions profiling;
    gpu::ExecutionOptions execution;
    platform::GlDebugLog* gl_debug{};
    bool shader_cache{};
    gpu::RenderOptions render;
    gpu::VfxOptions vfx;
    bool unpaced{};
    gpu::ThermalOptions thermal;
    unsigned thermal_scene{}, test_seconds{};
    std::optional<unsigned> gpu_scene{}, lab_scene{}; // set per scene by the scene benchmark
    StartMode start{StartMode::menu};
    SmokeScenario smoke{SmokeScenario::none};
    BenchmarkRun* benchmark{};
};

class Application {
    friend class strefa::app::ScenarioRunner;

public:
    Application(platform::Window& window, ui::Session& gui, const platform::SettingsStore& store,
                core::Settings settings, const ApplicationAssets& assets, std::string message,
                const LaunchConfig& config)
        : window_{window},
          gui_{gui},
          store_{store},
          images_{assets.backgrounds},
          canvas_{assets.canvas},
          soldier_{assets.soldier},
          parts_{assets.parts},
          weapons_{assets.weapons},
          state_{ui::Screen::main_menu, settings, settings, settings.show_diagnostics, settings},
          message_{std::move(message)},
          renderer_{gl_string(GL_RENDERER)},
          gl_version_{gl_string(GL_VERSION)},
          gpu_available_{platform::context_has_gl43()},
          physics_{config.physics},
          gpu_profiling_{config.profiling},
          execution_{config.execution},
          gl_debug_{config.gl_debug},
          shader_cache_{config.shader_cache},
          start_{config.start},
          smoke_{config.smoke} {
        thermal_options_ = config.thermal;
        if (config.thermal_scene)
            initial_scene_ = SceneConfiguration{false, gpu::Scene::empty, config.thermal_scene};
        else if (config.gpu_scene)
            initial_scene_ = SceneConfiguration{false, static_cast<gpu::Scene>(*config.gpu_scene)};
        if (config.lab_scene) lab_controls_.scene = static_cast<sim::Scene>(*config.lab_scene);
        benchmark_ = config.benchmark;
        window_seconds_ = config.test_seconds;
        gpu_controls_.thermal_enabled = config.thermal.enabled;
        vfx_options_ = config.vfx;
        gpu_controls_.vfx_quality = static_cast<int>(config.vfx.quality);
        unpaced_ = config.unpaced;
        gpu_controls_.render_quality = static_cast<int>(config.render.quality);
        gpu_controls_.lighting = config.render.lighting;
        lighting_quality_ = static_cast<int>(config.render.lighting.quality);
    }

    std::expected<int, std::string> run() {
        const bool smoke_test = smoke_ == SmokeScenario::menu;
        const bool lab_smoke = smoke_ == SmokeScenario::cpu_laboratory;
        const bool gpu_smoke = smoke_ == SmokeScenario::gpu_laboratory;
        const bool mission_smoke = smoke_ == SmokeScenario::mission;
        const bool session_smoke = smoke_ == SmokeScenario::session;
        const bool mission_quick = smoke_ == SmokeScenario::mission_quick;
        const bool fault_smoke = smoke_ == SmokeScenario::mode_faults;
        const bool window_test = smoke_ != SmokeScenario::none;
        const bool bench = smoke_ == SmokeScenario::scene_benchmark;
        const bool start_in_laboratory = start_ == StartMode::cpu_laboratory;
        const bool start_gpu = start_ == StartMode::gpu_laboratory;
        const bool start_mission = start_ == StartMode::mission;
        if (smoke_test) {
            world_ = std::make_unique<sim::World>(0x57EF'A023U, physics_);
            world_->load_demo();
            lab_snapshot_ = sim::present_world(*world_, sim::RenderMode::materials);
        }
        if (lab_smoke) lab_controls_.scene = sim::Scene::dam_break;
        if (start_in_laboratory && !smoke_test) perform(ui::Action::open_laboratory);
        if (lab_smoke) {
            lab_controls_.scene = sim::Scene::dam_break;
            perform(ui::Action::open_laboratory);
        }
        // Interactive starts fall back to the menu with the reason (e.g. OpenGL 3.3).
        if (start_gpu || gpu_smoke) {
            if (!perform(ui::Action::open_gpu) && gpu_smoke) return std::unexpected{message_};
        }
        if (gpu_smoke) {
            simulation_.reset({false, gpu::Scene::sand_water});
            gpu_controls_.scene = static_cast<int>(gpu::Scene::sand_water);
        }
        if (start_mission || mission_smoke || session_smoke || mission_quick) {
            if (!perform(ui::Action::open_mission) && !start_mission) return std::unexpected{message_};
        }
        // A benchmark scene that cannot open (e.g. no OpenGL 4.3) fails instead of measuring the menu.
        if (bench && state_.screen == ui::Screen::main_menu) return std::unexpected{message_};
        // The scripted mission route is indexed by tick, so the run ends after its ticks at any FPS.
        constexpr std::uint64_t mission_smoke_min_ticks = ScenarioRunner::mission_smoke_min_ticks;
        ScenarioRunner scenario{smoke_test, lab_smoke, gpu_smoke, mission_smoke, gpu_profiling_.collect, session_smoke};
        scenario.mission_quick = mission_quick;
        scenario.fault_smoke = fault_smoke;
        if (fault_smoke) {
            std::error_code ignored; // the test counts its own entries
            std::filesystem::remove(store_.path().parent_path() / "errors.log", ignored);
        }
        auto& visited_screens = scenario.visited_screens;
        auto& visited_themes = scenario.visited_themes;
        auto& frames = scenario.frames;
        auto& mission_frames = scenario.mission_frames;
        auto& profile_frames = scenario.profile_frames;
        auto& iterations = scenario.iterations;
        auto append_frame_profile = [&](const FrameProfile& row) { scenario.append(row); };
        if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::runtime);
        auto previous = SDL_GetTicksNS();
        const auto window_started = previous;
        refresh_hz_ = window_.refresh_hz();
        while (running_) {
            // A failing mode (GPU world, CPU laboratory) is closed and the menu
            // shows the reason; window tests still fail on the first error.
            try {
                const auto frame_start = SDL_GetTicksNS();
                const float frame_ms = static_cast<float>(frame_start - previous) / 1'000'000.0F;
                const double frame_seconds = static_cast<double>(frame_start - previous) / 1e9;
                previous = frame_start;
                FrameProfile frame_profile;
                std::optional<std::array<double, 12>> mission_frame;
                auto phase_started = frame_start;
                auto mark = [&](double& duration) {
                    if (profile_frames) {
                        const auto now = SDL_GetTicksNS();
                        duration += static_cast<double>(now - phase_started) / 1e6;
                        phase_started = now;
                    }
                };
                if (profile_frames) {
                    frame_profile.iteration = iterations++;
                    frame_profile.frame = frames;
                    frame_profile.screen = static_cast<int>(state_.screen);
                    frame_profile.start_ns = frame_start;
                    frame_profile.previous_interval_ms = frame_ms;
                    frame_profile.frame_limit = unpaced_ ? 0U : static_cast<unsigned>(state_.applied.frame_limit);
                }
                mission_ticks_this_frame_ = 0;
                gpu_ticks_this_frame_ = 0;
                world_render_submit_ms_ = 0;
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    gui_.process_event(event);
                    if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED
                        || event.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED)
                        refresh_hz_ = window_.refresh_hz();
                    if (event.type == SDL_EVENT_QUIT
                        || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                            && event.window.windowID == SDL_GetWindowID(window_.native())))
                        running_ = false;
                }
                if (!running_) break;
                // With VSync a frame lasts whole refresh periods; snapping the measured jitter
                // keeps a constant tick count per frame (60 Hz: 2 ticks of 1/120 s).
                double sim_seconds = frame_seconds;
                if (window_.vsync_enabled())
                    sim_seconds = vsync_snap_(frame_seconds, refresh_hz_);
                else
                    vsync_snap_.reset();
                mark(frame_profile.input_ms);
                if (profile_frames) frame_profile.presentation = window_.presentation_info();
                mark(frame_profile.metadata_ms);
                if (window_.minimized()) {
                    if (lab_worker_) lab_worker_->set_active(false);
                    clock_.reset();
                    simulation_.suspend();
                    if (profile_frames) {
                        frame_profile.mission_tick = simulation_ ? simulation_->metrics().steps : 0;
                        frame_profile.mission_pending_seconds = simulation_.clock().pending_seconds();
                        frame_profile.mission_dropped_seconds = simulation_.clock().dropped_seconds();
                    }
                    mark(frame_profile.update_ms);
                    SDL_Delay(30);
                    mark(frame_profile.sleep_ms);
                    if (profile_frames) append_frame_profile(frame_profile);
                    continue;
                }

                scenario.before_frame(*this, frame_ms);
                mark(frame_profile.scenario_ms);
                if (profile_frames) frame_profile.screen = static_cast<int>(state_.screen);
                gui_.set_scale(window_.display_scale(), state_.applied.ui_scale);
                gui_.new_frame();
                if (static_cast<std::size_t>(state_.screen) < visited_screens.size())
                    visited_screens[static_cast<std::size_t>(state_.screen)] = true;
                visited_themes[static_cast<std::size_t>(state_.applied.map_theme)] = true;
                const ui::FrameInfo info{gui_.scale(),
                                         frame_ms,
                                         renderer_,
                                         gl_version_,
                                         message_,
                                         gpu_available_,
                                         core::confirmed_settings(state_.applied, state_.stored) != state_.applied};
                mark(frame_profile.ui_prepare_ms);
                // Render one coherent snapshot of actors and terrain. Input collected by
                // the UI advances the next snapshot; no GPU view can mutate the world.
                if (state_.screen == ui::Screen::laboratory) {
                    if (lab_worker_) lab_snapshot_ = lab_worker_->snapshot();
                    if (!lab_snapshot_->error.empty()) throw std::runtime_error{lab_snapshot_->error};
                    if (last_lab_pixels_ != lab_snapshot_->pixels) {
                        const auto uploaded = canvas_.update_rgba(std::as_bytes(std::span{*lab_snapshot_->pixels}));
                        if (!uploaded) throw std::runtime_error{uploaded.error()};
                        last_lab_pixels_ = lab_snapshot_->pixels;
                        ++uploads_;
                    }
                    lab_controls_.recording = lab_snapshot_->recording;
                }
                mark(frame_profile.upload_ms);
                ui::LaboratoryInput input;
                std::optional<ui::LaboratoryFrame> lab;
                if (state_.screen == ui::Screen::laboratory)
                    lab.emplace(ui::LaboratoryFrame{{canvas_.id(), canvas_.width(), canvas_.height()},
                                                    *lab_snapshot_,
                                                    lab_controls_,
                                                    input,
                                                    lab_snapshot_->step_ms,
                                                    lab_snapshot_->overload_seconds});
                ui::ArenaInput arena_input;
                const bool focused = (SDL_GetWindowFlags(window_.native()) & SDL_WINDOW_INPUT_FOCUS) != 0;
                std::optional<ui::ArenaFrame> arena;
                if (state_.screen == ui::Screen::gpu_mission) {
                    arena.emplace(ui::ArenaFrame{{canvas_.id(), canvas_.width(), canvas_.height()},
                                                 ui::ArenaView{simulation_->mission()},
                                                 arena_input,
                                                 focused});
                    arena->scene_mission = simulation_.scene().mission;
                    arena->paused = gpu_controls_.paused;
                    arena->lighting_quality = &lighting_quality_;
                    arena->camera = simulation_->camera_offset();
                    arena->clock = &clock_stats_;
                    arena->vfx_quality = &gpu_controls_.vfx_quality;
                }
                double ui_poll_ms = 0;
                gpu::PollTimings ui_poll;
                if (state_.screen == ui::Screen::gpu_mission && simulation_) {
                    const auto poll_started = gpu_profiling_.collect ? SDL_GetTicksNS() : std::uint64_t{0};
                    simulation_->poll();
                    arena->world = {simulation_->texture(), gpu::width * 3, gpu::height * 3};
                    arena->game = ui::ArenaView{simulation_->mission()};
                    if (gpu_profiling_.collect) {
                        ui_poll_ms = static_cast<double>(SDL_GetTicksNS() - poll_started) / 1e6;
                        ui_poll = simulation_->poll_timings();
                    }
                }
                ui::GpuInput gpu_input;
                std::optional<ui::GpuLaboratoryFrame> gpu_frame;
                if (state_.screen == ui::Screen::gpu_laboratory && simulation_) {
                    simulation_->poll();
                    gpu_frame.emplace(ui::GpuLaboratoryFrame{{simulation_->texture(), gpu::width * 3, gpu::height * 3},
                                                             simulation_->metrics(),
                                                             gpu_controls_,
                                                             gpu_input,
                                                             simulation_.scene().mission,
                                                             simulation_->mission(),
                                                             simulation_.recording()});
                }
                mark(frame_profile.poll_ms);
                if (gpu_frame) {
                    gpu_frame->camera = simulation_->camera_offset();
                    gpu_frame->thermal = &simulation_->thermal_totals();
                    gpu_frame->thermal_memory = simulation_->thermal_memory();
                    gpu_frame->inspection = &simulation_->thermal_inspection();
                    gpu_frame->thermal_scene = simulation_.scene().thermal_scene;
                    gpu_frame->clock = &clock_stats_;
                }
                if (gpu_frame && render_device_) {
                    gpu_frame->vfx = &render_device_->vfx.stats();
                    gpu_frame->vfx_memory = render_device_->vfx.memory_bytes();
                }
                const auto action = ui::draw(state_, info, images_, gui_.fonts(), lab ? &*lab : nullptr,
                                             arena ? &*arena : nullptr, gpu_frame ? &*gpu_frame : nullptr);
                if (state_.screen == ui::Screen::gpu_mission)
                    gpu_controls_.lighting.quality = static_cast<gpu::LightingQuality>(lighting_quality_);
                else
                    lighting_quality_ = static_cast<int>(gpu_controls_.lighting.quality);
                {
                    auto saved = state_.applied;
                    saved.lighting_quality = lighting_quality_;
                    saved.bloom = gpu_controls_.lighting.bloom;
                    saved.vignette = gpu_controls_.lighting.vignette;
                    saved.camera_shake = gpu_controls_.lighting.shake;
                    saved.aberration = gpu_controls_.lighting.aberration;
                    saved.water_quality = gpu_controls_.render_quality;
                    saved.effects_quality = gpu_controls_.vfx_quality;
                    if (saved != state_.applied) {
                        state_.applied = saved;
                        state_.draft = saved;
                        settings_dirty_ = true;
                    }
                    // Written once the slider or combo edit ends, not on every dragged frame.
                    if (!gui_.editing()) flush_settings();
                }
                vfx_options_.quality = static_cast<gpu::VfxQuality>(gpu_controls_.vfx_quality);
                if (render_device_) render_device_->vfx.set_options(vfx_options_);
                mark(frame_profile.ui_build_ms);
                if (session_smoke) {
                    if (scenario.injected_stroke) gpu_input.stroke = std::exchange(scenario.injected_stroke, {});
                    gpu_input.step = std::exchange(scenario.injected_step, false);
                }
                if (scenario.injected_brush) gpu_input.thermal_brush = *std::exchange(scenario.injected_brush, {});
                if (fault_screen_ == state_.screen) {
                    fault_screen_.reset();
                    throw std::runtime_error{"Wstrzyknięty błąd testowy trybu."};
                }
                if (simulation_) {
                    thermal_options_.enabled = gpu_controls_.thermal_enabled;
                    thermal_options_.radiation = gpu_controls_.thermal_radiation;
                    simulation_.set_thermal_options(thermal_options_);
                    simulation_->set_thermal_view(static_cast<gpu::ThermalView>(gpu_controls_.thermal_view));
                    simulation_->set_render_options({static_cast<gpu::RenderQuality>(gpu_controls_.render_quality),
                                                     false, gpu_controls_.lighting});
                    simulation_->set_vfx_options({static_cast<gpu::VfxQuality>(gpu_controls_.vfx_quality), false});
                    simulation_->set_background_texture(
                            static_cast<unsigned>(images_[static_cast<std::size_t>(state_.applied.map_theme)].handle));
                }
                if (gpu_frame) {
                    update_gpu(gpu_input, sim_seconds, focused || gpu_smoke || session_smoke || bench);
                }
                if (state_.screen == ui::Screen::laboratory) {
                    const auto updated = update_lab(input, sim_seconds);
                    if (!updated) throw std::runtime_error{updated.error()};
                } else {
                    if (lab_worker_) lab_worker_->set_active(false);
                    clock_.reset();
                    lab_controls_.previous.reset();
                }
                if (state_.screen == ui::Screen::gpu_mission && action != ui::Action::back) {
                    update_mission(arena_input, sim_seconds,
                                   focused || mission_smoke || session_smoke || mission_quick || bench,
                                   mission_smoke || session_smoke || mission_quick || bench);
                    if (mission_smoke) {
                        const auto& metrics = simulation_->metrics();
                        const bool timing_available = gpu::effective_timer_mode(gpu_profiling_) != gpu::TimerMode::off
                                                   && metrics.timing_samples > 0;
                        mission_frame = std::array<double, 12>{static_cast<double>(frames),
                                                               frame_ms,
                                                               metrics.physics_ms,
                                                               metrics.cpu_submit_ms,
                                                               simulation_.clock().dropped_seconds(),
                                                               ui_poll_ms,
                                                               ui_poll.timers_ms,
                                                               ui_poll.fence_ms,
                                                               ui_poll.read_ms,
                                                               mission_step_submit_ms_,
                                                               timing_available ? 1.0 : 0.0,
                                                               static_cast<double>(metrics.timing_samples)};
                    }
                } else if (state_.screen != ui::Screen::gpu_laboratory)
                    simulation_.suspend();
                if ((state_.screen == ui::Screen::gpu_mission || state_.screen == ui::Screen::gpu_laboratory)
                    && simulation_)
                    update_clock_stats(sim_seconds, static_cast<double>(frame_start) / 1e9);
                if (mission_smoke && simulation_) scenario.mission_clock_frame(*this, sim_seconds, frame_start);
                if (bench) record_benchmark_frame(frame_start, frame_ms);
                mark(frame_profile.update_ms);
                if (profile_frames) {
                    frame_profile.world_render_ms = world_render_submit_ms_;
                    frame_profile.update_ms -= world_render_submit_ms_;
                    frame_profile.mission_ticks = mission_ticks_this_frame_;
                    frame_profile.gpu_ticks = gpu_ticks_this_frame_;
                    frame_profile.mission_tick = simulation_ ? simulation_->metrics().steps : 0;
                    frame_profile.mission_pending_seconds = simulation_.clock().pending_seconds();
                    frame_profile.mission_dropped_seconds = simulation_.clock().dropped_seconds();
                }
                const bool duration_done =
                        window_seconds_ && static_cast<double>(frame_start - window_started) / 1e9 >= window_seconds_;
                const bool mission_final_frame = mission_smoke && (!window_seconds_ || duration_done)
                                              && simulation_->metrics().steps >= mission_smoke_min_ticks;
                // Stay inside the CTest TIMEOUT (60 s) and leave a progress report instead of being killed.
                if (mission_smoke && !mission_final_frame
                    && SDL_GetTicksNS() >= ScenarioRunner::mission_smoke_deadline_ns)
                    return std::unexpected{scenario.mission_deadline(*this)};
                if (gpu_input.save_recording) {
                    if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::capture);
                    const auto folder =
                            store_.path().parent_path() / "recordings" / ("gpu-" + std::to_string(SDL_GetTicksNS()));
                    const auto saved = simulation_.save_recording(folder);
                    if (saved) last_recording_ = folder / "session.txt";
                    message_ = saved ? "Zapisano nagranie w katalogu profilu: " + folder.filename().string()
                                     : saved.error();
                    mark(frame_profile.capture_ms);
                    if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::runtime);
                }
                if (gpu_input.replay_recording) {
                    if (last_recording_.empty())
                        message_ = "Najpierw zapisz nagranie w tej sesji.";
                    else {
                        const auto replayed = simulation_.replay_recording(last_recording_);
                        message_ = replayed ? "Odtworzono nagranie. Scena jest zatrzymana." : replayed.error();
                        if (replayed) {
                            gpu_controls_.paused = true;
                            gpu_controls_.scene =
                                    simulation_.scene().mission ? 12 : static_cast<int>(simulation_.scene().laboratory);
                        }
                    }
                    mark(frame_profile.capture_ms);
                }
                const bool capture_frame =
                        (gpu_smoke && (window_seconds_ ? duration_done : frames == 230)) || mission_final_frame;
                frame_profile.capture_performed =
                        capture_frame || gpu_input.save_recording || gpu_input.replay_recording;
                mark(frame_profile.metadata_ms);
                if (capture_frame) {
                    if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::capture);
                    if (gpu_smoke) simulation_->save_image(store_.path().parent_path() / "gpu-preview.bmp");
                    if (mission_smoke) simulation_->save_image(store_.path().parent_path() / "mission-preview.bmp");
                    if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::runtime);
                    mark(frame_profile.capture_ms);
                }
                window_.clear();
                gui_.render();
                if (simulation_) simulation_->invalidate_gl_state();
                mark(frame_profile.gui_render_ms);
                if (mission_final_frame) {
                    if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::capture);
                    int w{}, h{};
                    SDL_GetWindowSizeInPixels(window_.native(), &w, &h);
                    std::vector<std::uint32_t> capture(static_cast<std::size_t>(w) * h);
                    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, capture.data());
                    for (int y = 0; y < h / 2; ++y)
                        for (int x = 0; x < w; ++x) std::swap(capture[y * w + x], capture[(h - 1 - y) * w + x]);
                    auto* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, capture.data(), w * 4);
                    if (surface) {
                        const auto path = (store_.path().parent_path() / "mission-window.bmp").u8string();
                        SDL_SaveBMP(surface, reinterpret_cast<const char*>(path.c_str()));
                        SDL_DestroySurface(surface);
                    }
                }
                if (mission_final_frame) {
                    if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::runtime);
                    mark(frame_profile.capture_ms);
                }
                if ((smoke_test || gpu_smoke || mission_smoke || session_smoke || mission_quick || fault_smoke || bench)
                    && glGetError() != GL_NO_ERROR)
                    return std::unexpected{"Test renderowania wykrył błąd OpenGL."};
                mark(frame_profile.validation_ms);
                window_.present();
                mark(frame_profile.present_ms);
                frame_profile.rendered = true;
                if (action != ui::Action::none) perform(action);
                ++frames;
                if (smoke_test && frames >= ScenarioRunner::menu_smoke_frames) running_ = false;
                if (mission_quick && frames >= ScenarioRunner::mission_quick_frames) running_ = false;
                if (fault_smoke && frames >= ScenarioRunner::fault_smoke_frames) running_ = false;
                if (lab_smoke && frames >= 300) running_ = false;
                if (gpu_smoke && (window_seconds_ ? duration_done : frames >= 240)) running_ = false;
                if (mission_final_frame || (session_smoke && frames >= 130)) running_ = false;

                mark(frame_profile.actions_ms);
                if (!window_.vsync_enabled() && !unpaced_) {
                    const auto budget = static_cast<Uint64>(1'000'000'000 / state_.applied.frame_limit);
                    const auto elapsed = SDL_GetTicksNS() - frame_start;
                    if (elapsed < budget) SDL_DelayPrecise(budget - elapsed);
                }
                mark(frame_profile.sleep_ms);
                if (mission_frame) mission_frames.push_back(*mission_frame);
                if (profile_frames) append_frame_profile(frame_profile);
                if (window_test) scenario.heartbeat(*this, SDL_GetTicksNS());
            } catch (const std::exception& error) {
                if (!recovers_mode_errors()) return std::unexpected{error.what()};
                fail_mode(state_.screen, error.what());
                ++frames; // the interrupted iteration still advances scripted scenarios
            }
        }
        flush_settings();
        if (window_test) scenario.write_heartbeat(*this, "walidacja");
        if (auto exported = scenario.write_frames(*this); !exported) return std::unexpected{exported.error()};
        if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::validation);
        if (lab_worker_) {
            lab_worker_->stop();
            if (!lab_worker_->snapshot()->error.empty()) return std::unexpected{lab_worker_->snapshot()->error};
        }
        if (recording_) {
            recording_->finish();
            recording_.reset();
        }
        if (auto checked = scenario.finish(*this); !checked) {
            if (window_test) scenario.write_heartbeat(*this, "niezaliczony");
            return std::unexpected{checked.error()};
        }
        if (window_test) scenario.write_heartbeat(*this, "zakończony");
        write_gpu_startup();
        {
            std::ofstream vfx{store_.path().parent_path() / "vfx-state.txt"};
            vfx << "quality=" << gpu::vfx_quality_name(vfx_options_.quality)
                << "\nrender_device_created=" << bool(render_device_) << "\nworld_creations=" << simulation_.creations()
                << '\n';
            if (render_device_) {
                render_device_->vfx.poll();
                const auto& c = render_device_->vfx.stats();
                vfx << "generation=" << c.clock[0] << "\nsource_tick=" << c.clock[1]
                    << "\nbudget_limited_passes=" << c.clock[2] << "\nmalformed=" << c.clock[3] << "\n";
                for (unsigned kind = 0; kind < gpu::vfx_kinds; ++kind)
                    vfx << "emitted_" << gpu::vfx_kind_names[kind] << '=' << c.emitted[kind] << "\nrejected_"
                        << gpu::vfx_kind_names[kind] << '=' << c.rejected[kind] << '\n';
                vfx << "alive=" << c.counts[0] << "\nfree=" << c.counts[1] << "\npeak=" << c.counts[3]
                    << "\nmemory_bytes=" << render_device_->vfx.memory_bytes() << '\n';
            }
        }
        return 0;
    }

private:
    // --scene-benchmark: warm-up frames only advance the baselines; the measurement window
    // records one row per frame and ends the loop. GPU samples are drained every frame, so the
    // 10 000-entry queues of ParticleWorld never fill and every sample has a frame time.
    void record_benchmark_frame(std::uint64_t frame_start, double frame_ms) {
        auto& run = *benchmark_;
        // A mode failure returns to the menu; the scene fails instead of measuring the menu.
        if (state_.screen == ui::Screen::main_menu) {
            run.record.error = message_.empty() ? std::string{"tryb zamknięty przed końcem pomiaru"} : message_;
            running_ = false;
            return;
        }
        if (!benchmark_started_ns_) benchmark_started_ns_ = frame_start;
        const double elapsed = static_cast<double>(frame_start - benchmark_started_ns_) / 1e9;
        BenchmarkFrame row;
        row.sdl_ns = frame_start;
        row.frame_ms = frame_ms;
        const auto flags = SDL_GetWindowFlags(window_.native());
        row.window = {(flags & SDL_WINDOW_HIDDEN) == 0, (flags & SDL_WINDOW_INPUT_FOCUS) != 0,
                      (flags & SDL_WINDOW_MINIMIZED) != 0, (flags & SDL_WINDOW_OCCLUDED) != 0};
        // frame_ms is the time since the previous frame started, so the reload of a restart made in
        // the previous call shows up in this frame: flag this one, not the frame that requested it.
        row.route_reset = std::exchange(benchmark_route_reset_pending_, false);
        if (row.route_reset && run.measuring) ++run.record.route_resets;
        // The scripted mission route ends at scripted_mission_ticks; restart it so a long window
        // keeps measuring the route instead of an idle scene.
        if (run.scene.kind == BenchmarkScene::Kind::mission && simulation_
            && simulation_->metrics().steps >= gpu::scripted_mission_ticks) {
            simulation_.reset(simulation_.scene());
            benchmark_route_reset_pending_ = true;
        }
        double dropped = 0;
        if (state_.screen == ui::Screen::laboratory && lab_snapshot_) {
            const auto tick = lab_snapshot_->report.tick;
            row.ticks = static_cast<unsigned>(tick - std::min(tick, std::exchange(benchmark_lab_tick_, tick)));
            row.cpu_step_ms = lab_snapshot_->step_ms;
            dropped = lab_snapshot_->overload_seconds; // worker overrun, cumulative per scene
        } else if (simulation_) {
            row.ticks = mission_ticks_this_frame_ + gpu_ticks_this_frame_;
            row.pending_seconds = simulation_.clock().pending_seconds();
            dropped = simulation_.clock().dropped_seconds();
        }
        // A route restart resets the clock's counter; the difference never goes negative.
        row.dropped_seconds = std::max(0.0, dropped - std::exchange(benchmark_dropped_, dropped));
        const bool last = elapsed >= run.warmup_seconds + run.measure_seconds;
        if (simulation_) {
            if (last) {
                glFinish(); // completes the queries of ticks submitted inside the window
                simulation_->poll(false);
            }
            const auto& metrics = simulation_->metrics();
            const std::array drops{metrics.timings_dropped, metrics.presentation_timings_dropped,
                                   metrics.render_timings_dropped};
            auto physics = simulation_->take_timings();
            auto presentation = simulation_->take_presentation_timings();
            auto render = simulation_->take_render_timings();
            if (elapsed < run.warmup_seconds) return;
            if (!std::exchange(run.measuring, true)) {
                run.drops_at_start = drops; // samples completed during warm-up are discarded above
                return;
            }
            const auto append = [](std::vector<double>& all, const std::vector<double>& frame, unsigned& count,
                                   double& sum) {
                count = static_cast<unsigned>(frame.size());
                for (const double ms : frame) sum += ms;
                all.insert(all.end(), frame.begin(), frame.end());
            };
            auto& record = run.record;
            append(record.physics_gpu_ms, physics, row.physics_samples, row.physics_gpu_ms);
            append(record.presentation_gpu_ms, presentation, row.presentation_samples, row.presentation_gpu_ms);
            append(record.render_gpu_ms, render, row.render_samples, row.render_gpu_ms);
            record.physics_truncated = drops[0] != run.drops_at_start[0];
            record.presentation_truncated = drops[1] != run.drops_at_start[1];
            record.render_truncated = drops[2] != run.drops_at_start[2];
        } else {
            if (elapsed < run.warmup_seconds) return;
            if (!std::exchange(run.measuring, true)) return;
        }
        // The first measured frame only opens the window (GPU queues drained, baselines set).
        if (!run.measure_started_ns) run.measure_started_ns = frame_start;
        row.elapsed_seconds = static_cast<double>(frame_start - run.measure_started_ns) / 1e9;
        row.utc_unix_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
        run.record.frames.push_back(row);
        if (!last) return;
        run.completed = true;
        running_ = false;
    }
    // Written right after the first GPU world opens (a test killed later still leaves it)
    // and again at exit with the view switches.
    void write_gpu_startup() const {
        if (!simulation_) return;
        const auto cache = simulation_->shader_cache_stats();
        std::ofstream startup{store_.path().parent_path() / "gpu-startup.txt"};
        startup << "world_creations=" << simulation_.creations() << "\nprograms=" << cache.programs
                << "\nsource_builds=" << cache.source_builds << "\ncache_hits=" << cache.hits
                << "\ncache_misses=" << cache.misses << "\ncache_rejected=" << cache.rejected
                << "\ncache_write_failures=" << cache.write_failures << "\ncache_supported=" << cache.supported
                << "\ncompile_ms=" << cache.compile_ms << "\ncache_load_ms=" << cache.load_ms
                << "\nfirst_world_ms=" << first_world_ms_ << "\nview_switches=" << view_switches_
                << "\nview_switch_total_ms=" << view_switch_ms_ << "\nview_switch_max_ms=" << view_switch_max_ms_
                << "\ncpu_laboratory_created=" << bool(lab_worker_) << '\n';
    }
    // HUD pacing for the GPU views; restarts with every scene.
    void update_clock_stats(double sim_seconds, double wall) {
        if (simulation_.scene_epoch() != clock_epoch_) {
            clock_epoch_ = simulation_.scene_epoch();
            speed_meter_.reset();
            gpu_monitor_.reset();
            scene_seconds_ = lost_seconds_ = last_dropped_ = 0;
        }
        const auto& metrics = simulation_->metrics();
        const unsigned ticks = mission_ticks_this_frame_ + gpu_ticks_this_frame_;
        if (ticks > 0) scene_seconds_ += std::min(sim_seconds, sim::interactive_clock.max_frame_seconds);
        // suspend() restarts the clock's own counter; the HUD keeps the scene total.
        const double dropped = simulation_.clock().dropped_seconds();
        lost_seconds_ += dropped >= last_dropped_ ? dropped - last_dropped_ : dropped;
        last_dropped_ = dropped;
        speed_meter_.sample(wall, metrics.steps);
        const bool timed = metrics.timing_samples > 0;
        if (timed && metrics.timing_samples != last_timing_samples_)
            gpu_monitor_.sample(scene_seconds_, metrics.physics_ms); // physics only: lighting/thermal do not trip it
        last_timing_samples_ = metrics.timing_samples;
        clock_stats_ = {speed_meter_.speed(SimulationWorld::tick_seconds), lost_seconds_, timed ? metrics.gpu_ms : 0,
                        gpu_monitor_.slowed()};
    }
    void ensure_render_device() {
        if (render_device_ || !gpu_available_) return;
        render_device_ = std::make_shared<gpu::RenderDevice>(
                utf8_path(SDL_GetBasePath()) / "assets/shaders",
                shader_cache_ ? store_.path().parent_path() / "shader-cache" : std::filesystem::path{});
        render_device_->vfx.set_options(vfx_options_);
    }
    void update_mission(const ui::ArenaInput& input, double elapsed, bool focused, bool scripted) {
        if (input.toggle_pause) gpu_controls_.paused = !gpu_controls_.paused;
        if (input.reset) {
            simulation_.reset({true});
            gpu_controls_.scene = 12;
            gpu_controls_.paused = false;
            elapsed = 0;
        }
        const auto result =
                simulation_.advance({input.controls, {}, elapsed, focused, gpu_controls_.paused, false, scripted});
        mission_step_submit_ms_ = result.cpu_ms;
        mission_ticks_this_frame_ = result.ticks;
        if (gpu_profiling_.collect) world_render_submit_ms_ += result.render_ms;
    }
    void update_gpu(const ui::GpuInput& input, double elapsed, bool focused) {
        if (input.inspect) simulation_->request_thermal_inspection((*input.inspect)[0], (*input.inspect)[1]);
        if (input.begin_recording) {
            simulation_.begin_recording();
            elapsed = 0;
        }
        if (input.thermal_reset) {
            simulation_.reset({false, gpu::Scene::empty, static_cast<unsigned>(gpu_controls_.thermal_scene)});
            gpu_controls_.thermal_enabled = true;
            gpu_controls_.scene = 3;
            elapsed = 0;
        }
        if (input.select_scene || input.reset) {
            // Reset restores the loaded scene, so a thermal scene stays a thermal scene.
            simulation_.reset(input.select_scene
                                      ? SceneConfiguration{gpu_controls_.scene == 12,
                                                           static_cast<gpu::Scene>(std::min(gpu_controls_.scene, 11))}
                                      : simulation_.scene());
            gpu_controls_.lost_seconds = 0;
            gpu_controls_.previous.reset();
            elapsed = 0;
        }
        const auto result = simulation_.advance(
                {{}, input.stroke, elapsed, focused, gpu_controls_.paused, input.step, false, input.thermal_brush});
        gpu_controls_.lost_seconds = simulation_.clock().dropped_seconds();
        gpu_ticks_this_frame_ = result.ticks;
        if (gpu_profiling_.collect) world_render_submit_ms_ += result.render_ms;
    }
    std::expected<void, std::string> upload_world(const sim::World& world) {
        last_lab_pixels_.reset();
        world.render(std::span<sim::Color, sim::World::cell_count>{pixels_.data(), pixels_.size()}, false,
                     lab_controls_.view);
        return canvas_.update_rgba(std::as_bytes(std::span{pixels_}));
    }
    std::expected<void, std::string> update_lab(const ui::LaboratoryInput& input, double elapsed) {
        if (lab_worker_) {
            sim::LaboratoryControl control{true, lab_controls_.paused, lab_controls_.speed, lab_controls_.view,
                                           input.probe};
            sim::LaboratoryCommand command;
            if (input.reset) command.scene = lab_controls_.scene;
            command.clear = input.clear;
            command.toggle_boundary = input.boundary_toggle;
            command.toggle_recording = input.record_toggle;
            command.manual_steps = input.single_step ? 1 : 0;
            command.stroke = input.stroke;
            if (input.heat) command.heat = sim::HeatSource{*input.heat, lab_controls_.tool == 1 ? 100.0 : -100.0};
            const bool event = !command.empty();
            const auto ticket = lab_worker_->submit(control, std::move(command));
            if (event && ticket == 0) message_ = "Kolejka edycji jest pełna. Zatrzymaj pędzel na chwilę.";
            return {};
        }
        if (input.reset || input.clear) {
            if (recording_) {
                recording_->finish();
                recording_.reset();
                lab_controls_.recording = false;
            }
            if (input.reset)
                world_->load_scene(lab_controls_.scene);
            else
                world_->clear();
            clock_.reset();
            lab_controls_.previous.reset();
            elapsed = 0;
        }
        if (input.boundary_toggle) world_->set_open_top(!world_->config().open_top);
        if (input.heat) world_->inject_heat(*input.heat, lab_controls_.tool == 1 ? 100 : -100);
        if (input.record_toggle) {
            try {
                if (recording_) {
                    recording_->finish();
                    recording_.reset();
                } else
                    recording_.emplace(store_.path().parent_path() / "recordings" / std::to_string(SDL_GetTicksNS()));
                lab_controls_.recording = recording_.has_value();
            } catch (const std::exception& e) {
                return std::unexpected{e.what()};
            }
        }
        if (input.stroke) {
            const auto& stroke = *input.stroke;
            world_->paint(stroke.from, stroke.to, stroke.radius, stroke.material, stroke.erase);
        }
        const int steps = clock_.advance(elapsed, lab_controls_.paused, lab_controls_.speed,
                                         input.single_step || std::exchange(smoke_single_step_, false));
        for (int i = 0; i < steps; ++i) {
            const auto start = SDL_GetPerformanceCounter();
            world_->step();
            step_ms_ = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0
                     / static_cast<double>(SDL_GetPerformanceFrequency());
            if (recording_) {
                try {
                    recording_->capture(*world_, step_ms_, lab_controls_.view);
                } catch (const std::exception& e) {
                    return std::unexpected{e.what()};
                }
            }
        }
        auto snapshot = sim::present_world(*world_, lab_controls_.view, input.probe);
        snapshot->step_ms = step_ms_;
        snapshot->recording = recording_.has_value();
        lab_snapshot_ = std::move(snapshot);
        const auto uploaded = upload_world(*world_);
        if (uploaded) ++uploads_;
        return uploaded;
    }

    [[nodiscard]] bool recovers_mode_errors() const {
        return smoke_ == SmokeScenario::none || smoke_ == SmokeScenario::mode_faults;
    }
    void log_error(const std::string& text) const {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str());
        std::ofstream log{store_.path().parent_path() / "errors.log", std::ios::app};
        log << std::format("{:%F %T} {}\n", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
                           text);
    }
    // HUD changes: experimental options raised in the HUD stay in this session until confirmed.
    void flush_settings() {
        if (!std::exchange(settings_dirty_, false)) return;
        write_settings(core::confirmed_settings(state_.applied, state_.stored));
    }
    bool write_settings(const core::Settings& settings) {
        if (settings == state_.stored) return true;
        if (const auto written = store_.save(settings); !written) {
            message_ = written.error() + " Zmiany działają w bieżącej sesji.";
            return false;
        }
        state_.stored = settings;
        return true;
    }
    // Releases the failed mode and its resources and returns to the menu.
    void fail_mode(ui::Screen mode, std::string_view what) {
        gui_.abandon_frame();
        log_error(std::format("{}: {}", mode_name(mode), what));
        try {
            switch (mode) {
            case ui::Screen::laboratory:
                lab_worker_.reset();
                world_.reset();
                recording_.reset();
                lab_snapshot_.reset();
                last_lab_pixels_.reset();
                lab_controls_.recording = false;
                lab_controls_.previous.reset();
                clock_.reset();
                break;
            case ui::Screen::gpu_laboratory:
            case ui::Screen::gpu_mission:
                simulation_.close();
                if (render_device_) render_device_->vfx.reset();
                gpu_controls_.previous.reset();
                break;
            default: break;
            }
        } catch (const std::exception& error) {
            log_error(std::format("{}: zwalnianie trybu: {}", mode_name(mode), error.what()));
        }
        state_.screen = ui::Screen::main_menu;
        state_.draft = state_.applied;
        message_ = std::format("Tryb „{}” zgłosił błąd i został zamknięty (wpis w errors.log w katalogu profilu): {}",
                               mode_name(mode), what);
        ++mode_failures_;
    }

    bool perform(ui::Action action) {
        try {
            return perform_action(action);
        } catch (const std::exception& error) {
            if (!recovers_mode_errors()) throw;
            fail_mode(target_screen(action, state_.screen), error.what());
            return false;
        }
    }
    bool perform_action(ui::Action action) {
        switch (action) {
        case ui::Action::none: break;
        case ui::Action::open_mission:
        case ui::Action::open_gpu: {
            const bool mission = action == ui::Action::open_mission;
            if (!gpu_available_) {
                message_ = std::format("„{}” wymaga OpenGL 4.3, a ten kontekst to OpenGL {}. Laboratorium CPU i "
                                       "ustawienia działają normalnie.",
                                       mode_name(mission ? ui::Screen::gpu_mission : ui::Screen::gpu_laboratory),
                                       gl_version_);
                return false;
            }
            const bool existing = bool(simulation_);
            const auto open_started = SDL_GetTicksNS();
            ensure_render_device();
            if (render_device_) render_device_->vfx.set_options(vfx_options_);
            if (state_.screen != ui::Screen::gpu_laboratory && state_.screen != ui::Screen::gpu_mission
                && render_device_)
                render_device_->vfx.reset();
            const auto opened = simulation_.open(
                    utf8_path(SDL_GetBasePath()) / "assets" / "shaders", execution_, gpu_profiling_, {mission},
                    shader_cache_ ? store_.path().parent_path() / "shader-cache" : std::filesystem::path{},
                    render_device_);
            if (!opened) {
                message_ = opened.error();
                return false;
            }
            const double open_ms = static_cast<double>(SDL_GetTicksNS() - open_started) / 1e6;
            if (existing) {
                ++view_switches_;
                view_switch_ms_ += open_ms;
                view_switch_max_ms_ = std::max(view_switch_max_ms_, open_ms);
            } else {
                first_world_ms_ = open_ms;
                write_gpu_startup();
            }
            simulation_.set_thermal_options(thermal_options_);
            if (!existing && initial_scene_ && !mission) simulation_.reset(*std::exchange(initial_scene_, {}));
            simulation_->set_actor_textures(static_cast<unsigned>(parts_.handle),
                                            static_cast<unsigned>(weapons_.handle), weapons_.width, weapons_.height);
            simulation_->set_render_options(
                    {static_cast<gpu::RenderQuality>(gpu_controls_.render_quality), false, gpu_controls_.lighting});
            simulation_->set_vfx_options({static_cast<gpu::VfxQuality>(gpu_controls_.vfx_quality), false});
            simulation_->set_background_texture(
                    static_cast<unsigned>(images_[static_cast<std::size_t>(state_.applied.map_theme)].handle));
            gpu_controls_.scene = simulation_.scene().mission ? 12 : static_cast<int>(simulation_.scene().laboratory);
            gpu_controls_.previous.reset();
            state_.screen = mission ? ui::Screen::gpu_mission : ui::Screen::gpu_laboratory;
            message_.clear();
            break;
        }
        case ui::Action::open_laboratory:
            if (!world_ && !lab_worker_) {
                lab_worker_ = std::make_unique<sim::LaboratoryRunner>(store_.path().parent_path() / "recordings",
                                                                      lab_controls_.scene, physics_);
                lab_snapshot_ = lab_worker_->snapshot();
            }
            state_.screen = ui::Screen::laboratory;
            clock_.reset();
            message_.clear();
            break;
        case ui::Action::open_preview:
            state_.screen = ui::Screen::preview;
            message_.clear();
            break;
        case ui::Action::open_settings:
            state_.draft = state_.applied;
            state_.screen = ui::Screen::settings;
            message_.clear();
            break;
        case ui::Action::open_about:
            state_.screen = ui::Screen::about;
            message_.clear();
            break;
        case ui::Action::back:
            flush_settings();
            simulation_.suspend();
            gpu_controls_.previous.reset();
            state_.draft = state_.applied;
            state_.screen = ui::Screen::main_menu;
            message_.clear();
            break;
        case ui::Action::reset:
            state_.draft = core::Settings{};
            message_.clear();
            break;
        case ui::Action::quit:
            flush_settings();
            running_ = false;
            break;
        case ui::Action::confirm_settings:
            settings_dirty_ = false;
            if (!write_settings(state_.applied)) return false;
            message_ = "Ustawienia eksperymentalne zapisano na stałe.";
            break;
        case ui::Action::apply:
        case ui::Action::apply_session: {
            if (const auto checked = core::validate(state_.draft); !checked) {
                message_ = checked.error();
                return false;
            }
            if (const auto changed = window_.apply_video(state_.draft, state_.applied); !changed) {
                message_ = changed.error();
                return false;
            }
            state_.applied = state_.draft;
            gpu_controls_.lighting.quality = static_cast<gpu::LightingQuality>(state_.applied.lighting_quality);
            lighting_quality_ = state_.applied.lighting_quality;
            gpu_controls_.lighting.bloom = state_.applied.bloom;
            gpu_controls_.lighting.vignette = state_.applied.vignette;
            gpu_controls_.lighting.shake = state_.applied.camera_shake;
            gpu_controls_.lighting.aberration = state_.applied.aberration;
            gpu_controls_.render_quality = state_.applied.water_quality;
            gpu_controls_.vfx_quality = state_.applied.effects_quality;
            state_.diagnostics = state_.applied.show_diagnostics;
            settings_dirty_ = false;
            const bool session_only = action == ui::Action::apply_session;
            if (!write_settings(session_only ? core::confirmed_settings(state_.applied, state_.stored)
                                             : state_.applied))
                return false;
            message_ = session_only ? "Ustawienia zapisano; opcje eksperymentalne działają tylko w tej sesji."
                                    : "Ustawienia zostały zapisane.";
            break;
        }
        }
        return true;
    }

    platform::Window& window_;
    ui::Session& gui_;
    const platform::SettingsStore& store_;
    std::array<ui::Image, 3> images_;
    platform::Texture& canvas_;
    ui::Image soldier_;
    ui::Image parts_, weapons_;
    ui::ViewState state_;
    bool running_{true}, unpaced_{};
    std::string message_;
    std::string renderer_;
    std::string gl_version_;
    bool gpu_available_{};
    bool settings_dirty_{};
    unsigned mode_failures_{};
    std::optional<ui::Screen> fault_screen_; // test hook: next frame on this screen throws
    std::unique_ptr<sim::World> world_;
    std::unique_ptr<sim::LaboratoryRunner> lab_worker_;
    std::shared_ptr<const sim::LaboratorySnapshot> lab_snapshot_;
    std::shared_ptr<const std::vector<sim::Color>> last_lab_pixels_;
    sim::FixedClock clock_{sim::interactive_clock};
    ui::LaboratoryControls lab_controls_;
    std::optional<sim::Recording> recording_;
    std::vector<sim::Color> pixels_ = std::vector<sim::Color>(sim::World::cell_count);
    double step_ms_{};
    unsigned uploads_{};
    bool smoke_single_step_{}, smoke_lab_ok_{true};
    std::uint64_t smoke_paused_tick_{};
    sim::PhysicsConfig physics_;
    std::shared_ptr<gpu::RenderDevice> render_device_;
    gpu::VfxOptions vfx_options_;
    SimulationWorld simulation_{sim::interactive_clock};
    sim::VsyncSnap vsync_snap_;
    float refresh_hz_{};
    SpeedMeter speed_meter_;
    GpuSlowdownMonitor gpu_monitor_;
    ui::ClockStats clock_stats_;
    unsigned clock_epoch_{};
    double scene_seconds_{}, lost_seconds_{}, last_dropped_{};
    std::uint64_t last_timing_samples_{};
    gpu::ProfileOptions gpu_profiling_;
    gpu::ExecutionOptions execution_;
    platform::GlDebugLog* gl_debug_{};
    bool shader_cache_{};
    unsigned view_switches_{};
    double first_world_ms_{}, view_switch_ms_{}, view_switch_max_ms_{};
    std::filesystem::path last_recording_;
    double mission_step_submit_ms_{}, world_render_submit_ms_{};
    unsigned mission_ticks_this_frame_{}, gpu_ticks_this_frame_{};
    ui::GpuControls gpu_controls_;
    int lighting_quality_{};
    gpu::ThermalOptions thermal_options_;
    StartMode start_{StartMode::menu};
    SmokeScenario smoke_{SmokeScenario::none};
    std::optional<SceneConfiguration> initial_scene_; // --scene / benchmark / --gpu-thermal-scene, first GPU open
    unsigned window_seconds_{};
    BenchmarkRun* benchmark_{};
    std::uint64_t benchmark_started_ns_{}, benchmark_lab_tick_{};
    double benchmark_dropped_{};
    bool benchmark_route_reset_pending_{};
};

std::int64_t utc_unix_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}

// --scene-benchmark: a fresh Application (world, clocks, UI state) per scene, in one window and GL context.
template<class RunScene>
std::expected<int, std::string> run_scene_benchmark(const LaunchOptions& options, const LaunchConfig& base,
                                                    const platform::Window& window,
                                                    const std::filesystem::path& profile, RunScene run_scene) {
    const double seconds = options.benchmark_seconds;
    const double warmup = std::clamp(seconds / 6, 1.0, 10.0);
    std::ofstream frames_csv{profile / "scene-benchmark-frames.csv"};
    std::ofstream summary_csv{profile / "scene-benchmark.csv"};
    std::ofstream info{profile / "scene-benchmark-info.txt"};
    if (!frames_csv || !summary_csv || !info) return std::unexpected{"Nie można zapisać wyników benchmarku scen."};
    const auto anchor_sdl = SDL_GetTicksNS();
    const auto anchor_utc = utc_unix_ns();
    const auto anchor_sdl_after = SDL_GetTicksNS();
    const auto presentation = window.presentation_info();
    const auto flags = SDL_GetWindowFlags(window.native());
    int logical_width = 0, logical_height = 0;
    SDL_GetWindowSize(window.native(), &logical_width, &logical_height);
#ifdef NDEBUG
    constexpr bool debug_build = false;
#else
    constexpr bool debug_build = true;
#endif
    info << "schema=2\nbuild_version=" << build::version << "\nndebug=" << !debug_build
         << "\ngl_vendor=" << gl_string(GL_VENDOR) << "\ngl_renderer=" << gl_string(GL_RENDERER)
         << "\ngl_version=" << gl_string(GL_VERSION) << "\nvsync_requested=" << options.benchmark_vsync
         << "\nvsync_enabled=" << window.vsync_enabled() << "\nswap_interval=" << presentation.swap_interval
         << "\nrefresh_hz=" << window.refresh_hz() << "\nwindow_pixels=" << presentation.pixel_width << 'x'
         << presentation.pixel_height << "\nwindow_size=" << logical_width << 'x' << logical_height
         << "\ndisplay_scale=" << SDL_GetWindowDisplayScale(window.native())
         << "\nwindow_visible=" << ((flags & SDL_WINDOW_HIDDEN) == 0)
         << "\nwindow_focused=" << ((flags & SDL_WINDOW_INPUT_FOCUS) != 0)
         << "\nwindow_minimized=" << ((flags & SDL_WINDOW_MINIMIZED) != 0)
         << "\nwindow_occluded=" << ((flags & SDL_WINDOW_OCCLUDED) != 0)
         << "\nwater_quality=" << static_cast<int>(base.render.quality)
         << "\nlighting_quality=" << static_cast<int>(base.render.lighting.quality)
         << "\nvfx_quality=" << static_cast<int>(base.vfx.quality)
         << "\nthermal=" << (base.thermal.enabled ? "on" : "off_except_thermal_scenes")
         << "\nphysics_threads=" << base.physics.worker_threads << "\nsettings=defaults_core_Settings_no_settings_ini"
         << "\nframe_limiter="
         << (base.unpaced             ? "off"
             : window.vsync_enabled() ? "vsync"
                                      : "frame_limit")
         << "\ngpu_timer_mode=" << gpu::timer_mode_name(gpu::effective_timer_mode(base.profiling))
         << "\nwarmup_seconds=" << warmup << "\nmeasure_seconds=" << seconds << "\nrepeat=" << options.benchmark_repeat
         << "\nloop_minutes=" << options.benchmark_loop_minutes << "\nscenes=" << options.benchmark_scenes.size()
         << "\nanchor_sdl_ns=" << anchor_sdl << "\nanchor_utc_unix_ns=" << anchor_utc
         << "\nanchor_sdl_after_ns=" << anchor_sdl_after
         << "\nutc_unix_ns=system_clock_after_frame_update_same_epoch_as_GpuTelemetry_csv"
         << "\nphysics_gpu_ms=commands_solver_diagnostics_thermal_tick"
         << "\npresentation_gpu_ms=thermal_presentation_lighting_tick_vfx_update_event_submit"
         << "\nrender_gpu_ms=world_render_pass\ngpu_distributions=all_completed_timer_queries_in_window"
         << "\nlab_scenes=cpu_worker_thread_no_gpu_physics" << "\nmission=scripted_route_restarted_every_"
         << gpu::scripted_mission_ticks << "_ticks"
         << "\nstatus=ok|invalid(window_hidden_minimized_or_occluded)|failed\n";
    frames_csv << benchmark_frames_header << '\n';
    summary_csv << benchmark_summary_header << '\n';
    int failures = 0, measured = 0;
    const auto loop_started = SDL_GetTicksNS();
    const bool looping = options.benchmark_repeat > 1 || options.benchmark_loop_minutes;
    // One process for every pass: a process restart (fresh driver state, idle gap) cannot hide a slow regime.
    for (unsigned pass = 1;; ++pass) {
        if (options.benchmark_loop_minutes) {
            const double minutes = static_cast<double>(SDL_GetTicksNS() - loop_started) / 6e10;
            if (pass > 1 && minutes >= options.benchmark_loop_minutes) break;
        } else if (pass > options.benchmark_repeat)
            break;
        if (looping) std::cout << std::format("Przebieg {}\n", pass) << std::flush;
        for (const auto& scene : options.benchmark_scenes) {
            BenchmarkRun run;
            run.scene = scene;
            run.warmup_seconds = warmup;
            run.measure_seconds = seconds;
            auto config = base;
            config.benchmark = &run;
            config.thermal_scene = 0;
            config.gpu_scene.reset();
            config.lab_scene.reset();
            switch (scene.kind) {
            case BenchmarkScene::Kind::gpu:
                config.start = StartMode::gpu_laboratory;
                config.gpu_scene = scene.index;
                break;
            case BenchmarkScene::Kind::mission: config.start = StartMode::mission; break;
            case BenchmarkScene::Kind::thermal:
                config.start = StartMode::gpu_laboratory;
                config.thermal_scene = scene.index;
                config.thermal.enabled = true;
                break;
            case BenchmarkScene::Kind::lab:
                config.start = StartMode::cpu_laboratory;
                config.lab_scene = scene.index;
                break;
            }
            const auto id = scene.id();
            // One failing scene is reported in its row; the remaining scenes are still measured.
            try {
                const auto result = run_scene(config);
                if (result && *result != 0) return *result; // e.g. 77: no OpenGL 4.3 for any scene
                if (!result && run.record.error.empty()) run.record.error = result.error();
            } catch (const std::exception& error) {
                run.record.error = error.what();
            }
            if (run.record.error.empty() && !run.completed) run.record.error = "pomiar przerwany przed końcem okna";
            for (const auto& frame : run.record.frames) frames_csv << benchmark_frame_row(id, pass, frame) << '\n';
            const bool gpu_physics = scene.kind != BenchmarkScene::Kind::lab;
            auto summary =
                    summarize(id, std::move(run.record), gpu_physics ? 1.0 / SimulationWorld::tick_seconds : 0.0);
            summary.pass = pass;
            failures += summary.status != BenchmarkStatus::ok;
            ++measured;
            summary_csv << benchmark_summary_row(summary) << '\n';
            std::cout << benchmark_summary_line(summary) << std::endl;
        }
        // Rows of finished passes survive a later crash or a TIMEOUT kill.
        frames_csv.flush();
        summary_csv.flush();
    }
    if (!frames_csv.flush() || !summary_csv.flush() || !info.flush())
        return std::unexpected{"Niepełny zapis wyników benchmarku scen."};
    if (failures) {
        std::cerr << std::format("Benchmark scen: {} z {} scen ze statusem innym niż ok (kolumna status w "
                                 "scene-benchmark.csv).\n",
                                 failures, measured);
        return 1;
    }
    return 0;
}

} // namespace

std::expected<int, std::string> run(const LaunchOptions& options, const DiagnosticRunner& diagnostics) {
    SDL_SetAppMetadata("Strefa Zero", build::version.data(), "local.strefazero.prototype");
    auto system = platform::SdlSystem::create();
    if (!system) return std::unexpected{system.error()};
    auto profile = options.profile;
    if (profile.empty()) {
        using SdlString = std::unique_ptr<char, decltype(&SDL_free)>;
        SdlString path{SDL_GetPrefPath("StrefaZero", "Prototype"), SDL_free};
        if (!path) return std::unexpected{"Nie można ustalić katalogu ustawień."};
        profile = utf8_path(path.get());
    }
    platform::SettingsStore store{profile / "settings.ini"};
    const bool testing = options.is_test();
    if (testing) std::filesystem::create_directories(profile);
    auto loaded = store.load();
    std::string message = loaded ? std::string{} : loaded.error() + " Użyto ustawień domyślnych.";
    // A smoke run starts from known defaults and only writes its dedicated profile.
    auto settings = testing ? core::Settings{} : loaded.value_or(core::Settings{});
    auto render_options = options.gpu_render;
    if (!options.lighting_quality_set)
        render_options.lighting.quality = static_cast<gpu::LightingQuality>(settings.lighting_quality);
    render_options.lighting.bloom = settings.bloom;
    render_options.lighting.vignette = settings.vignette;
    render_options.lighting.shake = settings.camera_shake;
    render_options.lighting.aberration = settings.aberration;
    if (!options.render_quality_set) render_options.quality = static_cast<gpu::RenderQuality>(settings.water_quality);
    auto vfx_options = options.gpu_vfx;
    if (!options.vfx_quality_set) vfx_options.quality = static_cast<gpu::VfxQuality>(settings.effects_quality);
    if (options.forces_test_pacing()) {
        settings.vsync = false;
        settings.frame_limit = 60;
    }
    if (options.test_vsync.has_value()) settings.vsync = *options.test_vsync;
    const bool scene_benchmark = !options.benchmark_scenes.empty();
    if (scene_benchmark) settings.vsync = options.benchmark_vsync;
    if (options.test_frame_limit.has_value()) settings.frame_limit = static_cast<int>(*options.test_frame_limit);
    if (options.test_window_size) {
        settings.width = (*options.test_window_size)[0];
        settings.height = (*options.test_window_size)[1];
    }
    auto window = platform::Window::create(settings, !options.legacy_graphics, options.gl_debug);
    if (!window) return std::unexpected{window.error()};
    // The scene benchmark measures what a player sees: a hidden window takes a different presentation path.
    if (testing && (options.show_test_window || scene_benchmark)) {
        window->show();
        SDL_RaiseWindow(window->native());
    }
    bool debug_log = options.gl_debug;
#ifndef NDEBUG
    debug_log = true;
#endif
    auto gl_debug = debug_log ? platform::GlDebugLog::create(profile / "gl-debug.txt") : nullptr;
    settings.vsync = window->vsync_enabled();
    if (!window->warning().empty()) message = window->warning();
    const char* base = SDL_GetBasePath();
    if (!base) return std::unexpected{"Nie można ustalić katalogu aplikacji."};
    const auto assets = utf8_path(base) / "assets";
    if (options.requires_gl43() && !platform::context_has_gl43()) {
        const int code = platform::missing_gl43_exit_code();
        std::ofstream{profile / "gpu-skipped.txt"}
                << (code == 77 ? "OpenGL 4.3 unavailable; GPU tests skipped, CPU mode remains supported.\n"
                               : "OpenGL 4.3 unavailable; STREFA_REQUIRE_GL43=1, test failed.\nrequired=1\n");
        return code;
    }
    if (testing && !options.is_window_smoke() && gl_debug) gl_debug->set_phase(platform::DebugPhase::validation);
    if (diagnostics)
        if (auto diagnostic = diagnostics(options, assets, profile)) return *diagnostic;
    auto gui = ui::Session::create(*window, assets);
    if (!gui) return std::unexpected{gui.error()};
    std::array<std::optional<platform::Texture>, 3> textures;
    std::array<ui::Image, 3> images{};
    constexpr std::array filenames{"forest.bmp", "industrial.bmp", "winter.bmp"};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        auto loaded_texture = platform::Texture::load_bmp(assets / "backgrounds" / filenames[i]);
        if (loaded_texture) {
            textures[i].emplace(std::move(*loaded_texture));
            images[i] = {textures[i]->id(), textures[i]->width(), textures[i]->height()};
        } else
            message = loaded_texture.error();
    }
    std::vector<sim::Color> blank(sim::World::cell_count);
    auto canvas =
            platform::Texture::create_rgba(sim::World::width, sim::World::height, std::as_bytes(std::span{blank}));
    if (!canvas) return std::unexpected{canvas.error()};
    auto soldier = platform::Texture::load_bmp(assets / "actors" / "soldier_pl.bmp");
    if (!soldier) return std::unexpected{soldier.error()};
    if (soldier->width() != 128 || soldier->height() != 192)
        return std::unexpected{"Atlas żołnierza wymaga rozmiaru 128 × 192."};
    auto parts = platform::Texture::load_bmp(assets / "actors" / "parts.bmp");
    auto weapons = platform::Texture::load_bmp(assets / "weapons" / "weapons.bmp");
    if (!parts || !weapons) return std::unexpected{"Brak atlasu części postaci lub broni."};
    if (!testing) window->show();
    sim::PhysicsConfig physics;
    physics.worker_threads = options.physics_threads;
    const ApplicationAssets application_assets{images,
                                               *canvas,
                                               {soldier->id(), soldier->width(), soldier->height()},
                                               {parts->id(), parts->width(), parts->height()},
                                               {weapons->id(), weapons->width(), weapons->height()}};
    const LaunchConfig config{.physics = physics,
                              .profiling = options.gpu_profiling,
                              .execution = options.gpu_execution,
                              .gl_debug = gl_debug.get(),
                              .shader_cache = options.shader_cache,
                              .render = render_options,
                              .vfx = vfx_options,
                              .unpaced = options.test_unpaced || (scene_benchmark && !options.benchmark_vsync),
                              .thermal = options.gpu_thermal,
                              .thermal_scene = options.thermal_scene,
                              .test_seconds = options.test_seconds,
                              .gpu_scene = options.gpu_scene,
                              .lab_scene = options.lab_scene,
                              .start = options.start_mode(),
                              .smoke = options.smoke_scenario()};
    if (scene_benchmark) {
        auto result = run_scene_benchmark(options, config, *window, profile, [&](const LaunchConfig& scene) {
            Application application{*window, *gui, store, settings, application_assets, message, scene};
            return application.run();
        });
        if (gl_debug) gl_debug->set_phase(platform::DebugPhase::shutdown);
        return result;
    }
    Application application{*window, *gui, store, settings, application_assets, std::move(message), config};
    auto result = application.run();
    if (gl_debug) gl_debug->set_phase(platform::DebugPhase::shutdown);
    return result;
}

} // namespace strefa::app
