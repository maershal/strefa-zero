#pragma once
#include "gpu/world.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Test and validation access to ParticleWorld, kept out of its public API (Z09). Explicit synchronous diagnostics:
// never used by the interactive loop. The definitions still live in world.cpp; the next step moves them to
// strefa_diagnostics on top of gpu/world_impl.hpp.
namespace strefa::gpu::detail {
struct WorldAccess {
    using Impl = ParticleWorld::Impl;
    // Same constness as the owning unique_ptr: diagnostics on a const world may still issue GL reads.
    static Impl& impl(const ParticleWorld& world) noexcept { return *world.impl_.get(); }
};
} // namespace strefa::gpu::detail

namespace strefa::gpu::test {
// After load(), no projectiles/events.
void restore_coupling_checkpoint(ParticleWorld&, const MissionSnapshot&, std::uint64_t engine_tick);
ThermalSnapshot read_thermal(ParticleWorld&, ThermalRead what = ThermalRead::all);
NeighborSnapshot read_neighbors(ParticleWorld&);
std::array<std::uint32_t, 240> read_phase_mask(ParticleWorld&); // same anchor generation
// No rebuild: compare current cache bank against authoritative ID state.
HotAudit audit_hot_view(ParticleWorld&);
void start_diagnostic_trace(ParticleWorld&, const TraceConfig& config); // CLI only, off by default
TraceReport read_diagnostic_trace(ParticleWorld&);                      // explicit GPU drain required; no rebuild
void capture_diagnostic_snapshot(ParticleWorld&, std::string label);    // current generation only, no rebuild
// <=256 stable IDs for pair audit.
std::vector<std::array<float, 4>> audit_neighbors(ParticleWorld&, bool pairs = true);
void displace_for_test(ParticleWorld&, std::span<const std::array<float, 4>> deltas);
BodySnapshot read_body(ParticleWorld&);
void inject_body_for_test(ParticleWorld&, std::span<const std::array<float, 4>> reactions, bool apply = true);
std::array<bool, 5> pbf_restrict_programs(const ParticleWorld&);
// Source retrieved from attached GL shaders.
std::array<std::string, 5> pbf_program_sources_for_test(const ParticleWorld&);
// Must reject before GL, source 8 = unbound.
void probe_pbf_alias_for_test(ParticleWorld&, unsigned operation, unsigned source, unsigned binding);
std::array<std::uint32_t, 37> read_dispatch_commands(const ParticleWorld&);
std::vector<std::array<float, 4>> read_fluid_terms(ParticleWorld&);
void project_liquids_for_test(ParticleWorld&, unsigned iterations);
std::vector<float> read_boundary_weights(const ParticleWorld&);
std::vector<std::array<float, 4>> fluid_fields_for_test(ParticleWorld&);
void apply_fluid_velocities_for_test(ParticleWorld&);
} // namespace strefa::gpu::test
