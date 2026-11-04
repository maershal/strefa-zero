#pragma once
#include "gpu/gl_api.hpp"

#include <span>

namespace strefa::gpu::detail {
// What a system outside ParticleWorld needs to record passes on the shared particle state.
struct PassContext {
    std::span<const GLuint, 8> bindings; // SSBO 0-7: A/B, cells, index, terrain, free/active, mission, fluid terms
    unsigned bank{};                     // current particle copy in bindings 0/1; passes that swap update it
    bool conditional{};                  // ExecutionOptions::conditional_work (indirect, gated dispatches)
};
} // namespace strefa::gpu::detail
