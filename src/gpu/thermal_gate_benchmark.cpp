// --gpu-check thermal-gate: ABBA measurement of the exact gas-stage gate (ThermalOptions::gas_gate) in one process.
// Every block draws a scene order (cold, idle, fire, quench); every scene runs four segments in the order off, on, on,
// off, each from a fresh world: 120 warm-up ticks, then 240 measured ticks with a GL_TIMESTAMP pair around step().
// Calibration probes (Z01) bracket every segment, with the cost-model cool-down gate before it. Analysis with
// scripts/profiling/analyze-thermal-gate.py (via profile-thermal-gate.ps1, which also records GPU telemetry).
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
#include <fstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
struct Plan {
    std::string name{"full"};
    unsigned blocks{4}, warmup{120}, measured{240}, max_cooldown_s{180};
};
Plan plan_from_environment() {
    const char* text = SDL_getenv("STREFA_THERMAL_GATE_PLAN");
    const std::string_view name = text ? text : "";
    if (name.empty() || name == "full") return {};
    require(name == "quick", "STREFA_THERMAL_GATE_PLAN: full lub quick.");
    return {"quick", 1, 12, 24, 0};
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
std::int64_t utc_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}
constexpr std::array<std::string_view, 4> scene_names{"cold", "idle", "fire", "quench"};
void load(ParticleWorld& world, unsigned scene, bool gate) {
    ThermalOptions options;
    options.gas_gate = gate;
    world.set_thermal_options(options); // reset_thermal_scene keeps the options and enables thermal
    if (scene <= 1) {
        options.enabled = true;
        world.set_thermal_options(options);
        world.reset(scene == 0 ? Scene::sand_water : Scene::empty);
    } else {
        world.reset_thermal_scene(scene == 2 ? 1 : 3);
    }
}
double percentile(std::vector<double> values, double q) {
    std::ranges::sort(values);
    return values.empty() ? 0 : values[static_cast<std::size_t>(q * static_cast<double>(values.size() - 1))];
}
} // namespace

