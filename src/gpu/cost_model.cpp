// Z01: where the cost of a GPU tick comes from. Part 1 times synthetic passes without ParticleWorld, part 2 sweeps
// the particle count on the real solver in one geometry, part 3 brackets every segment with calibration probes.
// Only the public ParticleWorld API is used (create, reset, load, step, poll, take_timings, counters, readback).
#include "gpu/synthetic_passes.hpp"
#include "gpu/validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace strefa::gpu {
namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error{message};
}
std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E37'79B9'7F4A'7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return z ^ (z >> 31U);
}
template<class T> void shuffle(std::vector<T>& items, std::uint64_t& state) {
    for (std::size_t i = items.size(); i > 1; --i)
        std::swap(items[i - 1], items[static_cast<std::size_t>(splitmix64(state) % i)]);
}
std::string environment(const char* name) {
    const char* value = SDL_getenv(name);
    return value ? value : "";
}
std::uint64_t benchmark_seed() {
    const auto text = environment("STREFA_BENCHMARK_SEED");
    if (text.empty()) {
        std::random_device device;
        return (std::uint64_t{device()} << 32U) ^ device();
    }
    std::uint64_t seed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
    require(error == std::errc{} && end == text.data() + text.size(),
            "STREFA_BENCHMARK_SEED musi być liczbą całkowitą bez znaku.");
    return seed;
}
double median(std::vector<double> values) {
    require(!values.empty(), "Brak próbek do mediany");
    std::ranges::sort(values);
    const auto n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}
double percentile(std::vector<double> values, double q) {
    require(!values.empty(), "Brak próbek do percentyla");
    std::ranges::sort(values);
    return values[std::min(values.size() - 1, static_cast<std::size_t>(q * static_cast<double>(values.size())))];
}
struct Fit {
    double a{}, b{}, r2{};
};
// Ordinary least squares y = a + b x.
Fit fit(const std::vector<std::pair<double, double>>& points) {
    require(points.size() >= 2, "Za mało punktów do dopasowania");
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& [x, y] : points) {
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const auto n = static_cast<double>(points.size());
    Fit f;
    const double d = n * sxx - sx * sx;
    f.b = d != 0 ? (n * sxy - sx * sy) / d : 0;
    f.a = (sy - f.b * sx) / n;
    double total = 0, residual = 0;
    for (const auto& [x, y] : points) {
        total += (y - sy / n) * (y - sy / n);
        residual += (y - f.a - f.b * x) * (y - f.a - f.b * x);
    }
    f.r2 = total > 0 ? 1 - residual / total : 1;
    return f;
}

// STREFA_COST_MODEL_PLAN: full (default, the measurement), part1 (only the synthetic passes, for the manual repeat
// with the driver's threaded optimization switched off), quick (CTest on any renderer: same code paths, small sizes).
struct Plan {
    std::string name{"full"};
    unsigned blocks{3}, repetitions{10};
    std::vector<unsigned> passes{1, 16, 64, 256}, threads{256, 4096, 32768}, chains{16, 256};
    std::vector<unsigned> fused_passes{16, 64, 256};
    std::vector<unsigned> counts{1000, 2000, 4000, 8000, 16000, 32000};
    unsigned warmup{120}, measured{240};
    bool sweep{true};
    double prefix_target_ms{10};
    // Cool-down gate: before a segment, wait (10 s steps, at most max_cooldown_s) until the CPU probe is within 15% of
    // the fastest reading of the run. A GTX 1650 laptop slows its CPU about 5x after ~40 s of GPU load (shared
    // cooling) with no GPU throttle reason; without the gate most segments would measure that state.
    unsigned max_cooldown_s{180};
};
Plan plan_from_environment() {
    Plan plan;
    const auto name = environment("STREFA_COST_MODEL_PLAN");
    if (name.empty() || name == "full") return plan;
    if (name == "part1") {
        plan.name = name;
        plan.sweep = false;
        return plan;
    }
    require(name == "quick", "STREFA_COST_MODEL_PLAN: full, part1 lub quick.");
    plan.name = name;
    plan.blocks = 1;
    plan.repetitions = 2;
    plan.passes = {1, 16};
    plan.threads = {256, 4096};
    plan.chains = {16};
    plan.fused_passes = {16};
    plan.counts = {1000, 4000};
    plan.warmup = 12;
    plan.measured = 24;
    plan.prefix_target_ms = 2;
    plan.max_cooldown_s = 0;
    return plan;
}

