#include "sim/world.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <chrono>
#include <stdexcept>

namespace strefa::sim {
using namespace detail;
World::World(std::uint32_t seed, PhysicsConfig config) {
    auto between=[](double value,double low,double high) {return std::isfinite(value)&&value>=low&&value<=high;};
    if(!between(config.cfl,.01,1)||!between(config.liquid_cfl,.01,1)
        ||!between(config.gravity,-10000,10000)||!between(config.sound_speed,.01,1000)
        ||!between(config.air_density,.001,1000)||!between(config.pressure_tolerance,1e-12,.1)
        ||!between(config.heat_contact,0,10000)||!between(config.contact_resistance,0,10000)
        ||!between(config.pressure_damping,0,10000)||!between(config.velocity_damping,0,10000)
        ||config.pressure_iterations<1||config.pressure_iterations>4096||config.worker_threads>4)
        throw std::invalid_argument{"Invalid simulation stability/solver configuration"};
    state_.allocate(); state_.seed = seed; state_.config = config;
    state_.executor.configure(config.worker_threads); clear();
}
void World::clear() {
    auto& s = state_;
    std::ranges::fill(s.material, Material::empty); std::ranges::fill(s.shade, std::uint8_t{0});
    std::ranges::fill(s.loose, std::uint8_t{0}); std::ranges::fill(s.burning, std::uint8_t{0});
    std::ranges::fill(s.updated, std::uint64_t{0});
    for (auto* a : {&s.mass, &s.energy, &s.fuel, &s.acid_capacity, &s.dissolved}) std::ranges::fill(*a, 0.0);
    for (auto* a : {&s.vx, &s.vy, &s.offset_x, &s.offset_y, &s.liquid.u, &s.liquid.v,
        &s.air.u, &s.air.v, &s.air.pressure, &s.shock}) std::ranges::fill(*a, 0.F);
    for (auto& a : s.liquid.volume) std::ranges::fill(a, 0.0);
    for (auto& a : s.liquid.energy) std::ranges::fill(a, 0.0);
    std::ranges::fill(s.liquid.pressure, 0.0);
    for (auto& a : s.air.mass) std::ranges::fill(a, 0.0);
    std::ranges::fill(s.air.mass[gi(Gas::inert)], .015);
    std::ranges::fill(s.air.mass[gi(Gas::oxygen)], .005);
    std::ranges::fill(s.air.energy, .02 * ambient_temperature);
    std::ranges::fill(s.air.pressure_source, 0.0);
    s.report = {}; s.budget = {}; s.next_event = 1;
    s.budget.initial_mass = total_mass(s); s.budget.initial_energy = total_energy(s);
    s.events.clear(); s.explosions.clear();
    s.movement_awake.fill(false); s.next_awake.fill(false); s.thermal_awake.fill(false); s.reaction_awake.fill(false);
    refresh_thermal(s); update_report(s);
}
void World::erase_cell(int i) {
    auto& s = state_;
    s.material[i] = Material::empty; s.mass[i] = s.energy[i] = s.fuel[i] = 0;
    s.loose[i] = s.burning[i] = 0;
    s.acid_capacity[i] = s.dissolved[i] = 0;
    s.vx[i] = s.vy[i] = s.offset_x[i] = s.offset_y[i] = 0;
    for (auto& a : s.liquid.volume) a[i] = 0;
    for (auto& a : s.liquid.energy) a[i] = 0;
    for (auto& a : s.air.mass) a[i] = 0;
    s.air.mass[gi(Gas::inert)][i] = .015; s.air.mass[gi(Gas::oxygen)][i] = .005;
    s.air.energy[i] = .02 * ambient_temperature;
    s.updated[i] = s.report.tick; wake(s, i);
}
bool World::set(Point p, Material material) { return set(p, material, properties(material).spawn_temperature); }
bool World::set(Point p, Material material, double kelvin, double fill) {
    if (!contains(p) || !valid(material) || !std::isfinite(kelvin) || !std::isfinite(fill)
        || kelvin < 1 || kelvin > 10000 || fill < 0 || fill > 1) return false;
    auto& s = state_; const int i = cell_index(p);
    const double old_mass = matter_mass(s, i) + gas_mass(s, i), old_energy = matter_energy(s, i) + s.air.energy[i];
    erase_cell(i);
    const auto& props = properties(material);
    if (const int channel = liquid_index(material); channel >= 0) {
        s.liquid.volume[channel][i] = fill;
        s.liquid.energy[channel][i] = fill * props.density * specific_enthalpy(material, kelvin);
        if (material == Material::acid) s.acid_capacity[i] = fill * .8;
        if (fill > .99) { for (auto& a : s.air.mass) a[i] = 0;
        s.air.energy[i] = 0; }
    } else if (definition(material).phase == Phase::solid || definition(material).phase == Phase::powder) {
        s.material[i] = material; s.mass[i] = props.density * fill;
        s.energy[i] = s.mass[i] * specific_enthalpy(material, kelvin);
        s.fuel[i] = s.mass[i] * (1 - props.residue);
        if (props.ignition == 0) s.fuel[i] = 0;
        for (auto& a : s.air.mass) a[i] = 0;
        s.air.energy[i] = 0;
    } else if (material != Material::empty) {
        const auto gas = material == Material::steam ? Gas::steam : material == Material::smoke ? Gas::smoke
            : material == Material::gas ? Gas::fuel : Gas::products;
        const double mass = props.density * fill;
        s.air.mass[gi(gas)][i] += mass;
        s.air.energy[i] += mass * (gas_cp(gas) * kelvin + gas_offset(gas));
    }
    s.shade[i] = static_cast<std::uint8_t>(random(s, i, 91) & 15U);
    s.budget.edited_mass += matter_mass(s, i) + gas_mass(s, i) - old_mass;
    s.budget.edited_energy += matter_energy(s, i) + s.air.energy[i] - old_energy;
    // Keep authoring O(1); full field caches are refreshed at the next step/render/sample.
    s.temperature[i] = kelvin; s.air.temperature[i] = kelvin;
    s.thermal_dirty=true;
    return true;
}
Cell World::at(Point p) const noexcept {
    if (!contains(p)) return {Material::metal, 0};
    const int i = cell_index(p); return {dominant(state_, i), state_.shade[i]};
}
FluidSample World::sample_fluid(Point p)const noexcept {
    FluidSample result;if(!contains(p)){result.material=Material::metal;return result;}
    const auto& s=state_;const int i=cell_index(p);result.material=dominant(s,i);
    result.water=static_cast<float>(s.liquid.volume[liquid_index(Material::water)][i]);result.oil=static_cast<float>(s.liquid.volume[liquid_index(Material::oil)][i]);
    result.velocity={(s.liquid.u[u_index(p.x,p.y)]+s.liquid.u[u_index(p.x+1,p.y)])*.5F,(s.liquid.v[v_index(p.x,p.y)]+s.liquid.v[v_index(p.x,p.y+1)])*.5F};return result;
}
PhysicalSample World::sample(Point p) const noexcept {
    PhysicalSample sample;
    if (!contains(p)) { sample.material = Material::metal; return sample; }
    const auto& s = state_; const int i = cell_index(p);
    sample.material = dominant(s, i); sample.mass = matter_mass(s, i) + gas_mass(s, i);
    double capacity = s.mass[i] * properties(s.material[i]).heat_capacity + s.dissolved[i];
    double offset = s.mass[i] * enthalpy_offset(s.material[i]);
    for (std::size_t c = 0; c < liquid_materials.size(); ++c) {
        const double m = s.liquid.volume[c][i] * properties(liquid_materials[c]).density;
        capacity += m * properties(liquid_materials[c]).heat_capacity; offset += m * enthalpy_offset(liquid_materials[c]);
    }
    double air_capacity = 0, air_offset = 0;
    for (unsigned c = 0; c < gas_count; ++c) {
        air_capacity += s.air.mass[c][i] * gas_cp(static_cast<Gas>(c));
        air_offset += s.air.mass[c][i] * gas_offset(static_cast<Gas>(c));
    }
    sample.air_temperature = air_capacity > 1e-12 ? (s.air.energy[i] - air_offset) / air_capacity : ambient_temperature;
    sample.temperature = capacity > 1e-8 ? std::max(1.0,(matter_energy(s, i) - offset) / capacity) : sample.air_temperature;
    sample.pressure = s.air.pressure[i]; sample.liquid_pressure = s.liquid.pressure[i];
    sample.liquid_fill = liquid_fill(s, i); sample.fuel = s.fuel[i]; sample.oxygen = s.air.mass[gi(Gas::oxygen)][i];
    sample.dissolved = s.dissolved[i];
    sample.air_velocity = {(s.air.u[u_index(p.x, p.y)] + s.air.u[u_index(p.x + 1, p.y)]) * .5F,
        (s.air.v[v_index(p.x, p.y)] + s.air.v[v_index(p.x, p.y + 1)]) * .5F};
    sample.liquid_velocity = {(s.liquid.u[u_index(p.x, p.y)] + s.liquid.u[u_index(p.x + 1, p.y)]) * .5F,
        (s.liquid.v[v_index(p.x, p.y)] + s.liquid.v[v_index(p.x, p.y + 1)]) * .5F};
    for (std::size_t c = 0; c < liquid_materials.size(); ++c) sample.liquid_fractions[c] = s.liquid.volume[c][i];
    return sample;
}
double World::inject_heat(Point p, double energy) {
    if (!contains(p) || !std::isfinite(energy)) return 0;
    auto& s = state_; const int i = cell_index(p);
    refresh_thermal(s);
    const bool matter = s.capacity[i] > 0;
    double minimum=s.mass[i]*specific_enthalpy(s.material[i],1)+s.dissolved[i];
    for(std::size_t c=0;c<liquid_materials.size();++c) {
        const auto liquid=liquid_materials[c];
        const auto cold=liquid==Material::water?Material::ice:liquid==Material::molten_metal?Material::metal:
            liquid==Material::lava?Material::stone:liquid;
        minimum+=s.liquid.volume[c][i]*properties(liquid).density*specific_enthalpy(cold,1);
    }
    const double available = matter ? matter_energy(s, i)-minimum : s.air.capacity[i] * std::max(0.0, s.air.temperature[i] - 1);
    const double applied = std::max(energy, -std::max(0.0, available));
    if (matter) add_matter_heat(s, i, applied); else s.air.energy[i] += applied;
    s.budget.injected_heat += applied; wake(s, i);
    phase_changes(s); update_report(s);
    return applied;
}
void World::set_temperature(Point p, double kelvin) {
    if (!contains(p) || !std::isfinite(kelvin) || kelvin < 1 || kelvin > 10000) return;
    refresh_thermal(state_); const int i = cell_index(p);
    const double capacity = state_.capacity[i] > 0 ? state_.capacity[i] : state_.air.capacity[i];
    const double temperature = state_.capacity[i] > 0 ? state_.temperature[i] : state_.air.temperature[i];
    inject_heat(p, capacity * (kelvin - temperature));
}
void World::set_oxygen(double fraction) {
    if (!std::isfinite(fraction)) return;
    fraction = std::clamp(fraction, 0.0, 1.0); auto& s = state_;
    for (int i = 0; i < grid_cells; ++i) {
        const double old = s.air.mass[gi(Gas::oxygen)][i];
        const double next = barrier(s, i) || liquid_fill(s, i) > .99 ? 0 : .02 * fraction;
        const double delta = next - old;
        s.air.mass[gi(Gas::oxygen)][i] = next; s.air.energy[i] += delta * ambient_temperature;
        s.budget.edited_mass += delta; s.budget.edited_energy += delta * ambient_temperature;
    }
    refresh_thermal(s); update_report(s);
}
void World::rectangle(Point a, Point b, Material material) {
    for (int y = a.y; y <= b.y; ++y) for (int x = a.x; x <= b.x; ++x) set({x, y}, material);
}
void World::paint(Point from, Point to, int radius, Material material, bool erase) {
    if (!contains(from) || !contains(to) || !valid(material)) return;
    radius = std::clamp(radius, 1, 16); refresh_thermal(state_);
    const int dx = std::abs(to.x - from.x), sx = from.x < to.x ? 1 : -1;
    const int dy = -std::abs(to.y - from.y), sy = from.y < to.y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
            const Point p{from.x + x, from.y + y};
            if (x * x + y * y > radius * radius || !contains(p)) continue;
            if (erase || material == Material::empty) set(p, Material::empty);
            else if (at(p).material == Material::empty || material == Material::fire) {
                if (material == Material::fire && matter_mass(state_, cell_index(p)) > 0) {
                    const int i = cell_index(p);
                    const double capacity = state_.capacity[i];
                    const double amount = capacity * std::max(0.0, 1050 - sample(p).temperature);
                    add_matter_heat(state_, i, amount); state_.budget.injected_heat += amount; wake(state_, i);
                } else set(p, material);
            }
        }
        if (from.x == to.x && from.y == to.y) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; from.x += sx; }
        if (twice <= dx) { error += dx; from.y += sy; }
    }
    refresh_thermal(state_); update_report(state_);
}
void World::projectile_hit(Point p) {
    if (!contains(p)) return;
    const auto m = at(p).material;
    if (m == Material::gunpowder || m == Material::nitroglycerin || m == Material::thermite) {
        set_temperature(p, properties(m).ignition + 150); return;
    }
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        const Point q{p.x + x, p.y + y}; if (!contains(q) || x * x + y * y > 1) continue;
        const auto target = at(q).material;
        if (target == Material::wood || target == Material::sand || target == Material::coal || target == Material::ash) {
            // A physical chip keeps the original material and heat, instead of deleting mass.
            const int i = cell_index(q);
            state_.loose[i] = 1;
            state_.vx[i] += static_cast<float>(x * 15); state_.vy[i] -= 24;
            wake(state_, i);
        }
    }
}
std::uint64_t World::queue_explosion(Point p, int radius) {
    ExplosionCommand command; command.center = p; command.radius = radius; return queue_explosion(command);
}
std::uint64_t World::queue_explosion(ExplosionCommand command) {
    auto& s = state_;
    if (!contains(command.center) || !std::isfinite(command.energy) || !std::isfinite(command.pressure)
        || !std::isfinite(command.impulse) || !std::isfinite(command.gas_mass) || command.gas_mass<0
        || command.energy < 0 || command.energy>1e8 || std::abs(command.pressure)>1e6
        || std::abs(command.impulse)>1e6 || command.gas_mass>1e4 || s.explosions.size() >= grid_cells) return 0;
    command.radius = std::clamp(command.radius, 1, 24);
    if (command.id == 0) command.id = s.next_event++;
    s.explosions.push_back(command); return command.id;
}
void World::step(ScanMode mode) {
    auto& s = state_;
    auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { const auto now=std::chrono::steady_clock::now();
        const double ms=std::chrono::duration<double,std::milli>(now-start).count(); start=now; return ms; };
    if (!s.report.finite) return;
    ++s.report.tick; s.events.clear(); s.report.moves = s.report.visited_cells = 0;
    s.report.pressure_iterations = 0; s.report.pressure_residual = s.report.max_divergence = s.report.max_overfill = 0;
    s.report.pressure_converged = true;
    s.report.air_substeps=s.report.liquid_substeps=0;
    s.report.pressure_ms=s.report.transport_ms=0;
    s.report.air_wave_ms=s.report.air_transport_ms=s.report.thermal_refresh_ms=0;
    s.movement_awake = s.next_awake; s.next_awake.fill(false);
    if(s.thermal_dirty) refresh_thermal(s);
    s.report.bookkeeping_ms = elapsed();
    s.report.heat_ms = 0;
    const bool chemistry=s.config.reactions&&s.chemistry_awake;
    if (chemistry) react_materials(s, tick_duration);
    else s.reaction_awake.fill(false);
    std::ranges::fill(s.shock,0.F);
    const bool exploded=!s.explosions.empty();
    for (int n = 0; n < 16 && !s.explosions.empty(); ++n) {
        const auto command = s.explosions.front(); s.explosions.erase(s.explosions.begin()); detonate(s, command);
    }
    s.report.reaction_ms = elapsed();
    const bool air=s.config.air&&s.air_awake;
    if (air) solve_air(s, tick_duration);
    s.report.air_ms=elapsed();
    if (s.config.motion) move_powders(s, tick_duration, mode == ScanMode::full_grid);
    s.report.motion_ms=elapsed();
    if (s.config.fluids) solve_liquids(s, tick_duration);
    s.report.liquid_ms=elapsed();
    const bool conducted=s.config.thermal&&s.heat_awake;
    if (conducted) conduct_heat(s, tick_duration);
    else s.thermal_awake.fill(false);
    s.report.heat_ms += elapsed();
    // conduct_heat finishes with phase_changes and a complete cache/budget
    // refresh. Repeating that full-grid pass here used several milliseconds.
    if(!conducted&&(chemistry||exploded||air||s.report.moves>0||s.report.liquid_substeps>0)) refresh_thermal(s);
    update_report(s);
    s.report.bookkeeping_ms += elapsed();
}
std::size_t World::count(Material material) const noexcept {
    if (!valid(material)) return 0;
    std::size_t count = 0;
    for (int i = 0; i < grid_cells; ++i) if (dominant(state_, i) == material) ++count;
    return count;
}
double World::mass_of(Material material) const noexcept {
    const auto& s = state_; double mass = 0;
    const int c = liquid_index(material);
    for (int i = 0; i < grid_cells; ++i) {
        if (c >= 0) mass += s.liquid.volume[c][i] * properties(material).density;
        else if (material == Material::steam) mass += s.air.mass[gi(Gas::steam)][i];
        else if (material == Material::smoke) mass += s.air.mass[gi(Gas::smoke)][i];
        else if (material == Material::gas) mass += s.air.mass[gi(Gas::fuel)][i];
        else if (s.material[i] == material) mass += s.mass[i];
    }
    return mass;
}
bool World::chunk_awake(int index) const noexcept {
    return index >= 0 && index < chunk_count && (state_.movement_awake[index] || state_.next_awake[index]
        || state_.thermal_awake[index] || state_.reaction_awake[index]);
}
std::uint64_t World::checksum() const noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    auto add = [&](std::uint64_t value) { hash ^= value; hash *= 1099511628211ULL; };
    auto values = [&](const auto& a) {
        for (const auto value : a) {
            if constexpr (sizeof(value) == 8) add(std::bit_cast<std::uint64_t>(value));
            else add(std::bit_cast<std::uint32_t>(value));
        }
    };
    add(state_.report.tick);
    add(state_.next_event);
    for (auto m : state_.material) add(static_cast<unsigned>(m));
    for (const auto* a : {&state_.loose,&state_.burning,&state_.shade}) for(auto v:*a) add(v);
    values(state_.liquid.pressure); values(state_.air.pressure_source);
    for(const auto& e:state_.explosions) { add(e.id);add(e.center.x);add(e.center.y);add(e.radius);
        add(std::bit_cast<std::uint64_t>(e.energy));add(std::bit_cast<std::uint64_t>(e.pressure));add(std::bit_cast<std::uint64_t>(e.impulse)); }
    for (const auto* a : {&state_.mass, &state_.energy, &state_.fuel, &state_.acid_capacity, &state_.dissolved, &state_.air.energy}) values(*a);
    for (const auto& a : state_.liquid.volume) values(a);
    for (const auto& a : state_.liquid.energy) values(a);
    for (const auto& a : state_.air.mass) values(a);
    for (const auto* a : {&state_.vx, &state_.vy, &state_.offset_x, &state_.offset_y, &state_.air.u, &state_.air.v,
        &state_.air.pressure, &state_.liquid.u, &state_.liquid.v, &state_.shock}) values(*a);
    return hash;
}
void World::render(std::span<Color, cell_count> pixels, bool transparent, RenderMode mode) const {
    const auto& s = state_;
    for (int i = 0; i < grid_cells; ++i) {
        const Material m = dominant(s, i); Color color = definition(m).color;
        const double temp = s.capacity[i] > 0 ? s.temperature[i] : s.air.temperature[i];
        if (mode == RenderMode::temperature) {
            const double t = std::clamp(std::log(std::max(1.0, temp) / 200) / std::log(20.0), 0.0, 1.0);
            color = {static_cast<std::uint8_t>(255 * t), static_cast<std::uint8_t>(220 * t * t), static_cast<std::uint8_t>(180 * (1 - t))};
        } else if (mode == RenderMode::pressure) {
            const double raw=liquid_fill(s,i)>.01?s.liquid.pressure[i]/1000:s.air.pressure[i];
            const double p = std::clamp(raw * .04, -1.0, 1.0);
            color = {static_cast<std::uint8_t>(25 + 230 * std::max(0.0, p)), 35, static_cast<std::uint8_t>(35 + 220 * std::max(0.0, -p))};
        } else if (mode == RenderMode::velocity) {
            const int x = i % width, y = i / width;
            const bool liquid=liquid_fill(s,i)>.01;
            const double speed = liquid?std::hypot(s.liquid.u[u_index(x,y)],s.liquid.v[v_index(x,y)])/40:
                std::hypot(s.air.u[u_index(x, y)], s.air.v[v_index(x, y)]) / 40;
            color = {20, static_cast<std::uint8_t>(40 + 215 * std::min(1.0, speed)), 140};
        } else if (mode == RenderMode::oxygen) {
            color = {25, static_cast<std::uint8_t>(255 * std::clamp(s.air.mass[gi(Gas::oxygen)][i] / .005, 0.0, 1.0)), 40};
        } else if (mode == RenderMode::activity) {
            const int chunk = (i / width / 16) * 20 + i % width / 16;
            color = {static_cast<std::uint8_t>(s.thermal_awake[chunk] ? 230 : 20),
                static_cast<std::uint8_t>(s.next_awake[chunk] ? 210 : 25), static_cast<std::uint8_t>(s.reaction_awake[chunk] ? 230 : 30)};
        } else {
            if (m != Material::empty) {
                const int noise = static_cast<int>(s.shade[i]) - 7;
                auto shade = [=](int v) { return static_cast<std::uint8_t>(std::clamp(v + noise * 2, 0, 255)); };
                color = {shade(color.r), shade(color.g), shade(color.b)};
            }
            if(liquid_index(m)>=0) {
                color.a=static_cast<std::uint8_t>(std::clamp(liquid_fill(s,i)*255,24.0,255.0));
                const int x=i%width,y=i/width;
                const double speed=std::hypot(s.liquid.u[u_index(x,y)],s.liquid.v[v_index(x,y)]);
                color.b=static_cast<std::uint8_t>(std::min(255.0,color.b+std::min(24.0,speed)));
            }
            if(s.burning[i]) {
                color.r=static_cast<std::uint8_t>(std::max(190,static_cast<int>(color.r)));
                color.g=static_cast<std::uint8_t>(90+(random(s,i,103)%70)); color.b=40;
            }
            const bool gas = definition(m).phase == Phase::gas || m == Material::fire;
            if (gas) {
                const double opacity = 1 - std::exp(-12 * (s.air.mass[gi(Gas::steam)][i]
                    + s.air.mass[gi(Gas::smoke)][i] + s.air.mass[gi(Gas::fuel)][i] + s.air.mass[gi(Gas::products)][i]));
                color.a = static_cast<std::uint8_t>(255 * std::clamp(opacity, .08, .9));
            }
            if (temp > 700 && m != Material::empty) {
                const double glow = std::clamp((temp - 700) / 1600, 0.0, 1.0);
                color.r = static_cast<std::uint8_t>(color.r + (255 - color.r) * glow);
                color.g = static_cast<std::uint8_t>(color.g + (215 - color.g) * glow);
                color.b = static_cast<std::uint8_t>(color.b + (90 - color.b) * glow);
                if (m == Material::fire) color = {255, static_cast<std::uint8_t>(90 + 150 * glow), 35, color.a};
            }
            if (m == Material::empty) color.a = transparent ? 0 : 255;
            else if (!transparent && color.a < 255) {
                const auto bg = definition(Material::empty).color; const float a = color.a / 255.F;
                color = {static_cast<std::uint8_t>(color.r * a + bg.r * (1-a)),
                    static_cast<std::uint8_t>(color.g * a + bg.g * (1-a)), static_cast<std::uint8_t>(color.b * a + bg.b * (1-a))};
            }
        }
        pixels[i] = color;
    }
}
} // namespace strefa::sim


