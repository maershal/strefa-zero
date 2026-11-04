#include "gpu/validation.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
// Blast sites, far enough apart that no radius reaches a neighbour. Queue order = site order.
constexpr std::array<std::array<float, 2>, 10> sites{{{32, 60},
                                                      {92, 60},
                                                      {152, 60},
                                                      {212, 60},
                                                      {272, 60},
                                                      {32, 140},
                                                      {92, 140},
                                                      {152, 140},
                                                      {212, 140},
                                                      {272, 140}}};
constexpr float radius = 16, impulse = 60;
// Isolated destructible cells around each blast (as in explosion_validation.cpp): no ray through a cell corner, no
// ray to one cell crosses another, |offset| <= 7.1, so the damage max(1, 8 (1 - r/R)) >= 4 exceeds the strength 3.
constexpr std::array<std::array<int, 2>, 8> ring{
        {{7, 1}, {-7, -1}, {1, -7}, {-1, 7}, {5, 4}, {-5, -4}, {4, -6}, {-4, 5}}};
constexpr float sand = 1, ash = 2, metal = 3, debris = 4;
constexpr float grain_offset = 5; // free grains left and right of the centre, on the centre row

Grain grain(float x, float y, float material) {
    // Inverse mass of the class, as granular.comp inverseMass(): sand 1, ash 4, metal .35.
    const float inverse_mass = material == ash ? 4.F : material == metal ? .35F : 1.F;
    Grain p;
    p.position = {x, y, x, y};
    p.motion = {0, 0, inverse_mass, material};
    return p;
}
std::size_t cell(const std::array<float, 2>& site, const std::array<int, 2>& offset) {
    return static_cast<std::size_t>((static_cast<int>(site[1]) + offset[1]) * width + static_cast<int>(site[0])
                                    + offset[0]);
}
std::vector<std::uint32_t> arena(std::span<const std::array<float, 2>> blasts) {
    std::vector<std::uint32_t> walls(width * height);
    for (int x = 0; x < width; ++x)
        for (int y = 187; y < height; ++y) walls[y * width + x] = 1;
    for (int y = 0; y < height; ++y) walls[y * width] = walls[y * width + width - 1] = 1;
    for (const auto& site : blasts)
        for (const auto& offset : ring) walls[cell(site, offset)] = 2;
    return walls;
}
std::vector<Grain> grains_beside(std::span<const std::array<float, 2>> blasts) {
    std::vector<Grain> result;
    for (const auto& [x, y] : blasts) {
        result.push_back(grain(x - grain_offset, y + .5F, sand));
        result.push_back(grain(x + grain_offset, y + .5F, sand));
    }
    return result;
}
Explosion blast(unsigned id, const std::array<float, 2>& site, float strength = impulse) {
    return {id, site[0], site[1], radius, strength};
}
unsigned alive(const std::vector<Grain>& grains) {
    return static_cast<unsigned>(std::ranges::count_if(grains, [](const Grain& p) { return p.motion[3] > .5F; }));
}
// The grain nearest to a position (validation fixtures place every grain apart).
const Grain& nearest(const std::vector<Grain>& grains, float x, float y) {
    const Grain* best = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (const auto& p : grains)
        if (p.motion[3] > .5F && p.motion[3] != debris) {
            const float d = std::hypot(p.position[0] - x, p.position[1] - y);
            if (d < distance) {
                distance = d;
                best = &p;
            }
        }
    require(best && distance < 2, std::format("no grain near ({}, {})", x, y));
    return *best;
}
bool site_destroyed(const std::vector<std::uint32_t>& terrain, const std::array<float, 2>& site) {
    return std::ranges::all_of(ring, [&](const auto& offset) { return terrain[cell(site, offset)] == 0; });
}
bool site_intact(const std::vector<std::uint32_t>& terrain, const std::array<float, 2>& site) {
    return std::ranges::all_of(ring, [&](const auto& offset) { return terrain[cell(site, offset)] == 2; });
}
// Both side grains of an applied blast move away from its centre; unapplied ones have no horizontal velocity.
bool site_pushed(const std::vector<Grain>& grains, const std::array<float, 2>& site) {
    const auto& left = nearest(grains, site[0] - grain_offset, site[1] + .5F);
    const auto& right = nearest(grains, site[0] + grain_offset, site[1] + .5F);
    return left.motion[0] < -1 && right.motion[0] > 1;
}
bool site_still(const std::vector<Grain>& grains, const std::array<float, 2>& site) {
    const auto& left = nearest(grains, site[0] - grain_offset, site[1] + .5F);
    const auto& right = nearest(grains, site[0] + grain_offset, site[1] + .5F);
    return left.motion[0] == 0 && right.motion[0] == 0;
}
struct DebrisCheck {
    unsigned count{}, moving{}, outward{};
    float min_speed{std::numeric_limits<float>::max()};
};
DebrisCheck check_debris(const std::vector<Grain>& grains, std::span<const std::array<float, 2>> blasts) {
    DebrisCheck result;
    for (const auto& p : grains) {
        if (p.motion[3] != debris) continue;
        ++result.count;
        const auto& site = *std::ranges::min_element(
                blasts, {}, [&](const auto& s) { return std::hypot(p.position[0] - s[0], p.position[1] - s[1]); });
        const float speed = std::hypot(p.motion[0], p.motion[1]);
        result.min_speed = std::min(result.min_speed, speed);
        result.moving += speed > 0;
        result.outward += (p.position[0] - site[0]) * p.motion[0] + (p.position[1] - site[1]) * p.motion[1] > 0;
    }
    return result;
}
void step(ParticleWorld& world, const std::string& label) {
    require(world.step(), label + ": tick refused by backpressure");
    glFinish();
}
} // namespace

