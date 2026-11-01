#include "gpu/validation.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error{message};
}
double mean_y(const std::vector<Grain>& state, int kind) {
    double sum = 0;
    unsigned count = 0;
    for (const auto& p : state)
        if (p.motion[3] == kind) {
            sum += p.position[1];
            ++count;
        }
    return sum / std::max(1U, count);
}
unsigned above(const std::vector<Grain>& state, float y, int kind) {
    return static_cast<unsigned>(
            std::ranges::count_if(state, [&](const Grain& p) { return p.motion[3] == kind && p.position[1] < y; }));
}
void conservation(const std::vector<Grain>& before, const std::vector<Grain>& after) {
    for (std::size_t i = 0; i < before.size(); ++i) {
        require(before[i].motion[3] == after[i].motion[3] && before[i].motion[2] == after[i].motion[2],
                "Flow test lost material/mass");
        if (after[i].motion[3] < .5F) continue;
        for (auto v : after[i].position) require(std::isfinite(v), "Nonfinite flow position");
        for (auto v : after[i].motion) require(std::isfinite(v), "Nonfinite flow motion");
    }
}
double largest_oil_component(const std::vector<Grain>& state) {
    std::vector<unsigned> ids;
    for (unsigned i = 0; i < state.size(); ++i)
        if (state[i].motion[3] == 6) ids.push_back(i);
    std::vector<bool> seen(ids.size());
    std::size_t largest = 0;
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (!seen[i]) {
            std::vector<std::size_t> queue{i};
            seen[i] = true;
            for (std::size_t k = 0; k < queue.size(); ++k) {
                const auto& a = state[ids[queue[k]]];
                for (std::size_t j = 0; j < ids.size(); ++j)
                    if (!seen[j]) {
                        const auto& b = state[ids[j]];
                        if (std::hypot(a.position[0] - b.position[0], a.position[1] - b.position[1]) < 2.1F) {
                            seen[j] = true;
                            queue.push_back(j);
                        }
                    }
            }
            largest = std::max(largest, queue.size());
        }
    return static_cast<double>(largest) / std::max(std::size_t{1}, ids.size());
}
} // namespace
std::expected<void, std::string> validate_flow(ParticleWorld& world, const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "gpu-flow-tests.txt"}, csv{output / "gpu-flow-benchmark.csv"},
                history{output / "gpu-flow-history.csv"};
        report << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << std::endl;
        csv << "scene,particles,ticks,median_ms,p95_ms,p99_ms,max_ms\n";
        history << "scene,tick,top_sand,water_mean_y,oil_mean_y\n";
        bool passed = true;
        std::vector<std::uint32_t> no_walls(width * height);
        std::vector<Grain> slow(2);
        for (unsigned i = 0; i < 2; ++i) {
            float x = i ? 240.F : 20.F;
            slow[i].position = {x, 20, x, 20};
            slow[i].motion = {.0005F, 0, 2.65F, 5};
        }
        world.load(slow, no_walls);
        for (int t = 0; t < 12; ++t) world.step();
        auto tiny = world.readback();
        double expected = 0, v = .0005;
        for (int t = 0; t < 24; ++t) {
            v *= std::exp(-.02 / 240);
            expected += v / 240;
        }
        double tiny_error = 0;
        for (unsigned i = 0; i < 2; ++i)
            tiny_error = std::max(tiny_error, std::abs(static_cast<double>(tiny[i].position[0]) + tiny[i].residual[0]
                                                       - slow[i].position[0] - expected));
        report << "subpixel_displacement_error=" << tiny_error << std::endl;
        passed = passed && tiny_error < .00000003;
        // Unequal masses exchange viscous momentum symmetrically. Gravity has
        // no x component; only the declared air drag removes x momentum.
        slow[0].position = {100, 20, 100, 20};
        slow[0].motion = {1, 0, 2.65F, 5};
        slow[1].position = {101.1F, 20, 101.1F, 20};
        slow[1].motion = {-1, 0, 3.3125F, 6};
        world.load(slow, no_walls);
        for (int t = 0; t < 6; ++t) world.step();
        auto pair = world.readback();
        const double momentum = pair[0].motion[0] / pair[0].motion[2] + pair[1].motion[0] / pair[1].motion[2];
        const double expected_momentum = (1 / 2.65 - 1 / 3.3125) * std::exp(-.02 * 6 / 120);
        report << "mixed_pair_momentum_error=" << std::abs(momentum - expected_momentum) << std::endl;
        passed = passed && std::abs(momentum - expected_momentum) < .00001;
        for (int y = 0; y < height; ++y) no_walls[y * width + 156] = y == 100 ? 0U : 1U;
        slow.resize(1);
        slow[0].position = {154.9F, 100.45F, 154.9F, 100.45F};
        slow[0].motion = {80, 0, 2.65F, 5};
        world.load(slow, no_walls);
        for (int t = 0; t < 12; ++t) world.step();
        auto channel = world.readback();
        report << "one_cell_liquid_channel_x=" << channel[0].position[0] << std::endl;
        passed = passed && channel[0].position[0] > 157.32F;
        // Mirror slopes plus a level shelf: sliding must come from geometry,
        // not a horizontal random kick or a global drift force.
        std::vector<std::uint32_t> floor(width * height);
        for (int y = 187; y < height; ++y)
            for (int x = 0; x < width; ++x) floor[y * width + x] = 1;
        std::vector<Grain> slope_particles(3);
        for (unsigned i = 0; i < 3; ++i) {
            float x = i == 0 ? 60.5F : (i == 1 ? 259.5F : 160.5F), y = i == 2 ? 140.F : 60.F;
            slope_particles[i].position = {x, y, x, y};
            slope_particles[i].motion = {0, 0, 1, 1};
        }
        world.load(slope_particles, floor);
        world.paint({40, 70, 120, 150, 1, Material::wall});
        world.paint({280, 70, 200, 150, 1, Material::wall});
        world.paint({140, 160, 180, 160, 1, Material::wall});
        for (int t = 0; t < 300; ++t) {
            world.step();
            if (t % 16 == 15) {
                glFinish();
                world.poll();
            }
        }
        auto slid = world.readback();
        conservation(slope_particles, slid);
        report << "slope_right_dx=" << slid[0].position[0] - 60.5F << "\nslope_left_dx=" << slid[1].position[0] - 259.5F
               << "\nflat_dx=" << slid[2].position[0] - 160.5F << std::endl;
        passed = passed && slid[0].position[0] > 90.5F && slid[1].position[0] < 229.5F
              && std::abs(slid[2].position[0] - 160.5F) < .01F;
        for (auto scene : {Scene::hourglass, Scene::inverted_oil, Scene::oil_drop}) {
            world.reset(scene);
            glFinish();
            world.poll();
            static_cast<void>(world.take_timings());
            auto initial = world.readback();
            unsigned count = static_cast<unsigned>(
                    std::ranges::count_if(initial, [](const Grain& p) { return p.motion[3] > .5F; }));
            const int ticks = scene == Scene::hourglass ? 2400 : 1800;
            unsigned outside = 0;
            double submerged_component = 0;
            for (int tick = 0; tick < ticks; ++tick) {
                world.step();
                if (tick % 16 == 15) {
                    glFinish();
                    world.poll();
                }
                if ((tick + 1) % 120 == 0) {
                    auto state = world.readback();
                    conservation(initial, state);
                    if (scene == Scene::oil_drop && tick == 119) submerged_component = largest_oil_component(state);
                    history << static_cast<int>(scene) << ',' << tick + 1 << ',' << above(state, 110, 1) << ','
                            << mean_y(state, 5) << ',' << mean_y(state, 6) << std::endl;
                    if (scene == Scene::hourglass) {
                        for (const auto& p : state)
                            if (p.motion[3] == 1) {
                                const float y = p.position[1];
                                float left =
                                        y < 94 ? 92 + (y - 20) * 63 / 74 : (y < 102 ? 155 : 155 - (y - 102) * 63 / 74);
                                if (p.position[0] < left - 1 || p.position[0] > 320 - left + 1 || y > 177) ++outside;
                            }
                    }
                }
                if ((tick + 1) % 120 == 0) {
                    world.render(1);
                    world.save_image(output
                                     / ("flow-" + std::to_string(static_cast<int>(scene)) + "-"
                                        + std::to_string(tick + 1) + ".bmp"));
                }
            }
            auto result = world.readback();
            conservation(initial, result);
            glFinish();
            world.poll();
            auto timings = world.take_timings();
            std::ranges::sort(timings);
            require(timings.size() == static_cast<unsigned>(ticks), "Missing flow timing samples");
            auto pct = [&](double q) { return timings[static_cast<std::size_t>(q * (timings.size() - 1))]; };
            csv << static_cast<int>(scene) << ',' << count << ',' << ticks << ',' << pct(.5) << ',' << pct(.95) << ','
                << pct(.99) << ',' << timings.back() << std::endl;
            if (scene == Scene::hourglass) {
                unsigned top = above(result, 110, 1);
                double drained = 1 - static_cast<double>(top) / count;
                report << "hourglass_count=" << count << "\nhourglass_drained_fraction=" << drained
                       << "\nhourglass_wall_leaks=" << outside << std::endl;
                passed = passed && drained > .98 && outside == 0;
                // Shut the neck with the same brush; an airtight barrier must
                // remain a barrier, even when slope normals are smoothed.
                world.reset(scene);
                world.paint({155, 98, 165, 98, 1, Material::wall});
                initial = world.readback();
                for (int t = 0; t < 480; ++t) {
                    world.step();
                    if (t % 16 == 15) {
                        glFinish();
                        world.poll();
                    }
                }
                result = world.readback();
                conservation(initial, result);
                auto above_neck = above(result, 102, 1);
                report << "closed_hourglass_above_neck=" << above_neck << std::endl;
                passed = passed && above_neck == count;
            } else {
                double water = mean_y(result, 5), oil = mean_y(result, 6);
                report << "scene_" << static_cast<int>(scene) << "_water_y=" << water << "\nscene_"
                       << static_cast<int>(scene) << "_oil_y=" << oil << std::endl;
                if (scene == Scene::inverted_oil)
                    passed = passed && water > oil + 10;
                else {
                    double connected = largest_oil_component(result);
                    const double surface_fraction =
                            static_cast<double>(above(result, 134, 6)) / above(result, static_cast<float>(height), 6);
                    report << "submerged_oil_largest_component=" << submerged_component
                           << "\nsurfaced_oil_largest_component=" << connected
                           << "\noil_at_surface_fraction=" << surface_fraction << std::endl;
                    // A submerged drop should cohere; after reaching the free
                    // surface it is allowed to spread into separate film patches.
                    passed = passed && oil < 132 && submerged_component > .8 && surface_fraction > .95;
                }
            }
        }
        require(glGetError() == GL_NO_ERROR, "OpenGL error in flow test");
        report << "status=" << (passed ? "passed" : "failed") << std::endl;
        require(passed, "Hourglass / oil-water behavioural regression failed; see gpu-flow-tests.txt");
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
