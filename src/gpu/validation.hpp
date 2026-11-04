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
void validate_coupling_replay(const std::filesystem::path& output, std::ostream& checks,
                              const ExecutionOptions& layout);
std::expected<void, std::string> validate_neighbors(const std::filesystem::path& output, bool benchmark = false);
std::expected<void, std::string> validate_execution(const std::filesystem::path& output, bool benchmark = false);
// --gpu-thermal-benchmark: ABBA matrix of thermal stages per scene, scene order drawn per block (seed in every CSV
// row, STREFA_BENCHMARK_SEED repeats it), stage/kernel timestamps, dispatch and barrier counts.
std::expected<void, std::string> benchmark_thermal(ParticleWorld&, const std::filesystem::path& output);
// Scene order of one benchmark block: Fisher-Yates over SplitMix64, identical on every toolchain.
std::array<unsigned, 4> thermal_benchmark_order(std::uint64_t& state);
std::expected<void, std::string> validate_pbf_restrict(const std::filesystem::path& output);
std::expected<void, std::string> validate_readback(const std::filesystem::path& output);
std::expected<void, std::string> profile_readback(const std::filesystem::path& output);
} // namespace strefa::gpu
