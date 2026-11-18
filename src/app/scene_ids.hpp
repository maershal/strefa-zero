#pragma once
// Scene ids of --scene and --gpu-thermal-scene (strefa_launch). The --scene-benchmark list is parsed only by StrefaQA
// (app/benchmark_report.hpp).
#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace strefa::app {

// Thermal scene ids of --gpu-thermal-scene and --scene-benchmark; scene number = index + 1.
inline constexpr std::array thermal_scene_ids{"fire", "oil", "quench", "phases"};

// Index of name in ids, for the --scene, --gpu-thermal-scene and --scene-benchmark values.
[[nodiscard]] std::expected<unsigned, std::string> find_scene_id(std::span<const char* const> ids,
                                                                 std::string_view name, std::string_view what);

} // namespace strefa::app
