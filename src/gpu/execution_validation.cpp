#include "gpu/dispatch_layout.hpp"
#include "gpu/validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
void drain(ParticleWorld& world) {
    glFinish();
    world.poll();
}
void clear(ParticleWorld& world) {
    static_cast<void>(world.take_timings());
    static_cast<void>(world.take_render_timings());
    static_cast<void>(world.take_cpu_timings());
    static_cast<void>(world.take_step_profiles());
    static_cast<void>(world.take_group_profiles());
}
double percentile(std::vector<double> values, double q) {
    std::ranges::sort(values);
    return values.empty() ? 0
                          : values[std::min(values.size() - 1,
                                            static_cast<std::size_t>(q * static_cast<double>(values.size())))];
}
void scene(ParticleWorld& world, unsigned count) {
    if (count == 3096) {
        world.reset_mission();
        return;
    }
    std::vector<Grain> grains(count);
    std::vector<std::uint32_t> walls(width * height);
    for (int y = 187; y < height; ++y)
        for (int x = 0; x < width; ++x) walls[y * width + x] = 1;
    for (unsigned i = 0; i < count; ++i) {
        const float x = 2.5F + static_cast<float>(i % 300), y = 2.5F + static_cast<float>(i / 300);
        grains[i].position = {x, y, x, y};
        grains[i].motion = {0, 0, 1, 1};
    }
    world.load(grains, walls);
    MissionConfig config;
    config.spawn_x = 310;
    world.configure_mission(config);
}
struct Variant {
    const char* name;
    ExecutionOptions options;
};
std::vector<Variant> variants() {
    auto reference = ExecutionOptions::reference();
    auto bindings = reference;
    bindings.cached_bindings = true;
    auto passes = bindings;
    passes.specialized = true;
    auto fifo = passes;
    fifo.fifo_queries = true;
    ExecutionOptions optimized;
    optimized.local_size = 256;
    optimized.reduction_size = 256;
    auto size128 = optimized;
    size128.local_size = 128;
    auto size64 = optimized;
    size64.local_size = 64;
    auto reduction128 = optimized;
    reduction128.reduction_size = 128;
    auto reduction64 = optimized;
    reduction64.reduction_size = 64;
    return {{"reference", reference}, {"bindings", bindings},          {"passes", passes},
            {"fifo", fifo},           {"optimized_256", optimized},    {"optimized_128", size128},
            {"optimized_64", size64}, {"reduction_128", reduction128}, {"reduction_64", reduction64}};
}
void pbf_dispatch_checks(const std::filesystem::path& shaders, std::ostream& checks) {
    ProfileOptions quiet;
    quiet.gpu_timers = false;
    quiet.readback_period = 0;
    const std::vector<std::uint32_t> walls(width * height);
    for (unsigned size : {64U, 128U, 256U}) {
        ExecutionOptions options;
        options.pbf_size = size;
        auto made = ParticleWorld::create(shaders, options, quiet);
        require(made.has_value(), made ? "" : made.error().c_str());
        auto& world = **made;
        double max_error = 0;
        for (unsigned count : {0U, 1U, size - 1, size, size + 1, 255U, 256U, 257U, static_cast<unsigned>(capacity)}) {
            // The final active lane is not the largest ID. An odd permutation
            // covers sparse stable slots, including the complete capacity.
            std::vector<Grain> initial(capacity);
            for (unsigned i = 0; i < count; ++i) {
                auto& p = initial[(i * 2053U) % capacity];
                const float x = 2.5F + static_cast<float>(i % 300), y = 2.5F + static_cast<float>(i / 300);
                p.position = {x, y, x, y};
                p.motion = {0, 0, 2.65F, 5};
            }
            world.load(initial, walls);
            const auto commands = world.read_dispatch_commands();
            require(commands[detail::active_command] == (count + 255) / 256, "PBF size changed the granular launch");
            require(commands[detail::pbf_command] == (count + size - 1) / size && commands[detail::pbf_command + 1] == 1
                            && commands[detail::pbf_command + 2] == 1,
                    "PBF indirect launch misses empty/tail lanes");
            const auto terms = world.read_fluid_terms();
            for (unsigned i = 0; i < count; ++i) {
                // Independent double Poly6 sum over the known regular fixture.
                double rho = 0;
                const int x = static_cast<int>(i % 300), y = static_cast<int>(i / 300);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int qx = x + dx, qy = y + dy;
                        if (qx < 0 || qx >= 300 || qy < 0 || static_cast<unsigned>(qy * 300 + qx) >= count) continue;
                        rho += std::pow(4.0 - dx * dx - dy * dy, 3) / (64 * std::numbers::pi);
                    }
                const double error = std::abs(terms[(i * 2053U) % capacity][1] - rho / 1.014612762210833);
                require(std::isfinite(error) && error < .00001,
                        "PBF density missed a stable ID or disagrees with CPU sum");
                max_error = std::max(max_error, error);
            }
            world.step();
            const auto after = world.readback();
            for (unsigned id = 0; id < capacity; ++id) {
                require(after[id].motion[3] == initial[id].motion[3] && after[id].motion[2] == initial[id].motion[2],
                        "PBF tail changed identity/mass");
                if (initial[id].motion[3] > .5F)
                    for (float value : after[id].position) require(std::isfinite(value), "Nonfinite PBF tail state");
            }
        }
        checks << "pbf_" << size
               << "=passed (general/reduction=256; empty,tails,sparse IDs,capacity,CPU density); max_error="
               << max_error << std::endl;
    }
}
} // namespace

