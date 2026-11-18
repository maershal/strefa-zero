#pragma once
// Scene ids and the --scene-benchmark list parser (strefa_launch). The measurement itself (statistics, CSV) is in
// app/benchmark_report.hpp and the driver in scene_benchmark_run.cpp, both linked only into StrefaQA.
#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::app {

// Thermal scene ids of --gpu-thermal-scene and --scene-benchmark; scene number = index + 1.
inline constexpr std::array thermal_scene_ids{"fire", "oil", "quench", "phases"};

// One scene measured by --scene-benchmark (StrefaQA).
struct BenchmarkScene {
    enum class Kind { gpu, mission, thermal, lab };
    Kind kind{Kind::gpu};
    unsigned index{}; // gpu::Scene, thermal scene number (1-4) or sim::Scene; 0 for the mission
    // gpu:<id>, mission, thermal:<id> or lab:<id>
    [[nodiscard]] std::string id() const;
    bool operator==(const BenchmarkScene&) const = default;
};
// GPU laboratory scenes, the mission, thermal scenes and CPU laboratory scenes, in that order.
[[nodiscard]] std::vector<BenchmarkScene> all_benchmark_scenes();
// "all" or a comma-separated list of ids; duplicates are rejected.
[[nodiscard]] std::expected<std::vector<BenchmarkScene>, std::string> parse_benchmark_scenes(std::string_view list);
// Index of name in ids, for the --scene-benchmark and --gpu-thermal-scene values.
[[nodiscard]] std::expected<unsigned, std::string> find_scene_id(std::span<const char* const> ids,
                                                                 std::string_view name, std::string_view what);

} // namespace strefa::app
