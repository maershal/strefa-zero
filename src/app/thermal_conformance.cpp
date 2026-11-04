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
#include <optional>
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
    std::optional<Point> where{}; // sampled cell; checked to lie inside the grid
};
struct Scene {
    std::string_view id, description;
    std::function<void(sim::World&)> setup; // initial state only: no edits while the scene runs
    std::vector<Probe> probes;
    std::vector<unsigned> ticks; // CPU ticks (60 Hz) at which every probe is sampled
};

const std::vector<unsigned> default_ticks{0, 30, 120, 300};

// Scene coordinates are relative to the grid instead of 320x192 literals: x from the middle column, y from the top
// of the floor (8 rows above the bottom; negative is up).
Point at(int dx, int dy) {
    return {sim::grid_width / 2 + dx, sim::grid_height - 8 + dy};
}

Probe temperature(std::string_view name, Point p) {
    return {name, "K", [p](const sim::World& w) { return w.sample(p).temperature; }, p};
}
Probe air_temperature(std::string_view name, Point p) {
    return {name, "K", [p](const sim::World& w) { return w.sample(p).air_temperature; }, p};
}
Probe pressure(std::string_view name, Point p) {
    return {name, "sim_pressure", [p](const sim::World& w) { return w.sample(p).pressure; }, p};
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
// Heats existing cells in place with set_temperature: hot gas in empty cells, or the ignition spot of a powder.
// Keep the regions small (see hot()).
void heat_cells(sim::World& world, Point a, Point b, double kelvin) {
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
    fill(world, {0, at(0, 0).y}, {sim::grid_width - 1, sim::grid_height - 1}, Material::stone);
}
// Temperatures compare the rise above ambient: a relative tolerance of an absolute temperature in kelvin would
// dwarf the absolute one (5% of 293 K is already 14.7 K).
bool relative_to_rise(const Probe& probe) {
    return probe.unit == "K";
}
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::vector<Scene> scenes() {
    std::vector<Scene> result;
    result.push_back({"rod",
                      "Pręt z gorącym końcem",
                      [](sim::World& w) {
                          fill(w, at(-60, -89), at(59, -87), Material::metal);
                          hot(w, at(-66, -90), at(-61, -86), Material::metal, 1500);
                      },
                      {temperature("t_at_2", at(-58, -88)), temperature("t_at_5", at(-55, -88)),
                       temperature("t_at_10", at(-50, -88)), air_temperature("air_above_hot_end", at(-56, -94))},
                      default_ticks});
    result.push_back({"chamber",
                      "Komora z gorącym gazem",
                      [](sim::World& w) {
                          box(w, at(-40, -124), at(40, -44), 2);
                          heat_cells(w, at(-5, -89), at(4, -80), 900);
                      },
                      {air_temperature("air_center", at(0, -84)), air_temperature("air_corner", at(-36, -48)),
                       pressure("pressure_center", at(0, -84)), temperature("wall_inner", at(-41, -84)),
                       air_temperature("air_outside", at(-50, -84))},
                      default_ticks});
    result.push_back({"pulse",
                      "Impuls ciśnienia",
                      [](sim::World& w) {
                          floor(w);
                          heat_cells(w, at(-2, -90), at(2, -86), 2000);
                      },
                      {pressure("pressure_0", at(0, -88)), pressure("pressure_10", at(10, -88)),
                       pressure("pressure_20", at(20, -88)), pressure("pressure_40", at(40, -88)),
                       air_temperature("air_center", at(0, -88))},
                      {0, 2, 5, 10, 30, 120}});
    result.push_back({"open_beam",
                      "Belka przy otwartej górze",
                      [](sim::World& w) {
                          w.set_open_top(true);
                          floor(w);
                          hot(w, at(-30, -34), at(30, -31), Material::metal, 900);
                      },
                      {air_temperature("air_10_above", at(0, -44)),
                       air_temperature("air_40_above", at(0, -74)),
                       air_temperature("air_beside", at(-40, -33)),
                       {"escaped_energy", "J", [](const sim::World& w) { return w.statistics().escaped_energy; }}},
                      default_ticks});
    result.push_back({"door",
                      "Ściana i drzwi 1 px",
                      [](sim::World& w) {
                          box(w, at(-60, -144), at(60, -34), 2);
                          fill(w, at(0, -144), at(0, -85), Material::stone);
                          fill(w, at(0, -83), at(0, -34), Material::stone);
                          heat_cells(w, at(-25, -89), at(-16, -80), 800);
                      },
                      {air_temperature("air_left", at(-20, -84)), air_temperature("air_door", at(2, -84)),
                       air_temperature("air_right", at(30, -84)), pressure("pressure_left", at(-20, -84)),
                       pressure("pressure_right", at(30, -84))},
                      default_ticks});
    result.push_back({"water_heater",
                      "Woda z grzałką",
                      [](sim::World& w) {
                          floor(w);
                          box(w, at(-40, -84), at(40, -34), 2);
                          fill(w, at(-40, -74), at(40, -34), Material::water);
                          hot(w, at(-10, -39), at(10, -36), Material::metal, 1500);
                      },
                      {temperature("water_low", at(0, -41)), temperature("water_high", at(0, -69)),
                       cells("steam_cells", Material::steam), mass("water_mass", Material::water)},
                      default_ticks});
    result.push_back({"lava_water",
                      "Lawa na wodzie",
                      [](sim::World& w) {
                          floor(w);
                          box(w, at(-40, -84), at(40, -34), 2);
                          fill(w, at(-40, -59), at(40, -34), Material::water);
                          fill(w, at(-10, -79), at(10, -72), Material::lava);
                      },
                      {cells("water_cells", Material::water), cells("steam_cells", Material::steam),
                       cells("stone_cells", Material::stone), cells("lava_cells", Material::lava),
                       temperature("water_far", at(-30, -39))},
                      default_ticks});
    result.push_back({"ice_heater",
                      "Lód przy grzałce",
                      [](sim::World& w) {
                          floor(w);
                          fill(w, at(-20, -24), at(20, -1), Material::ice);
                          hot(w, at(-24, -24), at(-21, -1), Material::metal, 1200);
                      },
                      {cells("ice_cells", Material::ice), cells("water_cells", Material::water),
                       temperature("ice_near", at(-18, -14)), temperature("ice_far", at(15, -14))},
                      default_ticks});
    result.push_back({"gunpowder_line",
                      "Linia prochu",
                      [](sim::World& w) {
                          floor(w);
                          fill(w, at(-100, -3), at(100, -1), Material::gunpowder);
                          heat_cells(w, at(-100, -3), at(-99, -1), 1000);
                      },
                      {cells("gunpowder_cells", Material::gunpowder), cells("fire_cells", Material::fire),
                       cells("smoke_cells", Material::smoke), temperature("line_middle", at(0, -2))},
                      default_ticks});
    result.push_back({"acid_stone",
                      "Kwas na kamieniu",
                      [](sim::World& w) {
                          floor(w);
                          fill(w, at(-30, -24), at(30, -1), Material::stone);
                          fill(w, at(-31, -44), at(-31, -25), Material::metal);
                          fill(w, at(31, -44), at(31, -25), Material::metal);
                          fill(w, at(-30, -34), at(30, -25), Material::acid);
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

std::size_t thermal_conformance_sample_count() {
    std::size_t count = 0;
    for (const auto& scene : scenes()) count += scene.probes.size() * scene.ticks.size();
    return count;
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
    if (!file) return std::unexpected{"Nie można odczytać pliku progów " + utf8(thresholds_path) + "."};
    const std::string text{std::istreambuf_iterator<char>{file}, {}};
    const auto thresholds = parse_conformance_thresholds(text);
    if (!thresholds) return std::unexpected{utf8(thresholds_path.filename()) + ": " + thresholds.error()};
    // Thresholds and recorded observables must match one to one: a new probe needs its threshold first.
    std::map<std::pair<std::string, std::string>, const ConformanceThreshold*> by_key;
    for (const auto& row : *thresholds) by_key[{row.scene, row.observable}] = &row;
    std::set<std::pair<std::string, std::string>> recorded;
    std::string mismatch;
    for (const auto& o : thermal_conformance_observables()) {
        recorded.emplace(o.scene, o.observable);
        if (!by_key.contains({std::string{o.scene}, std::string{o.observable}}))
            mismatch += std::format(" brak progu {} {};", o.scene, o.observable);
    }
    for (const auto& [key, row] : by_key)
        if (!recorded.contains(key)) mismatch += std::format(" próg bez obserwabli {} {};", key.first, key.second);
    if (!mismatch.empty()) return std::unexpected{"Progi zgodności termiki nie pasują do obserwabli:" + mismatch};

    for (const auto& scene : scenes())
        for (const auto& probe : scene.probes)
            if (probe.where && !sim::contains(*probe.where))
                return std::unexpected{std::format("{} {}: sonda poza siatką.", scene.id, probe.name)};

    std::error_code created;
    std::filesystem::create_directories(output, created);
    if (created) return std::unexpected{"Nie można utworzyć katalogu wyników zgodności termiki."};
    std::ofstream csv{output / "thermal-conformance-cpu.csv"}, report{output / "thermal-conformance.txt"};
    if (!csv || !report) return std::unexpected{"Nie można zapisać wyników zgodności termiki."};
    csv << std::setprecision(17) << "scene,observable,unit,seconds,cpu_tick,cpu_value,absolute,relative,relative_of\n";
    report << "schema=1\nthresholds=" << utf8(thresholds_path.filename()) << "\nthreshold_rows=" << thresholds->size()
           << "\ncpu_tick_seconds=" << sim::tick_duration << "\nambient_k=" << sim::ambient_temperature
           << "\npass_rule=abs(gpu-cpu)<=max(absolute,relative*abs(base)),base=cpu|cpu-ambient_k(relative_of)"
              "\nthresholds_status=provisional_until_gpu_side\ngpu=not_implemented\n";
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
                    << sample << ',' << value << ',' << limit.absolute << ',' << limit.relative << ','
                    << (relative_to_rise(probe) ? "rise_above_ambient" : "value") << '\n';
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
