#pragma once
#include "core/profiling.hpp"

// Tracy GPU zones: STREFA_GPU_ZONE(name) brackets the GL commands of its scope with two timestamp queries. The name
// may be a runtime string. Window creates the Tracy GPU context with its GL context and collects the results after
// every present; without STREFA_TRACY all of this compiles to nothing.
#ifdef STREFA_TRACY
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <cstring>
#include <new>
#include <type_traits>

namespace strefa::platform::detail {
// Entry points that TracyOpenGL.hpp calls by their gl* names. A separate table from gpu::detail::Api, so Tracy's own
// queries never show in the GL call counters of the profiles.
struct ProfilerGl {
    PFNGLGENQUERIESPROC GenQueries{};
    PFNGLGETQUERYIVPROC GetQueryiv{};
    PFNGLGETQUERYOBJECTIVPROC GetQueryObjectiv{};
    PFNGLGETQUERYOBJECTUI64VPROC GetQueryObjectui64v{};
    PFNGLGETINTEGER64VPROC GetInteger64v{};
    PFNGLQUERYCOUNTERPROC QueryCounter{};
    PFNGLGETSTRINGIPROC GetStringi{};
};
inline ProfilerGl profiler_gl;
} // namespace strefa::platform::detail

// clang-format off
#define glGenQueries strefa::platform::detail::profiler_gl.GenQueries
#define glGetQueryiv strefa::platform::detail::profiler_gl.GetQueryiv
#define glGetQueryObjectiv strefa::platform::detail::profiler_gl.GetQueryObjectiv
#define glGetQueryObjectui64v strefa::platform::detail::profiler_gl.GetQueryObjectui64v
#define glGetInteger64v strefa::platform::detail::profiler_gl.GetInteger64v
#define glQueryCounter strefa::platform::detail::profiler_gl.QueryCounter
#define glGetStringi strefa::platform::detail::profiler_gl.GetStringi
// clang-format on
#include <tracy/TracyOpenGL.hpp>
#undef glGenQueries
#undef glGetQueryiv
#undef glGetQueryObjectiv
#undef glGetQueryObjectui64v
#undef glGetInteger64v
#undef glQueryCounter
#undef glGetStringi

namespace strefa::platform {
// Zones outside a window's GL context (no context yet, or after it was destroyed) stay inactive.
inline bool gpu_zones_ready() noexcept {
    return tracy::GetGpuCtx().ptr != nullptr;
}
// Called with the new context current. One Tracy GPU context at a time: a second window reuses the first.
inline void create_gpu_zones() {
    if (gpu_zones_ready()) return;
    auto& gl = detail::profiler_gl;
    const auto load = [](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(SDL_GL_GetProcAddress(name));
        return function != nullptr;
    };
    if (!load(gl.GenQueries, "glGenQueries") || !load(gl.GetQueryiv, "glGetQueryiv")
        || !load(gl.GetQueryObjectiv, "glGetQueryObjectiv") || !load(gl.GetQueryObjectui64v, "glGetQueryObjectui64v")
        || !load(gl.GetInteger64v, "glGetInteger64v") || !load(gl.QueryCounter, "glQueryCounter")
        || !load(gl.GetStringi, "glGetStringi"))
        return;
    TracyGpuContext;
    TracyGpuContextName("OpenGL", 6);
}
// Called before the GL context is destroyed; pending queries die with the context.
inline void destroy_gpu_zones() noexcept {
    auto*& context = tracy::GetGpuCtx().ptr;
    if (!context) return;
    context->~GpuCtx();
    tracy::tracy_free(context);
    context = nullptr;
}
// After every present: reads the finished timestamps without waiting for the GPU.
inline void collect_gpu_zones() {
    if (gpu_zones_ready()) TracyGpuCollect;
}
} // namespace strefa::platform

#define STREFA_GPU_ZONE(name) TracyGpuZoneTransient(strefa_gpu_zone_, name, strefa::platform::gpu_zones_ready())
#else
namespace strefa::platform {
inline void create_gpu_zones() noexcept {}
inline void destroy_gpu_zones() noexcept {}
inline void collect_gpu_zones() noexcept {}
} // namespace strefa::platform
#define STREFA_GPU_ZONE(name) static_cast<void>(sizeof(name))
#endif
