#pragma once
#include "gpu/profiling.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <stdexcept>
#include <tuple>

namespace strefa::gpu::detail {
enum class CallKind {
    other,
    dispatch,
    indirect,
    uniform,
    barrier,
    bind,
    base_bind,
    program,
    query_poll,
    query_result,
    fence_poll,
    upload,
    copy,
    read
};
constexpr CallKind call_kind(std::string_view name) {
    if (name == "DispatchCompute") return CallKind::dispatch;
    if (name == "DispatchComputeIndirect") return CallKind::indirect;
    if (name == "Uniform1i" || name == "Uniform1f" || name == "Uniform4f") return CallKind::uniform;
    if (name == "MemoryBarrier") return CallKind::barrier;
    if (name == "BindBuffer") return CallKind::bind;
    if (name == "BindBufferBase") return CallKind::base_bind;
    if (name == "UseProgram") return CallKind::program;
    if (name == "GetQueryObjectiv") return CallKind::query_poll;
    if (name == "GetQueryObjectui64v") return CallKind::query_result;
    if (name == "ClientWaitSync") return CallKind::fence_poll;
    if (name == "BufferSubData") return CallKind::upload;
    if (name == "CopyBufferSubData") return CallKind::copy;
    if (name == "GetBufferSubData") return CallKind::read;
    return CallKind::other;
}
template<class Function, CallKind kind> struct ObservedFunction {
    Function function{};
    CallCounters* counters{};
    template<class... Args> decltype(auto) operator()(Args... args) const {
        if (counters) {
            ++counters->api_calls;
            if constexpr (kind == CallKind::dispatch) ++counters->dispatches;
            if constexpr (kind == CallKind::indirect) {
                ++counters->dispatches;
                ++counters->indirect_dispatches;
            }
            if constexpr (kind == CallKind::uniform) ++counters->uniform_updates;
            if constexpr (kind == CallKind::barrier) ++counters->barriers;
            if constexpr (kind == CallKind::bind) ++counters->buffer_binds;
            if constexpr (kind == CallKind::base_bind) ++counters->base_binds;
            if constexpr (kind == CallKind::program) ++counters->program_uses;
            if constexpr (kind == CallKind::query_poll) ++counters->query_polls;
            if constexpr (kind == CallKind::query_result) ++counters->query_results;
            if constexpr (kind == CallKind::fence_poll) ++counters->fence_polls;
            if constexpr (kind == CallKind::upload)
                counters->upload_bytes += static_cast<std::uint64_t>(std::get<2>(std::tuple{args...}));
            if constexpr (kind == CallKind::copy)
                counters->copy_bytes += static_cast<std::uint64_t>(std::get<4>(std::tuple{args...}));
            if constexpr (kind == CallKind::read) {
                ++counters->read_calls;
                counters->read_bytes += static_cast<std::uint64_t>(std::get<2>(std::tuple{args...}));
            }
        }
        return function(args...);
    }
};
// clang-format off
#define GL_FUNCTIONS(X) \
    X(PFNGLOBJECTLABELPROC, ObjectLabel) \
    X(PFNGLPUSHDEBUGGROUPPROC, PushDebugGroup) \
    X(PFNGLPOPDEBUGGROUPPROC, PopDebugGroup) \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate) \
    X(PFNGLBINDSAMPLERPROC, BindSampler) \
    X(PFNGLBINDIMAGETEXTUREPROC, BindImageTexture) \
    X(PFNGLDRAWBUFFERSPROC, DrawBuffers) \
    X(PFNGLGETBUFFERPARAMETERIVPROC, GetBufferParameteriv) \
    X(PFNGLMAPBUFFERRANGEPROC, MapBufferRange) \
    X(PFNGLUNMAPBUFFERPROC, UnmapBuffer) \
    X(PFNGLGENBUFFERSPROC, GenBuffers) \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
    X(PFNGLBINDBUFFERPROC, BindBuffer) \
    X(PFNGLBUFFERDATAPROC, BufferData) \
    X(PFNGLBUFFERSUBDATAPROC, BufferSubData) \
    X(PFNGLBINDBUFFERBASEPROC, BindBufferBase) \
    X(PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData) \
    X(PFNGLCOPYBUFFERSUBDATAPROC, CopyBufferSubData) \
    X(PFNGLCREATESHADERPROC, CreateShader) \
    X(PFNGLSHADERSOURCEPROC, ShaderSource) \
    X(PFNGLCOMPILESHADERPROC, CompileShader) \
    X(PFNGLGETSHADERIVPROC, GetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLGETATTACHEDSHADERSPROC, GetAttachedShaders) \
    X(PFNGLGETSHADERSOURCEPROC, GetShaderSource) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) \
    X(PFNGLATTACHSHADERPROC, AttachShader) \
    X(PFNGLLINKPROGRAMPROC, LinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
    X(PFNGLUSEPROGRAMPROC, UseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, Uniform1i) \
    X(PFNGLUNIFORM1FPROC, Uniform1f) \
    X(PFNGLUNIFORM4FPROC, Uniform4f) \
    X(PFNGLDISPATCHCOMPUTEINDIRECTPROC, DispatchComputeIndirect) \
    X(PFNGLDRAWARRAYSINDIRECTPROC, DrawArraysIndirect) \
    X(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute) \
    X(PFNGLMEMORYBARRIERPROC, MemoryBarrier) \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, DrawArraysInstanced) \
    X(PFNGLGENQUERIESPROC, GenQueries) \
    X(PFNGLDELETEQUERIESPROC, DeleteQueries) \
    X(PFNGLBEGINQUERYPROC, BeginQuery) \
    X(PFNGLENDQUERYPROC, EndQuery) \
    X(PFNGLQUERYCOUNTERPROC, QueryCounter) \
    X(PFNGLGETQUERYOBJECTIVPROC, GetQueryObjectiv) \
    X(PFNGLGETQUERYOBJECTUI64VPROC, GetQueryObjectui64v) \
    X(PFNGLFENCESYNCPROC, FenceSync) \
    X(PFNGLCLIENTWAITSYNCPROC, ClientWaitSync) \
    X(PFNGLDELETESYNCPROC, DeleteSync)
