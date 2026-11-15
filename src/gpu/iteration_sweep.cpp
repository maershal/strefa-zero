// Z08, --gpu-check iteration-sweep: how many solver iterations the existing quality gates need.
// For every (PBF iterations, contact iterations) pair at two substeps the check runs the unchanged existing
// validations (fluid quality I07, liquids, grains, flow, coupling Q, mission) on worlds created with that
// FluidOptions, records PASS/FAIL per gate with the first failure message, extracts the I07 quality metrics from
// fluid-quality.csv, and measures GPU ms per tick (GL_TIMESTAMP pair around step()) and dispatches per tick on the
// scripted mission route and on the dam break, configurations in a random order per block, with the Z01 calibration
// probe and cool-down gate around every segment. Thresholds are the ones in the validations; nothing is relaxed.
// STREFA_ITERATION_SWEEP_PLAN = full (default) | gates | timing | quick.
#include "gpu/gl_api.hpp"
#include "gpu/render_device.hpp"
#include "gpu/synthetic_passes.hpp"
#include "gpu/validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
struct Config {
    unsigned pbf{}, contact{};
    std::string name() const { return std::format("{}/{}", pbf, contact); }
};
constexpr Config baseline{8, 12};
struct Plan {
    std::string name{"full"};
    std::vector<Config> configs{{8, 12}, {7, 11}, {6, 10}, {6, 8}, {5, 8}, {4, 6}};
    std::vector<std::string_view> gates{"fluid_quality", "liquids", "granular", "flow", "coupling", "mission"};
    unsigned blocks{4}, warmup{120}, measured{240}, max_cooldown_s{180};
};
constexpr std::array<std::string_view, 6> all_gates{"fluid_quality", "liquids",  "granular",
                                                    "flow",          "coupling", "mission"};
