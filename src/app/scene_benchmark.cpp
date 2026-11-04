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
    case Kind::thermal: return "thermal:" + (index ? id_at(thermal_scene_ids, index - 1) : std::string{"?0"});
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
    double sum = 0;
    for (const double value : values) sum += value; // ascending order: same result on every platform
    result.mean = sum / static_cast<double>(values.size());
    result.median = at(.5);
    result.p95 = at(.95);
    result.p99 = at(.99);
    result.max = values.back();
    return result;
}

std::string_view status_name(BenchmarkStatus status) {
    switch (status) {
    case BenchmarkStatus::ok: return "ok";
    case BenchmarkStatus::invalid: return "invalid";
    case BenchmarkStatus::failed: return "failed";
    }
    return "failed";
}

namespace {
double median_of(std::vector<double> values) {
    return distribution(std::move(values)).median;
}

BenchmarkPhase phase(std::span<const BenchmarkFrame> frames, double seconds) {
    BenchmarkPhase result;
    std::vector<double> frame_ms;
    std::uint64_t ticks = 0, samples = 0;
    double gpu_ms = 0;
    for (const auto& frame : frames) {
        ++result.frames;
        if (!frame.route_reset) frame_ms.push_back(frame.frame_ms);
        ticks += frame.ticks;
        samples += frame.physics_samples;
        gpu_ms += frame.physics_gpu_ms + frame.presentation_gpu_ms;
    }
    result.frame_median_ms = median_of(std::move(frame_ms));
    if (seconds > 0) result.ticks_per_second = static_cast<double>(ticks) / seconds;
    if (samples) result.gpu_tick_mean_ms = gpu_ms / static_cast<double>(samples);
    return result;
}

// CSV cells are unquoted: keep a reason on one line and free of separators.
std::string csv_text(std::string_view text) {
    std::string result{text};
    for (auto& c : result)
        if (c == ',' || c == ';' || c == '\n' || c == '\r') c = '|';
    return result;
}
} // namespace

BenchmarkSummary summarize(std::string scene, BenchmarkRecord record, double tick_rate) {
    BenchmarkSummary result;
    result.scene = std::move(scene);
    const std::span<const BenchmarkFrame> frames{record.frames};
    result.frames = frames.size();
    result.physics_gpu_ms = distribution(std::move(record.physics_gpu_ms));
    result.presentation_gpu_ms = distribution(std::move(record.presentation_gpu_ms));
    result.render_gpu_ms = distribution(std::move(record.render_gpu_ms));
    result.physics_truncated = record.physics_truncated;
    result.presentation_truncated = record.presentation_truncated;
    result.render_truncated = record.render_truncated;
    result.route_resets = record.route_resets;
    if (!record.error.empty()) {
        result.status = BenchmarkStatus::failed;
        result.status_reason = record.error;
    } else if (frames.empty()) {
        result.status = BenchmarkStatus::failed;
        result.status_reason = "brak zmierzonych klatek";
    }
    if (frames.empty()) return result;
    std::vector<double> frame_ms, cpu_step_ms, pending;
    std::array<std::vector<double>, 3> by_ticks;
    std::uint64_t ticks = 0;
    for (const auto& frame : frames) {
        if (!frame.route_reset) {
            frame_ms.push_back(frame.frame_ms);
            by_ticks[std::min(frame.ticks, 2U)].push_back(frame.frame_ms);
        }
        pending.push_back(frame.pending_seconds);
        if (frame.cpu_step_ms > 0) cpu_step_ms.push_back(frame.cpu_step_ms);
        ticks += frame.ticks;
        result.ticks_max = std::max(result.ticks_max, frame.ticks);
        result.dropped_seconds += frame.dropped_seconds;
        result.hidden_frames += !frame.window.visible;
        result.minimized_frames += frame.window.minimized;
        result.occluded_frames += frame.window.occluded;
        result.unfocused_frames += !frame.window.focused;
        result.cloaked_frames += frame.window.cloaked;
    }
    for (std::size_t i = 0; i < by_ticks.size(); ++i)
        result.by_ticks[i] = {by_ticks[i].size(), median_of(std::move(by_ticks[i]))};
    result.frame_ms = distribution(std::move(frame_ms));
    result.cpu_step_ms = distribution(std::move(cpu_step_ms));
    result.pending_seconds = distribution(std::move(pending));
    result.ticks_mean = static_cast<double>(ticks) / static_cast<double>(frames.size());
    result.window_seconds = frames.back().elapsed_seconds;
    if (result.window_seconds > 0) {
        result.ticks_per_second = static_cast<double>(ticks) / result.window_seconds;
        result.frames_per_second = static_cast<double>(frames.size()) / result.window_seconds;
    }
    if (result.physics_gpu_ms.samples) {
        const double tick_ms = result.physics_gpu_ms.mean + result.presentation_gpu_ms.mean;
        result.gpu_ms_per_sim_second = tick_ms * tick_rate;
        result.gpu_ms_per_wall_second =
                tick_ms * result.ticks_per_second + result.render_gpu_ms.mean * result.frames_per_second;
        result.budget_60fps = (tick_ms * tick_rate + result.render_gpu_ms.mean * 60) / 1000;
    }
    const auto split = std::ranges::find_if(
            frames, [](const BenchmarkFrame& frame) { return frame.elapsed_seconds >= early_phase_seconds; });
    const auto early_count = static_cast<std::size_t>(split - frames.begin());
    result.early = phase(frames.first(early_count), std::min(result.window_seconds, early_phase_seconds));
    result.late = phase(frames.subspan(early_count), std::max(0.0, result.window_seconds - early_phase_seconds));
    if (result.status == BenchmarkStatus::ok
        && (result.hidden_frames || result.minimized_frames || result.occluded_frames || result.cloaked_frames)) {
        result.status = BenchmarkStatus::invalid;
        result.status_reason = std::format(
                "okno niewidoczne w {} klatkach, zminimalizowane w {}, zasłonięte w {}, ukryte przez DWM "
                "w {}",
                result.hidden_frames, result.minimized_frames, result.occluded_frames, result.cloaked_frames);
    }
    result.start_utc_unix_ns = frames.front().utc_unix_ns;
    result.end_utc_unix_ns = frames.back().utc_unix_ns;
    return result;
}

