#pragma once
#include "gpu/profiling.hpp"
#include "gpu/diagnostics.hpp"
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
    bool start_in_gpu{},gpu_test{},gpu_liquid_test{},gpu_flow_test{},gpu_smoke_test{};
    bool start_in_mission{},mission_test{},mission_smoke{};
    bool legacy_graphics{};
    bool gpu_profile{},gpu_profile_check{},gl_debug{};
    bool stabilization_profile{},work_test{},transport_test{},hot_test{};
    bool cost_profile{},cost_check{};
    bool coupling_reference_control{},coupling_legacy_check{};
    gpu::CostMode cost_mode{gpu::CostMode::off};
    bool show_test_window{};
    bool readback_test{},readback_profile{},execution_test{},execution_profile{},neighbor_test{},neighbor_profile{},coupling_test{},coupling_profile{};
    gpu::ExecutionOptions gpu_execution;
    gpu::ProfileOptions gpu_profiling;
    unsigned physics_threads{};
    std::optional<bool> test_vsync;
    std::optional<unsigned> test_frame_limit;
    std::filesystem::path profile;
};

[[nodiscard]] std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv);
[[nodiscard]] std::expected<int, std::string> run(const LaunchOptions& options);

} // namespace strefa::app
