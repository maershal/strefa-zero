#include "gpu/validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace strefa::gpu {
namespace {
constexpr std::array<std::string_view, 4> scene_names{"cold", "fire", "oil", "quench"};
constexpr std::array<std::string_view, 4> stage_names{"off", "heat", "heat_gas", "full"};

// GL_ARB_timer_query entry points (core in 3.3), loaded here so the benchmark needs no ParticleWorld internals.
struct TimerQueries {
    PFNGLGENQUERIESPROC gen{};
    PFNGLDELETEQUERIESPROC remove{};
    PFNGLQUERYCOUNTERPROC counter{};
    PFNGLGETQUERYOBJECTUI64VPROC result{};
    TimerQueries() {
        gen = reinterpret_cast<PFNGLGENQUERIESPROC>(SDL_GL_GetProcAddress("glGenQueries"));
        remove = reinterpret_cast<PFNGLDELETEQUERIESPROC>(SDL_GL_GetProcAddress("glDeleteQueries"));
        counter = reinterpret_cast<PFNGLQUERYCOUNTERPROC>(SDL_GL_GetProcAddress("glQueryCounter"));
        result = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VPROC>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
        if (!gen || !remove || !counter || !result) throw std::runtime_error{"Brak zapytań czasu OpenGL."};
    }
};

std::int64_t utc_unix_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}

std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E37'79B9'7F4A'7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return z ^ (z >> 31U);
}

// STREFA_BENCHMARK_SEED repeats an order; otherwise every run draws a new seed. Either way it is in every row.
std::uint64_t benchmark_seed() {
    const std::string_view text = SDL_getenv("STREFA_BENCHMARK_SEED") ? SDL_getenv("STREFA_BENCHMARK_SEED") : "";
    if (text.empty()) {
        std::random_device device;
        return (std::uint64_t{device()} << 32U) ^ device();
    }
    std::uint64_t seed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error{"STREFA_BENCHMARK_SEED musi być liczbą całkowitą bez znaku."};
    return seed;
}
} // namespace

std::array<unsigned, 4> thermal_benchmark_order(std::uint64_t& state) {
    std::array<unsigned, 4> order{0, 1, 2, 3};
    for (unsigned i = static_cast<unsigned>(order.size()) - 1; i > 0; --i)
        std::swap(order[i], order[static_cast<std::size_t>(splitmix64(state) % (i + 1))]);
    return order;
}

