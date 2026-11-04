#include "app/scene_benchmark.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using strefa::app::BenchmarkFrame;
using strefa::app::BenchmarkRecord;
using strefa::app::BenchmarkScene;
using strefa::app::BenchmarkStatus;

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
        check(d.samples == 100 && near(d.median, 51) && near(d.p95, 96) && near(d.p99, 100) && near(d.max, 100)
                      && near(d.mean, 50.5),
              "Percentiles of 1..100");
        check(strefa::app::distribution({}).samples == 0, "Empty distribution");

        // 20 frames, 1 s apart: 0, 1, 2 ticks in turn; frames from 10 s on form the late phase.
        BenchmarkRecord record;
        for (unsigned i = 0; i < 20; ++i) {
            BenchmarkFrame frame;
            frame.utc_unix_ns = 1'000 + i;
            frame.elapsed_seconds = i;
            frame.frame_ms = 16 + i;
            frame.ticks = i % 3;
            frame.dropped_seconds = i == 4 ? .05 : 0;
            frame.pending_seconds = .001 * i;
            frame.window = {true, i != 3, false, false};
            frame.physics_samples = frame.presentation_samples = frame.ticks;
            frame.physics_gpu_ms = (i < 10 ? 2.0 : 4.0) * frame.ticks;
            frame.presentation_gpu_ms = .5 * frame.ticks;
            frame.render_samples = 1;
            frame.render_gpu_ms = 1;
            for (unsigned t = 0; t < frame.ticks; ++t) {
                record.physics_gpu_ms.push_back(i < 10 ? 2.0 : 4.0);
                record.presentation_gpu_ms.push_back(.5);
            }
            record.render_gpu_ms.push_back(1);
            record.frames.push_back(frame);
        }
        record.frames[7].route_reset = true; // excluded from frame-time statistics
        record.route_resets = 1;
        const auto summary = strefa::app::summarize("gpu:rain", record, 120);
        check(summary.status == BenchmarkStatus::ok && summary.unfocused_frames == 1, "Focus alone keeps status ok");
        check(summary.frames == 20 && summary.frame_ms.samples == 19 && near(summary.frame_ms.max, 35),
              "Frame distribution without the route restart frame");
        check(summary.by_ticks[0].frames == 7 && summary.by_ticks[1].frames == 6 && summary.by_ticks[2].frames == 6
                      && near(summary.by_ticks[0].frame_median_ms, 25),
              "Frame time by ticks per frame");
        check(near(summary.ticks_mean, 19.0 / 20) && summary.ticks_max == 2, "Ticks per frame");
        check(near(summary.window_seconds, 19) && near(summary.ticks_per_second, 1)
                      && near(summary.frames_per_second, 20.0 / 19),
              "Window rates");
        check(near(summary.dropped_seconds, .05) && near(summary.pending_seconds.max, .019),
              "Dropped time and backlog");
        check(summary.physics_gpu_ms.samples == 19 && summary.presentation_gpu_ms.samples == 19
                      && summary.render_gpu_ms.samples == 20 && summary.cpu_step_ms.samples == 0,
              "GPU samples");
        const double tick_ms = summary.physics_gpu_ms.mean + .5;
        check(near(summary.gpu_ms_per_sim_second, tick_ms * 120)
                      && near(summary.gpu_ms_per_wall_second, tick_ms * 1 + 1 * 20.0 / 19)
                      && near(summary.budget_60fps, (tick_ms * 120 + 60) / 1000),
              "GPU cost per simulated second, per wall second and 60 FPS budget");
        check(summary.early.frames == 10 && summary.late.frames == 10 && near(summary.early.gpu_tick_mean_ms, 2.5)
                      && near(summary.late.gpu_tick_mean_ms, 4.5) && near(summary.early.ticks_per_second, 0.9)
                      && near(summary.late.ticks_per_second, 10.0 / 9),
              "Early (0-10 s) and late phases");
        check(summary.route_resets == 1 && summary.start_utc_unix_ns == 1'000 && summary.end_utc_unix_ns == 1'019,
              "Route restarts and window bounds");

        auto hidden = record;
        hidden.frames[2].window.occluded = true;
        const auto invalid = strefa::app::summarize("gpu:rain", hidden, 120);
        check(invalid.status == BenchmarkStatus::invalid && invalid.occluded_frames == 1
                      && strefa::app::benchmark_summary_row(invalid).find(",1,invalid,") != std::string::npos,
              "An occluded window invalidates the scene");
        // Frames taken while minimized (the minimized branch of the loop) or cloaked by DWM count as well.
        for (const bool cloaked : {false, true}) {
            auto away = record;
            (cloaked ? away.frames[12].window.cloaked : away.frames[12].window.minimized) = true;
            const auto result = strefa::app::summarize("mission", away, 120);
            check(result.status == BenchmarkStatus::invalid && result.frames == 20
                          && (cloaked ? result.cloaked_frames : result.minimized_frames) == 1,
                  cloaked ? "A cloaked window invalidates the scene" : "A minimized frame invalidates the scene");
        }
        check(!strefa::app::WindowState{true, true, false, false, true}.presented(), "Cloaked is not presented");
        // Minimized during the warm-up: no frame row, but the scene must not end as ok.
        auto warm = record;
        warm.warmup_away_frames = 3;
        const auto warm_summary = strefa::app::summarize("gpu:rain", warm, 120);
        check(warm_summary.status == BenchmarkStatus::invalid && warm_summary.warmup_away_frames == 3
                      && warm_summary.minimized_frames == 0,
              "A window away during the warm-up invalidates the scene");

        // A restart requested in frame 2 is flagged in frame 3, whose frame_ms contains the reload.
        strefa::app::RouteResetMarker marker;
        const std::array requested{false, false, true, false, false, true, true, false};
        const std::array flagged{false, false, false, true, false, false, true, true};
        for (std::size_t i = 0; i < requested.size(); ++i)
            check(marker.next_frame(requested[i]) == flagged[i], "Route restart flagged in frame " + std::to_string(i));

        // Out-of-range indices name no scene instead of reading past the id arrays.
        check(BenchmarkScene{BenchmarkScene::Kind::thermal, 0}.id() == "thermal:?0", "Thermal scene 0 has no id");
        check(BenchmarkScene{BenchmarkScene::Kind::thermal, 99}.id() == "thermal:?99", "Thermal scene past the end");
        check(BenchmarkScene{BenchmarkScene::Kind::gpu, 1000}.id() == "gpu:?1000", "GPU scene past the end");
        check(BenchmarkScene{BenchmarkScene::Kind::lab, 1000}.id() == "lab:?1000", "Lab scene past the end");

        BenchmarkRecord failed;
        failed.error = "błąd, z przecinkiem; i średnikiem";
        const auto failure = strefa::app::summarize("lab:demo", failed, 0);
        check(failure.status == BenchmarkStatus::failed && failure.frames == 0, "A scene error fails the scene");
        check(fields(strefa::app::benchmark_summary_row(failure)) == fields(strefa::app::benchmark_summary_header),
              "A failure reason keeps the row width");

        // Rows must keep the column count of their headers (scripts/profiling/soak.ps1 reads them by name).
        check(fields(strefa::app::benchmark_summary_row(summary)) == fields(strefa::app::benchmark_summary_header),
              "Summary row width");
        check(fields(strefa::app::benchmark_frame_row("gpu:rain", 1, record.frames[0]))
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
