#include "app/clock_stats.hpp"
#include "game/input.hpp"
#include "sim/fixed_clock.hpp"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
using strefa::sim::FixedStepClock;
using GpuClock = FixedStepClock<120, 4, 2147483647>;
constexpr double tick = GpuClock::tick_seconds;

void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error{what};
}

// Interactive policy: a 5 s stall is dropped, not repaid as a fast-forward.
void interactive_stall() {
    GpuClock clock{strefa::sim::interactive_clock};
    int ticks = 0;
    for (int i = 0; i < 120; ++i) ticks += clock.advance(1.0 / 60, false, 1);
    check(ticks == 240 && clock.dropped_seconds() == 0, "interactive clock must keep up at 60 FPS");
    const int stall_ticks = clock.advance(5.0, false, 1);
    check(stall_ticks <= strefa::sim::interactive_clock.adaptive_catch_up_max, "a stalled frame runs a bounded burst");
    const double lost = clock.dropped_seconds();
    check(lost >= 5.0 - strefa::sim::interactive_clock.max_backlog_seconds && lost <= 5.0,
          "a stall must be counted as lost time");
    check(clock.pending_seconds() < tick, "a stall must leave no backlog");
    ticks = 0;
    for (int i = 0; i < 120; ++i) ticks += clock.advance(1.0 / 60, false, 1);
    check(ticks * tick / 2.0 <= 1.1, "no speed-up above 1.1x after a stall");
    check(std::abs(ticks * tick - 2.0) <= tick, "real time resumes after a stall");
    // The exact policy keeps the whole debt (I06 behaviour for replay and validation).
    GpuClock exact;
    exact.advance(5.0, false, 1);
    check(exact.dropped_seconds() == 0 && std::abs(exact.pending_seconds() - (5.0 - 4 * tick)) < 1e-9,
          "exact policy must retain the stall");
}

// Sustained overload: backlog stays bounded, the excess is counted, accounting adds up.
void interactive_overload() {
    GpuClock clock{strefa::sim::interactive_clock};
    double fed = 0;
    int ticks = 0;
    for (int i = 0; i < 600; ++i) {
        // A 0.06 s frame (17 FPS) owes 7.2 ticks of 120 Hz; the interactive clock runs at most 4.
        const double frame = 0.06;
        fed += frame;
        ticks += clock.advance(frame, false, 1);
        check(clock.pending_seconds() <= strefa::sim::interactive_clock.max_backlog_seconds + tick,
              "overload backlog must stay bounded");
    }
    check(std::abs(fed - (ticks * tick + clock.dropped_seconds() + clock.pending_seconds())) < 1e-6,
          "fed time = executed + dropped + pending");
    // Adaptive catch-up: 30/60/144 FPS keep real time at 120 Hz.
    for (int fps : {30, 60, 144}) {
        GpuClock paced{strefa::sim::interactive_clock};
        int count = 0;
        for (int i = 0; i < fps * 10; ++i) count += paced.advance(1.0 / fps, false, 1);
        check(count == 1200 && paced.dropped_seconds() == 0, "30/60/144 FPS must stay real time");
    }
}

// VSync snapping: jittered present intervals feed a constant tick count without drift.
void vsync_snapping() {
    for (const double hz : {60.0, 59.94, 144.0, 120.0}) {
        strefa::sim::VsyncSnap snap;
        GpuClock clock{strefa::sim::interactive_clock};
        std::mt19937 random{7};
        std::uniform_real_distribution<double> jitter{-0.001, 0.001};
        const double period = 1.0 / hz;
        const int frames = static_cast<int>(hz * 600);
        double previous = 0, wall = 0, fed = 0;
        int irregular = 0, first = -1;
        for (int i = 1; i <= frames; ++i) {
            // Presents are locked to vblank; the measured timestamp jitters around it.
            const double now = i * period + jitter(random);
            const double interval = now - previous;
            previous = now;
            wall += interval;
            const double snapped = snap(interval, hz);
            fed += snapped;
            const int steps = clock.advance(snapped, false, 1);
            if (i > 2 && first >= 0 && steps != first) ++irregular;
            if (i == 2) first = steps;
        }
        check(std::abs(fed - wall) <= 1.0 / std::round(hz) * strefa::sim::VsyncSnap::tolerance + 1e-9,
              "snapping must not drift from wall time");
        if (hz == 60.0 || hz == 120.0) check(irregular == 0, "60/120 Hz must give a constant tick count per frame");
        // 59.94 Hz snaps to 60 Hz: one extra tick per ~16 s, not a jitter of 1/2/3.
        if (hz == 59.94) check(irregular <= 600 / 16 * 2 + 2, "59.94 Hz must stay nearly constant");
    }
    strefa::sim::VsyncSnap snap;
    check(snap(0.05, 60) > 0.049 && snap(0.05, 60) < 0.051, "a missed frame snaps to whole periods");
    strefa::sim::VsyncSnap fresh;
    check(fresh(0.2, 60) == 0.2 && fresh.residual() == 0, "intervals over 4 periods pass through");
    check(snap(1.0 / 60, 0) == 1.0 / 60, "unknown refresh rate passes through");
}

