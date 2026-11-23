#include "gpu/validation.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// GAME-3, --gpu-check weapons: shotgun recoil and close-range chipping, C4 sticking and remote detonation.
namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
constexpr int floor_y = 187;
MissionSnapshot run(ParticleWorld& world, int n, MissionInput input = {}) {
    for (int i = 0; i < n; ++i) {
        world.step(input);
        if (i % 16 == 15) {
            glFinish();
            world.poll();
        }
    }
    glFinish();
    world.poll();
    const auto g = world.read_mission();
    for (float v : g.actor) require(std::isfinite(v), "Nonfinite player");
    return g;
}
std::vector<std::uint32_t> arena() {
    std::vector<std::uint32_t> walls(width * height);
    for (int y = floor_y; y < height; ++y)
        for (int x = 0; x < width; ++x) walls[y * width + x] = 1;
    return walls;
}
// Targets out of every line of fire, so only terrain and the actor react.
MissionConfig mission(float spawn_x) {
    MissionConfig c;
    c.spawn_x = spawn_x;
    for (auto& t : c.targets) t = {300, 20, 3, 0};
    return c;
}
void start(ParticleWorld& world, const std::vector<std::uint32_t>& walls, float spawn_x) {
    world.load({}, walls);
    world.configure_mission(mission(spawn_x));
    run(world, 60);
}
// Destructible block (strength 3 after load()) 3 cells thick in front of the player (x = 24).
std::vector<std::uint32_t> block(int left = 60) {
    auto walls = arena();
    for (int y = 150; y < floor_y; ++y)
        for (int x = left; x < left + 3; ++x) walls[y * width + x] = 2;
    return walls;
}
// Cells broken by one shotgun shot at a block starting at x = left.
unsigned shotgun_destroyed(ParticleWorld& world, int left) {
    start(world, block(left), 24);
    auto g = world.read_mission();
    const auto before = g.counts[1];
    world.step({0, static_cast<float>(left + 1), g.actor[1] - 12, select_shotgun | fire});
    g = run(world, 30, {0, static_cast<float>(left + 1), g.actor[1] - 12, 0});
    return g.counts[1] - before;
}
double nearest_solid(const std::vector<std::uint32_t>& walls, float px, float py) {
    double best = 1e9;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (walls[y * width + x])
                best = std::min(
                        best, std::hypot(std::clamp<double>(px, x, x + 1) - px, std::clamp<double>(py, y, y + 1) - py));
    return best;
}
} // namespace
std::expected<void, std::string> validate_weapons(ParticleWorld& world, const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "gpu-weapons-tests.txt"};

        // Recoil: a horizontal shot pushes the player back; a shot at the floor lifts them ("shotgun jump").
        start(world, arena(), 24);
        auto g = world.read_mission();
        const float x0 = g.actor[0], y0 = g.actor[1];
        world.step({0, 200, g.actor[1] - 12, select_shotgun | fire});
        g = run(world, 30, {0, 200, y0 - 12, 0});
        report << "recoil_dx=" << g.actor[0] - x0 << '\n';
        require(g.actor[0] < x0 - 4, "Shotgun recoil did not push the player back");
        require(game::Stock::unpack(g.inventory[1]).shells == game::shotgun_tube - 1, "Shotgun shell not committed");
        g = run(world, 60, {0, 200, y0 - 12, 0});
        require(std::abs(g.actor[1] - y0) < .01F, "Player did not land after the recoil");
        world.step({0, g.actor[0], 300, fire});
        float lift = 0;
        for (int i = 0; i < 40; ++i) {
            world.step({0, g.actor[0], 300, 0});
            lift = std::max(lift, y0 - world.read_mission().actor[1]);
        }
        report << "shotgun_jump_height=" << lift << '\n';
        require(lift > 5, "Shot at the floor did not lift the player");

        // Close range: one rifle round only chips the block; five pellets break cells.
        start(world, block(), 24);
        const float aim_y = world.read_mission().actor[1] - 12;
        world.step({0, 61, aim_y, fire});
        g = run(world, 30, {0, 61, aim_y, 0});
        report << "rifle_destroyed=" << g.counts[1] << '\n';
        require(g.counts[1] == 0, "A single rifle round broke a strength-3 cell");
        // Pellets hit twice as hard during their first 0.1 s (42 px): the same block breaks less beyond that range.
        const auto close_range = shotgun_destroyed(world, 60), long_range = shotgun_destroyed(world, 110);
        report << "shotgun_destroyed=" << close_range << "\nshotgun_destroyed_far=" << long_range << '\n';
        require(close_range >= 3, "Shotgun pellets did not break the block at close range");
        require(close_range > long_range, "Shotgun pellets were not stronger at close range");

        // C4: thrown at a wall, sticks with no fuse, explodes on command with the stronger C4 shape.
        auto wall = arena();
        for (int y = 100; y < floor_y; ++y) wall[y * width + 70] = 1;
        start(world, wall, 50);
        g = world.read_mission();
        const auto explosions = g.counts[0], delivered = world.gameplay().state().explosions;
        world.step({0, 70, g.actor[1] - 40, select_c4 | fire});
        g = run(world, 360);
        const auto stock = game::Stock::unpack(g.inventory[1]);
        require(stock.placed == 1 && stock.charges == game::c4_stock - 1, "C4 placement not acknowledged");
        require(g.counts[0] == explosions, "C4 exploded without a detonation command");
        const auto terrain = world.read_terrain();
        // The second charge is thrown on the detonation tick: only the stuck charge fires, never the one in hand.
        world.step({0, 70, g.actor[1] - 40, fire | detonate});
        g = run(world, 2);
        const double gap = nearest_solid(terrain, g.flash[0], g.flash[1]);
        report << "c4_blast=" << g.flash[0] << ',' << g.flash[1] << ',' << g.flash[2] << "\nc4_gap=" << gap << '\n';
        require(g.counts[0] == explosions + 1, "C4 detonation emitted no explosion");
        require(world.gameplay().state().explosions == delivered + 1, "Explosion event not delivered to the session");
        require(g.flash[2] == 28 && gap < 1.5 && g.flash[0] > 60 && g.flash[1] < floor_y - 2,
                "C4 did not stick to the wall");
        g = run(world, 360);
        require(g.counts[0] == explosions + 1, "Detonation also fired the charge in flight");
        require(game::Stock::unpack(g.inventory[1]).placed == 1, "Charge in flight was not kept placed");

        // The wall under the stuck charge disappears: it falls to the floor and still fires on command.
        world.paint({70, 100, 70, 183, 2, Material::erase});
        g = run(world, 120);
        world.step({0, 70, 150, detonate});
        g = run(world, 2);
        report << "c4_fallen_blast=" << g.flash[0] << ',' << g.flash[1] << '\n';
        require(g.counts[0] == explosions + 2 && g.flash[1] > 180 && std::abs(g.flash[0] - 70) < 4,
                "Charge did not fall after losing its anchor");
        require(game::Stock::unpack(g.inventory[1]).placed == 0, "Detonation did not disarm");
        report << "weapons=passed\n";
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{std::string{"Weapons check: "} + e.what()};
    }
}
} // namespace strefa::gpu
