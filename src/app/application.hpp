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
};

[[nodiscard]] std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv);
[[nodiscard]] std::expected<int, std::string> run(const LaunchOptions& options);

} // namespace strefa::app
