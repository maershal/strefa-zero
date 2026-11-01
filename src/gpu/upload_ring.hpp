#pragma once
#include "gpu/gl_api.hpp"

#include <array>

namespace strefa::gpu::detail {
// Host writes only staging in staged/immutable modes. A busy slot is orphaned,
// never overwritten or waited on in a CPU loop. GL retains its old store until
// the queued copy completes. Storage placement remains a driver decision.
class UploadRing {
public:
    explicit UploadRing(Api& gl) : gl_{gl} {}
    ~UploadRing();
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;
    void configure(DeviceStorageMode requested);
    void allocate(GLuint destination, GLsizeiptr bytes);
    void write(GLuint destination, GLintptr offset, GLsizeiptr bytes, const void* data);
    const UploadStats& stats() const noexcept { return stats_; }

private:
    struct Slot {
        GLuint buffer{};
        GLsync fence{};
        GLsizeiptr capacity{};
    };
    Api& gl_;
    std::array<Slot, 3> slots_{};
    unsigned cursor_{};
    UploadStats stats_;
};
} // namespace strefa::gpu::detail
