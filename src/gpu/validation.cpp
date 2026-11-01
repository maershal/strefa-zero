#include "gpu/world.hpp"
#include "sim/fixed_clock.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
Grain grain(float x, float y, float vx = 0, float vy = 0, int type = 1) {
    Grain p;
    p.position = {x, y, x, y};
    p.motion = {vx, vy, type == 1 ? 1.F : (type == 2 ? 4.F : .35F), static_cast<float>(type)};
    return p;
}
unsigned active(const std::vector<Grain>& particles) {
    return static_cast<unsigned>(std::ranges::count_if(particles, [](const Grain& p) { return p.motion[3] > .5F; }));
}
void finite(const std::vector<Grain>& particles) {
    for (const auto& p : particles)
        if (p.motion[3] > .5F) {
            for (auto v : p.position) require(std::isfinite(v), "Nonfinite GPU position");
            for (auto v : p.motion) require(std::isfinite(v), "Nonfinite GPU velocity");
            require(p.position[0] >= .459F && p.position[0] <= width - .459F && p.position[1] >= .459F
                            && p.position[1] <= height - .459F,
                    "Grain escaped world");
        }
}
double max_penetration(const std::vector<Grain>& particles) {
    std::vector<std::vector<unsigned>> bins(width * height);
    for (unsigned i = 0; i < particles.size(); ++i)
        if (particles[i].motion[3] > .5F) {
            const auto& p = particles[i].position;
            bins[static_cast<int>(p[1]) * width + static_cast<int>(p[0])].push_back(i);
        }
    double deepest = 0;
    for (unsigned i = 0; i < particles.size(); ++i)
        if (particles[i].motion[3] > .5F) {
            const auto& p = particles[i].position;
            const int cx = static_cast<int>(p[0]), cy = static_cast<int>(p[1]);
            for (int y = std::max(0, cy - 1); y <= std::min(height - 1, cy + 1); ++y)
                for (int x = std::max(0, cx - 1); x <= std::min(width - 1, cx + 1); ++x)
                    for (const auto j : bins[y * width + x])
                        if (j > i) {
                            const auto& q = particles[j].position;
                            deepest = std::max(deepest, .92 - std::hypot(p[0] - q[0], p[1] - q[1]));
                        }
        }
    return deepest;
}
} // namespace
std::expected<void, std::string> validate_granular(ParticleWorld& world, const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "gpu-tests.txt"};
        report << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
               << "\nopengl=" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << '\n';
        std::vector<std::uint32_t> walls(width * height);
        std::vector<Grain> initial;
        for (int i = 0; i < 64; ++i) initial.push_back(grain(20.F + i * 4.F, 20.F + static_cast<float>(i % 7) * .071F));
        world.load(initial, walls);
        for (int i = 0; i < 30; ++i) world.step();
        const auto falling = world.readback();
        finite(falling);
        require(active(falling) == 64, "Free fall lost grains");
        const double sub_dt = step_seconds * .5;
        double velocity = 0, displacement = 0;
        for (int i = 0; i < 60; ++i) {
            velocity = velocity * std::exp(-.12 * sub_dt) + 180 * sub_dt;
            displacement += velocity * sub_dt;
        }
        double max_error = 0;
        for (unsigned i = 0; i < initial.size(); ++i)
            max_error = std::max(max_error, std::abs(falling[i].position[1] - initial[i].position[1] - displacement));
        require(max_error < .006, "Ballistic integration mismatch");
        report << "ballistic_max_error_cells=" << max_error << '\n';
        // Fast grain versus a one-cell wall, including several ticks of contact.
        for (int y = 0; y < height; ++y) walls[y * width + 156] = 1;
        initial = {grain(150, 35, 80, 0)};
        world.load(initial, walls);
        for (int i = 0; i < 60; ++i) world.step();
        const auto shielded = world.readback();
        finite(shielded);
        require(shielded[0].position[0] <= 155.541F, "Tunneled through one-cell wall");
        report << "thin_wall=passed\n";
        std::ranges::fill(walls, 0U);
        initial = {grain(100, 30, 16, 0), grain(102, 30, -16, 0)};
        world.load(initial, walls);
        for (int i = 0; i < 15; ++i) world.step();
        const auto pair = world.readback();
        const double dx = pair[0].position[0] - pair[1].position[0], dy = pair[0].position[1] - pair[1].position[1];
        require(std::hypot(dx, dy) > .90, "Pair collision failed");
        require(std::abs((pair[0].position[0] + pair[1].position[0]) * .5 - 101) < .01, "Pair center-of-mass drift");
        report << "pair_contact=passed\n";
        initial = {grain(100, 30, 80, 0), grain(101, 30, -80, 0)};
        world.load(initial, walls);
        world.step();
        const auto fast_pair = world.readback();
        require(fast_pair[0].position[0] < fast_pair[1].position[0], "Fast grains crossed each other");
        report << "fast_pair=passed\n";
        float reference_y{};
        for (const int fps : {30, 60, 144}) {
            initial = {grain(70, 20)};
            world.load(initial, walls);
            sim::FixedStepClock<120, 4> clock;
            for (int frame = 0; frame < fps; ++frame) {
                const int steps = clock.advance(1.0 / fps, false, 1);
                for (int i = 0; i < steps; ++i) world.step();
                world.render(static_cast<float>(clock.fraction()));
            }
            require(world.metrics().steps == 120 && clock.dropped_seconds() < 1e-8, "Display rate changed step count");
            const float y = world.readback()[0].position[1];
            if (fps == 30)
                reference_y = y;
            else
                require(std::abs(reference_y - y) < .001F, "Display rate changed ballistic trajectory");
        }
        report << "fps_30_60_144=passed\n";
        // Editing explicitly changes mass; passive evolution must not. Reusing
        // deleted slots is verified before comparing each material's count.
        world.reset(Scene::empty);
        world.paint({160, 30, 160, 30, 6, Material::sand});
        auto edited = world.readback();
        const auto painted = active(edited);
        require(painted > 50, "GPU brush failed to spawn");
        world.paint({160, 30, 160, 30, 6, Material::erase});
        require(active(world.readback()) == 0, "GPU erase failed");
        world.paint({160, 30, 160, 30, 6, Material::ash});
        require(active(world.readback()) == painted, "Free slot reuse failed");
        // Several edits before polling must eventually publish the latest count.
        glFinish();
        world.poll();
        glFinish();
        world.poll();
        require(world.metrics().grains == painted, "Stale asynchronous count");
        report << "brush_erase_reuse=passed\n";
        glFinish();
        world.poll();
        static_cast<void>(world.take_timings());
        std::ofstream csv{output / "gpu-benchmark.csv"};
        csv << "scene,grains,steps,gpu_median_ms,gpu_p95_ms,gpu_p99_ms,gpu_max_ms,samples\n";
        for (int scene = 0; scene < 3; ++scene) {
            world.reset(static_cast<Scene>(scene));
            initial = world.readback();
            const auto count = active(initial);
            std::array<unsigned, 4> counts{};
            for (const auto& p : initial)
                if (p.motion[3] > .5F) ++counts[static_cast<unsigned>(p.motion[3])];
            // Synchronization is outside GL_TIME_ELAPSED, in this validation
            // executable only. The interactive loop collects queries with zero wait.
            for (int step = 0; step < 480; ++step) {
                world.step();
                if ((step + 1) % 16 == 0) {
                    glFinish();
                    world.poll();
                }
                if (step == 119 || step == 479) {
                    world.render(1);
                    world.save_image(output
                                     / ("scene-" + std::to_string(scene) + "-" + std::to_string(step + 1) + ".bmp"));
                }
            }
            auto result = world.readback();
            finite(result);
            require(active(result) == count, "Passive GPU evolution lost grains");
            const double penetration = max_penetration(result);
            require(penetration < .46, "Dense contact solve exceeded a grain radius of penetration");
            for (const auto& p : result)
                if (p.motion[3] > .5F) --counts[static_cast<unsigned>(p.motion[3])];
            require(std::ranges::all_of(counts, [](unsigned n) { return n == 0; }), "Material identity changed");
            glFinish();
            world.poll();
            auto timings = world.take_timings();
            require(timings.size() == 480, "Missing GPU timing samples");
            std::ranges::sort(timings);
            auto percentile = [&](double q) { return timings[static_cast<std::size_t>(q * (timings.size() - 1))]; };
            csv << scene << ',' << count << ",480," << percentile(.5) << ',' << percentile(.95) << ','
                << percentile(.99) << ',' << timings.back() << ',' << timings.size() << '\n';
            report << "scene_" << scene << "_count=" << count << " preserved\nscene_" << scene
                   << "_max_penetration_cells=" << penetration << '\n';
        }
        // Fully occupied particle capacity, no rendering or host copies inside
        // the evolution. This is a robustness check, not the 9500-grain timing.
        initial.clear();
        for (int i = 0; i < capacity; ++i) initial.push_back(grain(5.F + (i % 300) * 1.02F, 5.F + (i / 300) * 1.02F));
        std::ranges::fill(walls, 0U);
        world.load(initial, walls);
        for (int step = 0; step < 180; ++step) {
            world.step();
            if (step % 16 == 15) {
                glFinish();
                world.poll();
            }
        }
        const auto dense = world.readback();
        finite(dense);
        require(active(dense) == capacity, "Capacity stress lost grains");
        report << "capacity_32768=passed\n";
        require(glGetError() == GL_NO_ERROR, "OpenGL error during GPU validation");
        report << "status=passed\n";
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
