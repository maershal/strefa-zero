#pragma once
#include <algorithm>
#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>

namespace strefa::sim::detail {
// A bounded, persistent team. Each job owns disjoint output ranges; reductions
// are done in a fixed order by the caller. No task allocations, nested dispatch,
// spinning, or per-step thread creation. The calling physics thread is lane 0.
class Executor {
public:
    Executor() = default;
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    ~Executor() {
        stopping_.store(true, std::memory_order_relaxed);
        epoch_.fetch_add(1, std::memory_order_release);
        epoch_.notify_all();
        for (auto& worker : workers_) worker.join();
    }
    void configure(unsigned requested) {
        // Configure once, before the first job. Automatic mode leaves capacity
        // for UI/rendering and avoids relying on all SMT siblings.
        const unsigned automatic = std::clamp(std::thread::hardware_concurrency() / 2, 1U, 2U);
        lanes_ = std::clamp(requested ? requested : automatic, 1U, 4U);
        for (unsigned lane = 1; lane < lanes_; ++lane) workers_.emplace_back([this, lane] { worker_loop(lane); });
    }
    [[nodiscard]] unsigned lanes() const noexcept { return lanes_; }
    template<class F> void run(int count, int minimum_per_lane, F&& function) {
        if (lanes_ == 1 || count < minimum_per_lane * static_cast<int>(lanes_)) {
            function(0, count);
            return;
        }
        count_ = count;
        context_ = &function;
        invoke_ = [](void* context, int first, int last) noexcept {
            (*static_cast<std::remove_reference_t<F>*>(context))(first, last);
        };
        remaining_.store(lanes_ - 1, std::memory_order_relaxed);
        epoch_.fetch_add(1, std::memory_order_release);
        epoch_.notify_all();
        invoke_(context_, 0, count_ / static_cast<int>(lanes_));
        auto left = remaining_.load(std::memory_order_acquire);
        while (left != 0) {
            remaining_.wait(left, std::memory_order_acquire);
            left = remaining_.load(std::memory_order_acquire);
        }
    }

private:
    void worker_loop(unsigned lane) noexcept {
        unsigned observed = 0;
        for (;;) {
            epoch_.wait(observed, std::memory_order_acquire);
            observed = epoch_.load(std::memory_order_acquire);
            if (stopping_.load(std::memory_order_relaxed)) return;
            const int first = count_ * static_cast<int>(lane) / static_cast<int>(lanes_);
            const int last = count_ * static_cast<int>(lane + 1) / static_cast<int>(lanes_);
            invoke_(context_, first, last);
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) remaining_.notify_one();
        }
    }
    unsigned lanes_{1};
    std::vector<std::thread> workers_;
    std::atomic<unsigned> epoch_{0}, remaining_{0};
    std::atomic<bool> stopping_{false};
    int count_{};
    void* context_{};
    void (*invoke_)(void*, int, int) noexcept {};
};
} // namespace strefa::sim::detail