std::expected<void, std::string> benchmark_thermal_gate(const std::filesystem::path& shaders,
                                                        const std::filesystem::path& output,
                                                        const ExecutionOptions& layout) {
    try {
        std::filesystem::create_directories(output);
        const auto plan = plan_from_environment();
        const auto seed = seed_from_environment();
        auto state = seed;
        detail::Api gl;
        gl.load();
        auto probe_made = CalibrationProbe::create();
        require(probe_made.has_value(), probe_made ? "" : probe_made.error());
        auto& probe = *probe_made;
        std::ofstream info{output / "thermal-gate-info.txt"}, segments{output / "thermal-gate-segments.csv"},
                ticks{output / "thermal-gate-ticks.csv"};
        require(info && segments && ticks, "Nie można zapisać wyników thermal-gate.");
        const auto text = [](GLenum name) {
            const auto* value = reinterpret_cast<const char*>(glGetString(name));
            return std::string{value ? value : "?"};
        };
        info << "schema=1\nplan=" << plan.name << "\nseed=" << seed << "\nrenderer=" << text(GL_RENDERER)
             << "\nversion=" << text(GL_VERSION) << "\nscenes=cold,idle,fire,quench\norder=off,on,on,off"
             << "\nwarmup=" << plan.warmup << "\nmeasured=" << plan.measured << '\n';
        segments << std::setprecision(9)
                 << "seed,block,sequence,scene,variant,order,start_utc_unix_ns,end_utc_unix_ns,before_utc_unix_ns,"
                    "before_cpu_ms,before_gpu_ms,after_utc_unix_ns,after_cpu_ms,after_gpu_ms,cooldown_s,ticks,"
                    "gpu_median_ms,gpu_p95_ms,gpu_mean_ms,cpu_median_ms,gated_ticks,skipped_ticks\n";
        ticks << std::setprecision(9) << "sequence,tick,gpu_ms,cpu_ms\n";
        std::shared_ptr<RenderDevice> device;
        double fastest_cpu = 1e300, fastest_gpu = 1e300;
        unsigned sequence = 0;
        for (unsigned block = 1; block <= plan.blocks; ++block) {
            const auto order = thermal_benchmark_order(state);
            info << "block_" << block << "_scenes=";
            for (unsigned i = 0; i < order.size(); ++i) info << (i ? "," : "") << scene_names[order[i]];
            info << '\n';
            for (const unsigned scene : order)
                for (unsigned position = 0; position < 4; ++position) {
                    const bool gate = position == 1 || position == 2;
                    // Cool-down gate of the cost model: CPU probe within 15% of the fastest, GPU clock warmed up.
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
                    auto made = ParticleWorld::create(shaders, layout, {}, {}, device);
                    require(made.has_value(), made ? "" : made.error());
                    auto& world = **made;
                    if (!device) device = world.render_device();
                    world.set_vfx_options({VfxQuality::off, false});
                    load(world, scene, gate);
                    for (unsigned n = 0; n < plan.warmup; ++n) {
                        while (!world.step()) glFinish();
                        if (n % 16 == 15) world.poll(false);
                    }
                    glFinish();
                    const auto gate_before = world.read_thermal(ThermalRead::totals).totals;
                    world.invalidate_gl_state();
                    std::vector<std::array<GLuint, 2>> queries(plan.measured);
                    std::vector<double> cpu(plan.measured);
                    for (auto& q : queries) gl.GenQueries(2, q.data());
                    const auto start = utc_now_ns();
                    for (unsigned n = 0; n < plan.measured; ++n) {
                        const auto began = SDL_GetTicksNS();
                        gl.QueryCounter(queries[n][0], GL_TIMESTAMP);
                        while (!world.step()) glFinish();
                        gl.QueryCounter(queries[n][1], GL_TIMESTAMP);
                        cpu[n] = static_cast<double>(SDL_GetTicksNS() - began) / 1e6;
                        if (n % 16 == 15) world.poll(false);
                    }
                    glFinish();
                    const auto end = utc_now_ns();
                    const auto gate_after = world.read_thermal(ThermalRead::totals).totals;
                    world.invalidate_gl_state();
                    std::vector<double> gpu(plan.measured);
                    for (unsigned n = 0; n < plan.measured; ++n) {
                        std::array<GLuint64, 2> stamp{};
                        for (unsigned k = 0; k < 2; ++k)
                            gl.GetQueryObjectui64v(queries[n][k], GL_QUERY_RESULT, &stamp[k]);
                        gl.DeleteQueries(2, queries[n].data());
                        gpu[n] = static_cast<double>(stamp[1] - stamp[0]) / 1e6;
                        ticks << sequence << ',' << n << ',' << gpu[n] << ',' << cpu[n] << '\n';
                    }
                    const auto after = probe.sample();
                    double mean = 0;
                    for (const double v : gpu) mean += v / static_cast<double>(gpu.size());
                    segments << seed << ',' << block << ',' << sequence << ',' << scene_names[scene] << ','
                             << (gate ? "on" : "off") << ',' << position << ',' << start << ',' << end << ','
                             << before.utc_unix_ns << ',' << before.cpu_ms << ',' << before.gpu_ms << ','
                             << after.utc_unix_ns << ',' << after.cpu_ms << ',' << after.gpu_ms << ',' << waited << ','
                             << plan.measured << ',' << percentile(gpu, .5) << ',' << percentile(gpu, .95) << ','
                             << mean << ',' << percentile(cpu, .5) << ','
                             << gate_after.gate_ticks - gate_before.gate_ticks << ','
                             << gate_after.gate_skips - gate_before.gate_skips << '\n';
                    segments.flush();
                    ticks.flush();
                    ++sequence;
                }
        }
        require(glGetError() == GL_NO_ERROR, "Błąd OpenGL w pomiarze thermal-gate.");
        info << "result=completed\n";
        require(info.good() && segments.good() && ticks.good(), "Niepełny zapis wyników thermal-gate.");
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "thermal-gate-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