// clang-format on
struct Api {
    ObservedFunction<PFNGLBUFFERSTORAGEPROC, CallKind::other> BufferStorage;
    bool persistent_storage{};
    CallCounters* observation{};
#define DECLARE(type, name) ObservedFunction<type, call_kind(#name)> name;
    GL_FUNCTIONS(DECLARE)
#undef DECLARE
    void load() {
        // clang-format off
#define LOAD(type, name) \
    name.function = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name.function) throw std::runtime_error{"Brak OpenGL: gl" #name};
        // clang-format on
        GL_FUNCTIONS(LOAD)
#undef LOAD
        GLint major{}, minor{};
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        persistent_storage =
                major > 4 || (major == 4 && minor >= 4) || SDL_GL_ExtensionSupported("GL_ARB_buffer_storage");
        if (persistent_storage) {
            BufferStorage.function = reinterpret_cast<PFNGLBUFFERSTORAGEPROC>(SDL_GL_GetProcAddress("glBufferStorage"));
            persistent_storage = BufferStorage.function != nullptr;
        }
    }
    void observe(CallCounters* counters) {
        observation = counters;
        BufferStorage.counters = counters;
#define OBSERVE(type, name) name.counters = counters;
        GL_FUNCTIONS(OBSERVE)
#undef OBSERVE
    }
};

// Counts presentation work without hiding it from total API/dispatch counters.
// The physical profiler can retain its numerical-kernel contract separately.
class PresentationCounterScope {
public:
    explicit PresentationCounterScope(Api& api)
        : counters{api.observation},
          before{counters ? counters->dispatches : 0} {}
    ~PresentationCounterScope() {
        if (counters) counters->presentation_dispatches += counters->dispatches - before;
    }

private:
    CallCounters* counters{};
    std::uint64_t before{};
};

} // namespace strefa::gpu::detail
