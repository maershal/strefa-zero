#include "gpu/validation.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
constexpr float blast_x = 160, blast_y = 120, blast_radius = 20;
// Isolated destructible cells around the blast cell, offsets in cells. No offset is an exact diagonal (no ray
// through a cell corner) and no ray to one target crosses another. Every cell centre lies within R/2, so the
// damage max(1, 8 (1 - r/R)) >= 4 exceeds the strength 3 that load() gives every cell >= 2.
constexpr std::array<std::array<int, 2>, 8> ring{
        {{7, 1}, {-7, -1}, {1, -7}, {-1, 7}, {5, 4}, {-5, -4}, {4, -6}, {-4, 5}}};
constexpr unsigned debris_material = 4;
std::uint64_t fnv1a(const void* data, std::size_t bytes, std::uint64_t hash = 14695981039346656037ULL) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < bytes; ++i) hash = (hash ^ p[i]) * 1099511628211ULL;
    return hash;
}
std::vector<std::uint32_t> arena() {
    std::vector<std::uint32_t> walls(width * height);
    for (int x = 0; x < width; ++x)
        for (int y = 187; y < height; ++y) walls[y * width + x] = 1;
    for (int y = 0; y < height; ++y) walls[y * width] = walls[y * width + width - 1] = 1;
    for (const auto& [dx, dy] : ring)
        walls[(static_cast<int>(blast_y) + dy) * width + static_cast<int>(blast_x) + dx] = 2;
    return walls;
}
} // namespace
void validate_debris_history(ParticleWorld& world, std::ostream& report) {
    const bool bounds = world.execution_options().neighbors == NeighborMode::reused;
    const auto round = [&](const std::string& label) {
        // History: a settled sand block far from the blast, then erased, so the freed slots keep the anchors and
        // neighbour entries of grains that no longer exist when debris is spawned into them.
        const auto walls = arena();
        std::vector<Grain> sand;
        for (int y = 170; y < 187; ++y)
            for (int x = 250; x < 290; ++x) {
                Grain p;
                p.position = {x + .5F, y + .5F, x + .5F, y + .5F};
                p.motion = {0, 0, 1, 1};
                sand.push_back(p);
            }
        world.load(sand, walls);
        world.configure_mission({});
        for (int i = 0; i < 30; ++i) world.step();
        world.paint({250, 176, 290, 176, 8, Material::erase});
        const auto before = world.readback();
        const auto survivors = std::ranges::count_if(before, [](const Grain& p) { return p.motion[3] > .5F; });
        require(survivors < std::ssize(sand), label + ": history erase removed no grain");

        TraceConfig config;
        config.mode = bounds ? CostMode::bounds : CostMode::passes;
        config.max_events = 4096;
        config.max_samples = 1;
        world.start_diagnostic_trace(config);
        world.queue_explosion({1, blast_x, blast_y, blast_radius, 0});
        require(world.step(), label + ": explosion tick refused by backpressure");
        glFinish();
        world.poll();
        const auto trace = world.read_diagnostic_trace();

        const auto mission = world.read_mission();
        const auto terrain = world.read_terrain();
        const auto after = world.readback();
        report << label << "_counts=" << mission.counts[0] << ',' << mission.counts[1] << ',' << mission.counts[2]
               << ',' << mission.counts[3] << '\n';
        require(mission.counts[0] == 1 && mission.counts[1] == ring.size() && mission.counts[2] == 0
                        && mission.counts[3] == ring.size(),
                label + ": debris accounting differs from the ring of " + std::to_string(ring.size()) + " cells");
        auto expected_terrain = walls;
        for (const auto& [dx, dy] : ring)
            expected_terrain[(static_cast<int>(blast_y) + dy) * width + static_cast<int>(blast_x) + dx] = 0;
        // read_terrain() holds four layers; the first is the material of every cell.
        require(std::equal(expected_terrain.begin(), expected_terrain.end(), terrain.begin()),
                label + ": terrain other than the ring changed or a ring cell survived");
        std::vector<std::array<float, 2>> debris;
        for (const auto& p : after)
            if (p.motion[3] == debris_material) debris.push_back({p.position[0], p.position[1]});
        report << label << "_debris=" << debris.size() << '\n';
        require(debris.size() == ring.size(), label + ": " + std::to_string(debris.size()) + " debris grains for "
                                                      + std::to_string(ring.size()) + " destroyed cells");
        // One-to-one with the cell centres. Debris is created at rest in the prepare phase and then integrates one
        // tick (two substeps) of gravity, far below a quarter cell.
        std::vector<bool> used(debris.size());
        for (const auto& [dx, dy] : ring) {
            const float cx = blast_x + static_cast<float>(dx) + .5F, cy = blast_y + static_cast<float>(dy) + .5F;
            bool found = false;
            for (std::size_t i = 0; i < debris.size() && !found; ++i)
                if (!used[i] && std::hypot(debris[i][0] - cx, debris[i][1] - cy) < .25F) used[i] = found = true;
            require(found,
                    std::format("{}: no debris grain at the centre of cell ({}, {})", label, cx - .5F, cy - .5F));
        }

        // #23: debris spawned into a freed slot must not raise the reuse bound by the distance from the anchor of the
        // slot's former occupant. Mission op 5 (terrain damage) runs right before op 6 and moves no particle.
        if (bounds) {
            const auto mission_pass = [&](unsigned op) {
                const auto it = std::ranges::find_if(trace.dispatches, [&](const DispatchSample& event) {
                    return event.family == PassFamily::mission && event.operation == op && event.phase == 0;
                });
                require(it != trace.dispatches.end() && it->bounded,
                        std::format("{}: no bound after mission op {}", label, op));
                return it->displacement;
            };
            const auto damage = mission_pass(5), spawn = mission_pass(6);
            report << label << "_reuse_bound=" << damage << ',' << spawn << '\n';
            require(spawn == damage,
                    std::format("{}: debris spawn raised the reuse bound from {} to {} cells", label, damage, spawn));
        }

        // Evidence that moving the explosion path keeps the work identical: the pass sequence of the explosion
        // tick, the mission state bytes and the order-independent debris positions (slot reservation is atomic).
        std::string passes;
        unsigned count = 0;
        for (const auto& event : trace.dispatches) {
            passes += std::format("{} {};", pass_family_names[static_cast<std::size_t>(event.family)], event.operation);
            ++count;
        }
        std::ranges::sort(debris);
        report << label << "_dispatches=" << count << '\n'
               << label << "_dispatch_digest=" << std::format("{:016x}", fnv1a(passes.data(), passes.size())) << '\n'
               << label << "_mission_digest=" << std::format("{:016x}", fnv1a(&mission, sizeof(mission))) << '\n'
               << label
               << "_debris_digest=" << std::format("{:016x}", fnv1a(debris.data(), debris.size() * sizeof(debris[0])))
               << '\n';
        report.flush();
    };
    for (int i = 1; i <= 3; ++i) round("debris" + std::to_string(i));
    // The same rounds after a dense scene.
    world.reset(Scene::mixed);
    for (int i = 0; i < 120; ++i) world.step();
    for (int i = 4; i <= 6; ++i) round("debris" + std::to_string(i));
    report << "debris_history=passed\n";
}
} // namespace strefa::gpu
