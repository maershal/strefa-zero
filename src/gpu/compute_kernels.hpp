#pragma once
#include "gpu/gl_api.hpp"
#include "gpu/profiling.hpp"

#include <filesystem>
#include <span>
#include <string>

namespace strefa::gpu::detail {
class ShaderLibrary;
// One compute program of ParticleWorld with the uniform locations its passes set.
struct ComputeKernel {
    GLuint id{};
    unsigned local_size{};
    bool particle_restrict{};
    GLint operation{-1}, dirty{-1}, substep{-1}, stroke{-1}, radius{-1}, material{-1}, input{-1}, coupling{-1},
            test_count{-1};
    GLint hot_read{-1}, hot_write{-1}, hot_valid{-1};
    int hot_read_value{-2}, hot_write_value{-2}, hot_valid_value{-2};
    unsigned family{};
    int dirty_value{-1};
};
struct ComputeKernelSet {
    std::span<ComputeKernel> granular, liquids, neighbors, mission, coupling;
};
// Submits every world kernel to the library first, waits for the batch, then reads the
// uniform locations, so a driver with parallel compilation builds them concurrently.
// Programs, their order and their defines are the same as with one-by-one compilation.
void compile_kernels(ShaderLibrary& library, Api& gl, const ExecutionOptions& execution,
                     const std::string& layout_defines, const std::filesystem::path& shaders, bool debug_groups,
                     const ComputeKernelSet& kernels);
} // namespace strefa::gpu::detail
