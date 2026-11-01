#pragma once
#include "gpu/diagnostics.hpp"
#include "gpu/profiling.hpp"
#include "gpu/render_options.hpp"
#include "gpu/thermal.hpp"
#include "gpu/vfx.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace strefa::app {

// The parser allows at most one start mode and at most one test, so each maps to a single value.
enum class StartMode { menu, cpu_laboratory, cpu_arena, gpu_laboratory, mission };
enum class SmokeScenario { none, menu, cpu_laboratory, gpu_laboratory, mission, session };

struct LaunchOptions {
    bool smoke_test{false};
    bool laboratory_smoke_test{false};
    bool start_in_laboratory{false};
    bool start_in_arena{false};
    bool start_in_gpu{}, gpu_test{}, gpu_liquid_test{}, gpu_flow_test{}, gpu_smoke_test{};
    bool start_in_mission{}, mission_test{}, mission_smoke{}, gameplay_test{};
    bool legacy_graphics{}, shader_cache{}, session_smoke{}, cache_check{};
    bool gpu_profile{}, gpu_profile_check{}, gl_debug{};
    bool stabilization_profile{}, work_test{}, transport_test{}, hot_test{};
    bool cost_profile{}, cost_check{}, fluid_check{}, fluid_benchmark{};
    bool pbf_restrict_test{}; // selects the binding-contract subtest of execution_test
    bool coupling_reference_control{}, coupling_legacy_check{};
    gpu::CostMode cost_mode{gpu::CostMode::off};
    bool show_test_window{};
    bool readback_test{}, readback_profile{}, execution_test{}, execution_profile{}, neighbor_test{},
            neighbor_profile{}, coupling_test{}, coupling_profile{};
    gpu::ThermalOptions gpu_thermal;
    unsigned thermal_scene{};
    bool thermal_check{}, thermal_benchmark{};
    gpu::VfxOptions gpu_vfx;
    bool vfx_check{}, vfx_benchmark{};
    gpu::RenderOptions gpu_render;
    bool render_check{}, render_benchmark{};
    bool lighting_check{}, lighting_benchmark{}, lighting_quality_set{};
    gpu::ExecutionOptions gpu_execution;
    gpu::ProfileOptions gpu_profiling;
    unsigned physics_threads{};
    unsigned test_seconds{};
    bool test_unpaced{};
    std::optional<std::array<int, 2>> test_window_size;
    std::optional<bool> test_vsync;
    std::optional<unsigned> test_frame_limit;
    std::filesystem::path profile;

    // Number of selected test modes (window smoke tests, checks, benchmarks and profiles).
    [[nodiscard]] int test_count() const;
    [[nodiscard]] bool is_test() const { return test_count() > 0; }
    [[nodiscard]] bool is_window_smoke() const {
        return smoke_test || laboratory_smoke_test || gpu_smoke_test || mission_smoke || session_smoke;
    }
    // Tests that run unsynchronised at 60 FPS; the menu smoke test and CPU-side GPU physics tests keep settings.
    [[nodiscard]] bool forces_test_pacing() const {
        return test_count() - smoke_test - gpu_test - gpu_liquid_test - gpu_flow_test > 0;
    }
    // Tests skipped (exit 77) without OpenGL 4.3; both CPU-capable smoke tests also run on GL 3.3.
    [[nodiscard]] bool requires_gl43() const { return test_count() - smoke_test - laboratory_smoke_test > 0; }
    [[nodiscard]] StartMode start_mode() const;
    [[nodiscard]] SmokeScenario smoke_scenario() const;
};

[[nodiscard]] std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv);
[[nodiscard]] std::expected<int, std::string> run(const LaunchOptions& options);

} // namespace strefa::app
