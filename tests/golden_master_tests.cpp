// Golden master v2: fixed cases must reproduce the per-field digests in tests/golden/<toolchain>.txt. Values are
// raw IEEE bits and the step calls libm (exp, expm1, hypot), so each toolchain keeps its own reference. Any intended
// physics change regenerates them with scripts/golden.py and explains the diff in a report. Removing a case needs a
// replacement or the owner's approval (CLAUDE.md, Contracts).
//   --golden-dir DIR   compare with DIR/<toolchain>.txt
//   --actual FILE      on a mismatch, write the actual values to FILE (CI uploads it)
//   --print            write the actual values in the reference format to stdout
#include "sim/fixtures.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
using namespace strefa;

#if defined(_MSC_VER) && !defined(__clang__)
constexpr std::string_view toolchain = "msvc";
#elif defined(__clang__)
constexpr std::string_view toolchain = "clang";
#else
constexpr std::string_view toolchain = "gcc";
#endif

constexpr int world_steps = 60;

void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error{message};
}

struct Case {
    std::string name;
    std::function<void(sim::World&)> load;
    // Runs after every tick; throws when the case no longer covers what it exists for.
    std::function<void(const sim::World&, int tick)> after_step;
    std::function<void()> finish;
};

std::vector<Case> make_cases() {
    std::vector<Case> cases;
    for (auto scene : {sim::Scene::demonstration, sim::Scene::dam_break, sim::Scene::sand_water, sim::Scene::wood_fire,
                       sim::Scene::quench_metal, sim::Scene::shielded_explosion})
        cases.push_back({sim::scene_ids[static_cast<std::size_t>(scene)],
                         [scene](sim::World& w) { w.load_scene(scene); },
                         {},
                         {}});
    // Batched detonation (detonate_pending, #21): the case is only useful while ticks really apply several blasts.
    auto batched = std::make_shared<int>(0);
    cases.push_back({"chain",
                     [batched](sim::World& w) {
                         *batched = 0;
                         sim::load_powder_chain(w);
                     },
                     [batched](const sim::World& w, int) {
                         const auto blasts = std::ranges::count_if(w.events(), [](const sim::PhysicsEvent& e) {
                             return e.kind == sim::PhysicsEventKind::explosion;
                         });
                         if (blasts >= 2) ++*batched;
                     },
                     [batched] { check(*batched >= 30, "chain: za mało ticków z kilkoma wybuchami"); }});
    // projectile_hit "chip" branch: the hit must give the slab velocity that an unhit slab does not have.
    cases.push_back({"chip",
                     [](sim::World& w) {
                         sim::World slab{0x57EF'A023U, w.config()};
                         sim::load_wood_slab(slab);
                         sim::load_wood_chip(w);
                         constexpr auto velocity = static_cast<std::size_t>(sim::DigestField::velocity);
                         check(w.digests()[velocity] != slab.digests()[velocity],
                               "chip: trafienie nie nadało drewnu prędkości");
                     },
                     {},
                     {}});
    return cases;
}

using Key = std::tuple<std::string, unsigned, std::string>;
using Values = std::map<Key, std::uint64_t>;

void record(Values& out, const std::string& name, unsigned threads, const sim::World& world) {
    out[{name, threads, "checksum"}] = world.checksum();
    const auto digests = world.digests();
    for (std::size_t f = 0; f < digests.size(); ++f) out[{name, threads, sim::digest_field_names[f]}] = digests[f];
}

Values run_cases() {
    Values out;
    for (unsigned threads : {1U, 4U})
        for (const auto& c : make_cases()) {
            sim::PhysicsConfig config;
            config.worker_threads = threads;
            sim::World world{0x57EF'A023U, config};
            c.load(world);
            for (int i = 0; i < world_steps; ++i) {
                world.step();
                // Release-build check of the detonate_pending entry condition (physics.hpp): every tick must leave
                // the capacity cache fresh, so the next tick's batch starts from it.
                check(world.detonation_ready(), c.name + ": nieświeża pojemność cieplna po ticku " + std::to_string(i));
                if (c.after_step) c.after_step(world, i);
            }
            if (c.finish) c.finish();
            check(world.statistics().finite, c.name + ": nieskończone pola w złotym wzorcu");
            record(out, c.name, threads, world);
        }
    return out;
}

void write(std::ostream& out, const Values& values) {
    out << "# sim.golden_master reference, toolchain " << toolchain << ", " << world_steps << " ticks\n"
        << "# Regenerate: python3 scripts/golden.py --build-dir <dir>\n";
    char hex[19];
    for (const auto& [key, value] : values) {
        std::snprintf(hex, sizeof hex, "0x%016llX", static_cast<unsigned long long>(value));
        out << std::get<0>(key) << ' ' << std::get<1>(key) << ' ' << std::get<2>(key) << ' ' << hex << '\n';
    }
}

Values read(const std::filesystem::path& path) {
    std::ifstream in{path};
    check(static_cast<bool>(in), "Brak pliku wzorca " + path.string());
    Values values;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields{line};
        std::string name, field, hex;
        unsigned threads{};
        check(static_cast<bool>(fields >> name >> threads >> field >> hex), "Niepoprawny wiersz wzorca: " + line);
        values[{name, threads, field}] = std::stoull(hex, nullptr, 16);
    }
    return values;
}

// Prints one line per case and thread count with the fields that differ; returns the number of such lines.
int compare(const Values& actual, const Values& expected) {
    std::map<std::pair<std::string, unsigned>, std::vector<std::string>> changed;
    for (const auto& [key, value] : actual) {
        const auto found = expected.find(key);
        if (found == expected.end())
            changed[{std::get<0>(key), std::get<1>(key)}].push_back(std::get<2>(key) + " (brak wzorca)");
        else if (found->second != value)
            changed[{std::get<0>(key), std::get<1>(key)}].push_back(std::get<2>(key));
    }
    for (const auto& [key, value] : expected)
        if (!actual.contains(key))
            changed[{std::get<0>(key), std::get<1>(key)}].push_back(std::get<2>(key) + " (usunięty przypadek)");
    for (const auto& [run, fields] : changed) {
        std::cerr << run.first << " threads=" << run.second << ": zmienione pola";
        for (const auto& f : fields) std::cerr << ' ' << f;
        std::cerr << '\n';
    }
    return static_cast<int>(changed.size());
}

} // namespace

int main(int argc, char** argv) {
    bool print = false;
    std::filesystem::path golden_dir, actual_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--print")
            print = true;
        else if (arg == "--golden-dir" && i + 1 < argc)
            golden_dir = argv[++i];
        else if (arg == "--actual" && i + 1 < argc)
            actual_path = argv[++i];
        else {
            std::cerr << "Użycie: strefa_golden_master_tests --print | --golden-dir DIR [--actual FILE]\n";
            return 2;
        }
    }
    try {
        const auto actual = run_cases();
        if (print) {
            write(std::cout, actual);
            return 0;
        }
        check(!golden_dir.empty(), "Brak --golden-dir");
        const auto reference = golden_dir / (std::string{toolchain} + ".txt");
        const int failures = compare(actual, read(reference));
        if (failures == 0) return 0;
        std::cerr << "Przebiegi niezgodne ze wzorcem " << reference.string() << ": " << failures
                  << "; regeneracja: python3 scripts/golden.py --build-dir <dir>\n";
        if (!actual_path.empty()) {
            std::filesystem::create_directories(actual_path.parent_path());
            std::ofstream out{actual_path};
            write(out, actual);
            std::cerr << "Aktualne wartości: " << actual_path.string() << '\n';
        }
        return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
