#pragma once
// --gpu-thermal-conformance (StrefaQA): observables of 10 thermal scenes measured on the CPU oracle (sim::World),
// compared with thresholds fixed in a file before any GPU thermal change. The GPU side is not implemented yet.
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::app {

// One line of the thresholds file: a GPU value v passes when |v - cpu| <= max(absolute, relative * |base|), where
// base is cpu, or cpu - ambient for temperatures (column relative_of of the CSV).
struct ConformanceThreshold {
    std::string scene, observable;
    double absolute{}, relative{};
};
// "<scene> <observable> <absolute> <relative>" per line, '#' starts a comment. Rejects malformed lines, negative or
// non-finite tolerances and duplicates.
[[nodiscard]] std::expected<std::vector<ConformanceThreshold>, std::string>
parse_conformance_thresholds(std::string_view text);

// The (scene, observable) pairs the CPU side records, in output order.
struct ConformanceObservable {
    std::string_view scene, observable, unit;
};
[[nodiscard]] std::vector<ConformanceObservable> thermal_conformance_observables();
// Scene ids in output order.
[[nodiscard]] std::vector<std::string_view> thermal_conformance_scenes();
// Rows of thermal-conformance-cpu.csv: every observable at every sampled tick of its scene (of one scene if given).
[[nodiscard]] std::size_t thermal_conformance_sample_count(std::optional<std::string_view> scene = std::nullopt);

// Writes thermal-conformance-cpu.csv and thermal-conformance.txt into output for every scene, or only for the given
// one. Fails when the thresholds do not cover exactly the recorded observables of all scenes, the scene is unknown or
// a CPU value is not finite.
[[nodiscard]] std::expected<void, std::string>
run_thermal_conformance(const std::filesystem::path& thresholds, const std::filesystem::path& output,
                        std::optional<std::string_view> scene = std::nullopt);

} // namespace strefa::app
