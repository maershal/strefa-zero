#include "app/thermal_conformance.hpp"

#include "sim/world.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace strefa::app {
namespace {
using sim::Material;
using sim::Point;

struct Probe {
    std::string_view name, unit;
    std::function<double(const sim::World&)> read;
};
struct Scene {
    std::string_view id, description;
    std::function<void(sim::World&)> setup; // initial state only: no edits while the scene runs
    std::vector<Probe> probes;
    std::vector<unsigned> ticks; // CPU ticks (60 Hz) at which every probe is sampled
};

const std::vector<unsigned> default_ticks{0, 30, 120, 300};

Probe temperature(std::string_view name, Point p) {
    return {name, "K", [p](const sim::World& w) { return w.sample(p).temperature; }};
}
Probe air_temperature(std::string_view name, Point p) {
    return {name, "K", [p](const sim::World& w) { return w.sample(p).air_temperature; }};
}
Probe pressure(std::string_view name, Point p) {
    return {name, "sim_pressure", [p](const sim::World& w) { return w.sample(p).pressure; }};
}
Probe cells(std::string_view name, Material m) {
    return {name, "cells", [m](const sim::World& w) { return static_cast<double>(w.count(m)); }};
}
Probe mass(std::string_view name, Material m) {
    return {name, "kg", [m](const sim::World& w) { return w.mass_of(m); }};
}
void fill(sim::World& world, Point a, Point b, Material material) {
    for (int y = a.y; y <= b.y; ++y)
        for (int x = a.x; x <= b.x; ++x) world.set({x, y}, material);
}
// A heater is a block of matter placed hot (World::set with a temperature): cheap, unlike set_temperature, which
// refreshes the thermal state of the whole grid on every call.
void hot(sim::World& world, Point a, Point b, Material material, double kelvin) {
    for (int y = a.y; y <= b.y; ++y)
        for (int x = a.x; x <= b.x; ++x) world.set({x, y}, material, kelvin);
}
// Hot gas: set_temperature of empty cells; keep the regions small (see hot()).
void heat_air(sim::World& world, Point a, Point b, double kelvin) {
    for (int y = a.y; y <= b.y; ++y)
        for (int x = a.x; x <= b.x; ++x) world.set_temperature({x, y}, kelvin);
}
// Closed box with walls of the given thickness around [a, b] (inclusive interior).
void box(sim::World& world, Point a, Point b, int wall, Material material = Material::stone) {
    fill(world, {a.x - wall, a.y - wall}, {b.x + wall, a.y - 1}, material);
    fill(world, {a.x - wall, b.y + 1}, {b.x + wall, b.y + wall}, material);
    fill(world, {a.x - wall, a.y}, {a.x - 1, b.y}, material);
    fill(world, {b.x + 1, a.y}, {b.x + wall, b.y}, material);
}
void floor(sim::World& world) {
    fill(world, {0, sim::grid_height - 8}, {sim::grid_width - 1, sim::grid_height - 1}, Material::stone);
}

std::vector<Scene> scenes() {
    std::vector<Scene> result;
    result.push_back({"rod",
                      "Pręt z gorącym końcem",
                      [](sim::World& w) {
                          fill(w, {100, 95}, {219, 97}, Material::metal);
                          hot(w, {94, 94}, {99, 98}, Material::metal, 1500);
                      },
                      {temperature("t_at_2", {102, 96}), temperature("t_at_5", {105, 96}),
                       temperature("t_at_10", {110, 96}), air_temperature("air_above_hot_end", {104, 90})},
                      default_ticks});
    result.push_back({"chamber",
                      "Komora z gorącym gazem",
                      [](sim::World& w) {
                          box(w, {120, 60}, {200, 140}, 2);
                          heat_air(w, {155, 95}, {164, 104}, 900);
                      },
                      {air_temperature("air_center", {160, 100}), air_temperature("air_corner", {124, 136}),
                       pressure("pressure_center", {160, 100}), temperature("wall_inner", {119, 100}),
                       air_temperature("air_outside", {110, 100})},
                      default_ticks});
    result.push_back(
            {"pulse",
             "Impuls ciśnienia",
             [](sim::World& w) {
                 floor(w);
                 heat_air(w, {158, 94}, {162, 98}, 2000);
             },
             {pressure("pressure_0", {160, 96}), pressure("pressure_10", {170, 96}), pressure("pressure_20", {180, 96}),
              pressure("pressure_40", {200, 96}), air_temperature("air_center", {160, 96})},
             {0, 2, 5, 10, 30, 120}});
    result.push_back({"open_beam",
                      "Belka przy otwartej górze",
                      [](sim::World& w) {
                          w.set_open_top(true);
                          floor(w);
                          hot(w, {130, 150}, {190, 153}, Material::metal, 900);
                      },
                      {air_temperature("air_10_above", {160, 140}),
                       air_temperature("air_40_above", {160, 110}),
                       air_temperature("air_beside", {120, 151}),
                       {"escaped_energy", "J", [](const sim::World& w) { return w.statistics().escaped_energy; }}},
                      default_ticks});
    result.push_back({"door",
                      "Ściana i drzwi 1 px",
                      [](sim::World& w) {
                          box(w, {100, 40}, {220, 150}, 2);
                          fill(w, {160, 40}, {160, 99}, Material::stone);
                          fill(w, {160, 101}, {160, 150}, Material::stone);
                          heat_air(w, {135, 95}, {144, 104}, 800);
                      },
                      {air_temperature("air_left", {140, 100}), air_temperature("air_door", {162, 100}),
                       air_temperature("air_right", {190, 100}), pressure("pressure_left", {140, 100}),
                       pressure("pressure_right", {190, 100})},
                      default_ticks});
    result.push_back({"water_heater",
                      "Woda z grzałką",
                      [](sim::World& w) {
                          floor(w);
                          box(w, {120, 100}, {200, 150}, 2);
                          fill(w, {120, 110}, {200, 150}, Material::water);
                          hot(w, {150, 145}, {170, 148}, Material::metal, 1500);
                      },
                      {temperature("water_low", {160, 143}), temperature("water_high", {160, 115}),
                       cells("steam_cells", Material::steam), mass("water_mass", Material::water)},
                      default_ticks});
    result.push_back({"lava_water",
                      "Lawa na wodzie",
                      [](sim::World& w) {
                          floor(w);
                          box(w, {120, 100}, {200, 150}, 2);
                          fill(w, {120, 125}, {200, 150}, Material::water);
                          fill(w, {150, 105}, {170, 112}, Material::lava);
                      },
                      {cells("water_cells", Material::water), cells("steam_cells", Material::steam),
                       cells("stone_cells", Material::stone), cells("lava_cells", Material::lava),
                       temperature("water_far", {130, 145})},
                      default_ticks});
    result.push_back({"ice_heater",
                      "Lód przy grzałce",
                      [](sim::World& w) {
                          floor(w);
                          fill(w, {140, 160}, {180, 183}, Material::ice);
                          hot(w, {136, 160}, {139, 183}, Material::metal, 1200);
                      },
                      {cells("ice_cells", Material::ice), cells("water_cells", Material::water),
                       temperature("ice_near", {142, 170}), temperature("ice_far", {175, 170})},
                      default_ticks});
    result.push_back({"gunpowder_line",
                      "Linia prochu",
                      [](sim::World& w) {
                          floor(w);
                          fill(w, {60, 181}, {260, 183}, Material::gunpowder);
                          heat_air(w, {60, 181}, {61, 183}, 1000);
                      },
                      {cells("gunpowder_cells", Material::gunpowder), cells("fire_cells", Material::fire),
                       cells("smoke_cells", Material::smoke), temperature("line_middle", {160, 182})},
                      default_ticks});
    result.push_back({"acid_stone",
                      "Kwas na kamieniu",
                      [](sim::World& w) {
                          floor(w);
                          fill(w, {130, 160}, {190, 183}, Material::stone);
                          fill(w, {129, 140}, {129, 159}, Material::metal);
                          fill(w, {191, 140}, {191, 159}, Material::metal);
                          fill(w, {130, 150}, {190, 159}, Material::acid);
                      },
                      {cells("stone_cells", Material::stone), cells("acid_cells", Material::acid),
                       mass("acid_mass", Material::acid), mass("stone_mass", Material::stone)},
                      default_ticks});
    return result;
}

double parse_number(std::string_view text, bool& ok) {
    double value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    ok = ok && error == std::errc{} && end == text.data() + text.size() && std::isfinite(value) && value >= 0;
    return value;
}
} // namespace

