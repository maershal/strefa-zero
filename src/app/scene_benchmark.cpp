#include "app/scene_benchmark.hpp"

#include "gpu/world.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <format>

namespace strefa::app {
namespace {
std::string joined(std::span<const char* const> ids, std::string_view prefix = {}) {
    std::string text;
    for (const auto* id : ids) {
        if (!text.empty()) text += ", ";
        text += std::string{prefix} + id;
    }
    return text;
}
// An index outside the list (e.g. thermal scene 0) names no scene; never read past the array.
std::string id_at(std::span<const char* const> ids, std::size_t index) {
    return index < ids.size() ? std::string{ids[index]} : "?" + std::to_string(index);
}
} // namespace

std::string BenchmarkScene::id() const {
    switch (kind) {
    case Kind::gpu: return "gpu:" + id_at(gpu::scene_ids, index);
    case Kind::mission: return "mission";
    case Kind::thermal: // 1-based: index 0 and indices past the list both print the raw index
        return "thermal:"
             + (index >= 1 && index <= thermal_scene_ids.size() ? std::string{thermal_scene_ids[index - 1]}
                                                                : "?" + std::to_string(index));
    case Kind::lab: return "lab:" + id_at(sim::scene_ids, index);
    }
    return {};
}

std::vector<BenchmarkScene> all_benchmark_scenes() {
    std::vector<BenchmarkScene> scenes;
    for (unsigned i = 0; i < gpu::scene_ids.size(); ++i) scenes.push_back({BenchmarkScene::Kind::gpu, i});
    scenes.push_back({BenchmarkScene::Kind::mission, 0});
    for (unsigned i = 1; i <= thermal_scene_ids.size(); ++i) scenes.push_back({BenchmarkScene::Kind::thermal, i});
    for (unsigned i = 0; i < sim::scene_ids.size(); ++i) scenes.push_back({BenchmarkScene::Kind::lab, i});
    return scenes;
}

std::expected<unsigned, std::string> find_scene_id(std::span<const char* const> ids, std::string_view name,
                                                   std::string_view what) {
    for (unsigned i = 0; i < ids.size(); ++i)
        if (name == ids[i]) return i;
    return std::unexpected{std::format("Nieznana scena {} „{}”. Dostępne: {}.", what, name, joined(ids))};
}

std::expected<std::vector<BenchmarkScene>, std::string> parse_benchmark_scenes(std::string_view list) {
    if (list == "all") return all_benchmark_scenes();
    std::vector<BenchmarkScene> scenes;
    while (!list.empty()) {
        const auto comma = list.find(',');
        const auto item = list.substr(0, comma);
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (comma != std::string_view::npos && list.empty()) return std::unexpected{"Pusta pozycja listy scen."};
        BenchmarkScene scene;
        const auto colon = item.find(':');
        const auto group = item.substr(0, colon);
        const auto name = colon == std::string_view::npos ? std::string_view{} : item.substr(colon + 1);
        std::expected<unsigned, std::string> index = 0U;
        if (item == "mission")
            scene.kind = BenchmarkScene::Kind::mission;
        else if (group == "gpu") {
            scene.kind = BenchmarkScene::Kind::gpu;
            index = find_scene_id(gpu::scene_ids, name, "GPU");
        } else if (group == "thermal") {
            scene.kind = BenchmarkScene::Kind::thermal;
            index = find_scene_id(thermal_scene_ids, name, "termiki").transform([](unsigned i) { return i + 1; });
        } else if (group == "lab") {
            scene.kind = BenchmarkScene::Kind::lab;
            index = find_scene_id(sim::scene_ids, name, "modelu referencyjnego (CPU)");
        } else
            return std::unexpected{std::format(
                    "Nieznana scena benchmarku „{}”. Użyj all albo listy: gpu:<scena>, mission, thermal:<scena>, "
                    "lab:<scena>.",
                    item)};
        if (!index) return std::unexpected{index.error()};
        scene.index = *index;
        if (std::ranges::find(scenes, scene) != scenes.end())
            return std::unexpected{std::format("Scena „{}” powtórzona na liście.", scene.id())};
        scenes.push_back(scene);
    }
    if (scenes.empty()) return std::unexpected{"Lista scen benchmarku jest pusta."};
    return scenes;
}

} // namespace strefa::app
