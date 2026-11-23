// StrefaQA window test scripts (strefa_diagnostics): CPU and GPU laboratory, shared session, mission_quick and mode
// faults. Each is a FrameObserver that drives the application through ScenarioContext; the two StrefaZero smoke
// tests stay in app/window_test.cpp.
#include "app/frame_observer.hpp"
#include "app/launch_config.hpp"
#include "app/scenario_context.hpp"
#include "app/simulation_world.hpp"
#include "app/window_scenarios.hpp"
#include "app/window_test.hpp"
#include "platform/gl_debug.hpp"
#include "sim/fixed_clock.hpp"
#include "sim/laboratory.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace strefa::app {
namespace {

// A script runs `length` frames (0: the script decides) and sees the index of the current frame in before_frame.
class Script : public FrameObserver {
public:
    explicit Script(unsigned length) : length_{length} {}
    void before_frame(ScenarioContext& context, FrameView& view) override {
        frame_ = view.frames;
        step(context, view);
    }
    void after_update(FrameView& view) override {
        if (length_ && view.frames + 1 >= length_) view.stop = true;
    }

protected:
    virtual void step(ScenarioContext&, const FrameView&) = 0;
    unsigned length_{};
    std::uint64_t frame_{};
};

// --laboratory-smoke-test: CPU laboratory responsiveness while the worker thread steps dam_break.
class LaboratorySmoke final : public Script {
public:
    LaboratorySmoke() : Script{300} {}
    std::expected<void, std::string> check(ScenarioContext& context, const FrameView& view) override {
        const auto times = sorted_frame_times(std::move(frame_times_));
        if (!times) return std::unexpected{times.error()};
        const auto snapshot = context.laboratory->snapshot();
        const double p95 = percentile(*times, .95);
        std::ofstream report{context.profile / "laboratory-performance.txt"};
        if (!report) return std::unexpected{"Nie można zapisać raportu laboratorium."};
        report << "frames=" << view.frames << "\nui_median_ms=" << percentile(*times, .5) << "\nui_p95_ms=" << p95
               << "\nui_p99_ms=" << percentile(*times, .99) << "\nui_max_ms=" << times->back()
               << "\nphysics_last_ms=" << snapshot->step_ms << "\nphysics_ticks=" << snapshot->report.tick
               << "\nphysics_steps_per_second=" << snapshot->steps_per_second
               << "\nphysics_threads=" << snapshot->report.worker_threads << "\nfinite=" << snapshot->report.finite
               << "\np95_limit=" << p95_limit_name(context.renderer) << "\nrenderer=" << context.renderer << '\n';
        if (!snapshot->ready || !snapshot->report.finite || snapshot->report.tick < 5 || !p95_ok(context.renderer, p95)
            || (context.physics.worker_threads && snapshot->report.worker_threads != context.physics.worker_threads))
            return std::unexpected{"Test reaktywności laboratorium nie przeszedł."};
        return {};
    }
    [[nodiscard]] std::string progress_stage() const override { return std::format("klatka {}/300", frame_); }

private:
    void step(ScenarioContext& context, const FrameView& view) override {
        if (frame_ >= 30) frame_times_.push_back(view.frame_ms);
        if (frame_ == 120) context.lab_controls.paused = true;
        if (frame_ == 150) context.lab_controls.paused = false;
        if (frame_ == 200) context.perform(ui::Action::back);
        if (frame_ == 220) context.perform(ui::Action::open_laboratory);
    }
    std::vector<double> frame_times_;
};

// --gpu-smoke-test: GPU laboratory with sand_water, pause/resume and a view switch; with --test-seconds it runs that
// long without the scripted steps.
class GpuSmoke final : public Script {
public:
    GpuSmoke(const LaunchConfig& config, std::filesystem::path profile)
        : Script{0},
          seconds_{config.test_seconds},
          gl_debug_{config.gl_debug},
          profile_{std::move(profile)} {}
    void after_update(FrameView& view) override {
        const bool duration_done = seconds_ && static_cast<double>(view.frame_start - started_ns_) / 1e9 >= seconds_;
        if (seconds_ ? duration_done : view.frames == 230) {
            if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::capture);
            view.simulation->save_image(profile_ / "gpu-preview.bmp");
            if (gl_debug_) gl_debug_->set_phase(platform::DebugPhase::runtime);
        }
        if (seconds_ ? duration_done : view.frames + 1 >= 240) view.stop = true;
    }
    std::expected<void, std::string> check(ScenarioContext& context, const FrameView& view) override {
        const auto times = sorted_frame_times(std::move(frame_times_));
        if (!times) return std::unexpected{times.error()};
        std::ofstream report{context.profile / "gpu-ui.txt"};
        write_gpu_window_report(report, context,
                                {"sand_water", static_cast<unsigned>(view.frames), *times, pause_ok_,
                                 context.gpu_controls.lost_seconds});
        if (!pause_ok_ || context.simulation->metrics().steps < 100
            || !p95_ok(context.renderer, percentile(*times, .95)))
            return std::unexpected{"Nieudany test okna GPU."};
        return {};
    }
    [[nodiscard]] std::string progress_stage() const override {
        return seconds_ ? std::string{} : std::format("klatka {}/240", frame_);
    }

private:
    void step(ScenarioContext& context, const FrameView& view) override {
        if (!started_ns_) started_ns_ = view.frame_start;
        if (frame_ >= 30) frame_times_.push_back(view.frame_ms);
        if (seconds_) return;
        if (frame_ == 80) {
            context.gpu_controls.paused = true;
            paused_step_ = context.simulation->metrics().steps;
        }
        if (frame_ == 100) {
            pause_ok_ = context.simulation->metrics().steps == paused_step_;
            context.gpu_controls.paused = false;
        }
        if (frame_ == 140) context.perform(ui::Action::back);
        if (frame_ == 155) context.perform(ui::Action::open_gpu);
    }
    unsigned seconds_{};
    platform::GlDebugLog* gl_debug_{};
    std::filesystem::path profile_;
    std::uint64_t started_ns_{}, paused_step_{};
    bool pause_ok_{true};
    std::vector<double> frame_times_;
};