std::expected<void, std::string> validate_execution(const std::filesystem::path& output, bool benchmark) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream checks{output / "execution-tests.txt"};
        const auto shaders =
                std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())} / "assets/shaders";
        const auto create = [&](const ExecutionOptions& options) {
            auto made = ParticleWorld::create(shaders, options);
            if (!made) throw std::runtime_error{made.error()};
            return std::move(*made);
        };
        pbf_dispatch_checks(shaders, checks);
        for (unsigned local : {64U, 128U, 256U}) {
            ExecutionOptions options;
            options.local_size = local;
            options.reduction_size = local;
            auto world = create(options);
            ProfileOptions profile;
            profile.collect = true;
            profile.group_timers = true;
            world->set_profile_options(profile);
            for (unsigned count : {0U, local - 1, local, local + 1, static_cast<unsigned>(capacity)}) {
                scene(*world, count);
                drain(*world);
                clear(*world);
                const auto commands = world->read_dispatch_commands();
                require(commands[detail::active_command] == (count + local - 1) / local
                                && commands[detail::draw_command + 1] == count,
                        "Active dispatch/draw arguments disagree with actual loaded count");
                require(commands[detail::reduction_command] == (count ? 1U : 0U), "Empty reduction was not gated");
                for (unsigned i = 0; i < 6; ++i) world->step();
                drain(*world);
                require(world->take_timings().size() == 6 && world->take_group_profiles().size() == 6,
                        "FIFO lost GPU query samples");
                const auto idle = world->read_dispatch_commands();
                for (auto command : {detail::rebuild_command, detail::sync_command, detail::terrain_command,
                                     detail::blast_command, detail::blast_terrain_command, detail::debris_command})
                    require(idle[command] == 0 && idle[command + 1] == 1 && idle[command + 2] == 1,
                            "Idle work was not suppressed with valid indirect arguments");
                unsigned alive = 0;
                for (const auto& p : world->readback())
                    if (p.motion[3] > .5F) {
                        ++alive;
                        for (float value : p.position)
                            require(std::isfinite(value), "Nonfinite particle after group-size change");
                        require(p.motion[1] > 0, "Indirect dispatch missed an active particle");
                    }
                require(alive == count, "Group-size change lost or resurrected IDs");
            }
            // Both query queues wrap and may fill without overwriting pending results.
            scene(*world, 65);
            drain(*world);
            clear(*world);
            profile.group_timers = false;
            world->set_profile_options(profile);
            const auto before = world->counters();
            unsigned admission_retries = 0;
            for (unsigned i = 0; i < 70; ++i) {
                // This fixture tests timer FIFO wrap. Reliable events have their
                // own bounded admission; a refused tick has submitted no query.
                while (!world->step()) {
                    ++admission_retries;
                    drain(*world);
                }
            }
            drain(*world);
            const auto calls = world->counters() - before;
            require(calls.timer_submissions + calls.timer_skips == 70, "Query queue did not account for full slots");
            require(world->take_timings().size() == calls.timer_submissions, "Query queue overwrote a pending sample");
            checks << "query_queue_" << local << " submitted=" << calls.timer_submissions
                   << " skipped=" << calls.timer_skips << " admission_retries=" << admission_retries << '\n';
            for (unsigned i = 0; i < 12; ++i) world->step();
            clear(*world);
            scene(*world, 0);
            drain(*world);
            require(world->take_timings().empty(), "Old-generation timing updated a new world");
            checks << "groups_" << local << "=passed (empty, tails, capacity, FIFO wraps, reset)\n";
            checks.flush();
        }
        for (bool blast : {false, true}) {
            std::vector<Grain> reference;
            for (bool optimized : {false, true}) {
                auto world = create(optimized ? ExecutionOptions{} : ExecutionOptions::reference());
                Grain grain;
                grain.position = {178, 90, 178, 90};
                grain.motion = {3, 0, 1, 1};
                const std::vector<std::uint32_t> walls(width * height);
                world->load(std::span{&grain, 1}, walls);
                world->configure_mission({});
                if (blast) world->queue_explosion({1, 174, 90, 12, 20});
                for (unsigned i = 0; i < 3; ++i) world->step();
                const auto state = world->readback();
                if (!optimized)
                    reference = state;
                else
                    for (unsigned i = 0; i < 4; ++i) {
                        require(std::abs(state[0].position[i] - reference[0].position[i]) < .001F,
                                "In-place blast changed particle positions");
                        require(std::abs(state[0].motion[i] - reference[0].motion[i]) < .001F,
                                "In-place blast changed particle velocity");
                    }
            }
        }
        checks << "in_place_blast_equivalence=passed\n";
        // A blast may create pending debris after beginTick generated commands.
        // Test that same-tick conversion survives the conservative work gate.
        for (bool optimized : {false, true}) {
            auto world = create(optimized ? ExecutionOptions{} : ExecutionOptions::reference());
            std::vector<std::uint32_t> walls(width * height);
            for (int y = 80; y <= 100; ++y) walls[y * width + 180] = 2;
            world->load({}, walls);
            world->configure_mission({});
            world->queue_explosion({1, 174, 90, 12, 20});
            world->step();
            const auto result = world->read_mission();
            require(result.counts[3] > 0, "Blast debris was delayed or suppressed");
            const auto particles = world->readback();
            require(std::ranges::count_if(particles, [](const Grain& p) { return p.motion[3] > .5F; })
                            == result.counts[3],
                    "Debris membership not rebuilt in the same tick");
            world->paint({174, 70, 190, 110, 16, Material::erase});
            for (unsigned i = 0; i < 3; ++i) world->step();
            const auto erased = world->readback();
            require(std::ranges::none_of(erased, [](const Grain& p) { return p.motion[3] > .5F; }),
                    "Removed debris resurrected in ping-pong storage");
            checks << (optimized ? "optimized" : "reference") << "_blast_edit=passed\n";
        }
        require(glGetError() == GL_NO_ERROR, "OpenGL error during execution validation");
        checks << "status=passed\n";
        checks.close();
        if (!benchmark) return {};

        std::ofstream environment{output / "environment.txt"}, summary{output / "summary.csv"},
                steps{output / "steps.csv"}, progress{output / "progress.txt"};
        environment
                << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                << "\nversion=" << reinterpret_cast<const char*>(glGetString(GL_VERSION))
                << "\nrepeats=3\nwarmup_ticks=120\nmeasured_ticks=240\nfresh_world_per_variant=1\nreadback=auto, period 2\ngroup_timers=1\nmode=unpaced; glFinish and HUD poll every 16 ticks, outside step timer\n";
        summary << "scene,variant,repeat,metric,samples,median,p95,p99,max\n";
        steps << "scene,variant,repeat,tick,total_ms,poll_ms,prepare_ms,solver_ms,diagnostics_ms,staging_ms";
