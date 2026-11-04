#include "app/launch_config.hpp"
#include "app/simulation_world.hpp"
#include "build_info.hpp"
#include "gpu/world.hpp"
#include "platform/window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>

namespace strefa::app {
namespace {
std::string gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unknown";
}
std::int64_t utc_unix_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}
} // namespace

WindowState benchmark_window_state(const platform::Window& window) {
    const auto flags = SDL_GetWindowFlags(window.native());
    const auto shell = window.shell_state();
    return {(flags & SDL_WINDOW_HIDDEN) == 0, (flags & SDL_WINDOW_INPUT_FOCUS) != 0,
            (flags & SDL_WINDOW_MINIMIZED) != 0 || shell.iconic, (flags & SDL_WINDOW_OCCLUDED) != 0, shell.cloaked};
}

std::expected<int, std::string> run_scene_benchmark(const LaunchOptions& options, const LaunchConfig& base,
                                                    const platform::Window& window,
                                                    const std::filesystem::path& profile,
                                                    const BenchmarkSceneRunner& run_scene) {
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
         << "\nwindow_iconic=" << window.shell_state().iconic << "\nwindow_cloaked=" << window.shell_state().cloaked
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
         << "\nstatus=ok|invalid(window_hidden_minimized_occluded_or_cloaked)|failed\n";
    frames_csv << benchmark_frames_header << '\n';
    summary_csv << benchmark_summary_header << '\n';
    // The environment is on disk before the first scene: a crash or a TIMEOUT kill still leaves it.
    if (!info.flush() || !frames_csv.flush() || !summary_csv.flush())
        return std::unexpected{"Nie można zapisać wyników benchmarku scen."};
    bool quit = false;
    int failures = 0, measured = 0;
    const auto loop_started = SDL_GetTicksNS();
    const bool looping = options.benchmark_repeat > 1 || options.benchmark_loop_minutes;
    // One process for every pass: a process restart (fresh driver state, idle gap) cannot hide a slow regime.
    for (unsigned pass = 1; !quit; ++pass) {
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
            quit = run.quit;
            if (quit && !run.completed) run.record.error = "przerwany zamknięciem okna (QUIT)";
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
            if (quit) break;
        }
        // Rows of finished passes survive a later crash or a TIMEOUT kill.
        frames_csv.flush();
        summary_csv.flush();
    }
    info << "result=" << (quit ? "quit" : "completed") << "\nscenes_measured=" << measured << '\n';
    if (!frames_csv.flush() || !summary_csv.flush() || !info.flush())
        return std::unexpected{"Niepełny zapis wyników benchmarku scen."};
    if (quit) {
        std::cerr << std::format("Benchmark scen przerwany zamknięciem okna po {} scenach.\n", measured);
        return 1;
    }
    if (failures) {
        std::cerr << std::format("Benchmark scen: {} z {} scen ze statusem innym niż ok (kolumna status w "
                                 "scene-benchmark.csv).\n",
                                 failures, measured);
        return 1;
    }
    return 0;
}

} // namespace strefa::app
