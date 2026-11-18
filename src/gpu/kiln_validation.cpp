// --gpu-check thermal-kiln: two-stage wood combustion (#112) in a charcoal kiln. A block of wood is heated inside a
// stone chamber, once sealed and once with an open roof. Sealed: the pyrolysis gas burns until the oxygen is gone, the
// flame goes out, unburnt fuel gas stays and the wood is left as char. Vented: the flame keeps burning. Both runs keep
// the mass and energy balances of the thermal model. The model has no CPU reference (#111), so the pass conditions
// below are its specification; they were written before the constants were tuned.
#include "gpu/dimensions.hpp"
#include "gpu/render_device.hpp"
#include "gpu/validation.hpp"
#include "gpu/world_test_access.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace strefa::gpu {
namespace {
// Chamber interior and the wood block (cells, inclusive); the stone shell is two cells thick.
constexpr int left = 111, right = 128, top = 91, bottom = 100, shell = 2;
constexpr int wood_left = 114, wood_right = 125, wood_top = 95, wood_bottom = 96;
// The heater covers the wood cells from heated_left to heated_right; the end columns heat up by conduction only.
constexpr int heated_left = wood_left + 1, heated_right = wood_right - 1;
constexpr unsigned heating_ticks = 480, total_ticks = 2400, checkpoint = 240;
constexpr float heater_rate = 300; // kJ/s per selected cell
// Pass conditions.
constexpr double balance_tolerance = 1e-3;   // relative mass and energy error, as in --gpu-thermal-check
constexpr double oxygen_left = .02;          // sealed: share of the chamber's initial oxygen at the end
constexpr double flame_out = 1e-6;           // sealed: burn heat of the last tick relative to the peak checkpoint
constexpr double vented_fuel_gas_left = .01; // vented: fuel gas at the end relative to the sealed run

struct Chamber {
    double oxygen{}, fuel_gas{}, smoke{}, burn{}; // sums over the chamber interior; burn = heat of the last tick
    unsigned wood{}, charring{}, chars{}, ash{}, walls{};
    double hottest{};
};
struct Run {
    ThermalTotals before, after;
    Chamber start, end;
    double peak_burn{};
};
int cell(int x, int y) {
    return y * width + x;
}
Chamber measure(const ThermalSnapshot& snapshot, const std::vector<std::uint32_t>& terrain) {
    Chamber c;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const auto& v = snapshot.cells[cell(x, y)];
            c.oxygen += v.gas0[1];
            c.fuel_gas += v.gas1[0];
            c.smoke += v.gas0[3];
            c.burn += v.extra[0];
        }
    for (int y = wood_top; y <= wood_bottom; ++y)
        for (int x = wood_left; x <= wood_right; ++x) {
            const auto& m = snapshot.cells[cell(x, y)].matter;
            const auto material = static_cast<sim::Material>(static_cast<int>(m[3]));
            c.walls += terrain[cell(x, y)] != 0;
            c.wood += material == sim::Material::wood;
            c.charring += material == sim::Material::wood && m[2] < m[0];
            c.chars += material == sim::Material::coal;
            c.ash += material == sim::Material::ash;
            if (m[0] > 0 && material != sim::Material::ash)
                c.hottest = std::max(c.hottest, static_cast<double>(m[1]) / m[0]);
        }
    return c;
}
Run run(const std::filesystem::path& shaders, const ExecutionOptions& layout, std::shared_ptr<RenderDevice>& device,
        bool sealed, std::ofstream& csv) {
    auto made = ParticleWorld::create(shaders, layout, {}, {}, device);
    if (!made) throw std::runtime_error{made.error()};
    auto& world = **made;
    if (!device) device = world.render_device();
    world.set_vfx_options({VfxQuality::off, false});
    ThermalOptions options;
    options.enabled = true;
    world.set_thermal_options(options);
    std::vector<std::uint32_t> walls(static_cast<std::size_t>(width) * height);
    for (int y = top - shell; y <= bottom + shell; ++y)
        for (int x = left - shell; x <= right + shell; ++x) {
            const bool inside = x >= left && x <= right && y >= top && y <= bottom;
            const bool roof = y < top && x >= left && x <= right;
            if (!inside && (sealed || !roof)) walls[cell(x, y)] = 1;
        }
    for (int y = wood_top; y <= wood_bottom; ++y)
        for (int x = wood_left; x <= wood_right; ++x) walls[cell(x, y)] = 2;
    world.load({}, walls);
    const auto read = [&] {
        glFinish();
        auto snapshot = test::read_thermal(world, ThermalRead::all);
        auto terrain = world.read_terrain();
        world.invalidate_gl_state();
        return std::pair{std::move(snapshot), std::move(terrain)};
    };
    Run result;
    {
        const auto [snapshot, terrain] = read();
        result.before = snapshot.totals;
        result.start = measure(snapshot, terrain);
    }
    ThermalBrush heater;
    heater.tool = ThermalTool::heat;
    heater.radius = thermal_brush_min_radius;
    heater.rate = heater_rate;
    heater.segment = {static_cast<float>(heated_left) + .5F, static_cast<float>(wood_bottom),
                      static_cast<float>(heated_right) + .5F, static_cast<float>(wood_bottom)};
    world.set_thermal_brush(heater);
    for (unsigned tick = 1; tick <= total_ticks; ++tick) {
        if (tick == heating_ticks + 1) world.set_thermal_brush({});
        while (!world.step()) glFinish();
        if (tick % checkpoint != 0) continue;
        const auto [snapshot, terrain] = read();
        result.after = snapshot.totals;
        result.end = measure(snapshot, terrain);
        result.peak_burn = std::max(result.peak_burn, result.end.burn);
        const auto& e = result.end;
        csv << (sealed ? "sealed" : "vented") << ',' << tick << ',' << e.oxygen << ',' << e.fuel_gas << ',' << e.smoke
            << ',' << e.burn << ',' << e.wood << ',' << e.charring << ',' << e.chars << ',' << e.ash << ',' << e.walls
            << ',' << e.hottest << ',' << snapshot.totals.mass << ',' << snapshot.totals.enthalpy << ','
            << snapshot.totals.chemical << ',' << snapshot.totals.external_enthalpy << ','
            << snapshot.totals.released_heat << '\n';
    }
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error{"Błąd OpenGL w sprawdzeniu thermal-kiln."};
    return result;
}
} // namespace

