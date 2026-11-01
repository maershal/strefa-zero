#pragma once
#include "gpu/gl_api.hpp"
#include "gpu/mission.hpp"

#include <array>
#include <optional>
#include <type_traits>

namespace strefa::gpu::detail {
struct ReadbackPacket {
    std::array<std::uint32_t, 8> counters{};
    MissionSnapshot mission;
};
static_assert(sizeof(ReadbackPacket) == 32 + sizeof(MissionSnapshot));
static_assert(std::is_trivially_copyable_v<ReadbackPacket>);

// Telemetry only. This queue must not transport gameplay events requiring an ack.
class ReadbackRing {
public:
    explicit ReadbackRing(Api& api) : gl_{api} {}
    ~ReadbackRing();
    ReadbackRing(const ReadbackRing&) = delete;
    ReadbackRing& operator=(const ReadbackRing&) = delete;
    void configure(ReadbackMode requested, bool debug_labels = false);
    bool submit(GLuint source, unsigned generation, std::uint64_t revision, std::uint64_t tick);
    std::optional<ReadbackPacket> poll(unsigned generation, std::uint64_t revision, std::uint64_t tick);
    const ReadbackStats& stats() const noexcept { return stats_; }
    PollTimings timings() const noexcept { return timings_; }
    bool persistent_supported() const noexcept { return gl_.persistent_storage; }

private:
    struct Slot {
        GLuint buffer{};
        GLsync fence{};
        const void* mapped{};
        unsigned generation{};
        std::uint64_t revision{}, tick{};
    };
    Api& gl_;
    std::array<Slot, 3> slots_{};
    unsigned head_{};
    ReadbackStats stats_;
    PollTimings timings_;
    void release();
};
} // namespace strefa::gpu::detail
