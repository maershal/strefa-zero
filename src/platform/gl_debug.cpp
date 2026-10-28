#include "platform/gl_debug.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>

namespace strefa::platform {
struct GlDebugLog::Impl {
    PFNGLDEBUGMESSAGECALLBACKPROC callback{};
    std::filesystem::path output;
    struct Entry {unsigned source{},type{},id{},severity{};std::array<char,768> message{};};
    std::array<Entry,256> entries{};
    std::size_t count{};
    std::atomic<unsigned> dropped{};
    std::mutex mutex;
    static void APIENTRY receive(GLenum source,GLenum type,GLuint id,GLenum severity,GLsizei length,const GLchar* message,const void* user){
        auto& self=*static_cast<Impl*>(const_cast<void*>(user));
        std::unique_lock lock{self.mutex,std::try_to_lock};
        if(!lock||self.count==self.entries.size()){++self.dropped;return;}
        auto& entry=self.entries[self.count++];entry.source=source;entry.type=type;entry.id=id;entry.severity=severity;
        const auto size=std::min(entry.message.size()-1,static_cast<std::size_t>(std::max(0,length)));
        std::memcpy(entry.message.data(),message,size);entry.message[size]='\0';
    }
};
GlDebugLog::GlDebugLog(std::unique_ptr<Impl> impl):impl_{std::move(impl)}{}
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
        for(std::size_t i=0;i<impl_->count;++i){const auto& e=impl_->entries[i];out<<"source="<<e.source<<" type="<<e.type<<" id="<<e.id<<" severity="<<e.severity<<" "<<e.message.data()<<'\n';}
    }catch(...){/* A diagnostic write must not throw during context teardown. */}
}
}
