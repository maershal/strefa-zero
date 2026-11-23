#pragma once
// Extension point of the frame loop for code that is not part of the game (scene benchmark, frame profiles, window
// test scripts). StrefaQA creates observers and hands them to Application through LaunchConfig::observers; they see
// the application only through FrameView and, for scripts, ScenarioContext, never through its members.
#include "app/frame_profile.hpp"
#include "ui/view.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace strefa::platform {
class Window;
} // namespace strefa::platform
namespace strefa::sim {
struct LaboratorySnapshot;
} // namespace strefa::sim

namespace strefa::app {
class SimulationWorld;
struct ScenarioContext;

// The application as an observer sees it at one hook.
struct FrameView {
    std::uint64_t frame_start{}; // SDL_GetTicksNS at the start of the frame
    double frame_ms{};           // time since the previous frame started
    ui::Screen screen{};
    const std::string& message; // reason shown in the menu after a failed mode
    const platform::Window& window;
    SimulationWorld& simulation;
    const sim::LaboratorySnapshot* laboratory{}; // CPU laboratory screen only
    unsigned ticks{};                            // simulation ticks advanced by this frame (mission + GPU laboratory)
    std::uint64_t frames{};                      // frames rendered before this one (finish(): of the run)
    std::uint64_t iterations{};                  // finish(): loop iterations of the run
    bool stop{};                                 // set by an observer: the loop ends after this frame
};

class FrameObserver {
public:
    virtual ~FrameObserver() = default;
    // Before the UI of a frame (not while minimized): scripted input of a window test.
    virtual void before_frame(ScenarioContext&, FrameView&) {}
    // After the simulation update of a frame, and once per frame while the window is minimized.
    virtual void after_update(FrameView&) {}
    // End of a loop iteration with its profile row (only when gpu::ProfileOptions::collect is set).
    virtual void frame_end(const FrameProfile&) {}
    // The window was closed or the application asked to quit.
    virtual void quit_requested() {}
    // After the loop, before the mode resources are released; an error fails the run.
    virtual std::expected<void, std::string> finish(const FrameView&) { return {}; }
    // After finish() and after the CPU laboratory thread stopped: the verdict and report of a window test.
    virtual std::expected<void, std::string> check(ScenarioContext&, const FrameView&) { return {}; }
    // Stage of a window test for progress.txt; empty when the observer is not a script.
    [[nodiscard]] virtual std::string progress_stage() const { return {}; }
};

// The observers of one Application run, notified in order.
class FrameObservers {
public:
    explicit FrameObservers(std::vector<FrameObserver*> observers = {}) : observers_{std::move(observers)} {}
    void before_frame(ScenarioContext& context, FrameView& view) const {
        for (auto* observer : observers_) observer->before_frame(context, view);
    }
    void after_update(FrameView& view) const {
        for (auto* observer : observers_) observer->after_update(view);
    }
    void frame_end(const FrameProfile& row) const {
        for (auto* observer : observers_) observer->frame_end(row);
    }
    void quit_requested() const {
        for (auto* observer : observers_) observer->quit_requested();
    }
    std::expected<void, std::string> finish(const FrameView& view) const {
        for (auto* observer : observers_)
            if (auto done = observer->finish(view); !done) return done;
        return {};
    }
    std::expected<void, std::string> check(ScenarioContext& context, const FrameView& view) const {
        for (auto* observer : observers_)
            if (auto done = observer->check(context, view); !done) return done;
        return {};
    }
    [[nodiscard]] std::string progress_stage() const {
        for (const auto* observer : observers_)
            if (auto stage = observer->progress_stage(); !stage.empty()) return stage;
        return {};
    }

private:
    std::vector<FrameObserver*> observers_;
};

} // namespace strefa::app