std::expected<void, std::string> validate_thermal_kiln(const std::filesystem::path& shaders,
                                                       const std::filesystem::path& output,
                                                       const ExecutionOptions& layout) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "thermal-kiln.txt"}, csv{output / "thermal-kiln.csv"};
        csv << "run,tick,oxygen,fuel_gas,smoke,burn_heat,wood_cells,charring_cells,char_cells,ash_cells,wall_cells,"
               "hottest_specific_enthalpy,mass,enthalpy,chemical,external_enthalpy,released_heat\n";
        std::shared_ptr<RenderDevice> device;
        const auto sealed = run(shaders, layout, device, true, csv);
        const auto vented = run(shaders, layout, device, false, csv);
        std::string failure;
        const auto require = [&](bool ok, const char* name) {
            report << name << '=' << (ok ? "PASS" : "FAIL") << '\n';
            if (!ok && failure.empty()) failure = name;
        };
        const auto balance = [&](const Run& r, const char* mass_name, const char* energy_name) {
            const auto& a = r.before;
            const auto& b = r.after;
            const double dm = (b.mass - a.mass) - (b.external_mass - a.external_mass);
            const double de = (b.enthalpy + b.chemical - a.enthalpy - a.chemical)
                            - (b.external_enthalpy + b.external_chemical - a.external_enthalpy - a.external_chemical);
            const double rm = std::abs(dm) / std::max(a.mass, 1e-8);
            const double re = std::abs(de) / std::max(std::abs(a.enthalpy) + a.chemical, 1e-8);
            report << mass_name << "_relative_error=" << rm << '\n' << energy_name << "_relative_error=" << re << '\n';
            require(b.finite && rm < balance_tolerance, mass_name);
            require(b.finite && re < balance_tolerance, energy_name);
        };
        const unsigned block = (wood_right - wood_left + 1) * (wood_bottom - wood_top + 1);
        const unsigned heated = (heated_right - heated_left + 1) * (wood_bottom - wood_top + 1);
        for (const auto& [name, r] : {std::pair{"sealed", &sealed}, std::pair{"vented", &vented}}) {
            const auto& e = r->end;
            report << std::format(
                    "{} oxygen={} of {} fuel_gas={} smoke={} burn={} peak_burn={} wood={} char={} ash={}\n", name,
                    e.oxygen, r->start.oxygen, e.fuel_gas, e.smoke, e.burn, r->peak_burn, e.wood, e.chars, e.ash);
        }
        balance(sealed, "sealed_mass_balance", "sealed_energy_balance");
        balance(vented, "vented_mass_balance", "vented_energy_balance");
        require(sealed.start.wood == block && sealed.start.walls == block, "scene_starts_as_raw_wood");
        require(sealed.peak_burn > 0 && sealed.end.smoke > 0, "sealed_flame_burned_pyrolysis_gas");
        require(sealed.end.oxygen < oxygen_left * sealed.start.oxygen, "sealed_oxygen_exhausted");
        require(sealed.end.burn <= flame_out * sealed.peak_burn, "sealed_flame_went_out");
        require(sealed.end.fuel_gas > 0, "sealed_unburnt_fuel_gas_remains");
        require(sealed.end.chars >= heated, "sealed_heated_wood_became_char");
        require(sealed.end.ash == 0 && sealed.end.walls == block && sealed.end.wood + sealed.end.chars == block,
                "sealed_char_remains_as_terrain");
        // burn heat counts every reaction of the tick, so the char glowing at the end passes it as well; the gas flame
        // itself is the next condition: the vented chamber burns its pyrolysis gas instead of accumulating it.
        require(vented.end.burn > 0 && vented.end.burn > sealed.end.burn, "vented_combustion_continues");
        require(vented.end.fuel_gas < vented_fuel_gas_left * sealed.end.fuel_gas, "vented_pyrolysis_gas_burned");
        require(vented.end.oxygen > sealed.end.oxygen, "vented_chamber_keeps_oxygen");
        if (!failure.empty())
            throw std::runtime_error{std::format("thermal-kiln: {} (szczegóły w thermal-kiln.txt).", failure)};
        report << "result=PASS\n";
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "thermal-kiln-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
