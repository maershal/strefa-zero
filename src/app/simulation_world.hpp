#pragma once
#include "app/session_recording.hpp"
#include "game/session.hpp"
#include "gpu/world.hpp"
#include "sim/fixed_clock.hpp"

namespace strefa::app {
struct SimulationFrame {
    game::TickInput controls;
    std::optional<gpu::Stroke> stroke;
    double elapsed{};
    bool focused{}, paused{}, single_step{}, scripted{};
    gpu::ThermalBrush thermal_brush{};
};
struct SimulationSubmission {
    unsigned ticks{};
    double cpu_ms{}, render_ms{};
};
// One active scene and scheduler for both presentation views. The underlying
// world owns a device, program library, renderer and asynchronous readback ring.
class SimulationWorld {
public:
    std::expected<void, std::string> open(const std::filesystem::path&, const gpu::ExecutionOptions&,
                                          const gpu::ProfileOptions&, SceneConfiguration initial,
                                          const std::filesystem::path& shader_cache = {},
                                          std::shared_ptr<gpu::RenderDevice> device = {});
    void reset(SceneConfiguration);
    void set_thermal_options(gpu::ThermalOptions);
    void suspend();
    void begin_recording();
    bool recording() const noexcept { return recording_.active(); }
    std::expected<void, std::string> save_recording(const std::filesystem::path&) const;
    std::expected<void, std::string> replay_recording(const std::filesystem::path&);
    SimulationSubmission advance(const SimulationFrame&);
    gpu::ParticleWorld* operator->() const noexcept { return world_.get(); }
    gpu::ParticleWorld& operator*() const noexcept { return *world_; }
    explicit operator bool() const noexcept { return bool(world_); }
    SceneConfiguration scene() const noexcept { return scene_; }
    const auto& clock() const noexcept { return clock_; }
    unsigned creations() const noexcept { return creations_; }

private:
    std::unique_ptr<gpu::ParticleWorld> world_;
    SceneConfiguration scene_;
    sim::FixedStepClock<120, 4, 2147483647> clock_;
    game::InputBuffer input_;
    unsigned creations_{};
    bool pending_single_step_{};
    SessionRecording recording_;
    gpu::ThermalBrush thermal_brush_;
    bool thermal_pending_{};
};
} // namespace strefa::app
