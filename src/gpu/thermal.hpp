#pragma once
#include "sim/material.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>

namespace strefa::gpu {
enum class ThermalView { normal,temperature,pressure,velocity,oxygen,fuel };
struct ThermalOptions {bool enabled{},transport{true},reactions{true},profile{};unsigned pressure_iterations{32};bool radiation{};bool operator==(const ThermalOptions&)const=default;};
enum class ThermalTool {none,heat,cool,wood,ice,lava,fuel,smoke,steam};
struct ThermalBrush {std::array<float,4> segment{};float radius{4},rate{200};ThermalTool tool{};bool operator==(const ThermalBrush&)const=default;};
// Same material IDs and normalized mass/kJ units as sim/material.hpp.
struct PhaseState {double temperature{},liquid{},vapor{};sim::Material material{};};
constexpr sim::Material thermal_material(unsigned physical){
    constexpr std::array values{sim::Material::empty,sim::Material::sand,sim::Material::ash,
        sim::Material::metal,sim::Material::stone,sim::Material::water,sim::Material::oil};
    return physical<values.size()?values[physical]:sim::Material::empty;
}
inline PhaseState thermal_phase(sim::Material m,double h){
    using M=sim::Material;
    if(m==M::ice||m==M::water||m==M::steam){
        const double melt=sim::specific_enthalpy(M::ice,273.15),liquid=melt+334;
        const double boil=sim::specific_enthalpy(M::water,373.15),steam=boil+2256;
        if(h<melt)return {h/2.1,0,0,M::ice};
        if(h<liquid)return {273.15,(h-melt)/334,0,M::water};
        if(h<boil)return {(h-sim::enthalpy_offset(M::water))/4.18,1,0,M::water};
        if(h<steam)return {373.15,1,(h-boil)/2256,M::water};
        return {(h-sim::enthalpy_offset(M::steam))/2,1,1,M::steam};
    }
    if(m==M::stone||m==M::lava||m==M::metal||m==M::molten_metal){
        const bool metal=m==M::metal||m==M::molten_metal;const auto solid=metal?M::metal:M::stone,liquid=metal?M::molten_metal:M::lava;
        const double t=metal?1811:1400,latent=metal?270:400,hs=sim::specific_enthalpy(solid,t);
        if(h<hs)return {h/sim::properties(solid).heat_capacity,0,0,solid};
        if(h<hs+latent)return {t,(h-hs)/latent,0,liquid};
        return {(h-sim::enthalpy_offset(liquid))/sim::properties(liquid).heat_capacity,1,0,liquid};
    }
    return {(h-sim::enthalpy_offset(m))/sim::properties(m).heat_capacity,0,0,m};
}
// Transferred mass/enthalpy stays paired. A 2^10 atomic-energy accumulator
// cannot represent 32768 arbitrary hot metal particles; GPU uses a cell gather.
inline constexpr std::uint64_t heat_atomic_worst_case=32768ULL*2*32767;
struct alignas(16) ThermalParticle {
    std::array<float,4> matter{}; // mass,H,remaining original fuel mass,sim::Material
    std::array<float,4> request{}; // gas Q,terrain Q,terrain node,burn request
    std::array<float,4> transfer{}; // gas mass,H,oxygen consumed,smoke fraction
    bool operator==(const ThermalParticle&)const=default;
    std::array<float,4> history{}; // original physical kind,original mass,temperature,liquid fraction
};
static_assert(sizeof(ThermalParticle)==64);
// Byte-transfer record: every field offset is a multiple of 16, stride is 128.
struct ThermalCell {
    std::array<float,4> matter{};
    std::array<float,4> gas0{}; // inert,oxygen,steam,smoke
    std::array<float,4> gas1{}; // fuel,products,gas H,gas original-fuel request
    std::array<float,4> flow{}; // right-face u,bottom-face v,pressure,divergence
    std::array<float,4> demand{}; // gas heat in/out,terrain heat in/out
    std::array<float,4> budget{}; // external mass,external H,released chemical heat,external chemical energy
    std::array<float,4> extra{}; // last burn,previous terrain mask,particle temperature,particle material
    bool operator==(const ThermalCell&)const=default;
    std::array<float,4> factors{}; // gas Q scale,terrain Q scale,oxygen scale,terrain burn request
};
static_assert(sizeof(ThermalCell)==128&&offsetof(ThermalCell,gas0)==16&&offsetof(ThermalCell,factors)==112);
struct ThermalInspection {std::array<int,2> cell{};ThermalCell value;unsigned generation{},tick{};bool valid{};};
struct ThermalTotals {
    double radiated{},mass{},enthalpy{},chemical{},external_mass{},external_enthalpy{},external_chemical{},released_heat{},oxygen{},fuel{},steam{},smoke{},divergence_l2{},divergence_max{};
    std::uint64_t tick{},generation{};bool finite{true};
};
struct ThermalTiming {std::uint64_t tick{};double heat_ms{},gas_ms{},reaction_ms{},render_ms{},cpu_ms{};};
struct ThermalSnapshot {std::vector<ThermalCell> cells;std::vector<ThermalParticle> particles;ThermalTotals totals;};
}
