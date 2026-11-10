#pragma once
#include "app/scene_benchmark.hpp"
#include "gpu/diagnostics.hpp"
#include "gpu/profiling.hpp"
#include "gpu/render_options.hpp"
#include "gpu/thermal.hpp"
#include "gpu/vfx.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::app {

// The parser allows at most one start mode and at most one test, so each maps to a single value.
enum class StartMode { menu, cpu_laboratory, gpu_laboratory, mission };
enum class SmokeScenario {
    none,
    menu,
    cpu_laboratory,
    gpu_laboratory,
    mission,
    session,
    mission_quick,
    mode_faults,
    scene_benchmark
};
// StrefaZero accepts only the player options; StrefaQA and the contract tests accept every option.
enum class OptionScope { player, all };
// Names accepted by --gpu-check: one StrefaQA check per name instead of one boolean flag per check.
inline constexpr std::array<std::string_view, 2> gpu_checks{"lab-blast", "cost-model"};

struct LaunchOptions {
    bool smoke_test{false};
    bool laboratory_smoke_test{false};
    bool start_in_laboratory{false};
    bool start_in_gpu{}, gpu_test{}, gpu_liquid_test{}, gpu_flow_test{}, gpu_brush_test{}, gpu_smoke_test{};
    bool start_in_mission{}, mission_test{}, mission_smoke{}, gameplay_test{};
    bool legacy_graphics{}, shader_cache{}, session_smoke{}, cache_check{};
    bool mission_ui_smoke{}, fault_smoke{};
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
    std::optional<unsigned> gpu_scene, lab_scene; // --scene gpu:<id> / lab:<id> (gpu::Scene / sim::Scene index)
    bool scene_set{};
    std::vector<BenchmarkScene> benchmark_scenes; // --scene-benchmark, one window run per scene
    unsigned benchmark_seconds{60};
    unsigned benchmark_repeat{1};      // --repeat: passes over the scene list in one process
    unsigned benchmark_loop_minutes{}; // --loop-minutes: start passes until this much time has passed
    bool benchmark_vsync{true}, benchmark_tuned{};
    bool thermal_check{}, thermal_benchmark{};
    std::filesystem::path thermal_conformance; // --gpu-thermal-conformance <thresholds file>
    std::string gpu_check;                     // --gpu-check <name>: named GPU check, one of gpu_checks
    gpu::VfxOptions gpu_vfx;
    bool vfx_check{}, vfx_benchmark{};
    gpu::RenderOptions gpu_render;
    bool render_check{}, render_benchmark{};
    bool lighting_check{}, lighting_benchmark{}, lighting_quality_set{};
    bool render_quality_set{}, vfx_quality_set{}; // explicit CLI value wins over settings.ini
    gpu::ExecutionOptions gpu_execution;
    gpu::ProfileOptions gpu_profiling;
    unsigned physics_threads{};
    unsigned test_seconds{};
    bool test_unpaced{};
    std::optional<std::array<int, 2>> test_window_size;
    std::optional<bool> test_vsync;
    std::optional<unsigned> test_frame_limit;
    std::filesystem::path profile;
    bool help{};

    // Number of selected test modes (window smoke tests, checks, benchmarks and profiles).
    [[nodiscard]] int test_count() const;
    [[nodiscard]] bool is_test() const { return test_count() > 0; }
    [[nodiscard]] bool is_window_smoke() const {
        return smoke_test || laboratory_smoke_test || gpu_smoke_test || mission_smoke || session_smoke
            || mission_ui_smoke || fault_smoke;
    }
    // Tests that run unsynchronised at 60 FPS; the menu smoke test and CPU-side GPU physics tests keep settings.
    [[nodiscard]] bool forces_test_pacing() const {
        return test_count() - smoke_test - gpu_test - gpu_liquid_test - gpu_flow_test - gpu_brush_test > 0;
    }
    // Tests skipped (exit 77) without OpenGL 4.3; the CPU-capable smoke tests also run on GL 3.3.
    [[nodiscard]] bool requires_gl43() const {
        return test_count() - smoke_test - laboratory_smoke_test - fault_smoke > 0;
    }
    [[nodiscard]] StartMode start_mode() const;
    [[nodiscard]] SmokeScenario smoke_scenario() const;
};

// Returns nullopt for interactive and window scenarios, which continue into the application loop.
using DiagnosticRunner = std::function<std::optional<std::expected<int, std::string>>(
        const LaunchOptions&, const std::filesystem::path& assets, const std::filesystem::path& profile)>;

[[nodiscard]] std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv,
                                                                      OptionScope scope = OptionScope::all);
[[nodiscard]] std::string help_text(OptionScope scope);
// Value of STREFA_GPU_TIMEOUT_SCALE (set by CMake on every `graphics` test): a positive integer that stretches
// in-app test deadlines like the CTest TIMEOUT. Unset, empty or invalid text means 1.
[[nodiscard]] unsigned gpu_timeout_scale(const char* text);
[[nodiscard]] std::expected<int, std::string> run(const LaunchOptions& options,
                                                  const DiagnosticRunner& diagnostics = {});
// Shared main() of StrefaZero and StrefaQA: parses options, reports errors and runs the application.
int run_main(int argc, char** argv, OptionScope scope, const DiagnosticRunner& diagnostics = {});

} // namespace strefa::app
