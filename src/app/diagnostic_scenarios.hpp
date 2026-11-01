#pragma once
#include "app/application.hpp"

#include <memory>
namespace strefa::gpu {
class RenderDevice;
} // namespace strefa::gpu
namespace strefa::app {
std::expected<void, std::string> validate_thermal_session(const std::filesystem::path&, const std::filesystem::path&,
                                                          std::shared_ptr<gpu::RenderDevice>);
std::expected<int, std::string> validate_shader_cache(const std::filesystem::path&);
// nullopt means this is an interactive/window scenario.
std::optional<std::expected<int, std::string>> run_diagnostic_scenario(const LaunchOptions&,
                                                                       const std::filesystem::path& assets,
                                                                       const std::filesystem::path& profile);
} // namespace strefa::app