std::vector<std::string_view> split(std::string_view text) {
    std::vector<std::string_view> parts;
    while (!text.empty()) {
        const auto comma = text.find(',');
        parts.push_back(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return parts;
}
Plan named_plan(std::string_view name) {
    Plan plan;
    if (name.empty() || name == "full") return plan;
    if (name == "gates") {
        plan.name = "gates";
        plan.blocks = 0;
        return plan;
    }
    if (name == "timing") {
        plan.name = "timing";
        plan.gates.clear();
        return plan;
    }
    require(name == "quick", "STREFA_ITERATION_SWEEP_PLAN: full, gates, timing lub quick.");
    plan.name = "quick";
    plan.configs = {{8, 12}, {4, 6}};
    plan.gates = {"liquids", "granular"};
    plan.blocks = 1;
    plan.warmup = 12;
    plan.measured = 24;
    plan.max_cooldown_s = 0;
    return plan;
}
// STREFA_ITERATION_SWEEP_CONFIGS="6/9,5/10" and STREFA_ITERATION_SWEEP_GATES="granular,coupling" replace the lists
// of the named plan (follow-up runs at the pass/fail boundary and repeats of single gates).
Plan plan_from_environment() {
    const char* text = SDL_getenv("STREFA_ITERATION_SWEEP_PLAN");
    auto plan = named_plan(text ? text : "");
    if (const char* configs = SDL_getenv("STREFA_ITERATION_SWEEP_CONFIGS"); configs && *configs) {
        plan.configs.clear();
        for (const auto part : split(configs)) {
            Config c;
            const auto slash = part.find('/');
            const auto* end = part.data() + part.size();
            const auto a = std::from_chars(part.data(), part.data() + std::min(slash, part.size()), c.pbf);
            const auto b =
                    slash == std::string_view::npos ? a : std::from_chars(part.data() + slash + 1, end, c.contact);
            require(slash != std::string_view::npos && a.ec == std::errc{} && b.ec == std::errc{} && b.ptr == end,
                    "STREFA_ITERATION_SWEEP_CONFIGS: lista PBF/kontakt, np. 6/9,5/10.");
            plan.configs.push_back(c);
        }
    }
    if (const char* gates = SDL_getenv("STREFA_ITERATION_SWEEP_GATES"); gates && *gates) {
        plan.gates.clear();
        for (const auto part : split(gates)) {
            const auto it = std::ranges::find(all_gates, part);
            require(it != all_gates.end(), "STREFA_ITERATION_SWEEP_GATES: nieznana bramka " + std::string{part});
            plan.gates.push_back(*it);
        }
    }
    return plan;
}
std::uint64_t seed_from_environment() {
    const char* text = SDL_getenv("STREFA_BENCHMARK_SEED");
    if (!text || !*text) {
        std::random_device device;
        return (std::uint64_t{device()} << 32U) ^ device();
    }
    const std::string_view view{text};
    std::uint64_t seed{};
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), seed);
    require(error == std::errc{} && end == view.data() + view.size(),
            "STREFA_BENCHMARK_SEED musi być liczbą całkowitą bez znaku.");
    return seed;
}
std::uint64_t splitmix(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}
std::int64_t utc_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}
double percentile(std::vector<double> values, double q) {
    std::ranges::sort(values);
    return values.empty() ? 0 : values[static_cast<std::size_t>(q * static_cast<double>(values.size() - 1))];
}
ExecutionOptions with(const ExecutionOptions& layout, Config c) {
    auto e = layout;
    e.fluid.substeps = 2;
    e.fluid.pbf_iterations = c.pbf;
    e.fluid.contact_iterations = c.contact;
    require(e.fluid.valid(), "Nieprawidłowa konfiguracja cieczy " + c.name());
    return e;
}
using GateResult = std::expected<void, std::string>;
GateResult run_gate(std::string_view gate, const std::filesystem::path& shaders, const ExecutionOptions& e,
                    const std::filesystem::path& dir) {
    if (gate == "fluid_quality") return validate_fluid_quality(shaders, dir, e, false);
    if (gate == "coupling") return validate_coupling(dir, false, false, false, e);
    auto made = ParticleWorld::create(shaders, e);
    if (!made) return std::unexpected{made.error()};
    auto& world = **made;
    if (gate == "liquids") return validate_liquids(world, dir);
    if (gate == "granular") return validate_granular(world, dir);
    if (gate == "flow") return validate_flow(world, dir);
    if (gate == "mission") return validate_mission(world, dir);
    return std::unexpected{"Nieznana bramka " + std::string{gate}};
}
// I07 quality metrics from the fluid-quality.csv of one configuration: compression and kinetic energy at tick 1200,
// surface amplitude (range of the mean surface height) and mean cluster fraction over ticks 600-1200.
void summarize_fluid(const std::filesystem::path& csv, Config c, std::ostream& out) {
    std::ifstream in{csv};
    if (!in) return;
    struct Window {
        double lo{1e300}, hi{-1e300}, cluster{}, compression{}, energy{};
        unsigned samples{}, last{};
    };
    std::map<int, Window> scenes;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        std::vector<double> v;
        std::stringstream fields{line};
        for (std::string f; std::getline(fields, f, ',');) v.push_back(std::stod(f));
        if (v.size() < 13) continue;
        const int scene = static_cast<int>(v[0]);
        const auto tick = static_cast<unsigned>(v[1]);
        auto& w = scenes[scene];
        if (tick >= 600) {
            w.lo = std::min(w.lo, v[7]);
            w.hi = std::max(w.hi, v[7]);
            w.cluster += v[8];
            ++w.samples;
        }
        if (tick >= w.last) {
            w.last = tick;
            w.compression = v[5];
            w.energy = v[12];
        }
    }
    for (const auto& [scene, w] : scenes)
        out << c.pbf << ',' << c.contact << ',' << scene_ids[static_cast<std::size_t>(scene)] << ',' << w.last << ','
            << w.compression << ',' << (w.samples ? w.hi - w.lo : 0.0) << ','
            << (w.samples ? w.cluster / w.samples : 0.0) << ',' << w.energy << '\n';
}
constexpr std::array<std::string_view, 2> timed_scenes{"mission", "dam-break"};
} // namespace

