#pragma once
#include "gpu/device_context.hpp"
#include "gpu/vfx_pool.hpp"

#include <memory>
namespace strefa::gpu {
// Shared presentation resources. Declared before their borrowers and destroyed on the GL thread.
class RenderDevice {
public:
    RenderDevice(const std::filesystem::path& shaders, const std::filesystem::path& cache = {}) {
        library.configure_cache(cache);
        vfx.initialize(library, shaders);
    }
    detail::DeviceContext context;
    detail::ShaderLibrary library{context.gl};
    detail::VfxPool vfx{context.gl, context.state};
};
} // namespace strefa::gpu
