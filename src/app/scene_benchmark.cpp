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
} // namespace

std::string BenchmarkScene::id() const {
    switch (kind) {
    case Kind::gpu: return std::string{"gpu:"} + gpu::scene_ids[index];
    case Kind::mission: return "mission";
    case Kind::thermal: return std::string{"thermal:"} + thermal_scene_ids[index - 1];
    case Kind::lab: return std::string{"lab:"} + sim::scene_ids[index];
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
            index = find_scene_id(sim::scene_ids, name, "laboratorium CPU");
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

Distribution distribution(std::vector<double> values) {
    Distribution result;
    result.samples = values.size();
    if (values.empty()) return result;
    std::ranges::sort(values);
    const auto at = [&](double q) {
        return values[std::min(values.size() - 1, static_cast<std::size_t>(q * static_cast<double>(values.size())))];
    };
    result.median = at(.5);
    result.p95 = at(.95);
    result.p99 = at(.99);
    result.max = values.back();
    return result;
}

BenchmarkSummary summarize(std::string scene, std::span<const BenchmarkFrame> frames,
                           std::vector<double> physics_gpu_ms, std::vector<double> presentation_gpu_ms,
                           std::vector<double> render_gpu_ms) {
    BenchmarkSummary result;
    result.scene = std::move(scene);
    result.frames = frames.size();
    result.physics_gpu_ms = distribution(std::move(physics_gpu_ms));
    result.presentation_gpu_ms = distribution(std::move(presentation_gpu_ms));
    result.render_gpu_ms = distribution(std::move(render_gpu_ms));
    if (frames.empty()) return result;
    std::vector<double> frame_ms, cpu_step_ms, pending;
    std::uint64_t ticks = 0;
    for (const auto& frame : frames) {
        frame_ms.push_back(frame.frame_ms);
        pending.push_back(frame.pending_seconds);
        if (frame.cpu_step_ms > 0) cpu_step_ms.push_back(frame.cpu_step_ms);
        ticks += frame.ticks;
        result.ticks_max = std::max(result.ticks_max, frame.ticks);
        result.dropped_seconds += frame.dropped_seconds;
    }
    result.frame_ms = distribution(std::move(frame_ms));
    result.cpu_step_ms = distribution(std::move(cpu_step_ms));
    result.pending_seconds = distribution(std::move(pending));
    result.ticks_mean = static_cast<double>(ticks) / static_cast<double>(frames.size());
    result.start_utc_unix_ns = frames.front().utc_unix_ns;
    result.end_utc_unix_ns = frames.back().utc_unix_ns;
    return result;
}

std::string benchmark_frame_row(std::string_view scene, const BenchmarkFrame& f) {
    return std::format("{},{},{},{:.4f},{},{:.6f},{:.6f},{:.4f},{:.4f},{:.4f},{:.4f}", scene, f.utc_unix_ns, f.sdl_ns,
                       f.frame_ms, f.ticks, f.dropped_seconds, f.pending_seconds, f.physics_gpu_ms,
                       f.presentation_gpu_ms, f.render_gpu_ms, f.cpu_step_ms);
}

std::string benchmark_summary_row(const BenchmarkSummary& s) {
    return std::format("{},{},{:.4f},{:.4f},{:.4f},{:.4f},{},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},"
                       "{:.4f},{:.4f},{:.4f},{:.4f},{},{:.6f},{:.6f},{:.6f},{},{}",
                       s.scene, s.frames, s.frame_ms.median, s.frame_ms.p95, s.frame_ms.p99, s.frame_ms.max,
                       s.physics_gpu_ms.samples, s.physics_gpu_ms.median, s.physics_gpu_ms.p95, s.physics_gpu_ms.p99,
                       s.presentation_gpu_ms.median, s.presentation_gpu_ms.p95, s.presentation_gpu_ms.p99,
                       s.render_gpu_ms.median, s.render_gpu_ms.p95, s.render_gpu_ms.p99, s.cpu_step_ms.median,
                       s.cpu_step_ms.p95, s.ticks_mean, s.ticks_max, s.dropped_seconds, s.pending_seconds.p95,
                       s.pending_seconds.max, s.start_utc_unix_ns, s.end_utc_unix_ns);
}

std::string benchmark_summary_line(const BenchmarkSummary& s) {
    std::string line = std::format("{:<26} klatka {:.2f}/{:.2f}/{:.2f} ms (mediana/p95/p99)", s.scene,
                                   s.frame_ms.median, s.frame_ms.p95, s.frame_ms.p99);
    if (s.physics_gpu_ms.samples)
        line += std::format("  fizyka {:.3f}  prezentacja {:.3f}  render {:.3f} ms", s.physics_gpu_ms.median,
                            s.presentation_gpu_ms.median, s.render_gpu_ms.median);
    else if (s.cpu_step_ms.samples)
        line += std::format("  krok CPU {:.3f} ms", s.cpu_step_ms.median);
    line += std::format("  ticki/klatkę {:.2f} (max {})  utracone {:.3f} s  zaległość max {:.4f} s", s.ticks_mean,
                        s.ticks_max, s.dropped_seconds, s.pending_seconds.max);
    return line;
}

} // namespace strefa::app