// --session-smoke-test: one GPU world shared by the mission and the GPU laboratory; switching views, pausing and
// render settings must not change its state, and a recording replays to the same tick.
class SessionSmoke final : public Script {
public:
    SessionSmoke() : Script{130} {}
    std::expected<void, std::string> check(ScenarioContext& context, const FrameView& view) override {
        auto& simulation = context.simulation;
        const auto cache = simulation->shader_cache_stats();
        ok_ = ok_ && simulation.creations() == 1 && !context.laboratory && cache.programs == programs_;
        std::ofstream report{context.profile / "session-smoke.txt"};
        report << "frames=" << view.frames << "\nchecks=" << checks_ << "\npassed=" << ok_
               << "\nworld_creations=" << simulation.creations() << "\nprograms=" << cache.programs
               << "\ncpu_laboratory_created=" << bool(context.laboratory) << '\n';
        const auto folder = context.profile / "recording-check";
        if (auto saved = simulation.save_recording(folder); !saved) return saved;
        const auto tick = simulation->metrics().steps;
        if (auto replayed = simulation.replay_recording(folder / "session.txt"); !replayed) return replayed;
        if (simulation->metrics().steps != tick || simulation.scene().mission)
            return std::unexpected{"Odtwarzanie nie zachowało sceny i kroku."};
        if (!ok_ || checks_ != 12) return std::unexpected{"Wspólna scena: utrata stanu lub niepełny test przełączeń."};
        return {};
    }

private:
    void step(ScenarioContext& context, const FrameView&) override {
        auto& simulation = context.simulation;
        auto check = [&](bool condition) {
            ok_ = condition && ok_;
            ++checks_;
        };
        auto snapshot = [&] {
            mission_ = simulation->read_mission();
            grains_ = simulation->readback();
            terrain_ = simulation->read_terrain();
            tick_ = simulation->metrics().steps;
        };
        auto unchanged = [&] {
            const auto mission = simulation->read_mission();
            const auto grains = simulation->readback();
            return simulation->metrics().steps == tick_ && std::memcmp(&mission, &mission_, sizeof(mission)) == 0
                && grains.size() == grains_.size()
                && std::memcmp(grains.data(), grains_.data(), grains.size() * sizeof(gpu::Grain)) == 0
                && simulation->read_terrain() == terrain_;
        };
        switch (frame_) {
        case 0:
            simulation.begin_recording();
            programs_ = simulation->shader_cache_stats().programs;
            check(programs_ > 0 && !context.laboratory);
            break;
        case 30:
            context.gpu_controls.paused = true;
            snapshot();
            context.perform(ui::Action::open_gpu);
            break;
        case 31: check(unchanged() && simulation.scene().mission); break;
        case 32: context.inject.stroke = gpu::Stroke{8, 8, 8, 8, 2, gpu::Material::sand}; break;
        case 33:
            check(simulation->metrics().steps == tick_ && simulation->read_terrain() == terrain_);
            snapshot();
            break;
        case 34: context.perform(ui::Action::open_mission); break;
        case 35: check(unchanged()); break;
        case 36: context.gpu_controls.render_quality = 2; break;
        case 37: check(unchanged()); break;
        case 38: context.gpu_controls.render_quality = 0; break;
        case 39: check(unchanged()); break;
        case 40:
            context.perform(ui::Action::open_gpu);
            context.inject.step = true;
            break;
        case 41:
            check(simulation->metrics().steps == tick_ + 1);
            snapshot();
            break;
        case 50: context.perform(ui::Action::back); break;
        case 60:
            check(unchanged());
            context.perform(ui::Action::open_mission);
            break;
        case 61: check(unchanged()); break;
        case 70: context.gpu_controls.paused = false; break;
        case 90:
            context.gpu_controls.paused = true;
            context.perform(ui::Action::open_gpu);
            snapshot();
            break;
        case 91: check(unchanged()); break;
        case 100:
            simulation.reset({false, gpu::Scene::empty});
            snapshot();
            break;
        case 101:
            check(!simulation.scene().mission && tick_ == 0);
            context.perform(ui::Action::open_mission);
            break;
        case 102: check(unchanged() && !simulation.scene().mission); break;
        default: break;
        }
    }
    bool ok_{true};
    unsigned checks_{}, programs_{};
    std::uint64_t tick_{};
    gpu::MissionSnapshot mission_;
    std::vector<gpu::Grain> grains_;
    std::vector<std::uint32_t> terrain_;
};

