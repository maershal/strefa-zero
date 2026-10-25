#include "sim/physics.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <chrono>

namespace strefa::sim::detail {
namespace {
template<class T> void cells(std::vector<T>& values, int count = grid_cells) { values.resize(count); }
void equilibrate_phase(State& s, int i, Material solid, Material liquid, double transition, double latent) {
    if (s.material[i] != Material::empty && s.material[i] != solid) return;
    const int channel = liquid_index(liquid);
    const double old_solid = s.mass[i], old_liquid = s.liquid.volume[channel][i] * properties(liquid).density;
    const double mass = old_solid + old_liquid;
    if (mass < 1e-8) return; // Retain unresolved traces without unstable H/m division.
    const double energy = s.energy[i] + s.liquid.energy[channel][i];
    const double hs = specific_enthalpy(solid, transition), hl = hs + latent;
    const double liquid_mass = std::clamp((energy - mass * hs) / latent, 0.0, mass);
    const double solid_mass = mass - liquid_mass;
    double solid_energy = solid_mass * hs, liquid_energy = liquid_mass * hl;
    if (liquid_mass == 0) solid_energy = energy;
    else if (solid_mass == 0) liquid_energy = energy;
    s.material[i] = solid_mass > 1e-12 ? solid : Material::empty;
    s.mass[i] = solid_mass; s.energy[i] = solid_energy;
    s.liquid.volume[channel][i] = liquid_mass / properties(liquid).density;
    s.liquid.energy[channel][i] = liquid_energy;
    if (std::abs(old_solid - solid_mass) > 1e-10) {
        wake(s, i);
        if ((old_solid < 1e-10 || solid_mass < 1e-10) && s.events.size() < 2048)
            s.events.push_back({s.next_event++, s.report.tick, PhysicsEventKind::phase_change,
                {i % grid_width, i / grid_width}, static_cast<float>(liquid_mass)});
    }
}
double conductivity(const State& s, int i) {
    double k = s.mass[i] > 0 ? properties(s.material[i]).conductance : 0;
    for (std::size_t c = 0; c < liquid_materials.size(); ++c)
        if (s.liquid.volume[c][i] > 0) k = std::max(k, properties(liquid_materials[c]).conductance);
    return k;
}
}
void LiquidFields::allocate() {
    compact.allocate();
    transfers.reserve(grid_cells*2);component.reserve(grid_cells);
    closed_cells.reserve(grid_cells);closed_ends.reserve(grid_cells);
    next_u.resize(horizontal_faces); next_v.resize(vertical_faces);
    velocity_valid.resize(std::max(horizontal_faces,vertical_faces));velocity_next_valid.resize(velocity_valid.size());
    for (auto& a : volume) cells(a); for (auto& a : energy) cells(a);
    cells(u, horizontal_faces); cells(v, vertical_faces);
    for (auto* a : {&pressure, &density, &fill, &capacity, &viscosity, &diagonal, &right, &down, &rhs,
        &delta_volume, &delta_energy, &outflow}) cells(*a);
    cells(mask); cells(anchor); wet.reserve(grid_cells);
}
void AirFields::allocate() {
    flows.reserve(grid_cells*2);
    for(auto& a:delta) cells(a);
    for(auto& a:diffusion_energy) cells(a);
    cells(open);
    cells(buoyancy);
    for (auto& a : mass) cells(a);
    for (auto* a : {&energy, &temperature, &capacity, &pressure_source, &scalar_delta, &outflow}) cells(*a);
    cells(pressure); cells(next_pressure); cells(u, horizontal_faces); cells(v, vertical_faces);
    cells(next_u, horizontal_faces); cells(next_v, vertical_faces);
}
void State::allocate() {
    heat_fluxes.reserve(grid_cells*7);heat_load.resize(grid_cells*2);heat_delta.resize(grid_cells*2);
    cells(material); cells(shade); cells(loose); cells(burning); cells(updated);
    for (auto* a : {&mass, &energy, &fuel, &acid_capacity, &dissolved, &temperature, &capacity}) cells(*a);
    for (auto* a : {&vx, &vy, &offset_x, &offset_y}) cells(*a);
    cells(shock);
    liquid.allocate(); air.allocate(); events.reserve(256); explosions.reserve(256);
}
double matter_mass(const State& s, int i) noexcept {
    double value = s.mass[i] + s.dissolved[i];
    for (std::size_t c = 0; c < liquid_materials.size(); ++c) value += s.liquid.volume[c][i] * properties(liquid_materials[c]).density;
    return value;
}
double matter_energy(const State& s, int i) noexcept {
    double value = s.energy[i];
    for (const auto& channel : s.liquid.energy) value += channel[i];
    return value;
}
double gas_mass(const State& s, int i) noexcept {
    double value = 0; for (const auto& channel : s.air.mass) value += channel[i]; return value;
}
double total_mass(const State& s) noexcept {
    double value = 0; for (int i = 0; i < grid_cells; ++i) value += matter_mass(s, i) + gas_mass(s, i); return value;
}
double total_energy(const State& s) noexcept {
    double value = 0; for (int i = 0; i < grid_cells; ++i) value += matter_energy(s, i) + s.air.energy[i]; return value;
}
double solid_fraction(const State& s, int i) noexcept {
    return s.mass[i] > 0 ? s.mass[i] / properties(s.material[i]).density : 0;
}
double liquid_fill(const State& s, int i) noexcept {
    double result = 0; for (const auto& volume : s.liquid.volume) result += volume[i]; return result;
}
bool barrier(const State& s, int i) noexcept { return s.mass[i] > 1e-7; }
Material dominant(const State& s, int i) noexcept {
    if (barrier(s, i)) return s.material[i];
    double maximum = 1e-5; Material material = Material::empty;
    for (std::size_t c = 0; c < liquid_materials.size(); ++c)
        if (s.liquid.volume[c][i] > maximum) { maximum = s.liquid.volume[c][i]; material = liquid_materials[c]; }
    if (material != Material::empty) return material;
    if (s.air.temperature[i] > 850 && gas_mass(s, i) > .025) return Material::fire;
    if (s.air.mass[gi(Gas::steam)][i] > .002) return Material::steam;
    if (s.air.mass[gi(Gas::smoke)][i] > .0002) return Material::smoke;
    if (s.air.mass[gi(Gas::fuel)][i] > .0002) return Material::gas;
    return Material::empty;
}
void wake(State& s, int i) noexcept {
    const int cx = (i % grid_width) / 16, cy = (i / grid_width) / 16;
    for (int y = std::max(0, cy - 1); y <= std::min(11, cy + 1); ++y)
        for (int x = std::max(0, cx - 1); x <= std::min(19, cx + 1); ++x)
            s.movement_awake[y * 20 + x] = s.next_awake[y * 20 + x] = true;
}
void refresh_thermal(State& s) {
    const auto start=std::chrono::steady_clock::now();
    s.air_awake = s.heat_awake = s.chemistry_awake = false;
    s.thermal_dirty=false; s.cached_mass=s.cached_energy=0; s.cached_particles=0; s.cached_finite=true;
    struct Row {
        double mass{},energy{};std::size_t particles{};
        bool finite{true},air{},heat{},chemistry{};
    };
    std::array<Row,grid_height> rows{};
    s.executor.run(grid_height,16,[&](int begin,int end) {
    for(int y=begin;y<end;++y) {
    auto& row=rows[y];
    for (int i = y*grid_width; i < (y+1)*grid_width; ++i) {
        double capacity = s.mass[i] * properties(s.material[i]).heat_capacity + s.dissolved[i];
        double offset = s.mass[i] * enthalpy_offset(s.material[i]);
        double cell_mass=s.mass[i]+s.dissolved[i];
        for (std::size_t c = 0; c < liquid_materials.size(); ++c) {
            const auto m = liquid_materials[c];
            const double mass = s.liquid.volume[c][i] * properties(m).density;
            capacity += mass * properties(m).heat_capacity; offset += mass * enthalpy_offset(m);
            cell_mass+=mass;
            row.finite=row.finite && s.liquid.volume[c][i]>=-1e-9 && std::isfinite(s.liquid.energy[c][i]);
        }
        s.capacity[i] = capacity;
        const double cell_energy=matter_energy(s,i);
        s.temperature[i] = capacity > 1e-8 ? std::max(1.0, (cell_energy - offset) / capacity) : ambient_temperature;
        double air_capacity = 0, air_offset = 0;
        for (unsigned c = 0; c < gas_count; ++c) {
            const auto gas = static_cast<Gas>(c);
            air_capacity += s.air.mass[c][i] * gas_cp(gas); air_offset += s.air.mass[c][i] * gas_offset(gas);
            cell_mass+=s.air.mass[c][i];
            row.finite=row.finite && s.air.mass[c][i]>=-1e-9;
        }
        s.air.capacity[i] = air_capacity;
        s.air.temperature[i] = air_capacity > 1e-12 ? std::max(1.0, (s.air.energy[i] - air_offset) / air_capacity) : ambient_temperature;
        const bool gas_species = s.air.mass[gi(Gas::steam)][i] > 1e-9 || s.air.mass[gi(Gas::smoke)][i] > 1e-9
            || s.air.mass[gi(Gas::fuel)][i] > 1e-9 || s.air.mass[gi(Gas::products)][i] > 1e-9;
        const bool warm_air = std::abs(s.air.temperature[i] - ambient_temperature) > 1e-4;
        const bool oxygen_gradient = std::abs(s.air.mass[gi(Gas::oxygen)][i] * 3 - s.air.mass[gi(Gas::inert)][i]) > 1e-12;
        const int x=i%grid_width;
        row.air = row.air || gas_species || warm_air || oxygen_gradient || std::abs(s.air.pressure[i]) > 1e-9
            || s.air.pressure_source[i] != 0 || std::abs(s.air.u[u_index(x,y)]) > 1e-9 || std::abs(s.air.v[v_index(x,y)]) > 1e-9;
        row.heat = row.heat || warm_air || (capacity > 1e-6 && std::abs(s.temperature[i]-ambient_temperature)>1e-4);
        const auto material=dominant(s,i);
        if(!row.chemistry) row.chemistry = material==Material::acid
            || (s.air.mass[gi(Gas::fuel)][i]>0 && s.air.temperature[i]>properties(Material::gas).ignition)
            || ((s.capacity[i]>0?s.temperature[i]:s.air.temperature[i]) >= properties(material).ignition && properties(material).ignition>0)
            || ((material==Material::nitroglycerin || material==Material::gunpowder) && s.shock[i]>10);
        row.mass+=cell_mass; row.energy+=cell_energy+s.air.energy[i];
        if(material!=Material::empty) ++row.particles;
        row.finite=row.finite && std::isfinite(s.temperature[i]) && std::isfinite(s.air.temperature[i])
            && std::isfinite(s.air.pressure[i]) && std::isfinite(s.air.pressure_source[i]) && s.mass[i]>=-1e-9;
    }}
    });
    for(const auto& row:rows) {
        s.cached_mass+=row.mass;s.cached_energy+=row.energy;s.cached_particles+=row.particles;
        s.cached_finite=s.cached_finite&&row.finite;
        s.air_awake=s.air_awake||row.air;s.heat_awake=s.heat_awake||row.heat;
        s.chemistry_awake=s.chemistry_awake||row.chemistry;
    }
    s.report.thermal_refresh_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void add_matter_heat(State& s, int i, double heat) {
    if (s.capacity[i] <= 1e-12) { s.air.energy[i] += heat; return; }
    s.energy[i] += heat * (s.mass[i] * properties(s.material[i]).heat_capacity) / s.capacity[i];
    for (std::size_t c = 0; c < liquid_materials.size(); ++c) {
        const double capacity = s.liquid.volume[c][i] * properties(liquid_materials[c]).density * properties(liquid_materials[c]).heat_capacity;
        s.liquid.energy[c][i] += heat * capacity / s.capacity[i];
    }
    // Dissolved products share the carrier's energy channel.
    if (s.dissolved[i] > 0) {
        int carrier = 0;
        for (int c = 1; c < static_cast<int>(liquid_materials.size()); ++c)
            if (s.liquid.volume[c][i] > s.liquid.volume[carrier][i]) carrier = c;
        s.liquid.energy[carrier][i] += heat * s.dissolved[i] / s.capacity[i];
    }
}
void phase_changes(State& s) {
    for (int i = 0; i < grid_cells; ++i) {
        equilibrate_phase(s, i, Material::ice, Material::water, 273.15, 334);
        equilibrate_phase(s, i, Material::metal, Material::molten_metal, 1811, 270);
        equilibrate_phase(s, i, Material::stone, Material::lava, 1400, 400);
        const double water = s.liquid.volume[0][i];
        const double latent = 2256;
        const double boiled = std::clamp((s.liquid.energy[0][i] - water * specific_enthalpy(Material::water, 373.15)) / latent, 0.0, water);
        if (boiled > 0) {
            const double energy = boiled * specific_enthalpy(Material::steam, 373.15);
            s.liquid.volume[0][i] -= boiled; s.liquid.energy[0][i] -= energy;
            s.air.mass[gi(Gas::steam)][i] += boiled; s.air.energy[i] += energy;
            s.air.pressure_source[i] += boiled * 8;
            wake(s, i);
        }
        if(!barrier(s,i)&&s.air.mass[gi(Gas::steam)][i]>0) {
            double capacity=0,offset=0;
            for(unsigned c=0;c<gas_count;++c) {
                capacity+=s.air.mass[c][i]*gas_cp(static_cast<Gas>(c));
                offset+=s.air.mass[c][i]*gas_offset(static_cast<Gas>(c));
            }
            // Heat exchange and boiling changed H since the previous cache.
            // Condensation must use the current enthalpy, not last-step T.
            s.air.capacity[i]=capacity;
            s.air.temperature[i]=capacity>1e-12?std::max(1.0,(s.air.energy[i]-offset)/capacity):ambient_temperature;
        }
        if (!barrier(s, i) && s.air.temperature[i] < 373.15 && s.air.mass[gi(Gas::steam)][i] > 0) {
            const double condensed = std::min(s.air.mass[gi(Gas::steam)][i],
                s.air.capacity[i] * (373.15 - s.air.temperature[i]) / latent);
            const double energy = condensed * specific_enthalpy(Material::water, 373.15);
            s.air.mass[gi(Gas::steam)][i] -= condensed; s.air.energy[i] -= energy;
            s.liquid.volume[0][i] += condensed; s.liquid.energy[0][i] += energy;
            wake(s, i);
        }
    }
    refresh_thermal(s);
}
void conduct_heat(State& s, double dt) {
    refresh_thermal(s);
    s.thermal_awake.fill(false);
    auto& delta=s.heat_delta;auto& load=s.heat_load;auto& fluxes=s.heat_fluxes;
    std::ranges::fill(delta,0.0);std::ranges::fill(load,0.0);fluxes.clear();
    auto exchange=[&](int a,int b,double ca,double cb,double difference,double heat) {
        if(heat==0||std::abs(difference)<1e-12) return;
        const double conductance_dt=std::abs(heat/difference);
        load[a]+=conductance_dt/ca;load[b]+=conductance_dt/cb;
        fluxes.push_back({a,b,heat});
    };
    auto gas_contact = [&](int matter, int gas) {
        if (s.capacity[matter] <= 0 || s.air.capacity[gas] <= 0) return;
        const double difference = s.air.temperature[gas] - s.temperature[matter];
        const double limit = .12 * std::min(s.capacity[matter], s.air.capacity[gas]) * std::abs(difference);
        const double heat = std::clamp(s.config.heat_contact * difference * dt, -limit, limit);
        exchange(matter,grid_cells+gas,s.capacity[matter],s.air.capacity[gas],difference,heat);
    };
    auto face = [&](int a, int b) {
        gas_contact(a, b); gas_contact(b, a);
        if (s.capacity[a] < 1e-12 || s.capacity[b] < 1e-12) return;
        const double difference = s.temperature[b] - s.temperature[a];
        if (std::abs(difference) < 1e-9) return;
        const double ka = conductivity(s, a), kb = conductivity(s, b);
        const double conductance = ka > 0 && kb > 0 ? 1 / (.5/ka+.5/kb+s.config.contact_resistance) : 0;
        const double limit = .24 * std::min(s.capacity[a], s.capacity[b]) * std::abs(difference);
        const double heat = std::clamp(conductance * difference * dt, -limit, limit);
        exchange(a,b,s.capacity[a],s.capacity[b],difference,heat);
        s.thermal_awake[(a / grid_width / 16) * 20 + a % grid_width / 16] = true;
        s.thermal_awake[(b / grid_width / 16) * 20 + b % grid_width / 16] = true;
    };
    for (int y = 0; y < grid_height; ++y) for (int x = 0; x < grid_width; ++x) {
        const int i = y * grid_width + x;
        if (x + 1 < grid_width) face(i, i + 1);
        if (y + 1 < grid_height) face(i, i + grid_width);
    }
    for (int i = 0; i < grid_cells; ++i) {
        if (s.capacity[i] <= 0 || s.air.capacity[i] <= 0) continue;
        const double difference = s.air.temperature[i] - s.temperature[i];
        const double cap = .24 * std::min(s.capacity[i], s.air.capacity[i]) * std::abs(difference);
        const double heat = std::clamp(s.config.heat_contact * difference * dt, -cap, cap);
        exchange(i,grid_cells+i,s.capacity[i],s.air.capacity[i],difference,heat);
    }
    // A per-face cap alone is insufficient: up to nine simultaneous contacts
    // may drain the same node. Bound the SUM of coefficients so each new
    // temperature is a convex combination of old temperatures. Apply the same
    // factor at both ends to retain exact pairwise energy conservation.
    for(const auto& flux:fluxes) {
        const double scale=1/std::max({1.0,load[flux.a],load[flux.b]});
        const double heat=flux.heat*scale;
        delta[flux.a]+=heat;delta[flux.b]-=heat;
    }
    s.executor.run(grid_cells,4096,[&](int begin,int end) {
        for(int i=begin;i<end;++i) {
            if(delta[i]!=0) add_matter_heat(s,i,delta[i]);
            s.air.energy[i]+=delta[grid_cells+i];
        }
    });
    phase_changes(s);
}
void convert_solid(State& s, int i, Material material) {
    s.material[i] = material; s.fuel[i] = 0; wake(s, i);
}
void update_report(State& s) {
    auto& report = s.report;
    report.worker_threads=s.executor.lanes();
    report.mass = s.cached_mass; report.energy = s.cached_energy;
    report.mass_error = report.mass - (s.budget.initial_mass + s.budget.edited_mass + s.budget.incoming_mass + s.budget.injected_mass - s.budget.escaped_mass);
    report.energy_error = report.energy - (s.budget.initial_energy + s.budget.edited_energy + s.budget.injected_heat
        + s.budget.chemical_heat + s.budget.incoming_energy + s.budget.source_energy - s.budget.escaped_energy - s.budget.radiation);
    report.escaped_mass = s.budget.escaped_mass; report.escaped_energy = s.budget.escaped_energy;
    report.radiation = s.budget.radiation; report.pending_explosions = s.explosions.size();
    report.active_chunks = static_cast<int>(std::ranges::count(s.next_awake, true));
    report.thermal_chunks = static_cast<int>(std::ranges::count(s.thermal_awake, true));
    report.reactive_chunks = static_cast<int>(std::ranges::count(s.reaction_awake, true));
    report.particles = s.cached_particles;
    report.finite = s.cached_finite && std::isfinite(report.mass) && std::isfinite(report.energy);
}
} // namespace strefa::sim::detail