std::expected<void, std::string> benchmark_thermal(ParticleWorld& world, const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        const TimerQueries gl;
        const auto seed = benchmark_seed();
        auto state = seed;
        const auto original = world.profile_options();
        auto profile = original;
        profile.gpu_timers = false;
        profile.timer_mode = TimerMode::off;
        world.set_profile_options(profile);
        world.set_vfx_options({VfxQuality::off, false});
        std::ofstream csv{output / "thermal-samples.csv"}, passes{output / "thermal-pass-samples.csv"},
                operations{output / "thermal-op-samples.csv"}, pools{output / "thermal-pool-segments.csv"},
                segments{output / "thermal-profile-segments.csv"}, info{output / "thermal-benchmark-info.txt"};
        if (!csv || !passes || !operations || !pools || !segments || !info)
            throw std::runtime_error{"Nie można zapisać wyników benchmarku termiki."};
        csv << std::setprecision(12)
            << "seed,sequence,scene,stage,block,order,variant,tick,rendered,physics_gpu_ms,render_gpu_ms,"
               "frame_gpu_ms,cpu_submit_ms,memory_bytes\n";
        passes << std::setprecision(12)
               << "seed,sequence,scene,block,tick,heat_ms,gas_ms,reaction_ms,cpu_ms,dispatches,barriers\n";
        operations << std::setprecision(12) << "seed,sequence,scene,block,tick,index,op,gpu_ms\n";
        pools << std::setprecision(12)
              << "seed,sequence,scene,stage,block,order,variant,mass,H,chemical,oxygen,fuel,"
                 "steam,smoke,finite,start_utc_unix_ns,end_utc_unix_ns\n";
        segments << "seed,sequence,scene,block,start_utc_unix_ns,end_utc_unix_ns\n";
        info << "schema=2\nseed=" << seed
             << "\nscene_order=random_per_block_splitmix64_fisher_yates\nseed_override=STREFA_BENCHMARK_SEED\n";
        info.flush();
        unsigned sequence = 0;
        const auto load = [&](unsigned scene) {
            if (scene == 0)
                world.reset(Scene::sand_water);
            else
                world.reset_thermal_scene(scene);
        };
        const auto advance = [&](unsigned ticks, unsigned poll_every) {
            for (unsigned n = 0; n < ticks; ++n) {
                while (!world.step()) glFinish();
                if (n % 2 == 1) world.render(1);
                if (n % poll_every == poll_every - 1) {
                    glFinish();
                    world.poll();
                }
            }
            glFinish();
            world.poll();
        };
        // Blocks are the outer loop and each block draws its own scene order, so laptop heating spreads over every
        // scene instead of landing on the last one (fixed cold -> fire -> oil -> quench measured the warm-up).
        for (unsigned block = 1; block <= 3; ++block) {
            const auto order_of_scenes = thermal_benchmark_order(state);
            info << "block_" << block << "_scenes=";
            for (unsigned i = 0; i < order_of_scenes.size(); ++i)
                info << (i ? "," : "") << scene_names[order_of_scenes[i]];
            info << '\n';
            info.flush();
            for (const unsigned scene : order_of_scenes) {
                for (unsigned stage = 0; stage < 4; ++stage)
                    for (unsigned order = 0; order < 4; ++order) {
                        ++sequence;
                        const bool candidate = order == 1 || order == 2;
                        const unsigned mode = candidate ? stage : 0;
                        if (scene == 0) {
                            ThermalOptions t;
                            t.enabled = true;
                            world.set_thermal_options(t);
                        }
                        load(scene);
                        ThermalOptions t;
                        t.enabled = mode != 0;
                        t.transport = mode >= 2;
                        t.reactions = mode >= 3;
                        world.set_thermal_options(t);
                        const auto started = utc_unix_ns();
                        advance(120, 16);
                        struct Row {
                            std::array<GLuint, 3> q{};
                            double cpu{};
                        };
                        std::array<Row, 240> rows{};
                        for (auto& r : rows) gl.gen(3, r.q.data());
                        for (unsigned n = 0; n < rows.size(); ++n) {
                            auto& row = rows[n];
                            const auto began = SDL_GetTicksNS();
                            gl.counter(row.q[0], GL_TIMESTAMP);
                            while (!world.step()) glFinish();
                            gl.counter(row.q[1], GL_TIMESTAMP);
                            if (n % 2 == 1) world.render(1);
                            gl.counter(row.q[2], GL_TIMESTAMP);
                            row.cpu = static_cast<double>(SDL_GetTicksNS() - began) / 1e6;
                            if (n % 16 == 15) {
                                glFinish();
                                world.poll();
                            }
                        }
                        glFinish();
                        world.poll();
                        GLuint64 previous_start = 0;
                        for (unsigned n = 0; n < rows.size(); ++n) {
                            const auto& row = rows[n];
                            std::array<GLuint64, 3> stamp{};
                            for (unsigned k = 0; k < 3; ++k) gl.result(row.q[k], GL_QUERY_RESULT, &stamp[k]);
                            if (n % 2 == 0) previous_start = stamp[0];
                            csv << seed << ',' << sequence << ',' << scene_names[scene] << ',' << stage_names[stage]
                                << ',' << block << ',' << order << ',' << stage_names[mode] << ',' << n << ','
                                << (n % 2) << ',' << static_cast<double>(stamp[1] - stamp[0]) / 1e6 << ','
                                << static_cast<double>(stamp[2] - stamp[1]) / 1e6 << ','
                                << (n % 2 ? static_cast<double>(stamp[2] - previous_start) / 1e6 : 0) << ',' << row.cpu
                                << ',' << world.thermal_memory() << '\n';
                            gl.remove(3, row.q.data());
                        }
                        const auto stat = world.read_thermal().totals;
                        pools << seed << ',' << sequence << ',' << scene_names[scene] << ',' << stage_names[stage]
                              << ',' << block << ',' << order << ',' << stage_names[mode] << ',' << stat.mass << ','
                              << stat.enthalpy << ',' << stat.chemical << ',' << stat.oxygen << ',' << stat.fuel << ','
                              << stat.steam << ',' << stat.smoke << ',' << stat.finite << ',' << started << ','
                              << utc_unix_ns() << '\n';
                        csv.flush();
                        pools.flush();
                        if (block == 1 && order == 1) {
                            world.render(1);
                            world.save_image(output
                                             / (std::string{scene_names[scene]} + "-" + std::string{stage_names[mode]}
                                                + ".bmp"));
                        }
                    }
                // Stage profile of this scene in this block: per-stage and per-operation GPU time, dispatches and
                // barriers. The gas stage does not depend on the scene, so analyze-thermal.py uses it as a clock probe.
                ++sequence;
                load(scene);
                ThermalOptions detailed;
                detailed.enabled = detailed.profile = true;
                world.set_thermal_options(detailed);
                world.take_thermal_timings(); // drop timings of earlier segments
                const auto started = utc_unix_ns();
                advance(120, 8);
                segments << seed << ',' << sequence << ',' << scene_names[scene] << ',' << block << ',' << started
                         << ',' << utc_unix_ns() << '\n';
                for (const auto& row : world.take_thermal_timings()) {
                    passes << seed << ',' << sequence << ',' << scene_names[scene] << ',' << block << ',' << row.tick
                           << ',' << row.heat_ms << ',' << row.gas_ms << ',' << row.reaction_ms << ',' << row.cpu_ms
                           << ',' << row.dispatches << ',' << row.barriers << '\n';
                    for (unsigned i = 0; i < row.operations.size(); ++i)
                        operations << seed << ',' << sequence << ',' << scene_names[scene] << ',' << block << ','
                                   << row.tick << ',' << i << ',' << row.operations[i].kernel << ','
                                   << row.operations[i].gpu_ms << '\n';
                }
                passes.flush();
                operations.flush();
                segments.flush();
            }
        }
        world.set_profile_options(original);
        world.set_thermal_options({});
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error{"Błąd OpenGL w benchmarku termiki."};
        info << "result=completed\n";
        if (!csv.flush() || !passes.flush() || !operations.flush() || !pools.flush() || !info.flush())
            throw std::runtime_error{"Niepełny zapis wyników benchmarku termiki."};
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "thermal-benchmark-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