// --mission-ui-smoke-test (gpu.mission_ui_quick): open, leave and reopen the mission through the GPU tools; an
// injected 5 s stall must be dropped, not repaid as a fast-forward.
class MissionQuick final : public Script {
public:
    MissionQuick() : Script{180} {}
    std::expected<void, std::string> check(ScenarioContext& context, const FrameView& view) override {
        auto& simulation = context.simulation;
        const auto steps = simulation ? simulation->metrics().steps : 0;
        const double speed = speed_after_stall(context);
        const bool stall_ok = stall_dropped_ >= stall_seconds - sim::interactive_clock.max_backlog_seconds
                           && stall_dropped_ <= stall_seconds + 1 && speed > 0 && speed <= max_speed;
        const bool passed = ok_ && checks_ == 7 && stall_ok && steps > ticks_ && simulation.creations() == 1;
        std::ofstream report{context.profile / "mission-ui-quick.txt"};
        report << "frames=" << view.frames << "\nchecks=" << checks_ << "\nstall_seconds=" << stall_seconds
               << "\nstall_dropped_seconds=" << stall_dropped_ << "\nspeed_after_stall=" << speed
               << "\nstall_ok=" << stall_ok << "\npassed=" << passed << "\nsubmitted_ticks=" << steps
               << "\nworld_creations=" << simulation.creations() << "\nrenderer=" << context.renderer
               << "\nopengl=" << context.gl_version << '\n';
        if (!passed) return std::unexpected{"Krótki test ekranu misji nie przeszedł. Sprawdź mission-ui-quick.txt."};
        return {};
    }
    [[nodiscard]] std::string progress_stage() const override { return std::format("kontrola {}/7", checks_); }

private:
    static constexpr double stall_seconds = 5, max_speed = 1.1;
    static constexpr unsigned stall_frame = 148, speed_from = 150;
    void step(ScenarioContext& context, const FrameView& view) override {
        auto& simulation = context.simulation;
        auto check = [&](bool condition) {
            ok_ = condition && ok_;
            ++checks_;
        };
        switch (frame_) {
        case 60:
            ticks_ = simulation->metrics().steps;
            check(ticks_ > 0 && context.state.screen == ui::Screen::gpu_mission);
            check(context.perform(ui::Action::back) && context.state.screen == ui::Screen::main_menu);
            break;
        case 70: check(context.perform(ui::Action::open_mission) && simulation.creations() == 1); break;
        case 120:
            check(simulation->metrics().steps > ticks_);
            check(context.perform(ui::Action::back));
            break;
        case 125: check(context.perform(ui::Action::open_gpu)); break;
        case 140: context.perform(ui::Action::back); break;
        case 145: check(context.perform(ui::Action::open_mission)); break;
        case stall_frame:
            stall_dropped_ = simulation.clock().dropped_seconds();
            SDL_Delay(static_cast<Uint32>(stall_seconds * 1000));
            break;
        case speed_from:
            stall_dropped_ = simulation.clock().dropped_seconds() - stall_dropped_;
            ticks_after_stall_ = simulation->metrics().steps;
            break;
        default: break;
        }
        if (frame_ >= speed_from) seconds_after_stall_ += static_cast<float>(view.frame_ms) / 1000.0;
    }
    // Ticks run after the stall per second of frame time fed to the clock; ~1 when real time.
    double speed_after_stall(ScenarioContext& context) const {
        auto& simulation = context.simulation;
        const auto ticks = simulation ? simulation->metrics().steps - ticks_after_stall_ : 0;
        return seconds_after_stall_ > 0
                     ? static_cast<double>(ticks) * SimulationWorld::tick_seconds / seconds_after_stall_
                     : 0;
    }
    bool ok_{true};
    unsigned checks_{};
    std::uint64_t ticks_{}, ticks_after_stall_{};
    double stall_dropped_{}, seconds_after_stall_{};
};

