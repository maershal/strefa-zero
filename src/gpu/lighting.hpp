#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
namespace strefa::gpu {
enum class LightingQuality { off, low, high };
inline constexpr std::string_view lighting_quality_name(LightingQuality q) {
    switch (q) {
    case LightingQuality::off: return "off";
    case LightingQuality::low: return "low";
    case LightingQuality::high: return "high";
    }
    return "unknown";
}
struct LightingOptions {
    LightingQuality quality{LightingQuality::off};
    float bloom{.08F}, vignette{.10F}, shake{}, aberration{}, exposure{1}, ambient{.85F};
    bool operator==(const LightingOptions&) const = default;
    bool valid() const {
        for (float v : {bloom, vignette, shake, aberration})
            if (!std::isfinite(v) || v < 0 || v > 1) return false;
        return quality >= LightingQuality::off && quality <= LightingQuality::high && std::isfinite(exposure)
            && exposure > 0 && exposure <= 8 && std::isfinite(ambient) && ambient >= 0 && ambient <= 2;
    }
};
// Presentation state is separate from the 64-byte physical particle ABI.
inline float update_moisture(float wet, bool water, float kelvin, float dt) {
    const float drying = std::clamp(1.F + std::max(0.F, kelvin - 293.15F) / 100.F, 1.F, 8.F);
    return std::clamp(wet + (water ? 4.F : -drying / 30.F) * dt, 0.F, 1.F);
}
struct alignas(16) PointLight {
    std::array<float, 4> position{};
    std::array<float, 4> color{};
    std::array<float, 4> history{};
};
static_assert(sizeof(PointLight) == 48);
inline constexpr std::array<std::string_view, 7> lighting_pass_names{
        "presentation_tick", "shadow_rebuild", "lighting", "composition", "bloom", "postprocess", "total"};
struct LightingSample {
    std::uint64_t serial{};
    bool tick{};
    std::array<double, 7> milliseconds{};
};
struct LightingSnapshot {
    std::array<unsigned, 4> clock{}, counts{};
    std::array<PointLight, 32> lights{};
};
} // namespace strefa::gpu
