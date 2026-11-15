#pragma once
#include <cstdint>
#include <string_view>

// Row types of the material registry (sim/materials.toml -> sim/material_tables.hpp).
namespace strefa::sim {

enum class Phase : std::uint8_t { empty, solid, powder, liquid, gas, flame };
struct Color {
    std::uint8_t r, g, b, a{255};
};
static_assert(sizeof(Color) == 4);
struct MaterialDefinition {
    std::string_view name;
    std::string_view description;
    Phase phase;
    Color color;
};
// Normalized simulation mass/volume; enthalpy uses kJ per unit mass. Conductance
// is accelerated for game-scale time, not a claim of laboratory SI fidelity.
struct PhysicalProperties {
    double density, heat_capacity, conductance, spawn_temperature;
    double ignition, burn_rate, heat_release, residue, strength;
};

} // namespace strefa::sim
