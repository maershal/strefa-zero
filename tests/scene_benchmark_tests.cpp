#include "app/scene_benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using strefa::app::BenchmarkFrame;
using strefa::app::BenchmarkScene;

void check(bool ok, const std::string& text) {
    if (!ok) throw std::runtime_error{text};
}
bool near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}
std::size_t fields(std::string_view row) {
    return static_cast<std::size_t>(std::ranges::count(row, ',')) + 1;
}
} // namespace

int main() {
    try {
        using strefa::app::parse_benchmark_scenes;
        const auto all = parse_benchmark_scenes("all");
        check(all && all->size() == 32, "all must list 12 GPU, mission, 4 thermal and 15 CPU laboratory scenes");
        for (const auto& scene : *all) {
            const auto again = parse_benchmark_scenes(scene.id());
            check(again && again->size() == 1 && again->front() == scene, scene.id() + " must round-trip");
        }
        const auto list = parse_benchmark_scenes("gpu:dam-break,mission,thermal:fire,lab:wood-fire");
        check(list && list->size() == 4, "Mixed list rejected");
        check((*list)[0].kind == BenchmarkScene::Kind::gpu && (*list)[0].index == 5, "gpu:dam-break index");
        check((*list)[1].kind == BenchmarkScene::Kind::mission, "mission kind");
        check((*list)[2].kind == BenchmarkScene::Kind::thermal && (*list)[2].index == 1, "thermal scenes are 1-based");
        check((*list)[3].kind == BenchmarkScene::Kind::lab && (*list)[3].index == 9, "lab:wood-fire index");
        for (const auto* bad : {"", "gpu:", "gpu:nope", "lab:dam", "thermal:ice", "mission,", ",mission", "cpu:demo",
                                "mission,mission", "gpu:rain,gpu:rain"})
            check(!parse_benchmark_scenes(bad), std::string{"Accepted invalid list: "} + bad);

        // Nearest rank, as in gpu/profiling.cpp: sorted[min(n-1, q*n)].
        std::vector<double> values;
        for (int i = 100; i >= 1; --i) values.push_back(i);
        const auto d = strefa::app::distribution(values);
        check(d.samples == 100 && near(d.median, 51) && near(d.p95, 96) && near(d.p99, 100) && near(d.max, 100),
              "Percentiles of 1..100");
        check(strefa::app::distribution({}).samples == 0, "Empty distribution");

        std::vector<BenchmarkFrame> frames;
        for (unsigned i = 0; i < 10; ++i) {
            BenchmarkFrame frame;
            frame.utc_unix_ns = 1'000 + i;
            frame.frame_ms = 16 + i;
            frame.ticks = i % 3;
            frame.dropped_seconds = i == 4 ? .05 : 0;
            frame.pending_seconds = .001 * i;
            frames.push_back(frame);
        }
        const auto summary = strefa::app::summarize("gpu:rain", frames, {2, 1, 3}, {.5, .5, .5}, {});
        check(summary.frames == 10 && near(summary.frame_ms.median, 21) && near(summary.frame_ms.max, 25),
              "Frame distribution");
        check(near(summary.ticks_mean, 0.9) && summary.ticks_max == 2, "Ticks per frame");
        check(near(summary.dropped_seconds, .05) && near(summary.pending_seconds.max, .009),
              "Dropped time and backlog");
        check(summary.physics_gpu_ms.samples == 3 && near(summary.physics_gpu_ms.median, 2), "GPU samples");
        check(summary.render_gpu_ms.samples == 0 && summary.cpu_step_ms.samples == 0, "Missing series stay empty");
        check(summary.start_utc_unix_ns == 1'000 && summary.end_utc_unix_ns == 1'009, "Window bounds");

        // Rows must keep the column count of their headers (scripts/profiling/soak.ps1 reads them by name).
        check(fields(strefa::app::benchmark_summary_row(summary)) == fields(strefa::app::benchmark_summary_header),
              "Summary row width");
        check(fields(strefa::app::benchmark_frame_row("gpu:rain", frames[0]))
                      == fields(strefa::app::benchmark_frames_header),
              "Frame row width");
        check(strefa::app::benchmark_summary_line(summary).starts_with("gpu:rain"), "Summary line names the scene");
        std::cout << "Scene benchmark contract PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
