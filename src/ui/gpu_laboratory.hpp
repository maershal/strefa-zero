#pragma once
#include "gpu/world.hpp"
#include "ui/view.hpp"

#include <algorithm>
#include <optional>
namespace strefa::ui {
inline constexpr int gpu_brush_min = 1, gpu_brush_max = 16;
inline constexpr float gas_rate_min = .001F, gas_rate_max = 1.F;
inline constexpr float heat_rate_min = .01F, heat_rate_max = 2000.F;
struct GpuControls {
    gpu::LightingOptions lighting;
    bool paused{};
    int scene{9}, material{1}, radius{4}, render_quality{0}, vfx_quality{1};
    std::optional<std::array<float, 2>> previous;
    double lost_seconds{};
    bool thermal_enabled{}, thermal_radiation{};
    int thermal_view{}, thermal_tool{}, thermal_scene{1};
    float thermal_rate{200}, gas_rate{.05F};
};
// Builds the brush for the selected thermal tool from UI state. Typed-in values
// (Ctrl+click) and stale settings are clamped to the ranges the sliders show,
// which lie inside the gpu::valid contract, so the world never rejects them.
inline gpu::ThermalBrush make_thermal_brush(const GpuControls& controls, const std::array<float, 4>& segment) {
    const int tool = std::clamp(controls.thermal_tool, 0, static_cast<int>(gpu::ThermalTool::steam));
    const bool gas = tool >= static_cast<int>(gpu::ThermalTool::fuel);
    const float rate = gas ? controls.gas_rate : controls.thermal_rate;
    return {segment, static_cast<float>(std::clamp(controls.radius, gpu_brush_min, gpu_brush_max)),
            std::isfinite(rate)
                    ? std::clamp(rate, gas ? gas_rate_min : heat_rate_min, gas ? gas_rate_max : heat_rate_max)
                    : (gas ? gas_rate_min : heat_rate_min),
            static_cast<gpu::ThermalTool>(tool)};
}
struct GpuInput {
    // select_scene loads controls.scene; reset reloads the scene that is loaded now
    // (including a thermal scene, which the scene list cannot select).
    bool select_scene{}, reset{}, thermal_reset{}, step{}, begin_recording{}, save_recording{}, replay_recording{};
    std::optional<gpu::Stroke> stroke;
    gpu::ThermalBrush thermal_brush;
    std::optional<std::array<int, 2>> inspect;
};
struct GpuLaboratoryFrame {
    Image image;
    const gpu::Metrics& metrics;
    GpuControls& controls;
    GpuInput& input;
    bool mission{};
    const gpu::MissionSnapshot& state;
    bool recording{};
    const gpu::VfxCounters* vfx{};
    std::uint64_t vfx_memory{};
    const gpu::ThermalTotals* thermal{};
    std::uint64_t thermal_memory{};
    const gpu::ThermalInspection* inspection{};
    std::array<float, 2> camera{};
    unsigned thermal_scene{}; // thermal scene loaded now, 0 = none
    const ClockStats* clock{};
};
Action draw_gpu_laboratory(GpuLaboratoryFrame&, const FrameInfo&);
} // namespace strefa::ui
