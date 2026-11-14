#pragma once
#include <cstdint>
namespace strefa::gpu {
struct ShaderCacheStats {
    unsigned programs{}, source_builds{}, hits{}, misses{}, rejected{}, write_failures{};
    // compile_ms: time the library spent submitting and waiting for source builds, without progress_ms
    // (frames drawn by the progress callback while the driver may keep compiling in the background);
    // submit_ms: its part inside glCreateProgram/glCompileShader/glLinkProgram, where no frame can be drawn.
    // progress_gap_ms: the longest time without a progress callback while one is set (window not serviced).
    double compile_ms{}, load_ms{}, progress_ms{}, submit_ms{}, progress_gap_ms{};
    bool enabled{}, supported{}, parallel{};
};
} // namespace strefa::gpu
