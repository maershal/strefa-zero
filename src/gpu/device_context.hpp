#pragma once
#include "gpu/gl_state.hpp"

#include <thread>

namespace strefa::gpu::detail {
// Borrows the SDL context owned by Window. Must be destroyed before Window;
// resources declared after this member die first, on this context's thread.
class DeviceContext {
public:
    DeviceContext() : context_{SDL_GL_GetCurrentContext()}, thread_{std::this_thread::get_id()} {
        if (!context_) throw std::runtime_error{"Urządzenie GPU wymaga bieżącego kontekstu OpenGL."};
        gl.load();
        GLint flags{};
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        debug_groups = (flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0;
    }
    void require_current() const {
        if (SDL_GL_GetCurrentContext() != context_ || std::this_thread::get_id() != thread_)
            throw std::logic_error{"Świat GPU użyty poza swoim kontekstem lub wątkiem."};
    }
    Api gl;
    GlStateTracker state{gl};
    bool debug_groups{};

private:
    SDL_GLContext context_;
    std::thread::id thread_;
};
} // namespace strefa::gpu::detail
