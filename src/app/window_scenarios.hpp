#pragma once
// StrefaQA window scenarios (strefa_diagnostics): FrameObservers around the application frame loop. Not linked into
// StrefaZero; StrefaQA passes run_window_scenario as QaHooks::window.
#include "app/application.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace strefa::app {

// --scene-benchmark, and frame-timings.csv of any run with gpu::ProfileOptions::collect (--gpu-counters,
// --gpu-group-timers). nullopt: no window scenario, the application runs once as in StrefaZero.
[[nodiscard]] std::optional<std::expected<int, std::string>>
run_window_scenario(const LaunchOptions&, const LaunchConfig& base, const platform::Window&,
                    const std::filesystem::path& profile, const ApplicationRunner&);

// Every run of the returned runner whose configuration collects GPU counters writes frame-timings.csv and
// frame-timings-info.txt into profile.
[[nodiscard]] ApplicationRunner with_frame_profile(ApplicationRunner run, std::filesystem::path profile);

// --scene-benchmark: a fresh Application (world, clocks, UI state) per scene, in one window and GL context.
[[nodiscard]] std::expected<int, std::string> run_scene_benchmark(const LaunchOptions&, const LaunchConfig& base,
                                                                  const platform::Window&,
                                                                  const std::filesystem::path& profile,
                                                                  const ApplicationRunner& run_scene);

} // namespace strefa::app
