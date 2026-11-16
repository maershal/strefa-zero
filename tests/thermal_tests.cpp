#include "gpu/thermal.hpp"

#include <iostream>
#include <limits>
using namespace strefa;
int main() {
    int failures = 0;
    const auto check = [&](bool good, const char* label) {
        if (!good) {
            std::cerr << label << '\n';
            ++failures;
        }
    };
    using M = sim::Material;
    for (auto m : {M::ice, M::water, M::steam, M::stone, M::lava, M::metal, M::molten_metal}) {
        for (double t : {100., 260., 293.15, 410., 1200., 1650., 2000.}) {
            const double h = sim::specific_enthalpy(m, t);
            const auto p = gpu::thermal_phase(m, h);
            check(std::isfinite(p.temperature) && p.liquid >= 0 && p.liquid <= 1 && p.vapor >= 0 && p.vapor <= 1,
                  "bounded phase decoder");
        }
    }
    const double ice = sim::specific_enthalpy(M::ice, 273.15), water = sim::specific_enthalpy(M::water, 273.15);
    check(std::abs(water - ice - 334) < 1e-9, "fusion latent heat matches CPU");
    const double boil = sim::specific_enthalpy(M::water, 373.15), steam = sim::specific_enthalpy(M::steam, 373.15);
    check(std::abs(steam - boil - 2256) < 1e-9, "vapor latent heat matches CPU");
    check(std::abs(gpu::thermal_phase(M::water, ice + 167).liquid - .5) < 1e-12, "half melt retains plateau");
    check(std::abs(gpu::thermal_phase(M::water, boil + 1128).vapor - .5) < 1e-12, "half boil retains plateau");
    check(std::abs(gpu::thermal_phase(M::lava, sim::specific_enthalpy(M::stone, 1400) + 200).liquid - .5) < 1e-12,
          "lava solidification plateau");
    check(std::abs(gpu::thermal_phase(M::molten_metal, sim::specific_enthalpy(M::metal, 1811) + 135).liquid - .5)
                  < 1e-12,
          "explicit metal reference plateau");
    check(gpu::heat_atomic_worst_case < static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()),
          "bounded atomic alternative range analysis");
    check(32768ULL * 1024 * 10000 > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()),
          "unbounded hot-particle accumulation cannot use int32");
    check(gpu::thermal_material(5) == M::water && gpu::thermal_material(6) == M::oil, "stable legacy material mapping");
    // The registry must reproduce the hand-written tables it replaced, bit for bit (review v2 B1-B2).
    static_assert(gpu::thermal_material(0) == M::empty && gpu::thermal_material(1) == M::sand
                  && gpu::thermal_material(2) == M::ash && gpu::thermal_material(3) == M::metal
                  && gpu::thermal_material(4) == M::stone && gpu::thermal_material(7) == M::empty);
    static_assert(sim::enthalpy_offset(M::water) == 334 + (2.1 - 4.18) * 273.15);
    static_assert(sim::enthalpy_offset(M::acid) == 334 + (2.1 - 4.18) * 273.15);
    static_assert(sim::enthalpy_offset(M::steam) == 334 + (2.1 - 4.18) * 273.15 + (4.18 - 2) * 373.15 + 2256);
    static_assert(sim::enthalpy_offset(M::molten_metal) == 270 + (.45 - .82) * 1811);
    static_assert(sim::enthalpy_offset(M::lava) == 400 + (.84 - 1.1) * 1400);
    static_assert(sim::enthalpy_offset(M::ice) == 0 && sim::enthalpy_offset(M::stone) == 0
                  && sim::enthalpy_offset(M::metal) == 0 && sim::enthalpy_offset(M::oil) == 0);
    return failures ? 1 : 0;
}
