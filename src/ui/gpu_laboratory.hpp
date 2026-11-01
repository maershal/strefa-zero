#pragma once
#include "gpu/world.hpp"
#include "ui/view.hpp"

#include <optional>
namespace strefa::ui {
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
struct GpuInput {
    bool reset{}, thermal_reset{}, step{}, begin_recording{}, save_recording{}, replay_recording{};
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
};
Action draw_gpu_laboratory(GpuLaboratoryFrame&, const FrameInfo&);
} // namespace strefa::ui
