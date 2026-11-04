#include "tools/renderdoc_capture.hpp"

#include <SDL3/SDL.h>
#include <renderdoc_app.h>

#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace strefa::tools {
namespace {

RENDERDOC_API_1_1_2* api = nullptr;
std::string capture_template;

pRENDERDOC_GetAPI find_get_api() {
    const char* requested = SDL_getenv("STREFA_RENDERDOC");
    const bool load = requested && *requested;
#ifdef _WIN32
    HMODULE module = GetModuleHandleA("renderdoc.dll");
    if (!module && load) {
        module = LoadLibraryA(std::string_view{requested} == "1" ? "renderdoc.dll" : requested);
        if (!module && std::string_view{requested} == "1")
            module = LoadLibraryA(R"(C:\Program Files\RenderDoc\renderdoc.dll)");
    }
    return module ? reinterpret_cast<pRENDERDOC_GetAPI>(
                            reinterpret_cast<void*>(GetProcAddress(module, "RENDERDOC_GetAPI")))
                  : nullptr;
#else
    void* module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!module && load)
        module = dlopen(std::string_view{requested} == "1" ? "librenderdoc.so" : requested, RTLD_NOW);
    return module ? reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module, "RENDERDOC_GetAPI")) : nullptr;
#endif
}

bool SDLCALL on_event(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_F11 && !event->key.repeat) {
        api->TriggerCapture();
        std::cerr << "RenderDoc: przechwytywanie następnej klatki (" << capture_template << "_*.rdc).\n";
    }
    return true;
}

} // namespace

void attach_renderdoc() {
    const auto get_api = find_get_api();
    if (!get_api || get_api(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&api)) != 1) {
        api = nullptr;
        if (SDL_getenv("STREFA_RENDERDOC"))
            std::cerr << "RenderDoc: nie udało się załadować biblioteki wskazanej przez STREFA_RENDERDOC.\n";
        return;
    }
    // F11 is the only trigger: RenderDoc's own F12/PrtScn keys stay free for the game and the system.
    api->SetCaptureKeys(nullptr, 0);
}

bool watch_renderdoc_hotkey(const std::filesystem::path& profile) {
    static bool watching = false;
    if (!api) return false;
    if (watching) return true;
    const auto folder = profile / "renderdoc";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    const auto text = (folder / "strefa").u8string();
    capture_template.assign(text.begin(), text.end());
    api->SetCaptureFilePathTemplate(capture_template.c_str());
    watching = SDL_AddEventWatch(on_event, nullptr);
    if (watching) std::cerr << "RenderDoc: F11 przechwytuje klatkę do " << capture_template << "_*.rdc.\n";
    return watching;
}

} // namespace strefa::tools