std::expected<void, std::string> sweep_solver_iterations(const std::filesystem::path& shaders,
                                                         const std::filesystem::path& output,
                                                         const ExecutionOptions& layout) {
    try {
        std::filesystem::create_directories(output);
        const auto plan = plan_from_environment();
        const auto seed = seed_from_environment();
        auto state = seed;
        const auto text = [](GLenum name) {
            const auto* value = reinterpret_cast<const char*>(glGetString(name));
            return std::string{value ? value : "?"};
        };
        std::ofstream info{output / "iteration-sweep-info.txt"};
        require(info.good(), "Nie można zapisać wyników iteration-sweep.");
        info << "schema=1\nplan=" << plan.name << "\nseed=" << seed << "\nrenderer=" << text(GL_RENDERER)
             << "\nversion=" << text(GL_VERSION) << "\nsubsteps=2\nconfigs=";
        for (std::size_t i = 0; i < plan.configs.size(); ++i) info << (i ? "," : "") << plan.configs[i].name();
        info << "\ngates=";
        for (std::size_t i = 0; i < plan.gates.size(); ++i) info << (i ? "," : "") << plan.gates[i];
        info << "\nwarmup=" << plan.warmup << "\nmeasured=" << plan.measured << "\nblocks=" << plan.blocks << '\n';
        info.flush();

        // Part 1: gates. A failing configuration is a result, not an error; only a failing baseline is.
        std::string baseline_failure;
        if (!plan.gates.empty()) {
            std::ofstream gates{output / "iteration-sweep-gates.csv"}, fluid{output / "iteration-sweep-fluid.csv"};
            require(gates && fluid, "Nie można zapisać wyników bramek iteration-sweep.");
            gates << std::setprecision(6) << "pbf,contact,gate,result,seconds,gl_errors,message\n";
            fluid << std::setprecision(9)
                  << "pbf,contact,scene,last_tick,compression,surface_amplitude_600_1200,cluster_fraction_600_1200,"
                     "kinetic_energy\n";
            for (const auto config : plan.configs) {
                const auto e = with(layout, config);
                for (const auto gate : plan.gates) {
                    const auto dir = output / std::format("p{}-c{}", config.pbf, config.contact) / gate;
                    std::filesystem::create_directories(dir);
                    const auto started = SDL_GetTicksNS();
                    GateResult result;
                    try {
                        result = run_gate(gate, shaders, e, dir);
                    } catch (const std::exception& error) {
                        result = std::unexpected{std::string{error.what()}};
                    }
                    const double seconds = static_cast<double>(SDL_GetTicksNS() - started) / 1e9;
                    // A gate that throws mid-way can leave a GL error behind; count and clear it here so it is
                    // attributed to this gate and not to the next one or to the cost measurement.
                    unsigned gl_errors = 0;
                    while (glGetError() != GL_NO_ERROR && gl_errors < 64) ++gl_errors;
                    auto message = result ? std::string{} : result.error();
                    std::ranges::replace(message, ',', ';');
                    std::ranges::replace(message, '\n', ' ');
                    gates << config.pbf << ',' << config.contact << ',' << gate << ',' << (result ? "PASS" : "FAIL")
                          << ',' << seconds << ',' << gl_errors << ',' << message << '\n';
                    gates.flush();
                    SDL_Log("iteration-sweep %s %.*s: %s", config.name().c_str(), static_cast<int>(gate.size()),
                            gate.data(), result ? "PASS" : message.c_str());
                    if (!result && config.pbf == baseline.pbf && config.contact == baseline.contact
                        && baseline_failure.empty())
                        baseline_failure = std::format("Konfiguracja bazowa {} nie przechodzi bramki {}: {}",
                                                       baseline.name(), gate, message);
                    if (gate == "fluid_quality") summarize_fluid(dir / "fluid-quality.csv", config, fluid);
                }
            }
            require(gates.good() && fluid.good(), "Niepełny zapis bramek iteration-sweep.");
        }

        // Part 2: cost. Fresh world per segment, configurations shuffled per block.
        if (plan.blocks > 0) {
            detail::Api gl;
            gl.load();
            // A destroyed gate world can leave its deleted program current; the calibration probe saves and restores
            // GL_CURRENT_PROGRAM, and restoring that handle after the probe unbinds it raises GL_INVALID_VALUE.
            gl.UseProgram(0);
            auto probe_made = CalibrationProbe::create();
            require(probe_made.has_value(), probe_made ? "" : probe_made.error());
            auto& probe = *probe_made;
            std::ofstream segments{output / "iteration-sweep-timing.csv"};
            require(segments.good(), "Nie można zapisać pomiaru iteration-sweep.");
            segments << std::setprecision(9)
                     << "seed,block,sequence,pbf,contact,scene,start_utc_unix_ns,end_utc_unix_ns,before_cpu_ms,"
                        "before_gpu_ms,after_cpu_ms,after_gpu_ms,cooldown_s,ticks,gpu_median_ms,gpu_p95_ms,"
                        "gpu_mean_ms,cpu_median_ms,dispatches_per_tick,indirect_per_tick,barriers_per_tick\n";
            std::shared_ptr<RenderDevice> device;
            double fastest_cpu = 1e300, fastest_gpu = 1e300;
            unsigned sequence = 0;
            for (unsigned block = 1; block <= plan.blocks; ++block) {
                std::vector<std::pair<Config, std::string_view>> order;
                for (const auto config : plan.configs)
                    for (const auto scene : timed_scenes) order.emplace_back(config, scene);
                for (std::size_t i = order.size(); i > 1; --i)
                    std::swap(order[i - 1], order[static_cast<std::size_t>(splitmix(state) % i)]);
                for (const auto& [config, scene] : order) {
                    auto before = probe.sample();
                    fastest_cpu = std::min(fastest_cpu, before.cpu_ms);
                    unsigned waited = 0;
                    while (before.cpu_ms > 1.15 * fastest_cpu && waited < plan.max_cooldown_s) {
                        SDL_Delay(10000);
                        waited += 10;
                        before = probe.sample();
                        fastest_cpu = std::min(fastest_cpu, before.cpu_ms);
                    }
                    fastest_gpu = std::min(fastest_gpu, before.gpu_ms);
                    for (int round = 0; plan.max_cooldown_s > 0 && round < 5 && before.gpu_ms > 1.15 * fastest_gpu;
                         ++round) {
                        const auto warm = std::chrono::steady_clock::now();
                        while (std::chrono::steady_clock::now() - warm < std::chrono::milliseconds{300})
                            static_cast<void>(probe.passes().run(
                                    {SyntheticKind::chain, 32768, probe.passes().prefix_chain(), 1, SubmitMode::cold}));
                        before = probe.sample();
                        fastest_gpu = std::min(fastest_gpu, before.gpu_ms);
                    }
                    auto made = ParticleWorld::create(shaders, with(layout, config), {}, {}, device);
                    require(made.has_value(), made ? "" : made.error());
                    auto& world = **made;
                    if (!device) device = world.render_device();
                    const bool mission = scene == "mission";
                    if (mission)
                        world.reset_mission();
                    else
                        world.reset(Scene::dam_break);
                    unsigned tick = 0;
                    const auto advance = [&] {
                        if (mission) {
                            while (!world.step(scripted_mission_input(tick))) glFinish();
                        } else {
                            while (!world.step()) glFinish();
                        }
                        if (++tick % 16 == 0) world.poll(false);
                    };
                    for (unsigned n = 0; n < plan.warmup; ++n) advance();
                    glFinish();
                    world.poll(false);
                    world.invalidate_gl_state();
                    std::vector<std::array<GLuint, 2>> queries(plan.measured);
                    std::vector<double> cpu(plan.measured);
                    for (auto& q : queries) gl.GenQueries(2, q.data());
                    const auto start = utc_now_ns();
                    for (unsigned n = 0; n < plan.measured; ++n) {
                        const auto began = SDL_GetTicksNS();
                        gl.QueryCounter(queries[n][0], GL_TIMESTAMP);
                        advance();
                        gl.QueryCounter(queries[n][1], GL_TIMESTAMP);
                        cpu[n] = static_cast<double>(SDL_GetTicksNS() - began) / 1e6;
                    }
                    glFinish();
                    const auto end = utc_now_ns();
                    world.invalidate_gl_state();
                    std::vector<double> gpu(plan.measured);
                    for (unsigned n = 0; n < plan.measured; ++n) {
                        std::array<GLuint64, 2> stamp{};
                        for (unsigned k = 0; k < 2; ++k)
                            gl.GetQueryObjectui64v(queries[n][k], GL_QUERY_RESULT, &stamp[k]);
                        gl.DeleteQueries(2, queries[n].data());
                        gpu[n] = static_cast<double>(stamp[1] - stamp[0]) / 1e6;
                    }
                    // Pass counts from a short collecting window after the measured one (collect costs CPU time).
                    auto profile = world.profile_options();
                    profile.collect = true;
                    world.set_profile_options(profile);
                    const auto calls_before = world.counters();
                    constexpr unsigned counted = 16;
                    for (unsigned n = 0; n < counted; ++n) advance();
                    glFinish();
                    world.poll(false);
                    const auto calls = world.counters() - calls_before;
                    const auto per_tick = [](std::uint64_t value) { return static_cast<double>(value) / counted; };
                    const auto after = probe.sample();
                    double mean = 0;
                    for (const double v : gpu) mean += v / static_cast<double>(gpu.size());
                    segments << seed << ',' << block << ',' << sequence << ',' << config.pbf << ',' << config.contact
                             << ',' << scene << ',' << start << ',' << end << ',' << before.cpu_ms << ','
                             << before.gpu_ms << ',' << after.cpu_ms << ',' << after.gpu_ms << ',' << waited << ','
                             << plan.measured << ',' << percentile(gpu, .5) << ',' << percentile(gpu, .95) << ','
                             << mean << ',' << percentile(cpu, .5) << ',' << per_tick(calls.dispatches) << ','
                             << per_tick(calls.indirect_dispatches) << ',' << per_tick(calls.barriers) << '\n';
                    segments.flush();
                    ++sequence;
                }
            }
            require(glGetError() == GL_NO_ERROR, "Błąd OpenGL w pomiarze iteration-sweep.");
            require(segments.good(), "Niepełny zapis pomiaru iteration-sweep.");
        }
        require(baseline_failure.empty(), baseline_failure);
        info << "result=completed\n";
        require(info.good(), "Niepełny zapis wyników iteration-sweep.");
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "iteration-sweep-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
