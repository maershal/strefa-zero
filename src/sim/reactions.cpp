#include "sim/physics.hpp"

#include <algorithm>
#include <cmath>

namespace strefa::sim::detail {
namespace {
constexpr std::array<Point, 5> neighbors{{{0, 0}, {0, -1}, {-1, 0}, {1, 0}, {0, 1}}};
int receiver(const State& s, int i) {
    const Point p{i % grid_width, i / grid_width};
    for (const auto d : neighbors) {
        const Point q{p.x + d.x, p.y + d.y};
        if (contains(q) && !barrier(s, cell_index(q)) && liquid_fill(s, cell_index(q)) < .9) return cell_index(q);
    }
    return i; // Retain trapped products rather than deleting their mass or heat.
}
struct Oxygen {
    double mass{}, energy{};
};
Oxygen oxygen(State& s, int i, double wanted) {
    Oxygen result;
    const Point p{i % grid_width, i / grid_width};
    for (const auto d : neighbors) {
        const Point q{p.x + d.x, p.y + d.y};
        if (!contains(q)) continue;
        const int j = cell_index(q);
        const double amount = std::min(wanted - result.mass, s.air.mass[gi(Gas::oxygen)][j]);
        if (amount <= 0) continue;
        const double energy = amount * s.air.temperature[j];
        s.air.mass[gi(Gas::oxygen)][j] -= amount;
        s.air.energy[j] -= energy;
        result.mass += amount;
        result.energy += energy;
    }
    return result;
}
void radiate_gas(State& s, int i, double maximum) {
    double capacity = 0, offset = 0;
    for (unsigned c = 0; c < gas_count; ++c) {
        capacity += s.air.mass[c][i] * gas_cp(static_cast<Gas>(c));
        offset += s.air.mass[c][i] * gas_offset(static_cast<Gas>(c));
    }
    const double radiated = std::max(0.0, s.air.energy[i] - offset - capacity * maximum);
    s.air.energy[i] -= radiated;
    s.budget.radiation += radiated;
}
void explode_material(State& s, int i, Material material, double mass, double energy) {
    const int c = liquid_index(material);
    if (c >= 0) {
        s.liquid.volume[c][i] = s.liquid.energy[c][i] = 0;
    } else {
        s.material[i] = Material::empty;
        s.mass[i] = s.energy[i] = s.fuel[i] = 0;
    }
    const int target = receiver(s, i);
    s.air.mass[gi(Gas::products)][target] += mass;
    s.air.energy[target] += energy;
    ExplosionCommand command;
    command.center = {i % grid_width, i / grid_width};
    command.radius = material == Material::nitroglycerin ? 12 : 9;
    command.energy = mass * properties(material).heat_release;
    command.pressure = 18 + mass * 5;
    command.impulse = 35;
    command.chemical = true;
    command.id = s.next_event++;
    command.gas_mass = 0;
    s.explosions.push_back(command);
    wake(s, i);
}
} // namespace
std::uint32_t random(const State& s, int i, unsigned salt) noexcept {
    auto mix = [](std::uint32_t v) {
        v ^= v >> 16;
        v *= 0x7feb352dU;
        v ^= v >> 15;
        v *= 0x846ca68bU;
        return v ^ (v >> 16);
    };
    return mix(s.seed ^ mix(static_cast<std::uint32_t>(i)) ^ mix(salt)
               ^ mix(static_cast<std::uint32_t>(s.report.tick)));
}
double visibility(const State& s, Point from, Point to) noexcept {
    const int dx = std::abs(to.x - from.x), sx = from.x < to.x ? 1 : -1;
    const int dy = -std::abs(to.y - from.y), sy = from.y < to.y ? 1 : -1;
    int error = dx + dy;
    double result = 1;
    bool first = true;
    for (;;) {
        if (from.x == to.x && from.y == to.y) return result;
        if (!contains(from)) return 0;
        if (!first) {
            const int i = cell_index(from);
            if (barrier(s, i)) {
                const auto m = s.material[i];
                result *= m == Material::metal ? .035 : m == Material::stone ? .24 : m == Material::wood ? .7 : .9;
            }
        }
        first = false;
        if (result < 1e-6) return 0;
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            from.x += sx;
        }
        if (twice <= dx) {
            error += dx;
            from.y += sy;
        }
    }
}
void detonate(State& s, const ExplosionCommand& blast) {
    struct Hit {
        int index;
        double weight, transmission;
        int dx, dy;
    };
    std::vector<Hit> hits;
    double sum = 0;
    for (int y = -blast.radius; y <= blast.radius; ++y)
        for (int x = -blast.radius; x <= blast.radius; ++x) {
            const Point p{blast.center.x + x, blast.center.y + y};
            const double distance = std::hypot(x, y);
            if (!contains(p) || distance > blast.radius) continue;
            const double transmission = visibility(s, blast.center, p);
            const double weight = std::max(0.0, 1 - distance / (blast.radius + 1)) * transmission;
            hits.push_back({cell_index(p), weight, transmission, x, y});
            sum += weight;
        }
    if (sum <= 0) return;
    refresh_thermal(s);
    if (blast.gas_mass > 0) {
        const int target = receiver(s, cell_index(blast.center));
        s.air.mass[gi(Gas::products)][target] += blast.gas_mass;
        const double energy = blast.gas_mass * ambient_temperature;
        s.air.energy[target] += energy;
        s.budget.injected_mass += blast.gas_mass;
        s.budget.source_energy += energy;
    }
    for (const auto& hit : hits) {
        const int i = hit.index;
        const double heat = blast.energy * hit.weight / sum;
        if (s.capacity[i] > 0)
            add_matter_heat(s, i, heat);
        else
            s.air.energy[i] += heat;
        s.air.pressure_source[i] += blast.pressure * hit.weight;
        s.shock[i] = std::max(s.shock[i], static_cast<float>(std::abs(blast.pressure) * hit.weight));
        const double distance = std::max(1.0, std::hypot(hit.dx, hit.dy));
        const double force = blast.impulse * hit.weight;
        if (barrier(s, i)) {
            if (force > properties(s.material[i]).strength) {
                s.loose[i] = 1;
                s.vx[i] += static_cast<float>(force * hit.dx / distance);
                s.vy[i] += static_cast<float>(force * hit.dy / distance - force * .35);
            }
        }
        const int x = i % grid_width, y = i / grid_width;
        const float vx = static_cast<float>(force * hit.dx / distance),
                    vy = static_cast<float>(force * hit.dy / distance);
        s.liquid.u[u_index(x, y)] += vx;
        s.liquid.u[u_index(x + 1, y)] += vx;
        s.liquid.v[v_index(x, y)] += vy;
        s.liquid.v[v_index(x, y + 1)] += vy;
        s.air.u[u_index(x, y)] += vx * .25F;
        s.air.u[u_index(x + 1, y)] += vx * .25F;
        s.air.v[v_index(x, y)] += vy * .25F;
        s.air.v[v_index(x, y + 1)] += vy * .25F;
        wake(s, i);
    }
    if (blast.chemical)
        s.budget.chemical_heat += blast.energy;
    else
        s.budget.injected_heat += blast.energy;
    s.air_awake = true;
    s.heat_awake = true;
    s.events.push_back(
            {blast.id, s.report.tick, PhysicsEventKind::explosion, blast.center, static_cast<float>(blast.pressure)});
}
void react_materials(State& s, double dt) {
    s.reaction_awake.fill(false);
    for (int i = 0; i < grid_cells; ++i) {
        const auto material = dominant(s, i);
        const auto& props = properties(material);
        const double temperature = s.capacity[i] > 0 ? s.temperature[i] : s.air.temperature[i];
        const int liquid = liquid_index(material);
        if ((material == Material::gunpowder || material == Material::nitroglycerin)
            && (temperature >= props.ignition || s.shock[i] > 10
                || (material == Material::nitroglycerin && std::abs(s.air.pressure[i]) > 16))) {
            const double mass = liquid >= 0 ? s.liquid.volume[liquid][i] * props.density : s.mass[i];
            const double energy = liquid >= 0 ? s.liquid.energy[liquid][i] : s.energy[i];
            explode_material(s, i, material, mass, energy);
            continue;
        }
        bool active = false;
        if (props.burn_rate > 0 && temperature >= props.ignition) {
            double available = liquid >= 0 ? s.liquid.volume[liquid][i] * props.density : s.fuel[i];
            const double wanted = std::min(available, props.burn_rate * dt * std::max(.2, available));
            const bool thermite = material == Material::thermite;
            const auto oxidizer = thermite ? Oxygen{} : oxygen(s, i, wanted * .7);
            const double burned = thermite ? wanted : std::min(wanted, oxidizer.mass / .7);
            if (burned > 1e-12) {
                active = true;
                const int target = receiver(s, i);
                const double released = burned * props.heat_release;
                double sensible = 0;
                if (thermite) {
                    s.fuel[i] -= burned;
                    s.energy[i] += released * .8;
                    s.air.energy[target] += released * .2;
                    if (s.fuel[i] <= 1e-9) convert_solid(s, i, Material::metal);
                } else {
                    const double mass = liquid >= 0 ? s.liquid.volume[liquid][i] * props.density : s.mass[i];
                    const double energy = liquid >= 0 ? s.liquid.energy[liquid][i] : s.energy[i];
                    sensible = mass > 0 ? energy * burned / mass : 0;
                    if (liquid >= 0) {
                        s.liquid.volume[liquid][i] -= burned / props.density;
                        s.liquid.energy[liquid][i] -= sensible;
                    } else {
                        s.mass[i] -= burned;
                        s.energy[i] -= sensible;
                        s.fuel[i] -= burned;
                        if (s.fuel[i] <= 1e-9) convert_solid(s, i, Material::ash);
                    }
                    s.air.mass[gi(Gas::smoke)][target] += burned * .15;
                    s.air.mass[gi(Gas::products)][target] += burned * .85 + oxidizer.mass;
                    s.air.energy[target] += sensible + oxidizer.energy + released;
                }
                s.budget.chemical_heat += released;
                s.air.pressure_source[target] += burned * 5;
                radiate_gas(s, target, thermite ? 3200 : 2200);
                if (!s.burning[i])
                    s.events.push_back({s.next_event++,
                                        s.report.tick,
                                        PhysicsEventKind::ignition,
                                        {i % grid_width, i / grid_width},
                                        1});
                wake(s, i);
            }
        }
        s.burning[i] = static_cast<std::uint8_t>(active);
        if (active) s.reaction_awake[(i / grid_width / 16) * 20 + i % grid_width / 16] = true;
        // Gas fuel is a separate transported species, not a cell with a TTL.
        if (s.air.mass[gi(Gas::fuel)][i] > 0 && s.air.temperature[i] > properties(Material::gas).ignition) {
            const double wanted = -std::expm1(-dt * .6) * s.air.mass[gi(Gas::fuel)][i];
            const auto oxidizer = oxygen(s, i, wanted * .7);
            const double amount = std::min(wanted, oxidizer.mass / .7);
            s.air.mass[gi(Gas::fuel)][i] -= amount;
            s.air.mass[gi(Gas::products)][i] += amount + oxidizer.mass;
            s.air.energy[i] += oxidizer.energy + amount * properties(Material::gas).heat_release;
            s.budget.chemical_heat += amount * properties(Material::gas).heat_release;
            radiate_gas(s, i, 2200);
        }
        const int acid = liquid_index(Material::acid);
        if (s.liquid.volume[acid][i] > 1e-8 && s.acid_capacity[i] > 1e-9) {
            const Point p{i % grid_width, i / grid_width};
            for (const auto d : neighbors) {
                const Point q{p.x + d.x, p.y + d.y};
                if (!contains(q)) continue;
                const int j = cell_index(q);
                const auto target = s.material[j];
                if (target != Material::stone && target != Material::sand && target != Material::wood
                    && target != Material::coal)
                    continue;
                const double amount = std::min({s.mass[j], s.acid_capacity[i] / .4, .8 * dt});
                if (amount <= 0) continue;
                const double fraction = amount / s.mass[j], energy = s.energy[j] * fraction;
                s.mass[j] -= amount;
                s.energy[j] -= energy;
                s.fuel[j] *= 1 - fraction;
                s.dissolved[i] += amount;
                s.liquid.energy[acid][i] += energy;
                s.acid_capacity[i] -= amount * .4;
                if (s.mass[j] < 1e-10) {
                    s.material[j] = Material::empty;
                    s.mass[j] = 0;
                }
                wake(s, j);
                s.reaction_awake[(i / grid_width / 16) * 20 + i % grid_width / 16] = true;
            }
            if (s.acid_capacity[i] < 1e-8) {
                s.liquid.volume[0][i] += s.liquid.volume[acid][i] * properties(Material::acid).density;
                s.liquid.energy[0][i] += s.liquid.energy[acid][i];
                s.liquid.volume[acid][i] = s.liquid.energy[acid][i] = 0;
            }
        }
    }
    refresh_thermal(s);
}
} // namespace strefa::sim::detail
