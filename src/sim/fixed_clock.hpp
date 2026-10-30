#pragma once
#include <algorithm>
#include <cmath>

namespace strefa::sim {

// Simulation time never depends on the display refresh rate. Discard excess catch-up
// work after a stall so interaction stays responsive (the discarded time is reported).
template<int Frequency=60,int CatchUpLimit=8,int RetainedSteps=0>
class FixedStepClock {
public:
    static_assert(Frequency>0&&CatchUpLimit>0&&RetainedSteps>=0);
    static constexpr double tick_seconds = 1.0 / Frequency;
    static constexpr int max_steps = CatchUpLimit;
    int advance(double seconds, bool paused, int speed, bool single_step = false) noexcept {
        if (paused) { accumulator_ = 0; return single_step ? 1 : 0; }
        if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
        accumulator_ += seconds * static_cast<double>(std::clamp(speed, 1, 4));
        const double due = std::floor((accumulator_ + 1e-10) / tick_seconds);
        const int steps = static_cast<int>(std::min(due, static_cast<double>(max_steps)));
        const double remaining=std::max(0.0,due-steps);
        const double retained=std::min(remaining,static_cast<double>(RetainedSteps));
        const double consumed = (due-retained) * tick_seconds;
        dropped_seconds_ += (remaining-retained)*tick_seconds;
        accumulator_ = std::max(0.0, accumulator_ - consumed);
        return steps;
    }
    void reset() noexcept { accumulator_ = 0; dropped_seconds_ = 0; }
    [[nodiscard]] double dropped_seconds() const noexcept { return dropped_seconds_; }
    // Includes retained whole ticks and the fractional next tick; observation only.
    [[nodiscard]] double pending_seconds() const noexcept { return accumulator_; }
    [[nodiscard]] double fraction() const noexcept { return std::fmod(accumulator_,tick_seconds)/tick_seconds; }
private:
    double accumulator_{};
    double dropped_seconds_{};
};
using FixedClock=FixedStepClock<>;

} // namespace strefa::sim