std::expected<void, std::string> validate_lab_blast(ParticleWorld& world, const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "lab-blast.txt"};
        const auto destroyed_per_site = static_cast<unsigned>(ring.size());

        // 1. Three explosions queued for one tick are all applied by that tick: one particle pass, one cell pass.
        {
            const std::span three{sites.data(), 3};
            const auto vfx = world.vfx_options();
            world.set_vfx_options({VfxQuality::low, true}); // profile: per-tick counter probes without rendering
            const auto grains = grains_beside(three);
            world.load(grains, arena(three));
            const auto before = alive(world.readback());
            for (unsigned i = 0; i < three.size(); ++i) world.queue_explosion(blast(i + 1, three[i]));
            step(world, "three");
            const auto terrain = world.read_terrain();
            const auto after = world.readback();
            const auto counts = world.read_mission().counts;
            for (std::size_t i = 0; i < three.size(); ++i) {
                require(site_destroyed(terrain, three[i]), std::format("three: blast {} destroyed no ring", i + 1));
                require(site_pushed(after, three[i]), std::format("three: blast {} pushed no grain", i + 1));
            }
            const auto d = check_debris(after, three);
            report << "three_counts=" << counts[1] << ',' << counts[2] << ',' << counts[3] << '\n'
                   << "three_debris=" << d.count << " moving=" << d.moving << " outward=" << d.outward
                   << " min_speed=" << d.min_speed << '\n';
            require(counts[1] == 3 * destroyed_per_site && counts[2] == 0 && counts[3] == 3 * destroyed_per_site,
                    "three: destroyed/pending/spawned counters differ from 3 rings");
            require(d.count == 3 * destroyed_per_site, "three: one debris grain per destroyed cell expected");
            require(d.moving == d.count, "three: debris grain at rest after the explosion tick");
            require(d.outward == d.count, "three: debris grain not moving away from its explosion");
            require(alive(after) == before + d.count, "three: particle count not conserved (grains + debris)");
            // The WorldEvents (kind 4) of the tick reach the VFX emitter: one explosion effect per event. The
            // counters arrive through an asynchronous probe read in render(), so later ticks only wait for it.
            const auto explosion = static_cast<std::size_t>(VfxKind::explosion);
            for (int i = 0; i < 60 && world.vfx_stats().emitted[explosion] < three.size(); ++i) {
                world.render(1.F); // collects the VFX counter probes
                step(world, "three");
            }
            const auto emitted = world.vfx_stats().emitted[explosion];
            report << "three_vfx_explosions=" << emitted << '\n';
            require(emitted == three.size(), "three: VFX explosions differ from the 3 explosion events");
            world.set_vfx_options(vfx);
        }

        // 2. Ten explosions in one tick: eight are applied, the other two wait in the queue for the next tick.
        {
            const auto grains = grains_beside(sites);
            world.load(grains, arena(sites));
            for (unsigned i = 0; i < sites.size(); ++i) world.queue_explosion(blast(i + 1, sites[i]));
            step(world, "overflow");
            auto terrain = world.read_terrain();
            auto after = world.readback();
            unsigned applied = 0;
            for (std::size_t i = 0; i < sites.size(); ++i) {
                const bool first_tick = i < blasts_per_tick;
                require(first_tick ? site_destroyed(terrain, sites[i]) && site_pushed(after, sites[i])
                                   : site_intact(terrain, sites[i]) && site_still(after, sites[i]),
                        std::format("overflow: blast {} {} in the first tick", i + 1,
                                    first_tick ? "not applied" : "applied"));
                applied += first_tick;
            }
            step(world, "overflow");
            terrain = world.read_terrain();
            after = world.readback();
            for (std::size_t i = blasts_per_tick; i < sites.size(); ++i)
                require(site_destroyed(terrain, sites[i]) && site_pushed(after, sites[i]),
                        std::format("overflow: deferred blast {} lost", i + 1));
            const auto counts = world.read_mission().counts;
            report << "overflow_first_tick=" << applied << " counts=" << counts[1] << ',' << counts[3] << '\n';
            require(counts[1] == sites.size() * destroyed_per_site && counts[3] == counts[1],
                    "overflow: destroyed or spawned counter differs from 10 rings");
        }

        // 3. The impulse is scaled by inverse mass: at the same distance ash > sand > metal.
        {
            const std::span three{sites.data(), 3};
            constexpr std::array material{ash, sand, metal};
            std::vector<Grain> grains;
            for (std::size_t i = 0; i < three.size(); ++i)
                grains.push_back(grain(three[i][0] + 6, three[i][1] + .5F, material[i]));
            world.load(grains, arena({}));
            // Small impulse: no class reaches the solver's 80 cells/s limit.
            for (unsigned i = 0; i < three.size(); ++i) world.queue_explosion(blast(i + 1, three[i], 20));
            step(world, "mass");
            const auto after = world.readback();
            std::array<float, 3> vx{};
            for (std::size_t i = 0; i < three.size(); ++i)
                vx[i] = nearest(after, three[i][0] + 6, three[i][1] + .5F).motion[0];
            report << "mass_vx_ash_sand_metal=" << vx[0] << ',' << vx[1] << ',' << vx[2] << '\n';
            require(vx[0] > vx[1] && vx[1] > vx[2] && vx[2] > 0, "mass: impulse not ordered ash > sand > metal");
        }

        // 4. Mission with ExecutionOptions::batched_blasts: three host explosions in one mission tick, each with its
        // WorldEvent (MissionSnapshot counts[0] counts them), and debris with velocity.
        if (world.execution_options().batched_blasts) {
            const std::span three{sites.data() + 1, 3};
            world.load(grains_beside(three), arena(three));
            world.configure_mission({});
            const auto before = alive(world.readback());
            for (unsigned i = 0; i < three.size(); ++i) world.queue_explosion(blast(i + 1, three[i]));
            step(world, "mission");
            const auto terrain = world.read_terrain();
            const auto after = world.readback();
            const auto counts = world.read_mission().counts;
            for (std::size_t i = 0; i < three.size(); ++i)
                require(site_destroyed(terrain, three[i]), std::format("mission: blast {} destroyed no ring", i + 1));
            const auto d = check_debris(after, three);
            report << "mission_counts=" << counts[0] << ',' << counts[1] << ',' << counts[3] << '\n'
                   << "mission_debris=" << d.count << " moving=" << d.moving << " outward=" << d.outward << '\n';
            require(counts[0] == three.size(), "mission: not every batched explosion emitted its event");
            require(d.count == 3 * destroyed_per_site && d.moving == d.count && d.outward == d.count,
                    "mission: debris count or direction differs");
            require(alive(after) == before + d.count, "mission: particle count not conserved (grains + debris)");
        }
        report << "result=PASS\n";
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{std::string{"Test wybuchów laboratorium GPU: "} + e.what()};
    }
}
} // namespace strefa::gpu