#define HEADER(name) steps << "," #name;
        STREFA_GPU_COUNTERS(HEADER)
#undef HEADER
        steps << '\n';
        const auto metric = [&](unsigned count, const char* variant, unsigned repeat, const char* name,
                                const std::vector<double>& values) {
            if (values.empty()) return;
            summary << count << ',' << variant << ',' << repeat << ',' << name << ',' << values.size() << ','
                    << percentile(values, .5) << ',' << percentile(values, .95) << ',' << percentile(values, .99) << ','
                    << *std::ranges::max_element(values) << '\n';
        };
        for (unsigned repeat = 1; repeat <= 3; ++repeat) {
            auto choices = variants();
            if (repeat % 2 == 0) std::ranges::reverse(choices);
            for (const auto& variant : choices)
                for (unsigned count : {3096U, 16000U, 32768U}) {
                    if (count != 3096 && std::string_view{variant.name} != "reference"
                        && std::string_view{variant.name} != "optimized_256"
                        && std::string_view{variant.name} != "optimized_128"
                        && std::string_view{variant.name} != "optimized_64")
                        continue;
                    progress << "repeat=" << repeat << " variant=" << variant.name << " count=" << count << '\n';
                    progress.flush();
                    auto world = create(variant.options);
                    ProfileOptions profile;
                    profile.collect = true;
                    profile.group_timers = true;
                    world->set_profile_options(profile);
                    scene(*world, count);
                    std::vector<double> external;
                    const auto run = [&](unsigned ticks) {
                        for (unsigned i = 0; i < ticks; ++i) {
                            world->step();
                            if (i % 16 == 15) {
                                glFinish();
                                const auto began = SDL_GetTicksNS();
                                world->poll();
                                external.push_back(static_cast<double>(SDL_GetTicksNS() - began) / 1e6);
                            }
                        }
                        glFinish();
                        world->poll();
                    };
                    run(120);
                    clear(*world);
                    external.clear();
                    const auto began = SDL_GetTicksNS();
                    run(240);
                    metric(count, variant.name, repeat, "batch_mean_step_wall_ms",
                           {static_cast<double>(SDL_GetTicksNS() - began) / 1e6 / 240});
                    metric(count, variant.name, repeat, "external_poll_ms", external);
                    metric(count, variant.name, repeat, "gpu_step_ms", world->take_timings());
                    metric(count, variant.name, repeat, "cpu_submit_ms", world->take_cpu_timings());
                    const auto samples = world->take_step_profiles();
                    require(samples.size() == 240, "Benchmark lost measured CPU ticks");
                    const auto phase = [&](const char* name, auto member) {
                        std::vector<double> values;
                        for (const auto& p : samples) values.push_back(p.*member);
                        metric(count, variant.name, repeat, name, values);
                    };
                    phase("timer_poll_ms", &StepProfile::timer_poll_ms);
                    phase("solver_submit_ms", &StepProfile::solver_ms);
                    const auto groups = world->take_group_profiles();
                    require(groups.size() == 240, "Benchmark lost grouped GPU timestamps");
                    for (std::size_t i = 0; i < gpu_group_names.size(); ++i) {
                        std::vector<double> values;
                        for (const auto& group : groups) values.push_back(group.milliseconds[i]);
                        metric(count, variant.name, repeat,
                               ((std::string{"gpu_"} + std::string{gpu_group_names[i]}) + "_ms").c_str(), values);
                    }
                    for (const auto& p : samples) {
                        steps << count << ',' << variant.name << ',' << repeat << ',' << p.tick << ',' << p.total_ms
                              << ',' << p.poll_ms << ',' << p.prepare_ms << ',' << p.solver_ms << ','
                              << p.diagnostics_ms << ',' << p.staging_ms;
#define VALUE(name) steps << ',' << p.calls.name;
                        STREFA_GPU_COUNTERS(VALUE)
#undef VALUE
                        steps << '\n';
                    }
                    summary.flush();
                    steps.flush();
                }
        }
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "execution-error.txt"} << e.what();
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
