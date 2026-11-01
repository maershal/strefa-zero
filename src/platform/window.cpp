#include "platform/window.hpp"
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace strefa::platform {
namespace {
std::string sdl_error(std::string_view operation) {
    return std::string{operation} + ": " + SDL_GetError();
}
}

std::expected<SdlSystem, std::string> SdlSystem::create() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return std::unexpected{sdl_error("SDL_Init")};
    SdlSystem result;
    result.active_ = true;
    return result;
}
SdlSystem::SdlSystem(SdlSystem&& other) noexcept : active_{std::exchange(other.active_, false)} {}
SdlSystem::~SdlSystem() { if (active_) SDL_Quit(); }
void WindowDeleter::operator()(SDL_Window* window) const noexcept { SDL_DestroyWindow(window); }
void ContextDeleter::operator()(SDL_GLContextState* context) const noexcept { SDL_GL_DestroyContext(context); }

std::expected<Window, std::string> Window::create(const core::Settings& settings,bool prefer_compute,bool debug_context) {
    if (const auto checked = core::validate(settings); !checked) return std::unexpected{checked.error()};
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)
        || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, prefer_compute?4:3)
        || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)
        || !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)
        || !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0)
        || !SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0))
        return std::unexpected{sdl_error("Konfiguracja OpenGL")};
#ifndef NDEBUG
    debug_context=true;
#endif
    int context_flags=debug_context?SDL_GL_CONTEXT_DEBUG_FLAG:0;
#ifdef __APPLE__
    context_flags|=SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
#endif
    if(!SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,context_flags))return std::unexpected{sdl_error("Flagi kontekstu OpenGL")};
    Window result;
    const auto display = SDL_GetPrimaryDisplay();
    const float scale = std::max(1.0F, SDL_GetDisplayContentScale(display));
    int width = static_cast<int>(std::lround(static_cast<float>(settings.width) * scale));
    int height = static_cast<int>(std::lround(static_cast<float>(settings.height) * scale));
    SDL_Rect available{};
    if (SDL_GetDisplayUsableBounds(display, &available)) {
        width = std::min(width, std::max(640, available.w - 80));
        height = std::min(height, std::max(480, available.h - 100));
    }
    result.window_.reset(SDL_CreateWindow("STREFA ZERO — prototyp offline", width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN));
    if (!result.window_) return std::unexpected{sdl_error("Tworzenie okna")};
    SDL_SetWindowMinimumSize(result.window_.get(), 960, 540);
    result.context_.reset(SDL_GL_CreateContext(result.window_.get()));
    if(!result.context_&&prefer_compute){
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        result.context_.reset(SDL_GL_CreateContext(result.window_.get()));
        result.warning_="OpenGL 3.3: dostępna fizyka CPU; tryb GPU wymaga OpenGL 4.3.";
    }
    if (!result.context_) return std::unexpected{sdl_error("OpenGL 3.3 Core")};
    if (!SDL_GL_MakeCurrent(result.window_.get(), result.context_.get()))
        return std::unexpected{sdl_error("Aktywacja OpenGL")};
    result.vsync_ = settings.vsync && SDL_GL_SetSwapInterval(1);
    if (!result.vsync_) {
        SDL_GL_SetSwapInterval(0);
        if (settings.vsync) result.warning_ = "VSync jest niedostępny. Włączono limit klatek.";
    }
    SDL_SetWindowPosition(result.window_.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return result;
}

float Window::display_scale() const noexcept {
    return std::max(1.0F, SDL_GetWindowDisplayScale(window_.get()));
}
bool Window::minimized() const noexcept {
    return (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_MINIMIZED) != 0;
}
PresentationInfo Window::presentation_info() const noexcept {
    PresentationInfo result;
    result.window_flags=SDL_GetWindowFlags(window_.get());
    result.focused=(result.window_flags&SDL_WINDOW_INPUT_FOCUS)!=0;
    result.minimized=(result.window_flags&SDL_WINDOW_MINIMIZED)!=0;
    result.display_id=SDL_GetDisplayForWindow(window_.get());
    SDL_GetWindowSizeInPixels(window_.get(),&result.pixel_width,&result.pixel_height);
    if(result.display_id!=0){
        if(const auto* mode=SDL_GetCurrentDisplayMode(result.display_id)){
            result.mode_available=true;
            result.mode_width=mode->w;result.mode_height=mode->h;
            result.refresh_hz=mode->refresh_rate;
            result.refresh_numerator=mode->refresh_rate_numerator;
            result.refresh_denominator=mode->refresh_rate_denominator;
        }
    }
    result.vsync_enabled=vsync_;
    result.swap_interval_available=SDL_GL_GetSwapInterval(&result.swap_interval);
    return result;
}
std::expected<void, std::string> Window::apply_video(
    const core::Settings& next, const core::Settings& previous) {
    // WSLg/X11 may expose no swap-control extension at all. If VSync is
    // already disabled, changing unrelated settings must still work.
    const bool change_vsync = next.vsync != previous.vsync && next.vsync != vsync_;
    if (change_vsync && !SDL_GL_SetSwapInterval(next.vsync ? 1 : 0))
        return std::unexpected{sdl_error("Zmiana VSync")};
    if (next.width != previous.width || next.height != previous.height) {
        const float scale = display_scale();
        if (!SDL_SetWindowSize(window_.get(),
            static_cast<int>(std::lround(static_cast<float>(next.width) * scale)),
            static_cast<int>(std::lround(static_cast<float>(next.height) * scale)))) {
            if (change_vsync) SDL_GL_SetSwapInterval(vsync_ ? 1 : 0);
            return std::unexpected{sdl_error("Zmiana rozmiaru okna")};
        }
    }
    if (change_vsync) vsync_ = next.vsync;
    return {};
}
void Window::show() const { SDL_ShowWindow(window_.get()); }
void Window::clear() const {
    int width{}, height{};
    SDL_GetWindowSizeInPixels(window_.get(), &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.035F, 0.045F, 0.035F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
}
void Window::present() const { SDL_GL_SwapWindow(window_.get()); }

} // namespace strefa::platform
