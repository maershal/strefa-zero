#include "gpu/validation.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
Grain grain(float x, float y, float vx = 0, float vy = 0) {
    Grain p;
    p.position = {x, y, x, y};
    p.motion = {vx, vy, 1, 1};
    return p;
}
unsigned active(const std::vector<Grain>& particles) {
    return static_cast<unsigned>(std::ranges::count_if(particles, [](const Grain& p) { return p.motion[3] > .5F; }));
}
constexpr float brush_x = 160, brush_y = 30, brush_radius = 6;
// Cells whose centre lies inside the point stroke (insideStroke in particle_common.glsl). No centre is exactly
// on the circle: (k + 1/2)^2 + (m + 1/2)^2 is never an integer. The empty scene has no wall near the brush and
// spawn candidates are never tested against each other, so an empty world must accept every cell.
unsigned expected_brush_count() {
    unsigned count = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) count += std::hypot(x + .5 - brush_x, y + .5 - brush_y) <= brush_radius;
    return count;
}
} // namespace
std::expected<void, std::string> validate_brush_history(ParticleWorld& world, const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "brush-history.txt"};
        report << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
               << "\nopengl=" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << '\n';
        const auto expected = expected_brush_count();
        report << "expected_cells=" << expected << '\n';
        const auto round = [&](const std::string& label) {
            world.paint({brush_x, brush_y, brush_x, brush_y, brush_radius, Material::sand});
            const auto sand = active(world.readback());
            world.paint({brush_x, brush_y, brush_x, brush_y, brush_radius, Material::erase});
            const auto erased = active(world.readback());
            world.paint({brush_x, brush_y, brush_x, brush_y, brush_radius, Material::ash});
            const auto ash = active(world.readback());
            world.paint({brush_x, brush_y, brush_x, brush_y, brush_radius, Material::erase});
            const auto cleared = active(world.readback());
            report << label << "_sand=" << sand << '\n'
                   << label << "_ash=" << ash << '\n'
                   << label << "_erased=" << erased << ',' << cleared << '\n';
            report.flush();
            require(erased == 0 && cleared == 0, label + ": GPU erase failed");
            require(sand == expected, label + ": sand brush spawned " + std::to_string(sand) + " of "
                                              + std::to_string(expected) + " cells");
            require(ash == expected, label + ": ash brush in reused slots spawned " + std::to_string(ash) + " of "
                                             + std::to_string(expected) + " cells");
        };
        // History of validate_granular from the world as created (hourglass): fixtures with grains far from the
        // brush and a wall through it, then a dense scene, so the neighbour order, anchors and free list hold
        // entries of particles that no longer exist.
        std::vector<std::uint32_t> walls(width * height);
        std::vector<Grain> fixture;
        for (int i = 0; i < 64; ++i) fixture.push_back(grain(20.F + i * 4.F, 20.F + static_cast<float>(i % 7) * .071F));
        world.load(fixture, walls);
        for (int i = 0; i < 30; ++i) world.step();
        for (int y = 0; y < height; ++y) walls[y * width + 156] = 1;
        fixture = {grain(150, 35, 80, 0)};
        world.load(fixture, walls);
        for (int i = 0; i < 60; ++i) world.step();
        std::ranges::fill(walls, 0U);
        fixture = {grain(100, 30, 16, 0), grain(102, 30, -16, 0)};
        world.load(fixture, walls);
        for (int i = 0; i < 15; ++i) world.step();
        fixture = {grain(100, 30, 80, 0), grain(101, 30, -80, 0)};
        world.load(fixture, walls);
        world.step();
        for (int frame = 0; frame < 3; ++frame) {
            fixture = {grain(70, 20)};
            world.load(fixture, walls);
            for (int i = 0; i < 120; ++i) world.step();
            world.render(1);
        }
        world.reset(Scene::empty);
        for (int i = 1; i <= 3; ++i) round("round" + std::to_string(i));
        // The same rounds after a dense scene.
        world.reset(Scene::mixed);
        for (int i = 0; i < 120; ++i) world.step();
        world.reset(Scene::empty);
        for (int i = 4; i <= 6; ++i) round("round" + std::to_string(i));
        glFinish();
        world.poll();
        report << "brush_history=passed\n";
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
