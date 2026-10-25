#pragma once
#include <algorithm>
#include <cmath>

namespace strefa::sim {

// Simulation time never depends on the display refresh rate. Discard excess catch-up
// work after a stall so interaction stays responsive (the discarded time is reported).
class FixedClock {
public:
    static constexpr double tick_seconds = 1.0 / 60.0;
    static constexpr int max_steps = 8;
    int advance(double seconds, bool paused, int speed, bool single_step = false) noexcept {
        if (paused) { accumulator_ = 0; return single_step ? 1 : 0; }
        if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
        accumulator_ += seconds * static_cast<double>(std::clamp(speed, 1, 4));
        const double due = std::floor((accumulator_ + 1e-10) / tick_seconds);
        const int steps = static_cast<int>(std::min(due, static_cast<double>(max_steps)));
        const double consumed = due * tick_seconds;
        dropped_seconds_ += std::max(0.0, consumed - steps * tick_seconds);
        accumulator_ = std::max(0.0, accumulator_ - consumed);
        return steps;
    }
    void reset() noexcept { accumulator_ = 0; dropped_seconds_ = 0; }
    [[nodiscard]] double dropped_seconds() const noexcept { return dropped_seconds_; }
private:
    double accumulator_{};
    double dropped_seconds_{};
};

} // namespace strefa::sim
