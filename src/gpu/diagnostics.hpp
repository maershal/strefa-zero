#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::gpu {
enum class CostMode { off,passes,bounds,snapshots };
inline constexpr std::string_view cost_mode_name(CostMode mode){
    switch(mode){case CostMode::off:return "off";case CostMode::passes:return "passes";case CostMode::bounds:return "bounds";case CostMode::snapshots:return "snapshots";}
    return "unknown";
}
enum class PassFamily { granular,liquids,neighbors,mission,coupling };
inline constexpr std::array<std::string_view,5> pass_family_names{"granular","liquids","neighbors","mission","coupling"};
struct TraceConfig {
    CostMode mode{CostMode::off};
    unsigned max_events{64000},max_samples{512},max_snapshots{24};
    std::array<std::uint64_t,4> snapshot_ticks{121,122,185,360};
};
struct DispatchSample {
    std::uint64_t tick{};unsigned generation{},ordinal{},operation{};
    PassFamily family{};
    int phase{-1},iteration{-1}; // external, prepare=0, halves=1/2, diagnostics=3
    bool sampled{},indexed{};
    double milliseconds{};
    float displacement{};unsigned builds{};
};
struct SpatialCapture {
    std::string label;
    std::uint64_t tick{};unsigned generation{},ordinal{};
    int phase{-1},iteration{-1};
    bool soa{};
    std::vector<std::byte> particles;
    std::vector<std::uint32_t> cells,index;
};
struct TraceReport {
    CostMode mode{CostMode::off};
    std::uint64_t staging_bytes{},header_copies{},snapshot_copies{};
    unsigned timestamp_pairs{};
    std::vector<DispatchSample> dispatches;
    std::vector<SpatialCapture> captures;
};
}