void hud_monitors() {
    strefa::app::GpuSlowdownMonitor monitor;
    double t = 0;
    for (; t < 10; t += 1.0 / 60) monitor.sample(t, 2.0);
    for (double end = t + 3; t < end; t += 1.0 / 60) monitor.sample(t, 2.1);
    check(monitor.baseline_ms() == 2.0 && !monitor.slowed(), "steady GPU time must not warn");
    for (double end = t + 3; t < end; t += 1.0 / 60) monitor.sample(t, 4.5);
    check(monitor.slowed(), "GPU time above 2x the first 10 s must warn");
    for (double end = t + 3; t < end; t += 1.0 / 60) monitor.sample(t, 2.0);
    check(!monitor.slowed(), "warning must clear when GPU time recovers");
    monitor.reset();
    for (t = 0; t < 1; t += 1.0 / 60) monitor.sample(t, 9.0);
    check(!monitor.slowed() && monitor.baseline_ms() == 0, "no warning before a baseline exists");

    strefa::app::SpeedMeter meter;
    check(meter.speed(tick) < 0, "speed is unknown without samples");
    for (int i = 0; i <= 120; ++i) meter.sample(i / 60.0, static_cast<std::uint64_t>(i) * 2);
    check(std::abs(meter.speed(tick) - 1.0) < 1e-9, "2 ticks per 60 Hz frame is 1.00x");
}
} // namespace

int main() {
    try {
        interactive_stall();
        interactive_overload();
        vsync_snapping();
        hud_monitors();
        auto check = [](bool ok) { ::check(ok, "Fixed clock/input contract failed"); };
        strefa::sim::FixedStepClock<120, 4, 24> clock;
        int count = clock.advance(.1, false, 1);
        check(count == 4 && clock.dropped_seconds() == 0);
        check(std::abs(clock.pending_seconds() - 8.0 / 120) < 1e-9);
        count += clock.advance(0, false, 1);
        count += clock.advance(0, false, 1);
        check(count == 12 && clock.dropped_seconds() == 0 && clock.fraction() < 1);
        clock.reset();
        check(clock.pending_seconds() == 0);
        check(clock.advance(1, false, 1) == 4);
        check(std::abs(clock.pending_seconds() - 24.0 / 120) < 1e-9);
        check(std::abs(clock.dropped_seconds() - 92.0 / 120) < 1e-9);
        for (int i = 0; i < 6; ++i) check(clock.advance(0, false, 1) == 4);
        check(clock.advance(0, false, 1) == 0 && clock.pending_seconds() < 1e-9);
        clock.reset();
        clock.advance(.1, false, 1);
        check(clock.advance(0, true, 1) == 0 && clock.advance(0, false, 1) == 0);
        for (int fps : {30, 60, 144}) {
            clock.reset();
            int ticks = 0;
            for (int i = 0; i < fps * 10; ++i) ticks += clock.advance(1.0 / fps, false, 1);
            check(ticks == 1200 && clock.dropped_seconds() == 0);
        }
        strefa::sim::FixedStepClock<120, 4> legacy;
        check(legacy.advance(.1, false, 1) == 4 && std::abs(legacy.dropped_seconds() - 8.0 / 120) < 1e-9);
        strefa::game::InputBuffer input;
        strefa::game::TickInput press;
        press.jump = true;
        press.grenade = true;
        press.reload = true;
        press.fire = true;
        input.submit(press);
        check(input.consume().jump);
        const auto next = input.consume();
        check(!next.jump && !next.grenade && !next.reload && next.fire);
        input.submit(press);
        input.clear();
        check(!input.consume().grenade);
        std::cout
                << "PASS: bounded backlog, overflow accounting, pause, 30/60/144 FPS, single-shot input, interactive stall/overload, vsync snapping, HUD monitors\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
