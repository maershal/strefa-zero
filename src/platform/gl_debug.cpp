#include "platform/gl_debug.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>

namespace strefa::platform {
namespace {
constexpr const char* phase_name(DebugPhase phase) noexcept {
    switch(phase){
    case DebugPhase::initialization:return "initialization";
    case DebugPhase::runtime:return "runtime";
    case DebugPhase::capture:return "capture";
    case DebugPhase::validation:return "validation";
    case DebugPhase::shutdown:return "shutdown";
    }
    return "unknown";
}
}
struct GlDebugLog::Impl {
    PFNGLDEBUGMESSAGECALLBACKPROC callback{};
    std::filesystem::path output;
    struct Entry {unsigned source{},type{},id{},severity{};std::uint64_t received_ns{};DebugPhase phase{};std::array<char,768> message{};};
    std::array<Entry,256> entries{};
    std::size_t count{};
    std::atomic<unsigned> dropped{};
    std::atomic<DebugPhase> phase{DebugPhase::initialization};
    std::uint64_t started_ns{SDL_GetTicksNS()};
    std::array<std::atomic<unsigned>,5> phase_messages{},phase_migrations{},phase_dropped{};
    std::mutex mutex;
    static void APIENTRY receive(GLenum source,GLenum type,GLuint id,GLenum severity,GLsizei length,const GLchar* message,const void* user){
        auto& self=*static_cast<Impl*>(const_cast<void*>(user));
        const auto phase=self.phase.load(std::memory_order_relaxed);
        const auto index=static_cast<std::size_t>(phase);
        ++self.phase_messages[index];
        if(id==131186)++self.phase_migrations[index];
        std::unique_lock lock{self.mutex,std::try_to_lock};
        if(!lock||self.count==self.entries.size()){++self.dropped;++self.phase_dropped[index];return;}
        auto& entry=self.entries[self.count++];entry.source=source;entry.type=type;entry.id=id;entry.severity=severity;
        entry.phase=phase;entry.received_ns=SDL_GetTicksNS()-self.started_ns;
        const auto size=std::min(entry.message.size()-1,static_cast<std::size_t>(std::max(0,length)));
        std::memcpy(entry.message.data(),message,size);entry.message[size]='\0';
    }
};
GlDebugLog::GlDebugLog(std::unique_ptr<Impl> impl):impl_{std::move(impl)}{}
void GlDebugLog::set_phase(DebugPhase phase) noexcept { impl_->phase.store(phase,std::memory_order_relaxed); }
std::unique_ptr<GlDebugLog> GlDebugLog::create(const std::filesystem::path& output){
    auto impl=std::make_unique<Impl>();impl->output=output;
    GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    if(major>4||(major==4&&minor>=3)||SDL_GL_ExtensionSupported("GL_KHR_debug"))
        impl->callback=reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(SDL_GL_GetProcAddress("glDebugMessageCallback"));
    if(impl->callback){
        glEnable(GL_DEBUG_OUTPUT);impl->callback(Impl::receive,impl.get());
        const auto control=reinterpret_cast<PFNGLDEBUGMESSAGECONTROLPROC>(SDL_GL_GetProcAddress("glDebugMessageControl"));
        if(control)control(GL_DONT_CARE,GL_DONT_CARE,GL_DEBUG_SEVERITY_NOTIFICATION,0,nullptr,GL_FALSE);
    }
    return std::unique_ptr<GlDebugLog>{new GlDebugLog{std::move(impl)}};
}
GlDebugLog::~GlDebugLog(){
    if(impl_->callback){impl_->callback(nullptr,nullptr);glDisable(GL_DEBUG_OUTPUT);}
    try{
        std::lock_guard lock{impl_->mutex};std::filesystem::create_directories(impl_->output.parent_path());
        std::ofstream out{impl_->output};out<<"callback_available="<<(impl_->callback!=nullptr)<<"\nmessages="<<impl_->count<<"\ndropped="<<impl_->dropped.load()<<'\n';
        out<<"started_sdl_ns="<<impl_->started_ns<<'\n';
        out<<"phase_semantics=callback_receipt; asynchronous delivery may describe earlier work\n";
        for(const auto phase:{DebugPhase::initialization,DebugPhase::runtime,DebugPhase::capture,DebugPhase::validation,DebugPhase::shutdown}){
            const auto index=static_cast<std::size_t>(phase);
            out<<"phase_"<<phase_name(phase)<<"_messages="<<impl_->phase_messages[index].load()
                <<"\nphase_"<<phase_name(phase)<<"_id131186="<<impl_->phase_migrations[index].load()
                <<"\nphase_"<<phase_name(phase)<<"_dropped="<<impl_->phase_dropped[index].load()<<'\n';
        }
        for(std::size_t i=0;i<impl_->count;++i){const auto& e=impl_->entries[i];out<<"source="<<e.source<<" type="<<e.type<<" id="<<e.id<<" severity="<<e.severity<<" received_ms="<<static_cast<double>(e.received_ns)/1e6<<" phase="<<phase_name(e.phase)<<" "<<e.message.data()<<'\n';}
    }catch(...){/* A diagnostic write must not throw during context teardown. */}
}
}
