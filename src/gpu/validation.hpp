#pragma once
// GPU validations, benchmarks and profiles; built into strefa_diagnostics (StrefaQA), not into the game.
#include "gpu/world.hpp"

#include <expected>
#include <filesystem>
#include <ostream>
#include <string>

namespace strefa::gpu {
std::expected<void, std::string> validate_mission(ParticleWorld&, const std::filesystem::path& output);
std::expected<void, std::string> validate_granular(ParticleWorld&, const std::filesystem::path& output);
std::expected<void, std::string> validate_liquids(ParticleWorld&, const std::filesystem::path& output);
std::expected<void, std::string> validate_flow(ParticleWorld&, const std::filesystem::path& output);
// The brush must spawn the same count after fixtures and scene resets as in a fresh world.
std::expected<void, std::string> validate_brush_history(ParticleWorld&, const std::filesystem::path& output);
// N06: batched laboratory explosions (and the mission with ExecutionOptions::batched_blasts).
std::expected<void, std::string> validate_lab_blast(ParticleWorld&, const std::filesystem::path& output);
// Part of the brush check: mission debris after the same kind of history must match the destroyed cells one to one.
// Throws on failure; writes per-round counts and digests of the explosion tick (passes, mission state, debris).
void validate_debris_history(ParticleWorld&, std::ostream& report);
std::expected<void, std::string> profile_gpu(ParticleWorld&, const std::filesystem::path& output,
                                             bool check_only = false);
std::expected<void, std::string> profile_stabilization(ParticleWorld&, const std::filesystem::path& output,
                                                       const ProfileOptions&);
std::expected<void, std::string> validate_transport(const std::filesystem::path& output);
std::expected<void, std::string> validate_hot_reads(const std::filesystem::path& output,
                                                    const ExecutionOptions& layout = {});
std::expected<void, std::string> profile_cost(const std::filesystem::path& output, const ExecutionOptions&, CostMode,
                                              bool check_only = false);
std::expected<void, std::string> validate_work(const std::filesystem::path& output,
                                               const ExecutionOptions& layout = {});
std::expected<void, std::string> validate_coupling(const std::filesystem::path& output, bool benchmark = false,
                                                   bool reference_control = false, bool legacy_check = false,
                                                   const ExecutionOptions& layout = {});
// Z05, --gpu-check coupling-bitwise: the single-group body reduction against the summation order of the former
// two-dispatch reduction, bit for bit, on injected batches and on reactions from the scripted mission route.
std::expected<void, std::string> validate_coupling_bitwise(const std::filesystem::path& shaders,
                                                           const std::filesystem::path& output,
                                                           const ExecutionOptions& layout);
// --gpu-check thermal-bits: digests of the thermal cells/particles and of the world particles/terrain after a fixed
// tick sequence in the cold (sand_water), fire, oil, quench and phases scenes, run twice and required to repeat;
// compared between two builds on one machine and driver, never against a reference in the repository.
std::expected<void, std::string> validate_thermal_bits(const std::filesystem::path& shaders,
                                                       const std::filesystem::path& output,
                                                       const ExecutionOptions& layout);
// --gpu-check thermal-gate: ABBA (gate off, on, on, off) of ThermalOptions::gas_gate in the cold, idle, fire and quench
// scenes, calibration probes around every segment (STREFA_THERMAL_GATE_PLAN = full | quick).
std::expected<void, std::string> benchmark_thermal_gate(const std::filesystem::path& shaders,
                                                        const std::filesystem::path& output,
                                                        const ExecutionOptions& layout);
void validate_coupling_replay(const std::filesystem::path& output, std::ostream& checks,
                              const ExecutionOptions& layout);
std::expected<void, std::string> validate_neighbors(const std::filesystem::path& output, bool benchmark = false);
std::expected<void, std::string> validate_execution(const std::filesystem::path& output, bool benchmark = false);
// --gpu-thermal-benchmark: ABBA matrix of thermal stages per scene, scene order drawn per block (seed in every CSV
// row, STREFA_BENCHMARK_SEED repeats it), stage/kernel timestamps, dispatch and barrier counts.
std::expected<void, std::string> benchmark_thermal(ParticleWorld&, const std::filesystem::path& output);
// Scene order of one benchmark block: Fisher-Yates over SplitMix64, identical on every toolchain.
std::array<unsigned, 4> thermal_benchmark_order(std::uint64_t& state);
// Z01, --gpu-check cost-model: synthetic pass series, particle-count sweep of the real solver and calibration probes
// around every segment (CSV into output; STREFA_COST_MODEL_PLAN = full | part1 | quick).
std::expected<void, std::string> model_tick_cost(const std::filesystem::path& shaders,
                                                 const std::filesystem::path& output, const ExecutionOptions&);
std::expected<void, std::string> validate_pbf_restrict(const std::filesystem::path& output);
std::expected<void, std::string> validate_readback(const std::filesystem::path& output);
std::expected<void, std::string> profile_readback(const std::filesystem::path& output);
} // namespace strefa::gpu
