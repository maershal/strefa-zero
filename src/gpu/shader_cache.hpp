#pragma once
#include <cstdint>
namespace strefa::gpu {
struct ShaderCacheStats {
    unsigned programs{}, source_builds{}, hits{}, misses{}, rejected{}, write_failures{};
    double compile_ms{}, load_ms{};
    bool enabled{}, supported{};
};
} // namespace strefa::gpu
