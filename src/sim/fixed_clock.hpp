#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace strefa::sim {

// How a clock treats wall time it cannot simulate in real time. Simulation results depend
// on the tick index only, so a policy changes pacing, never the outcome of a given tick.
// - exact: every second of wall time is owed (up to RetainedSteps); replays and validation.
// - interactive: a stalled frame is clamped and leaves no backlog, an overload backlog is
//   bounded and a frame catches up by at most one tick beyond its own share, so a stall or an
//   overload never turns into a fast-forward or a long slow motion. The discarded time is
//   counted in dropped_seconds().
struct ClockPolicy {
    double max_frame_seconds{std::numeric_limits<double>::infinity()};
    double max_backlog_seconds{std::numeric_limits<double>::infinity()};
    // 0 keeps the fixed CatchUpLimit; otherwise a frame runs at most its nominal ticks + 1,
    // capped by this value (60 FPS at 120 Hz: 3, 30 FPS and slower: 4, 144 FPS: 2). A GPU-bound
    // frame grows with every tick, so a higher cap adds latency without adding speed.
    int adaptive_catch_up_max{};
};
inline constexpr ClockPolicy exact_clock{};
inline constexpr ClockPolicy interactive_clock{0.1, 0.25, 4};

// Simulation time never depends on the display refresh rate. Discard excess catch-up
// work after a stall so interaction stays responsive (the discarded time is reported).
template<int Frequency = 60, int CatchUpLimit = 8, int RetainedSteps = 0> class FixedStepClock {
public:
    static_assert(Frequency > 0 && CatchUpLimit > 0 && RetainedSteps >= 0);
    static constexpr double tick_seconds = 1.0 / Frequency;
    static constexpr int max_steps = CatchUpLimit;
    FixedStepClock() = default;
    explicit FixedStepClock(ClockPolicy policy) noexcept : policy_{policy} {}
    void set_policy(ClockPolicy policy) noexcept { policy_ = policy; }
    [[nodiscard]] const ClockPolicy& policy() const noexcept { return policy_; }
    int advance(double seconds, bool paused, int speed, bool single_step = false) noexcept {
        if (paused) {
            accumulator_ = 0;
            return single_step ? 1 : 0;
        }
        if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
        const double scale = static_cast<double>(std::clamp(speed, 1, 4));
        const double frame = std::min(seconds, policy_.max_frame_seconds);
        dropped_seconds_ += (seconds - frame) * scale;
        accumulator_ += frame * scale;
        int limit = max_steps;
        if (policy_.adaptive_catch_up_max > 0) {
            const double nominal = std::max(0.0, std::ceil(frame * scale / tick_seconds - 1e-6));
            limit = static_cast<int>(std::min(nominal + 1, static_cast<double>(policy_.adaptive_catch_up_max)));
        }
        const double due = std::floor((accumulator_ + 1e-10) / tick_seconds);
        const int steps = static_cast<int>(std::min(due, static_cast<double>(limit)));
        const double remaining = std::max(0.0, due - steps);
        // A clamped (stalled) frame owes nothing beyond what it runs: repaying it would be
        // exactly the fast-forward the interactive policy prevents. The backlog limit is
        // for sustained overload.
        const double backlog_steps =
                seconds > frame ? 0.0 : std::floor(policy_.max_backlog_seconds / tick_seconds + 1e-9);
        const double retained = std::min({remaining, static_cast<double>(RetainedSteps), backlog_steps});
        const double consumed = (due - retained) * tick_seconds;
        dropped_seconds_ += (remaining - retained) * tick_seconds;
        accumulator_ = std::max(0.0, accumulator_ - consumed);
        return steps;
    }
    // Return unsubmitted ticks after transport backpressure; no input was consumed.
    void defer(int steps) noexcept { accumulator_ += std::max(0, steps) * tick_seconds; }
    void reset() noexcept {
        accumulator_ = 0;
        dropped_seconds_ = 0;
    }
    [[nodiscard]] double dropped_seconds() const noexcept { return dropped_seconds_; }
    // Includes retained whole ticks and the fractional next tick; observation only.
    [[nodiscard]] double pending_seconds() const noexcept { return accumulator_; }
    [[nodiscard]] double fraction() const noexcept { return std::fmod(accumulator_, tick_seconds) / tick_seconds; }

private:
    ClockPolicy policy_{};
    double accumulator_{};
    double dropped_seconds_{};
};
using FixedClock = FixedStepClock<>;

// Snaps vsync-paced frame intervals to whole refresh periods, so a 60 Hz display feeds a
// 120 Hz clock exactly 2 ticks per frame instead of a jittered 1/2/3. The snapped-off
// remainder is carried into the next interval, so simulated time does not drift from wall
// time; an interval that is not close to a whole period passes through unchanged.
class VsyncSnap {
public:
    static constexpr double tolerance = 0.125; // of a period: 2.1 ms at 60 Hz, 0.87 ms at 144 Hz
    static constexpr int max_periods = 4;
    // Reported rates such as 59.94 Hz snap to the nominal integer rate when within 0.5 %.
    [[nodiscard]] static double period(double refresh_hz) noexcept {
        if (!std::isfinite(refresh_hz) || refresh_hz < 20) return 0;
        const double nominal = std::round(refresh_hz);
        return 1.0 / (std::abs(refresh_hz - nominal) <= nominal * 0.005 ? nominal : refresh_hz);
    }
    double operator()(double seconds, double refresh_hz) noexcept {
        const double p = period(refresh_hz);
        if (p <= 0 || !std::isfinite(seconds) || seconds <= 0) {
            residual_ = 0;
            return seconds;
        }
        const double total = seconds + residual_;
        const double periods = std::round(total / p);
        if (periods < 1 || periods > max_periods || std::abs(total - periods * p) > tolerance * p) {
            residual_ = 0;
            return total;
        }
        residual_ = total - periods * p;
        return periods * p;
    }
    void reset() noexcept { residual_ = 0; }
    [[nodiscard]] double residual() const noexcept { return residual_; }

private:
    double residual_{};
};

} // namespace strefa::sim
