#pragma once
#include "gpu/lighting.hpp"

#include <array>
#include <cstdint>
#include <string_view>
namespace strefa::gpu {
enum class RenderQuality { basic, standard, high };
inline constexpr std::string_view render_quality_name(RenderQuality q) {
    switch (q) {
    case RenderQuality::basic: return "basic";
    case RenderQuality::standard: return "standard";
    case RenderQuality::high: return "high";
    }
    return "unknown";
}
// Presentation only. Never serialized into the physical recording contract.
struct RenderOptions {
    RenderQuality quality{RenderQuality::basic};
    bool profile_passes{};
    LightingOptions lighting{};
};
enum class RenderStage { reference, opaque, optics, complete }; // diagnostic A/B/C
inline constexpr std::array<std::string_view, 8> render_pass_names{"scene",     "splat",       "filter_x",   "filter_y",
                                                                   "thickness", "composition", "foreground", "total"};
struct RenderSample {
    std::uint64_t frame{};
    std::array<double, 8> milliseconds{};
    double terrain_ms{};
};
struct TargetPresentation {
    std::array<unsigned, 3> hp{3, 3, 3};
    std::array<float, 3> hit{};
};
} // namespace strefa::gpu