// --mode-fault-smoke-test (app.mode_faults): each mode throws once inside its frame; the application must release
// it, log the error, return to the menu and be able to open the mode again.
class ModeFaults final : public Script {
public:
    explicit ModeFaults(const std::filesystem::path& profile) : Script{150} {
        std::error_code ignored; // the test counts its own entries
        std::filesystem::remove(profile / "errors.log", ignored);
    }
    std::expected<void, std::string> check(ScenarioContext& context, const FrameView& view) override {
        const bool gpu = context.gpu_available;
        const unsigned expected = gpu ? 3 : 1;
        unsigned logged = 0;
        std::ifstream log{context.profile / "errors.log"};
        for (std::string line; std::getline(log, line);) logged += line.find("Wstrzyknięty") != std::string::npos;
        const bool passed = ok_ && !context.fault_screen && checks_ == (gpu ? 7U : 3U)
                         && context.mode_failures == expected && logged == expected;
        std::ofstream report{context.profile / "mode-faults.txt"};
        report << "frames=" << view.frames << "\nchecks=" << checks_ << "\npassed=" << passed
               << "\nrecovered_modes=" << context.mode_failures << "\nexpected_modes=" << expected
               << "\nlogged=" << logged << "\ngpu_modes=" << gpu << '\n';
        if (!passed) return std::unexpected{"Test błędów trybów nie przeszedł. Sprawdź mode-faults.txt."};
        return {};
    }

private:
    void step(ScenarioContext& context, const FrameView&) override {
        auto check = [&](bool condition) {
            ok_ = condition && ok_;
            ++checks_;
        };
        const bool gpu = context.gpu_available;
        const bool menu = context.state.screen == ui::Screen::main_menu && !context.message.empty();
        switch (frame_) {
        case 10: context.perform(ui::Action::open_laboratory); break;
        case 20: context.fault_screen = ui::Screen::laboratory; break;
        case 24: check(menu && !context.laboratory && !context.cpu_world); break;
        case 50:
            if (gpu) context.perform(ui::Action::open_gpu);
            break;
        case 60:
            if (gpu) context.fault_screen = ui::Screen::gpu_laboratory;
            break;
        case 64:
            if (gpu) check(menu && !context.simulation);
            break;
        case 70:
            if (gpu) context.perform(ui::Action::open_mission);
            break;
        case 80:
            if (gpu) context.fault_screen = ui::Screen::gpu_mission;
            break;
        case 84:
            if (gpu) check(menu && !context.simulation);
            break;
        case 90:
            if (gpu) check(context.perform(ui::Action::open_gpu));
            break;
        case 100:
            if (gpu) check(context.perform(ui::Action::back));
            break;
        case 110: check(context.perform(ui::Action::open_laboratory)); break;
        case 120: check(context.perform(ui::Action::back)); break;
        default: break;
        }
    }
    bool ok_{true};
    unsigned checks_{};
};

} // namespace

std::unique_ptr<FrameObserver> make_window_script(const LaunchConfig& config, const std::filesystem::path& profile) {
    switch (config.smoke) {
    case SmokeScenario::cpu_laboratory: return std::make_unique<LaboratorySmoke>();
    case SmokeScenario::gpu_laboratory: return std::make_unique<GpuSmoke>(config, profile);
    case SmokeScenario::session: return std::make_unique<SessionSmoke>();
    case SmokeScenario::mission_quick: return std::make_unique<MissionQuick>();
    case SmokeScenario::mode_faults: return std::make_unique<ModeFaults>(profile);
    case SmokeScenario::none:
    case SmokeScenario::menu:
    case SmokeScenario::mission:
    case SmokeScenario::scene_benchmark: break;
    }
    return nullptr;
}

} // namespace strefa::app