std::expected<std::vector<ConformanceThreshold>, std::string> parse_conformance_thresholds(std::string_view text) {
    std::vector<ConformanceThreshold> result;
    std::set<std::pair<std::string, std::string>> seen;
    std::istringstream lines{std::string{text}};
    std::string line;
    for (unsigned number = 1; std::getline(lines, line); ++number) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream fields{line};
        std::vector<std::string> cells;
        for (std::string field; fields >> field;) cells.push_back(field);
        if (cells.empty()) continue;
        if (cells.size() != 4)
            return std::unexpected{
                    std::format("Linia {}: oczekiwano „scena obserwabla bezwzględna względna”.", number)};
        bool ok = true;
        ConformanceThreshold row{cells[0], cells[1], parse_number(cells[2], ok), parse_number(cells[3], ok)};
        if (!ok) return std::unexpected{std::format("Linia {}: tolerancja musi być skończoną liczbą ≥ 0.", number)};
        if (!seen.emplace(row.scene, row.observable).second)
            return std::unexpected{std::format("Linia {}: powtórzony próg {} {}.", number, row.scene, row.observable)};
        result.push_back(std::move(row));
    }
    if (result.empty()) return std::unexpected{"Plik progów nie zawiera żadnego progu."};
    return result;
}

std::vector<ConformanceObservable> thermal_conformance_observables() {
    std::vector<ConformanceObservable> result;
    // Scene ids and probe names are string literals, so the views outlive the scene table.
    for (const auto& scene : scenes())
        for (const auto& probe : scene.probes) result.push_back({scene.id, probe.name, probe.unit});
    return result;
}

