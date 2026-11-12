#pragma once
// Benchmark-only: included by thermal_benchmark.cpp and, through world.cpp, by the benchmark .inl files. Production
// translation units include gpu/diagnostic_read.hpp alone.
#include "gpu/diagnostic_read.hpp"
#include "platform/gl_debug.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace strefa::gpu::detail {
// Driver messages 131186 per benchmark segment, split into the ticks and frames it measures (debug phase `runtime`)
// and the setup and diagnostic reads around them (phase `validation`). Restores the phase it found on destruction.
// The split is approximate: GlDebugLog uses asynchronous debug output, so the driver may deliver a message after the
// switch that follows the work it describes. Each switch drains the GPU first (glFinish), which makes that unlikely
// but is not guaranteed by the GL specification. Without --gl-debug only readback() drains, where the benchmarks
// already wait for the GPU, so the measured segments run as before.
class SegmentMigrations {
public:
    SegmentMigrations() : restore_{platform::gl_debug_phase.load()}, last_{platform::gl_debug_migrations.load()} {
        platform::gl_debug_phase.store(phase_);
    }
    // Own CSV `path` with the given key columns followed by the two count columns; rows come from row().
    SegmentMigrations(const std::filesystem::path& path, std::string_view columns) : SegmentMigrations{} {
        csv_.open(path);
        if (!csv_ || !(csv_ << columns << ',' << header << '\n'))
            throw std::runtime_error{"Nie można zapisać liczników komunikatów 131186."};
    }
    ~SegmentMigrations() { platform::gl_debug_phase.store(restore_); }
    SegmentMigrations(const SegmentMigrations&) = delete;
    SegmentMigrations& operator=(const SegmentMigrations&) = delete;
    void runtime() { enter(platform::DebugPhase::runtime, false); }
    // Waits for the GPU like glFinish, also without --gl-debug, so it can replace a benchmark's own drain.
    void readback() { enter(platform::DebugPhase::validation, true); }
    // CSV fields "runtime,readback" since the previous call; both empty without --gl-debug (no driver callback).
    std::string take() {
        enter(phase_, false);
        const auto fields =
                attached() ? std::to_string(counts_[0]) + ',' + std::to_string(counts_[1]) : std::string{","};
        counts_ = {};
        return fields;
    }
    // One row of the own CSV: the key fields, then take().
    template<class... Fields> void row(const Fields&... fields) {
        ((csv_ << fields << ','), ...);
        csv_ << take() << '\n';
    }
    static constexpr const char* header = "id131186_runtime,id131186_readback";

private:
    static bool attached() { return platform::gl_debug_attached.load(); }
    void enter(platform::DebugPhase next, bool drain) {
        if (drain || attached()) glFinish();
        const auto now = platform::gl_debug_migrations.load();
        counts_[phase_ == platform::DebugPhase::runtime ? 0 : 1] += now - last_;
        last_ = now;
        phase_ = next;
        platform::gl_debug_phase.store(next);
    }
    platform::DebugPhase restore_, phase_{platform::DebugPhase::validation};
    std::uint64_t last_;
    std::array<std::uint64_t, 2> counts_{};
    std::ofstream csv_;
};
} // namespace strefa::gpu::detail
