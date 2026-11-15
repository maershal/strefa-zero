#pragma once
// Material IDs and the `materials`/`physical` tables are generated from sim/materials.toml
// (scripts/gen_materials.py); this header adds the accessors.
#include "sim/material_tables.hpp"

namespace strefa::sim {

[[nodiscard]] constexpr bool valid(Material value) noexcept {
    return value < Material::count;
}
[[nodiscard]] constexpr const MaterialDefinition& definition(Material value) noexcept {
    return materials[static_cast<std::size_t>(valid(value) ? value : Material::empty)];
}

[[nodiscard]] constexpr const PhysicalProperties& properties(Material m) noexcept {
    return physical[static_cast<std::size_t>(valid(m) ? m : Material::empty)];
}
[[nodiscard]] constexpr double enthalpy_offset(Material m) noexcept {
    switch (m) {
    case Material::water:
    case Material::acid: return 334 + (2.1 - 4.18) * 273.15;
    case Material::steam: return 334 + (2.1 - 4.18) * 273.15 + (4.18 - 2) * 373.15 + 2256;
    case Material::molten_metal: return 270 + (.45 - .82) * 1811;
    case Material::lava: return 400 + (.84 - 1.1) * 1400;
    default: return 0;
    }
}
[[nodiscard]] constexpr double specific_enthalpy(Material m, double kelvin) noexcept {
    return properties(m).heat_capacity * kelvin + enthalpy_offset(m);
}

} // namespace strefa::sim
