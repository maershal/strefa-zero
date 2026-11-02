#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace strefa::app {

// Simulated seconds per wall second over a sliding window (HUD "Tempo ×1.00").
class SpeedMeter {
public:
    static constexpr double window_seconds = 1.0;
    void reset() noexcept { samples_.clear(); }
    // wall: monotonic seconds; ticks: total executed ticks of the scene.
    void sample(double wall, std::uint64_t ticks) {
        if (!samples_.empty() && ticks < samples_.back().second) samples_.clear();
        samples_.emplace_back(wall, ticks);
        while (samples_.size() > 2 && wall - samples_[1].first >= window_seconds) samples_.pop_front();
    }
    // -1 until the window holds at least half a second of samples.
    [[nodiscard]] double speed(double tick_seconds) const noexcept {
        if (samples_.size() < 2) return -1;
        const double wall = samples_.back().first - samples_.front().first;
        if (wall < window_seconds / 2) return -1;
        return static_cast<double>(samples_.back().second - samples_.front().second) * tick_seconds / wall;
    }

private:
    std::deque<std::pair<double, std::uint64_t>> samples_;
};

// Flags a GPU that became much slower than at the start of the scene (power saving,
// thermal throttling). Baseline = median GPU time per tick over the first 10 s of running
// scene time; the warning holds while the median of the last 2 s exceeds twice the baseline.
class GpuSlowdownMonitor {
public:
    static constexpr double baseline_seconds = 10, recent_seconds = 2, factor = 2;
    static constexpr std::size_t min_baseline_samples = 30, min_recent_samples = 10;
    void reset() {
        baseline_.clear();
        recent_.clear();
        baseline_ms_ = 0;
    }
    // scene_seconds: running time of the scene since reset(); gpu_tick_ms: one new timer sample.
    void sample(double scene_seconds, double gpu_tick_ms) {
        if (!(gpu_tick_ms > 0)) return;
        if (scene_seconds < baseline_seconds) {
            baseline_.push_back(gpu_tick_ms);
            return;
        }
        if (baseline_ms_ == 0 && baseline_.size() >= min_baseline_samples) baseline_ms_ = median(baseline_);
        recent_.emplace_back(scene_seconds, gpu_tick_ms);
        while (!recent_.empty() && scene_seconds - recent_.front().first > recent_seconds) recent_.pop_front();
    }
    [[nodiscard]] double baseline_ms() const noexcept { return baseline_ms_; }
    [[nodiscard]] bool slowed() const {
        if (baseline_ms_ <= 0 || recent_.size() < min_recent_samples) return false;
        std::vector<double> values;
        values.reserve(recent_.size());
        for (const auto& [time, ms] : recent_) values.push_back(ms);
        return median(std::move(values)) > factor * baseline_ms_;
    }

private:
    static double median(std::vector<double> values) {
        const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
        std::ranges::nth_element(values, middle);
        return *middle;
    }
    std::vector<double> baseline_;
    std::deque<std::pair<double, double>> recent_;
    double baseline_ms_{};
};

} // namespace strefa::app
