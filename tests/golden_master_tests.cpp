// Golden master: fixed scenes and a scripted arena session must reproduce the
// checksums recorded before the demo-stability changes. Any intended physics
// change has to update these values explicitly, with a report.
// Values recorded on main at eda5d76. Run with --print to list the current values.
#include "game/session.hpp"
#include "sim/world.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
using namespace strefa;

constexpr int world_steps = 60;
constexpr int session_ticks = 240;

struct Case {
    sim::Scene scene;
    std::uint64_t expected;
};
// clang-format off
constexpr std::array world_cases{
    Case{sim::Scene::demonstration, 0xEDDE90A520EC005CULL},
    Case{sim::Scene::dam_break, 0x5E1FCC0A14078035ULL},
    Case{sim::Scene::sand_water, 0x8F6FB28F82E08E33ULL},
    Case{sim::Scene::wood_fire, 0x3387E29D928FD9A3ULL},
    Case{sim::Scene::quench_metal, 0xD5764D486ED1F8F1ULL},
    Case{sim::Scene::shielded_explosion, 0x9BFB65CF009E673EULL},
};
// clang-format on
constexpr std::uint64_t session_expected = 0xAF411CB1DAE73558ULL;

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xFFU;
        hash *= 0x100'0000'01B3ULL;
    }
    return hash;
}
std::uint64_t mix(std::uint64_t hash, float value) {
    return mix(hash, std::uint64_t{std::bit_cast<std::uint32_t>(value)});
}

std::uint64_t world_hash(sim::Scene scene, unsigned threads) {
    sim::PhysicsConfig config;
    config.worker_threads = threads;
    sim::World world{0x57EF'A023U, config};
    world.load_scene(scene);
    for (int i = 0; i < world_steps; ++i) world.step();
    if (!world.statistics().finite) throw std::runtime_error{"Nieskończone pola w złotym wzorcu"};
    return world.checksum();
}

std::uint64_t session_hash(unsigned threads) {
    sim::PhysicsConfig config;
    config.worker_threads = threads;
    game::GameSession session{game::PlayerTuning{}, game::WeaponConfig{}, config};
    for (int frame = 0; frame < session_ticks; ++frame) {
        game::TickInput input;
        input.aim = {184, 140};
        input.move = frame < 40 ? 1.F : frame < 120 ? -1.F : 0.F;
        input.jump = frame == 3;
        input.jet = frame > 10 && frame < 90;
        input.fire = frame % 3 == 0;
        input.grenade = frame == 2;
        input.reload = frame == 120;
        session.tick(input);
    }
    auto hash = mix(0xCBF2'9CE4'8422'2325ULL, session.world().checksum());
    const auto& p = session.player();
    for (float v : {p.feet.x, p.feet.y, p.velocity.x, p.velocity.y, p.fuel}) hash = mix(hash, v);
    hash = mix(hash, session.ticks());
    hash = mix(hash, session.shots_fired());
    hash = mix(hash, session.explosions());
    return hash;
}

} // namespace

int main(int argc, char** argv) {
    const bool print = argc > 1 && std::string_view{argv[1]} == "--print";
    int failures = 0;
    auto compare = [&](const char* name, unsigned threads, std::uint64_t actual, std::uint64_t expected) {
        if (print)
            std::printf("%s threads=%u 0x%016llXULL\n", name, threads, static_cast<unsigned long long>(actual));
        else if (actual != expected) {
            std::fprintf(stderr, "%s threads=%u: 0x%016llX, oczekiwano 0x%016llX\n", name, threads,
                         static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
            ++failures;
        }
    };
    try {
        for (unsigned threads : {1U, 4U}) {
            for (const auto& c : world_cases)
                compare(sim::scene_names[static_cast<std::size_t>(c.scene)], threads, world_hash(c.scene, threads),
                        c.expected);
            compare("arena_session", threads, session_hash(threads), session_expected);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
