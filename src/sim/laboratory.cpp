#include "sim/laboratory.hpp"

#include "sim/recording.hpp"

#include <algorithm>
#include <chrono>

namespace strefa::sim {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}
bool same_probe(std::optional<Point> a, std::optional<Point> b) {
    return (!a && !b) || (a && b && a->x == b->x && a->y == b->y);
}
} // namespace
std::shared_ptr<LaboratorySnapshot> present_world(const World& world, RenderMode view, std::optional<Point> probe,
                                                  std::shared_ptr<const std::vector<Color>> existing) {
    auto snapshot = std::make_shared<LaboratorySnapshot>();
    if (existing)
        snapshot->pixels = std::move(existing);
    else {
        auto pixels = std::make_shared<std::vector<Color>>(grid_cells);
        world.render(std::span<Color, grid_cells>{pixels->data(), pixels->size()}, false, view);
        snapshot->pixels = std::move(pixels);
    }
    snapshot->report = world.statistics();
    snapshot->settings = world.config();
    snapshot->probe = probe;
    if (probe) snapshot->inspected = world.sample(*probe);
    for (int i = 0; i < World::chunk_count; ++i) snapshot->awake[i] = world.chunk_awake(i);
    snapshot->ready = true;
    return snapshot;
}
LaboratoryRunner::LaboratoryRunner(std::filesystem::path recordings, Scene initial, PhysicsConfig config) {
    auto empty = std::make_shared<LaboratorySnapshot>();
    empty->pixels = std::make_shared<std::vector<Color>>(grid_cells);
    snapshot_ = std::move(empty);
    worker_ = std::jthread([this, recordings = std::move(recordings), initial, config](std::stop_token stop) {
        run(stop, recordings, initial, config);
    });
}
LaboratoryRunner::~LaboratoryRunner() {
    stop();
}
void LaboratoryRunner::stop() {
    worker_.request_stop();
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
}
std::uint64_t LaboratoryRunner::submit(LaboratoryControl control, LaboratoryCommand command) {
    std::lock_guard lock{mutex_};
    control.speed = std::clamp(control.speed, 1, 4);
    if (!(control_ == control)) {
        control_ = control;
        ++control_revision_;
    }
    std::uint64_t ticket = next_ticket_;
    if (!command.empty()) {
        if (commands_.size() >= 512) {
            changed_.notify_all();
            return 0;
        }
        ticket = next_ticket_++;
        commands_.push_back({std::move(command), ticket});
    } else
        ticket = next_ticket_ - 1;
    changed_.notify_all();
    return ticket;
}
void LaboratoryRunner::set_active(bool active) {
    std::lock_guard lock{mutex_};
    if (control_.active != active) {
        control_.active = active;
        ++control_revision_;
        changed_.notify_all();
    }
}
std::shared_ptr<const LaboratorySnapshot> LaboratoryRunner::snapshot() const {
    std::lock_guard lock{mutex_};
    return snapshot_;
}
bool LaboratoryRunner::wait_for(std::uint64_t command, std::chrono::milliseconds timeout) {
    std::unique_lock lock{mutex_};
    return changed_.wait_for(lock, timeout, [&] {
        return snapshot_->ready && snapshot_->applied_command >= command && snapshot_->pending_manual_steps == 0;
    });
}
void LaboratoryRunner::run(std::stop_token stop, std::filesystem::path directory, Scene initial, PhysicsConfig config) {
    try {
        World world{0x57EF'A023U, config};
        world.load_scene(initial);
        std::optional<Recording> recording;
        LaboratoryControl previous;
        std::shared_ptr<const std::vector<Color>> pixels;
        std::uint64_t revision = 0, applied = 0, seen_control = 0;
        int manual = 0;
        double step_ms = 0, rate = 0, overload = 0;
        auto next_tick = Clock::now(), last_step = next_tick;
        bool first = true;
        while (!stop.stop_requested()) {
            LaboratoryControl control;
            std::deque<Queued> commands;
            std::uint64_t control_revision;
            {
                std::unique_lock lock{mutex_};
                auto pending = [&] { return first || !commands_.empty() || control_revision_ != seen_control; };
                if (!pending()) {
                    if (previous.active && !previous.paused && world.statistics().finite)
                        changed_.wait_until(lock, stop, next_tick, pending);
                    else
                        changed_.wait(lock, stop, pending);
                }
                if (stop.stop_requested()) break;
                control = control_;
                control_revision = control_revision_;
                commands.swap(commands_);
            }
            bool edited = false;
            bool publish = first || control_revision != seen_control;
            bool render = first || control.view != previous.view;
            if (control.active != previous.active || control.paused != previous.paused
                || control.speed != previous.speed) {
                next_tick = last_step = Clock::now();
                rate = 0;
            }
            for (const auto& queued : commands) {
                const auto& command = queued.command;
                if (command.scene || command.clear) {
                    if (recording) {
                        recording->finish();
                        recording.reset();
                    }
                    if (command.scene)
                        world.load_scene(*command.scene);
                    else
                        world.clear();
                    manual = 0;
                    overload = 0;
                    step_ms = 0;
                    rate = 0;
                    next_tick = last_step = Clock::now();
                    edited = true;
                }
                if (command.toggle_boundary) {
                    world.set_open_top(!world.config().open_top);
                    publish = true;
                }
                if (command.heat) {
                    world.inject_heat(command.heat->position, command.heat->energy);
                    edited = true;
                }
                if (command.stroke) {
                    const auto& s = *command.stroke;
                    world.paint(s.from, s.to, s.radius, s.material, s.erase);
                    edited = true;
                }
                if (command.toggle_recording) {
                    if (recording) {
                        recording->finish();
                        recording.reset();
                    } else
                        recording.emplace(directory / std::to_string(Clock::now().time_since_epoch().count()));
                    publish = true;
                }
                if (control.paused) manual = std::min(64, manual + std::clamp(command.manual_steps, 0, 8));
                applied = queued.ticket;
                publish = true;
            }
            const auto now = Clock::now();
            if (!control.paused) manual = 0;
            const bool running = control.active && !control.paused;
            const bool step = control.active && world.statistics().finite
                           && ((control.paused && manual > 0) || (running && now >= next_tick));
            const auto start = Clock::now();
            if (step) {
                world.step();
                step_ms = milliseconds(Clock::now() - start);
                if (control.paused) --manual;
                if (recording) recording->capture(world, step_ms, control.view);
                const double elapsed = std::chrono::duration<double>(Clock::now() - last_step).count();
                if (elapsed > 0)
                    rate = std::min(60.0 * control.speed, rate == 0 ? 1 / elapsed : rate * .85 + .15 / elapsed);
                last_step = Clock::now();
                render = publish = true;
            }
            render = render || edited;
            publish = publish || render || !same_probe(control.probe, previous.probe);
            if (publish) {
                const auto render_start = Clock::now();
                auto frame = present_world(world, control.view, control.probe, render ? nullptr : pixels);
                frame->render_ms = render ? milliseconds(Clock::now() - render_start) : 0;
                pixels = frame->pixels;
                frame->revision = ++revision;
                frame->applied_command = applied;
                frame->applied_control = control_revision;
                frame->step_ms = step_ms;
                frame->recording = recording.has_value();
                frame->pending_manual_steps = manual;
                frame->steps_per_second = rate;
                frame->overload_seconds = overload;
                {
                    std::lock_guard lock{mutex_};
                    snapshot_ = std::move(frame);
                }
                changed_.notify_all();
            }
            if (step) {
                const auto interval = std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>{tick_duration / control.speed});
                // At most one integration per iteration. A slow step never
                // starts an eight-step catch-up burst or blocks the UI thread.
                const auto finish = Clock::now();
                if (running)
                    overload += std::max(0.0, std::chrono::duration<double>(finish - start - interval).count())
                              * control.speed;
                next_tick = std::max(start + interval, finish);
            }
            if (!control.paused) manual = 0;
            previous = control;
            seen_control = control_revision;
            first = false;
            if (control.paused && manual > 0) { // Wake for another explicitly requested manual tick.
                std::lock_guard lock{mutex_};
                ++control_revision_;
            }
        }
        if (recording) recording->finish();
    } catch (const std::exception& error) {
        std::lock_guard lock{mutex_};
        auto failed = std::make_shared<LaboratorySnapshot>(*snapshot_);
        failed->error = error.what();
        snapshot_ = std::move(failed);
        changed_.notify_all();
    }
}
} // namespace strefa::sim
