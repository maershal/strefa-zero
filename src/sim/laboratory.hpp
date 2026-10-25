#pragma once
#include "sim/world.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace strefa::sim {
struct BrushStroke { Point from, to; int radius; Material material; bool erase; };
struct HeatSource { Point position; double energy; };
struct LaboratoryControl {
    bool active{}, paused{};
    int speed{1};
    RenderMode view{RenderMode::materials};
    std::optional<Point> probe;
    bool operator==(const LaboratoryControl&) const = default;
};
struct LaboratoryCommand {
    std::optional<Scene> scene;
    std::optional<BrushStroke> stroke;
    std::optional<HeatSource> heat;
    bool clear{}, toggle_boundary{}, toggle_recording{};
    int manual_steps{};
    [[nodiscard]] bool empty() const noexcept {
        return !scene && !stroke && !heat && !clear && !toggle_boundary && !toggle_recording && manual_steps==0;
    }
};
// An immutable presentation snapshot, never a shared mutable World. Inspector
// requests are answered by the worker; the UI does not lock the solver's state.
struct LaboratorySnapshot {
    std::shared_ptr<const std::vector<Color>> pixels;
    Statistics report;
    PhysicsConfig settings;
    std::array<bool, World::chunk_count> awake{};
    std::optional<Point> probe;
    PhysicalSample inspected;
    std::uint64_t revision{}, applied_command{}, applied_control{};
    double step_ms{}, render_ms{}, steps_per_second{}, overload_seconds{};
    bool ready{}, recording{};
    int pending_manual_steps{};
    std::string error;
    [[nodiscard]] const Statistics& statistics() const noexcept { return report; }
    [[nodiscard]] const PhysicsConfig& config() const noexcept { return settings; }
    [[nodiscard]] bool chunk_awake(int index) const noexcept { return awake[index]; }
    [[nodiscard]] std::optional<PhysicalSample> sample(Point p) const noexcept {
        if(probe && probe->x==p.x && probe->y==p.y) return inspected;
        return std::nullopt;
    }
};
[[nodiscard]] std::shared_ptr<LaboratorySnapshot> present_world(const World& world, RenderMode view,
    std::optional<Point> probe={}, std::shared_ptr<const std::vector<Color>> existing_pixels={});

class LaboratoryRunner {
public:
    explicit LaboratoryRunner(std::filesystem::path recordings, Scene initial=Scene::demonstration,
        PhysicsConfig config={});
    ~LaboratoryRunner();
    LaboratoryRunner(const LaboratoryRunner&)=delete;
    LaboratoryRunner& operator=(const LaboratoryRunner&)=delete;
    // Returns a command ticket, or zero when the bounded command queue is full.
    std::uint64_t submit(LaboratoryControl control, LaboratoryCommand command={});
    void set_active(bool active);
    [[nodiscard]] std::shared_ptr<const LaboratorySnapshot> snapshot() const;
    [[nodiscard]] bool wait_for(std::uint64_t command, std::chrono::milliseconds timeout);
    void stop();
private:
    struct Queued { LaboratoryCommand command; std::uint64_t ticket; };
    void run(std::stop_token stop, std::filesystem::path recordings, Scene initial, PhysicsConfig config);
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    LaboratoryControl control_;
    std::deque<Queued> commands_;
    std::uint64_t next_ticket_{1}, control_revision_{1};
    std::shared_ptr<const LaboratorySnapshot> snapshot_;
    std::jthread worker_; // Destroy/join before the synchronization primitives.
};
} // namespace strefa::sim
