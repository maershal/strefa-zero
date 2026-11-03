// Golden master: fixed scenes must reproduce the checksums recorded before the
// demo-stability changes. Any intended physics change has to update these values
// explicitly, with a report.
// Values recorded on main at eda5d76. Run with --print to list the current values.
#include "sim/world.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
using namespace strefa;

constexpr int world_steps = 60;

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

std::uint64_t world_hash(sim::Scene scene, unsigned threads) {
    sim::PhysicsConfig config;
    config.worker_threads = threads;
    sim::World world{0x57EF'A023U, config};
    world.load_scene(scene);
    for (int i = 0; i < world_steps; ++i) world.step();
    if (!world.statistics().finite) throw std::runtime_error{"Nieskończone pola w złotym wzorcu"};
    return world.checksum();
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
        for (unsigned threads : {1U, 4U})
            for (const auto& c : world_cases)
                compare(sim::scene_names[static_cast<std::size_t>(c.scene)], threads, world_hash(c.scene, threads),
                        c.expected);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
