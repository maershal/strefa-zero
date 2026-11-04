#pragma once
// Per-run configuration of Application and the --scene-benchmark driver (application.cpp,
// scene_benchmark_run.cpp); not part of the launch_options parser.
#include "app/application.hpp"
#include "app/scene_benchmark.hpp"
#include "sim/physics.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace strefa::platform {
class GlDebugLog;
class Window;
} // namespace strefa::platform

namespace strefa::app {

// One --scene-benchmark scene: settings in, collected frames and GPU samples out.
struct BenchmarkRun {
    BenchmarkScene scene;
    double warmup_seconds{}, measure_seconds{};
    BenchmarkRecord record;
    std::uint64_t measure_started_ns{};
    std::array<std::uint64_t, 3> drops_at_start{}; // Metrics::*_timings_dropped when the window opened
    bool measuring{}, completed{};
    bool quit{}; // SDL_EVENT_QUIT or a window close: the whole benchmark stops after this scene
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

// SDL flags of the window plus, on Windows, IsIconic and DWM cloaking.
[[nodiscard]] WindowState benchmark_window_state(const platform::Window&);

// --scene-benchmark: a fresh Application (world, clocks, UI state) per scene, in one window and GL context.
using BenchmarkSceneRunner = std::function<std::expected<int, std::string>(const LaunchConfig&)>;
std::expected<int, std::string> run_scene_benchmark(const LaunchOptions&, const LaunchConfig& base,
                                                    const platform::Window&, const std::filesystem::path& profile,
                                                    const BenchmarkSceneRunner& run_scene);

} // namespace strefa::app