struct SeriesItem {
    std::string family; // series, fused_chain (one pass, L*M per thread), fused_wide (one pass, T*M threads)
    SyntheticSeries series;
    unsigned threads{}, chain{}, passes{}; // logical T, L, M of the comparable series
};
std::vector<SeriesItem> series_plan(const Plan& plan) {
    std::vector<SeriesItem> items;
    for (const auto mode : {SubmitMode::cold, SubmitMode::primed, SubmitMode::finish_each})
        for (unsigned m : plan.passes) {
            for (const auto kind : {SyntheticKind::empty, SyntheticKind::indirect_zero})
                items.push_back({"series", {kind, 64, 0, m, mode}, 64, 0, m});
            for (unsigned t : plan.threads)
                for (unsigned l : plan.chains)
                    items.push_back({"series", {SyntheticKind::chain, t, l, m, mode}, t, l, m});
        }
    for (unsigned m : plan.fused_passes)
        for (unsigned t : plan.threads)
            for (unsigned l : plan.chains) {
                items.push_back({"fused_chain", {SyntheticKind::chain, t, l * m, 1, SubmitMode::cold}, t, l, m});
                if (static_cast<std::uint64_t>(t) * m <= SyntheticPasses::max_threads)
                    items.push_back({"fused_wide", {SyntheticKind::chain, t * m, l, 1, SubmitMode::cold}, t, l, m});
            }
    return items;
}

struct SweepPoint {
    std::string material; // grains, water
    unsigned count{};
};
struct Tank {
    std::vector<std::uint32_t> walls;
    static constexpr int left = 10, columns = 300, floor = 187;
};
// The same tank for every N: 300 columns between two walls on a floor; particles one per cell from the floor up, so
// only the fill height changes with N (32000 particles fill 107 rows).
Tank tank() {
    Tank t;
    t.walls.assign(static_cast<std::size_t>(width * height), 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (y >= Tank::floor || x == Tank::left - 1 || x == Tank::left + Tank::columns || x == 0 || x == width - 1)
                t.walls[static_cast<std::size_t>(y * width + x)] = 1;
    return t;
}
std::vector<Grain> fill(const Grain& prototype, unsigned count) {
    std::vector<Grain> result(count, prototype);
    for (unsigned i = 0; i < count; ++i) {
        const float x = static_cast<float>(Tank::left) + .5F + static_cast<float>(i % Tank::columns);
        const float y = static_cast<float>(Tank::floor) - .5F - static_cast<float>(i / Tank::columns);
        result[i].position = {x, y, x, y};
        result[i].motion[0] = result[i].motion[1] = 0;
        result[i].contact = result[i].residual = {};
    }
    return result;
}
// Material templates come from the solver's own scenes instead of literal inverse masses.
Grain first_alive(ParticleWorld& world, Scene scene) {
    world.reset(scene);
    for (const auto& p : world.readback())
        if (p.motion[3] > .5F) return p;
    throw std::runtime_error{"Scena bez cząstek"};
}

std::int64_t utc_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}
} // namespace