std::expected<void, std::string> run_thermal_conformance(const std::filesystem::path& thresholds_path,
                                                         const std::filesystem::path& output) {
    std::ifstream file{thresholds_path};
    if (!file) return std::unexpected{"Nie można odczytać pliku progów " + thresholds_path.string() + "."};
    const std::string text{std::istreambuf_iterator<char>{file}, {}};
    const auto thresholds = parse_conformance_thresholds(text);
    if (!thresholds) return std::unexpected{thresholds_path.filename().string() + ": " + thresholds.error()};
    // Thresholds and recorded observables must match one to one: a new probe needs its threshold first.
    std::map<std::pair<std::string, std::string>, const ConformanceThreshold*> by_key;
    for (const auto& row : *thresholds) by_key[{row.scene, row.observable}] = &row;
    std::string mismatch;
    std::size_t matched = 0;
    for (const auto& o : thermal_conformance_observables()) {
        if (by_key.contains({std::string{o.scene}, std::string{o.observable}}))
            ++matched;
        else
            mismatch += std::format(" brak progu {} {};", o.scene, o.observable);
    }
    if (matched != thresholds->size()) mismatch += " progi bez obserwabli w kodzie;";
    if (!mismatch.empty()) return std::unexpected{"Progi zgodności termiki nie pasują do obserwabli:" + mismatch};

    std::filesystem::create_directories(output);
    std::ofstream csv{output / "thermal-conformance-cpu.csv"}, report{output / "thermal-conformance.txt"};
    if (!csv || !report) return std::unexpected{"Nie można zapisać wyników zgodności termiki."};
    csv << std::setprecision(17) << "scene,observable,unit,seconds,cpu_tick,cpu_value,absolute,relative\n";
    report << "schema=1\nthresholds=" << thresholds_path.filename().string()
           << "\nthreshold_rows=" << thresholds->size() << "\ncpu_tick_seconds=" << sim::tick_duration
           << "\npass_rule=abs(gpu-cpu)<=max(absolute,relative*abs(cpu))\ngpu=not_implemented\n";
    report.flush();
    for (const auto& scene : scenes()) {
        sim::World world;
        world.clear();
        scene.setup(world);
        unsigned tick = 0;
        for (const unsigned sample : scene.ticks) {
            for (; tick < sample; ++tick) world.step();
            for (const auto& probe : scene.probes) {
                const double value = probe.read(world);
                if (!std::isfinite(value))
                    return std::unexpected{
                            std::format("{} {}: wartość CPU nieskończona w ticku {}.", scene.id, probe.name, sample)};
                const auto& limit = *by_key.at({std::string{scene.id}, std::string{probe.name}});
                csv << scene.id << ',' << probe.name << ',' << probe.unit << ',' << sample * sim::tick_duration << ','
                    << sample << ',' << value << ',' << limit.absolute << ',' << limit.relative << '\n';
            }
        }
        report << "scene_" << scene.id << "=cpu_recorded ticks=" << tick << " observables=" << scene.probes.size()
               << " (" << scene.description << ")\n";
        report.flush();
    }
    report << "status=cpu_only\n";
    if (!csv.flush() || !report.flush()) return std::unexpected{"Niepełny zapis wyników zgodności termiki."};
    return {};
}

} // namespace strefa::app
