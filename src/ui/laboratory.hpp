#pragma once
#include "ui/view.hpp"
#include "sim/laboratory.hpp"
#include <optional>

namespace strefa::ui {

struct LaboratoryControls {
    sim::Material material{sim::Material::sand};
    int radius{4};
    int speed{1};
    bool paused{};
    bool show_chunks{};
    sim::Scene scene{sim::Scene::demonstration};
    sim::RenderMode view{sim::RenderMode::materials};
    int tool{}; // 0 material, 1 heat, 2 cool
    bool recording{};
    std::optional<sim::Point> previous;
    sim::Material previous_material{sim::Material::sand};
    bool previous_erase{};
};
using Stroke=sim::BrushStroke;
struct LaboratoryInput {
    bool reset{}, clear{}, single_step{};
    std::optional<Stroke> stroke;
    std::optional<sim::Point> heat;
    bool record_toggle{}, boundary_toggle{};
    std::optional<sim::Point> probe;
};
struct LaboratoryFrame {
    Image image;
    const sim::LaboratorySnapshot& world;
    LaboratoryControls& controls;
    LaboratoryInput& input;
    double step_ms{}, dropped_seconds{};
};
[[nodiscard]] Action draw_laboratory(LaboratoryFrame& lab, const FrameInfo& frame, const Fonts& fonts);

} // namespace strefa::ui
