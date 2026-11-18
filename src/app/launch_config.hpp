#pragma once
// Per-run configuration of Application (application.cpp); not part of the launch_options parser.
#include "app/application.hpp"
#include "sim/physics.hpp"

#include <optional>
#include <vector>

namespace strefa::platform {
class GlDebugLog;
} // namespace strefa::platform

namespace strefa::app {

class FrameObserver;

struct LaunchConfig {
    sim::PhysicsConfig physics;
    gpu::ProfileOptions profiling;
    gpu::ExecutionOptions execution;
    platform::GlDebugLog* gl_debug{};
    bool shader_cache{};
    gpu::RenderOptions render;
    gpu::VfxOptions vfx;
    bool unpaced{};
    gpu::ThermalOptions thermal;
    unsigned thermal_scene{}, test_seconds{};
    std::optional<unsigned> gpu_scene{}, lab_scene{}; // set per scene by the scene benchmark
    StartMode start{StartMode::menu};
    SmokeScenario smoke{SmokeScenario::none};
    std::vector<FrameObserver*> observers{}; // StrefaQA only (QaHooks::window); not owned
};

} // namespace strefa::app
