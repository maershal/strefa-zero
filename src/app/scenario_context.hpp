#pragma once
// What a scripted window test reaches in the running Application (application.cpp): the two StrefaZero smoke tests
// (app/window_test.hpp) and the StrefaQA window scripts (app/qa_window_scripts.cpp, through FrameObserver). It
// replaces the former `friend` access: every field a script may read or change is listed here. Application builds
// it once per run; the references stay valid until Application::run returns.
#include "gpu/thermal.hpp"
#include "gpu/world.hpp"
#include "sim/physics.hpp"
#include "ui/gpu_laboratory.hpp"
#include "ui/laboratory.hpp"
#include "ui/view.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace strefa::sim {
class LaboratoryRunner;
class World;
} // namespace strefa::sim

namespace strefa::app {
class SimulationWorld;

// Input a script feeds into the next GPU laboratory frame, as if it came from the UI.
struct GpuInjection {
    std::optional<gpu::Stroke> stroke;
    bool step{};
    std::optional<gpu::ThermalBrush> thermal_brush;
};

struct ScenarioContext {
    std::function<bool(ui::Action)> perform; // as a menu button: opens or closes modes, applies settings
    ui::ViewState& state;
    const std::string& message; // reason shown in the menu after a failed mode
    ui::GpuControls& gpu_controls;
    ui::LaboratoryControls& lab_controls;
    SimulationWorld& simulation;
    const std::unique_ptr<sim::LaboratoryRunner>& laboratory; // CPU laboratory thread, null while closed
    const std::unique_ptr<sim::World>& cpu_world;             // synchronous CPU laboratory of the menu smoke test
    bool& cpu_single_step;                                    // one step of the synchronous CPU laboratory
    std::optional<ui::Screen>& fault_screen;                  // the next frame on this screen throws
    GpuInjection& inject;
    const unsigned& mode_failures; // modes closed after an error
    const unsigned& uploads;       // CPU laboratory canvas uploads
    std::span<const ui::Image> images;
    const ui::Image& parts;
    const ui::Image& weapons;
    std::filesystem::path profile; // folder of settings.ini and the test reports
    const std::string& renderer;
    const std::string& gl_version;
    bool gpu_available{};
    unsigned window_seconds{}; // --test-seconds, 0 = scripted length
    const sim::PhysicsConfig& physics;
    const gpu::ProfileOptions& profiling;
    const gpu::ExecutionOptions& execution;
};

} // namespace strefa::app
