#pragma once
#include "ui/view.hpp"
#include "gpu/world.hpp"
#include <optional>
namespace strefa::ui {
struct GpuControls {bool paused{};int scene{9},material{1},radius{4};std::optional<std::array<float,2>> previous;double lost_seconds{};};
struct GpuInput {bool reset{},step{};std::optional<gpu::Stroke> stroke;};
struct GpuLaboratoryFrame {Image image;const gpu::Metrics& metrics;GpuControls& controls;GpuInput& input;};
Action draw_gpu_laboratory(GpuLaboratoryFrame&,const FrameInfo&);
}
