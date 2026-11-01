#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
namespace strefa::gpu {
enum class VfxQuality { off, low, high };
constexpr std::string_view vfx_quality_name(VfxQuality q) {
    switch (q) {
    case VfxQuality::off: return "off";
    case VfxQuality::low: return "low";
    case VfxQuality::high: return "high";
    }
    return "unknown";
}
struct VfxOptions {
    VfxQuality quality{VfxQuality::low};
    bool profile{};
};
inline constexpr std::array<std::string_view, 12> vfx_kind_names{
        "none", "explosion", "impact", "muzzle", "jet", "spray", "foam", "bubble", "spark", "debris", "scorch", "wave"};
inline constexpr unsigned vfx_capacity = 16384, vfx_kinds = 12, vfx_event_capacity = 256;
enum class VfxKind : std::uint32_t {
    none,
    explosion,
    impact,
    muzzle,
    jet,
    spray,
    foam,
    bubble,
    spark,
    debris,
    scorch,
    wave
};
struct alignas(16) VfxParticle {
    std::array<float, 4> position_velocity{}, previous_age_life{};
    std::array<std::uint32_t, 4> appearance{};
};
struct alignas(16) VfxEvent {
    std::array<std::uint32_t, 4> identity{};
    std::array<float, 4> position_strength{}, velocity{};
};
struct VfxCounters {
    std::array<std::uint32_t, 4> counts{}; // alive, free, next alive, peak
    std::array<std::uint32_t, 4> clock{},
            layers{}; // source generation, tick, budget-limited emitter passes, malformed batches
    std::array<std::uint32_t, vfx_kinds> emitted{}, rejected{};
    std::array<std::uint32_t, 4> draw{}, copy{}, wave{}, pre{}, post{};
};
static_assert(sizeof(VfxParticle) == 48 && alignof(VfxParticle) == 16 && offsetof(VfxParticle, appearance) == 32);
static_assert(sizeof(VfxEvent) == 48 && sizeof(VfxCounters) == 224 && offsetof(VfxCounters, draw) == 144);
struct VfxSample {
    std::uint64_t tick{};
    double field_ms{}, update_ms{}, emit_ms{}, draw_ms{};
};
struct VfxSnapshot {
    VfxCounters counters;
    std::vector<VfxParticle> particles;
    std::vector<unsigned> alive, free;
};
} // namespace strefa::gpu