std::string benchmark_frame_row(std::string_view scene, unsigned pass, const BenchmarkFrame& f) {
    return std::format(
            "{},{},{},{},{:.6f},{:.4f},{},{:.6f},{:.6f},{},{:.4f},{},{:.4f},{},{:.4f},{:.4f},{:d},{:d},{:d},{:d},"
            "{:d},{:d}",
            scene, pass, f.utc_unix_ns, f.sdl_ns, f.elapsed_seconds, f.frame_ms, f.ticks, f.dropped_seconds,
            f.pending_seconds, f.physics_samples, f.physics_gpu_ms, f.presentation_samples, f.presentation_gpu_ms,
            f.render_samples, f.render_gpu_ms, f.cpu_step_ms, f.window.visible, f.window.focused, f.window.minimized,
            f.window.occluded, f.route_reset, f.window.cloaked);
}

std::string benchmark_summary_row(const BenchmarkSummary& s) {
    const auto gpu = [](const Distribution& d, bool truncated) {
        return std::format("{},{:d},{:.4f},{:.4f},{:.4f},{:.4f}", d.samples, truncated, d.mean, d.median, d.p95, d.p99);
    };
    const auto phase_cells = [](const BenchmarkPhase& p) {
        return std::format("{},{:.4f},{:.3f},{:.4f}", p.frames, p.frame_median_ms, p.ticks_per_second,
                           p.gpu_tick_mean_ms);
    };
    return std::format(
            "{},{},{},{},{},{:.3f},{:.4f},{:.4f},{:.4f},{:.4f},{},{:.4f},{},{:.4f},{},{:.4f},{},{},{},{:.3f},"
            "{:.3f},{:.4f},{:.4f},{:.4f},{:.4f},{},{:.3f},{:.3f},{:.6f},{:.6f},{:.6f},{},{},{},{},{},"
            "{},{},{},{},{}",
            s.scene, s.pass, status_name(s.status), csv_text(s.status_reason), s.frames, s.window_seconds,
            s.frame_ms.median, s.frame_ms.p95, s.frame_ms.p99, s.frame_ms.max, s.by_ticks[0].frames,
            s.by_ticks[0].frame_median_ms, s.by_ticks[1].frames, s.by_ticks[1].frame_median_ms, s.by_ticks[2].frames,
            s.by_ticks[2].frame_median_ms, gpu(s.physics_gpu_ms, s.physics_truncated),
            gpu(s.presentation_gpu_ms, s.presentation_truncated), gpu(s.render_gpu_ms, s.render_truncated),
            s.gpu_ms_per_sim_second, s.gpu_ms_per_wall_second, s.budget_60fps, s.cpu_step_ms.median, s.cpu_step_ms.p95,
            s.ticks_mean, s.ticks_max, s.ticks_per_second, s.frames_per_second, s.dropped_seconds,
            s.pending_seconds.p95, s.pending_seconds.max, phase_cells(s.early), phase_cells(s.late), s.hidden_frames,
            s.minimized_frames, s.occluded_frames, s.unfocused_frames, s.route_resets, s.start_utc_unix_ns,
            s.end_utc_unix_ns, s.cloaked_frames);
}

std::string benchmark_summary_line(const BenchmarkSummary& s) {
    std::string line = std::format("{:<26} klatka {:.2f}/{:.2f}/{:.2f} ms (mediana/p95/p99)", s.scene,
                                   s.frame_ms.median, s.frame_ms.p95, s.frame_ms.p99);
    if (s.physics_gpu_ms.samples)
        line += std::format("  fizyka {:.3f}  prezentacja {:.3f}  render {:.3f} ms  GPU {:.1f} ms/s symulacji  "
                            "budżet 60 FPS {:.0f}%",
                            s.physics_gpu_ms.median, s.presentation_gpu_ms.median, s.render_gpu_ms.median,
                            s.gpu_ms_per_sim_second, 100 * s.budget_60fps);
    else if (s.cpu_step_ms.samples)
        line += std::format("  krok CPU {:.3f} ms", s.cpu_step_ms.median);
    line += std::format("  ticki/klatkę {:.2f} (max {})  utracone {:.3f} s  zaległość max {:.4f} s", s.ticks_mean,
                        s.ticks_max, s.dropped_seconds, s.pending_seconds.max);
    if (s.unfocused_frames) line += std::format("  bez fokusu {} klatek", s.unfocused_frames);
    if (s.status != BenchmarkStatus::ok) line += std::format("  [{}: {}]", status_name(s.status), s.status_reason);
    return line;
}

} // namespace strefa::app
