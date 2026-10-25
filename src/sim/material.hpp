#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace strefa::sim {

enum class Material : std::uint8_t {
    empty, stone, sand, water, oil, wood, fire, smoke, steam, acid, lava, gas, coal, gunpowder,
    thermite, nitroglycerin, metal, ice, molten_metal, ash, count
};
enum class Phase : std::uint8_t { empty, solid, powder, liquid, gas, flame };
struct Color { std::uint8_t r, g, b, a{255}; };
static_assert(sizeof(Color) == 4);
struct MaterialDefinition {
    std::string_view name;
    std::string_view description;
    Phase phase;
    Color color;
};
inline constexpr std::array<MaterialDefinition, static_cast<std::size_t>(Material::count)> materials{{
    {"Gumka", "Usuwa materiał. Prawy przycisk myszy działa jak gumka.", Phase::empty, {13, 21, 25}},
    {"Kamień", "Stała przeszkoda; kwas i wybuch mogą ją zniszczyć.", Phase::solid, {105, 115, 112}},
    {"Piasek", "Sypie się, usypuje skarpy i wypiera ciecze.", Phase::powder, {218, 187, 111}},
    {"Woda", "Płynie, gasi ogień, przy lawie zamienia się w parę.", Phase::liquid, {49, 120, 214}},
    {"Olej", "Lżejszy od wody, unosi się na niej i płonie.", Phase::liquid, {153, 127, 54}},
    {"Drewno", "Stała, palna konstrukcja.", Phase::solid, {126, 83, 48}},
    {"Ogień", "Gorące produkty spalania; ochładzają się i przemieszczają z powietrzem.", Phase::flame, {255, 150, 40}},
    {"Dym", "Gęstość i temperatura wpływają na ruch. Masa pozostaje w świecie lub opuszcza otwartą granicę.", Phase::gas, {93, 100, 105}},
    {"Para", "Skrapla się po oddaniu ciepła, bez licznika czasu.", Phase::gas, {182, 213, 221}},
    {"Kwas", "Rozpuszcza kamień, piasek, drewno i węgiel.", Phase::liquid, {136, 224, 66}},
    {"Lawa", "Powoli płynie, zapala otoczenie; woda chłodzi ją do kamienia.", Phase::liquid, {255, 91, 21}},
    {"Gaz", "Palny gaz unosi się i zapala od ognia.", Phase::gas, {100, 158, 94}},
    {"Węgiel", "Ciężki, palny proszek.", Phase::powder, {50, 56, 60}},
    {"Proch", "Proszek; zapłon lub trafienie powoduje wybuch łańcuchowy.", Phase::powder, {119, 109, 127}},
    {"Termit", "Reakcja termitowa dostarcza ciepło; pozostaje metal, który może się stopić.", Phase::powder, {176, 136, 111}},
    {"Nitrogliceryna", "Wybuchowa ciecz. Ogień, pocisk lub wybuch wyzwala reakcję łańcuchową.", Phase::liquid, {181, 221, 64}},
    {"Metal", "Przewodzi ciepło, topi się po dostarczeniu energii; osłania przed podmuchem.", Phase::solid, {110, 132, 152}},
    {"Lód", "Topi się po pochłonięciu ciepła utajonego, odbierając energię otoczeniu.", Phase::solid, {156, 217, 238}},
    {"Stopiony metal", "Płynny metal; po oddaniu ciepła krzepnie z powrotem w metal.", Phase::liquid, {255, 166, 67}},
    {"Popiół", "Niepalna, lekka pozostałość po spalaniu.", Phase::powder, {104, 104, 100}}
}};
[[nodiscard]] constexpr bool valid(Material value) noexcept { return value < Material::count; }
[[nodiscard]] constexpr const MaterialDefinition& definition(Material value) noexcept {
    return materials[static_cast<std::size_t>(valid(value) ? value : Material::empty)];
}

// Normalized simulation mass/volume; enthalpy uses kJ per unit mass. Conductance
// is accelerated for game-scale time, not a claim of laboratory SI fidelity.
struct PhysicalProperties {
    double density, heat_capacity, conductance, spawn_temperature;
    double ignition, burn_rate, heat_release, residue, strength;
};
inline constexpr std::array<PhysicalProperties, materials.size()> physical{{
    {0, 1, .01, 293.15, 0, 0, 0, 0, 0},
    {2.6, .84, 1.0, 293.15, 0, 0, 0, 0, 7},
    {1.6, .8, .25, 293.15, 0, 0, 0, 0, .15},
    {1, 4.18, .7, 293.15, 0, 0, 0, 0, 0},
    {.85, 2, .12, 293.15, 560, .12, 12000, .02, 0},
    {.6, 1.7, .12, 293.15, 550, .065, 16000, .12, 1.8},
    {.5, 1, .2, 1500, 0, 0, 0, 0, 0},
    {.02, 1, .08, 380, 0, 0, 0, 0, 0},
    {.03, 2, .1, 410, 0, 0, 0, 0, 0},
    {1.1, 2.2, .4, 293.15, 0, 0, 0, 0, 0},
    {2.4, 1.1, 1.5, 1650, 0, 0, 0, 0, 0},
    {.025, 1.5, .08, 293.15, 600, .6, 10000, 0, 0},
    {1.3, 1, .25, 293.15, 650, .03, 24000, .1, .25},
    {1.4, 1, .2, 293.15, 650, 0, 3000, 0, .15},
    {3, 1, 2, 293.15, 850, .2, 4500, .65, .2},
    {1.2, 1.5, .25, 293.15, 500, 0, 4500, 0, 0},
    {7.8, .45, 8, 293.15, 0, 0, 0, 0, 24},
    {.917, 2.1, 1.2, 260, 0, 0, 0, 0, 2},
    {7, .82, 5, 2000, 0, 0, 0, 0, 0},
    {.25, 1, .08, 293.15, 0, 0, 0, 0, .08}
}};
[[nodiscard]] constexpr const PhysicalProperties& properties(Material m) noexcept {
    return physical[static_cast<std::size_t>(valid(m) ? m : Material::empty)];
}
[[nodiscard]] constexpr double enthalpy_offset(Material m) noexcept {
    switch (m) {
        case Material::water: case Material::acid: return 334 + (2.1 - 4.18) * 273.15;
        case Material::steam: return 334 + (2.1 - 4.18) * 273.15 + (4.18 - 2) * 373.15 + 2256;
        case Material::molten_metal: return 270 + (.45 - .82) * 1811;
        case Material::lava: return 400 + (.84 - 1.1) * 1400;
        default: return 0;
    }
}
[[nodiscard]] constexpr double specific_enthalpy(Material m, double kelvin) noexcept {
    return properties(m).heat_capacity * kelvin + enthalpy_offset(m);
}
[[nodiscard]] constexpr bool blocks_actor(Material value) noexcept {
    const auto phase = definition(value).phase;
    return phase == Phase::solid || phase == Phase::powder;
}
[[nodiscard]] constexpr bool blocks_projectile(Material value) noexcept {
    return blocks_actor(value) || value == Material::nitroglycerin;
}

} // namespace strefa::sim

