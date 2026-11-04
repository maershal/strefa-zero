#pragma once
#include "app/session_recording.hpp"
#include "game/input.hpp"
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
    using Clock = sim::FixedStepClock<120, 4, 2147483647>;

public:
    static constexpr double tick_seconds = Clock::tick_seconds;
    // The shipped game passes sim::interactive_clock; validation keeps the exact backlog.
    explicit SimulationWorld(sim::ClockPolicy policy = sim::exact_clock) noexcept : clock_{policy} {}
    std::expected<void, std::string> open(const std::filesystem::path&, const gpu::ExecutionOptions&,
                                          const gpu::ProfileOptions&, SceneConfiguration initial,
                                          const std::filesystem::path& shader_cache = {},
                                          std::shared_ptr<gpu::RenderDevice> device = {});
    void reset(SceneConfiguration);
    // Releases the world after a failure; the next open() creates a new one.
    void close();
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
    // Changes on every reset(), i.e. whenever a scene starts again.
    unsigned scene_epoch() const noexcept { return scene_epoch_; }

private:
    std::unique_ptr<gpu::ParticleWorld> world_;
    SceneConfiguration scene_;
    Clock clock_;
    game::InputBuffer input_;
    unsigned creations_{}, scene_epoch_{};
    bool pending_single_step_{};
    SessionRecording recording_;
    gpu::ThermalBrush thermal_brush_;
    bool thermal_pending_{};
};
} // namespace strefa::app