std::expected<void, std::string> model_tick_cost(const std::filesystem::path& shaders,
                                                 const std::filesystem::path& output,
                                                 const ExecutionOptions& execution) {
    try {
        std::filesystem::create_directories(output);
        const auto plan = plan_from_environment();
        const auto seed = benchmark_seed();
        auto state = seed;
        auto probe_made = CalibrationProbe::create();
        require(probe_made.has_value(), probe_made ? "" : probe_made.error());
        auto& probe = *probe_made;
        auto& passes = probe.passes();
        require(passes.verify_chain(), "Kernel łańcucha zwrócił wynik inny niż przejście CPU po tym samym cyklu");
        const auto prefix = passes.calibrate_prefix(plan.prefix_target_ms);

        std::ofstream info{output / "cost-model.txt"}, series_csv{output / "series.csv"},
                segments_csv{output / "segments.csv"}, sweep_csv{output / "sweep.csv"},
                ticks_csv{output / "sweep-ticks.csv"};
        info << "schema=1\nplan=" << plan.name << "\nseed=" << seed
             << "\nseed_override=STREFA_BENCHMARK_SEED\nrenderer="
             << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
             << "\nvendor=" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
             << "\nversion=" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << "\nblocks=" << plan.blocks
             << "\nrepetitions=" << plan.repetitions << "\ncycle_entries=" << SyntheticPasses::cycle_size
             << "\nprefix_threads=32768\nprefix_chain=" << prefix << "\nprefix_target_ms=" << plan.prefix_target_ms
             << "\nmax_cooldown_s=" << plan.max_cooldown_s << "\nwarmup_ticks=" << plan.warmup
             << "\nmeasured_ticks=" << plan.measured
             << "\norder=random_per_block_splitmix64_fisher_yates\nprobe=cpu xorshift 2^23 median of 3; gpu chain "
                "T=4096 L=16 M=64 cold median of 5\n";
        info.flush();
        series_csv << "seed,block,sequence,family,kind,mode,threads,chain,passes,logical_threads,logical_chain,"
                      "logical_passes,repetition,gpu_ms,submit_ms,wall_ms,prefix_ms,primed_clean\n";
        segments_csv << "seed,block,sequence,part,label,start_utc_unix_ns,end_utc_unix_ns,before_utc_unix_ns,"
                        "before_cpu_ms,before_gpu_ms,before_gpu_submit_ms,before_gpu_wall_ms,after_utc_unix_ns,"
                        "after_cpu_ms,after_gpu_ms,after_gpu_submit_ms,after_gpu_wall_ms,cooldown_s\n";
        sweep_csv << "seed,block,sequence,material,n,alive,finite,ticks,gpu_samples,gpu_tick_median_ms,"
                     "gpu_tick_p95_ms,cpu_submit_median_ms,wall_ms_per_tick,backpressure,dispatches_per_tick,"
                     "barriers_per_tick,api_calls_per_tick,measured_start_utc_unix_ns,measured_end_utc_unix_ns\n";
        ticks_csv << "block,material,n,tick,gpu_ms,cpu_submit_ms\n";
        for (auto* stream : {&series_csv, &segments_csv, &sweep_csv, &ticks_csv}) *stream << std::setprecision(9);

        unsigned sequence = 0;
        double fastest_cpu = std::numeric_limits<double>::max(), fastest_gpu = fastest_cpu;
        const auto segment = [&](unsigned block, const char* part, const std::string& label, auto&& body) {
            auto before = probe.sample();
            fastest_cpu = std::min(fastest_cpu, before.cpu_ms);
            unsigned waited = 0;
            while (before.cpu_ms > 1.15 * fastest_cpu && waited < plan.max_cooldown_s) {
                SDL_Delay(10000);
                waited += 10;
                before = probe.sample();
                fastest_cpu = std::min(fastest_cpu, before.cpu_ms);
            }
            // After an idle wait the GPU sits in a low P-state; ~0.3 s (wall clock) of the busy prefix brings its
            // clock back before the probe that opens the segment. Part of the gate, so off in the quick plan (a
            // software renderer's timestamps and probes would keep it looping).
            fastest_gpu = std::min(fastest_gpu, before.gpu_ms);
            for (int round = 0; plan.max_cooldown_s > 0 && round < 5 && before.gpu_ms > 1.15 * fastest_gpu; ++round) {
                const auto warm_start = std::chrono::steady_clock::now();
                for (int i = 0;
                     i < 200 && std::chrono::steady_clock::now() - warm_start < std::chrono::milliseconds{300}; ++i)
                    static_cast<void>(
                            passes.run({SyntheticKind::chain, 32768, passes.prefix_chain(), 1, SubmitMode::cold}));
                before = probe.sample();
                fastest_gpu = std::min(fastest_gpu, before.gpu_ms);
            }
            const auto start = utc_now_ns();
            body();
            const auto end = utc_now_ns();
            const auto after = probe.sample();
            segments_csv << seed << ',' << block << ',' << sequence << ',' << part << ',' << label << ',' << start
                         << ',' << end << ',' << before.utc_unix_ns << ',' << before.cpu_ms << ',' << before.gpu_ms
                         << ',' << before.gpu_submit_ms << ',' << before.gpu_wall_ms << ',' << after.utc_unix_ns << ','
                         << after.cpu_ms << ',' << after.gpu_ms << ',' << after.gpu_submit_ms << ','
                         << after.gpu_wall_ms << ',' << waited << '\n';
            segments_csv.flush();
            ++sequence;
        };

        // Part 2 setup: material templates and the tank, from a world that is discarded before any measurement.
        Grain grain_template, water_template;
        const auto walls = tank().walls;
        if (plan.sweep) {
            auto made = ParticleWorld::create(shaders, execution, ProfileOptions{});
            require(made.has_value(), made ? "" : made.error());
            grain_template = first_alive(**made, Scene::avalanche);
            water_template = first_alive(**made, Scene::water_tank);
            require(grain_template.motion[3] != water_template.motion[3],
                    "Szablony ziarna i wody mają ten sam materiał");
            info << "grain_material=" << grain_template.motion[3] << " inverse_mass=" << grain_template.motion[2]
                 << "\nwater_material=" << water_template.motion[3] << " inverse_mass=" << water_template.motion[2]
                 << "\ntank=columns " << Tank::columns << " from x=" << Tank::left << ", floor y=" << Tank::floor
                 << ", one particle per cell from the floor up\n";
        }

        using Key = std::tuple<std::string, std::string, std::string, unsigned, unsigned, unsigned>;
        std::map<Key, std::vector<std::array<double, 3>>> series_samples;
        std::map<std::pair<std::string, unsigned>, std::vector<std::array<double, 2>>> sweep_samples;
        const auto base_series = series_plan(plan);
        for (unsigned block = 1; block <= plan.blocks; ++block) {
            // Part 1: every series is its own segment, in a new random order per block.
            auto items = base_series;
            shuffle(items, state);
            for (const auto& item : items) {
                const auto& s = item.series;
                const auto label = std::format("{}:{}:{}:T{}:L{}:M{}", item.family, synthetic_kind_name(s.kind),
                                               submit_mode_name(s.mode), item.threads, item.chain, item.passes);
                segment(block, "synthetic", label, [&] {
                    for (unsigned r = 0; r < plan.repetitions; ++r) {
                        const auto t = passes.run(s);
                        require(std::isfinite(t.gpu_ms) && t.gpu_ms >= 0 && t.wall_ms > 0,
                                "Nieprawidłowy czas serii " + label);
                        const bool clean = s.mode != SubmitMode::primed || t.submit_ms < t.prefix_ms;
                        series_csv << seed << ',' << block << ',' << sequence << ',' << item.family << ','
                                   << synthetic_kind_name(s.kind) << ',' << submit_mode_name(s.mode) << ',' << s.threads
                                   << ',' << s.chain << ',' << s.passes << ',' << item.threads << ',' << item.chain
                                   << ',' << item.passes << ',' << r << ',' << t.gpu_ms << ',' << t.submit_ms << ','
                                   << t.wall_ms << ',' << t.prefix_ms << ',' << clean << '\n';
                        series_samples[{item.family, std::string{synthetic_kind_name(s.kind)},
                                        std::string{submit_mode_name(s.mode)}, item.threads, item.chain, item.passes}]
                                .push_back({t.gpu_ms, t.submit_ms, t.wall_ms});
                    }
                });
            }
            series_csv.flush();
            if (!plan.sweep) continue;
            // Part 2: a fresh world per point, points in a new random order per block.
            std::vector<SweepPoint> points;
            for (const char* material : {"grains", "water"})
                for (unsigned n : plan.counts) points.push_back({material, n});
            shuffle(points, state);
            for (const auto& point : points) {
                segment(block, "sweep", std::format("{}:N{}", point.material, point.count), [&] {
                    auto made = ParticleWorld::create(shaders, execution, ProfileOptions{});
                    require(made.has_value(), made ? "" : made.error());
                    auto& world = **made;
                    const auto particles =
                            fill(point.material == "grains" ? grain_template : water_template, point.count);
                    world.load(particles, walls);
                    unsigned backpressure = 0;
                    // A step per tick and a poll every second tick (60 Hz frames at 120 Hz ticks); every 16 ticks a
                    // glFinish, because unpaced ticks would run more than the 32 tick timers ahead of the GPU and
                    // lose them (the game is paced by the clock and VSync). The wall time includes these syncs.
                    const auto advance = [&](unsigned ticks) {
                        for (unsigned n = 0; n < ticks; ++n) {
                            while (!world.step()) {
                                ++backpressure;
                                glFinish();
                                world.poll();
                            }
                            if (n % 16 == 15) glFinish();
                            if (n % 2 == 1) world.poll();
                        }
                    };
                    advance(plan.warmup);
                    glFinish();
                    world.poll();
                    static_cast<void>(world.take_timings());
                    static_cast<void>(world.take_cpu_timings());
                    backpressure = 0;
                    const auto measured_start = utc_now_ns();
                    const auto started = std::chrono::steady_clock::now();
                    advance(plan.measured);
                    glFinish();
                    const auto measured_end = utc_now_ns();
                    const double wall =
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                                    .count();
                    world.poll();
                    const auto gpu = world.take_timings(), cpu = world.take_cpu_timings();
                    require(gpu.size() * 10 >= plan.measured * 9 && !cpu.empty(),
                            "Pomiar zgubił ponad 10% czasów GPU ticka");
                    // Pass counts from a short collecting window after the measured one (collect costs CPU time).
                    auto profile = world.profile_options();
                    profile.collect = true;
                    world.set_profile_options(profile);
                    const auto calls_before = world.counters();
                    constexpr unsigned counted = 16;
                    advance(counted);
                    glFinish();
                    world.poll();
                    const auto calls = world.counters() - calls_before;
                    unsigned alive = 0;
                    bool finite = true;
                    for (const auto& p : world.readback())
                        if (p.motion[3] > .5F) {
                            ++alive;
                            for (float v : p.position) finite = finite && std::isfinite(v);
                            for (float v : p.motion) finite = finite && std::isfinite(v);
                        }
                    require(alive == point.count && finite,
                            std::format("{} N={}: liczba cząstek {} albo stan nieskończony", point.material,
                                        point.count, alive));
                    const auto per_tick = [&](std::uint64_t value) { return static_cast<double>(value) / counted; };
                    sweep_csv << seed << ',' << block << ',' << sequence << ',' << point.material << ',' << point.count
                              << ',' << alive << ',' << finite << ',' << plan.measured << ',' << gpu.size() << ','
                              << median(gpu) << ',' << percentile(gpu, .95) << ',' << median(cpu) << ','
                              << wall / plan.measured << ',' << backpressure << ',' << per_tick(calls.dispatches) << ','
                              << per_tick(calls.barriers) << ',' << per_tick(calls.api_calls) << ',' << measured_start
                              << ',' << measured_end << '\n';
                    sweep_csv.flush();
                    for (std::size_t i = 0; i < gpu.size(); ++i)
                        ticks_csv << block << ',' << point.material << ',' << point.count << ',' << i << ',' << gpu[i]
                                  << ',' << (i < cpu.size() ? cpu[i] : 0.0) << '\n';
                    sweep_samples[{point.material, point.count}].push_back({median(gpu), wall / plan.measured});
                });
            }
        }

        // Summaries computed here so the output stands alone; analyze-cost-model.py recomputes them without the
        // segments that telemetry disqualifies.
        std::ofstream summary{output / "series-summary.csv"}, fits{output / "series-fit.csv"};
        summary << std::setprecision(9) << "family,kind,mode,threads,chain,passes,samples,gpu_ms,submit_ms,wall_ms\n";
        std::map<std::tuple<std::string, std::string, unsigned, unsigned>, std::vector<std::pair<double, double>>>
                by_passes, by_passes_wall;
        for (const auto& [key, samples] : series_samples) {
            const auto& [family, kind, mode, t, l, m] = key;
            std::vector<double> g, s, w;
            for (const auto& v : samples) {
                g.push_back(v[0]);
                s.push_back(v[1]);
                w.push_back(v[2]);
            }
            summary << family << ',' << kind << ',' << mode << ',' << t << ',' << l << ',' << m << ',' << g.size()
                    << ',' << median(g) << ',' << median(s) << ',' << median(w) << '\n';
            if (family == "series") {
                by_passes[{kind, mode, t, l}].emplace_back(m, median(g));
                by_passes_wall[{kind, mode, t, l}].emplace_back(m, median(w));
            }
        }
        fits << std::setprecision(9)
             << "kind,mode,threads,chain,gpu_fixed_ms,gpu_ms_per_pass,gpu_r2,wall_fixed_ms,wall_ms_per_pass,wall_r2\n";
        for (const auto& [key, points] : by_passes) {
            if (points.size() < 2) continue;
            const auto& [kind, mode, t, l] = key;
            const auto g = fit(points), w = fit(by_passes_wall[key]);
            fits << kind << ',' << mode << ',' << t << ',' << l << ',' << g.a << ',' << g.b << ',' << g.r2 << ',' << w.a
                 << ',' << w.b << ',' << w.r2 << '\n';
        }
        if (plan.sweep) {
            std::ofstream sweep_fit{output / "sweep-fit.csv"};
            sweep_fit
                    << std::setprecision(9)
                    << "material,metric,points,a_ms,b_ms_per_1000,r2,knee_n,extrapolated_64k_ms,extrapolated_128k_ms\n";
            for (const char* material : {"grains", "water"})
                for (unsigned metric = 0; metric < 2; ++metric) {
                    std::vector<std::pair<double, double>> points;
                    for (unsigned n : plan.counts) {
                        std::vector<double> values;
                        for (const auto& v : sweep_samples[{material, n}]) values.push_back(v[metric]);
                        points.emplace_back(n, median(values));
                    }
                    const auto f = fit(points);
                    sweep_fit << material << ',' << (metric == 0 ? "gpu_tick_median_ms" : "wall_ms_per_tick") << ','
                              << points.size() << ',' << f.a << ',' << f.b * 1000 << ',' << f.r2 << ','
                              << (f.b > 0 ? f.a / f.b : 0) << ',' << f.a + f.b * 65536 << ',' << f.a + f.b * 131072
                              << '\n';
                }
        }
        info << "segments=" << sequence << "\nstatus=ok\n";
        return {};
    } catch (const std::exception& error) {
        return std::unexpected{std::string{"Model kosztu ticka: "} + error.what()};
    }
}
} // namespace strefa::gpu
