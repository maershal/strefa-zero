#pragma once

#include "core/settings.hpp"
#include <SDL3/SDL.h>
#include <expected>
#include <memory>
#include <string>

namespace strefa::platform {

class SdlSystem {
public:
    [[nodiscard]] static std::expected<SdlSystem, std::string> create();
    ~SdlSystem();
    SdlSystem(SdlSystem&& other) noexcept;
    SdlSystem(const SdlSystem&) = delete;
    SdlSystem& operator=(const SdlSystem&) = delete;
private:
    SdlSystem() = default;
    bool active_{false};
};

struct WindowDeleter { void operator()(SDL_Window* window) const noexcept; };
struct ContextDeleter { void operator()(SDL_GLContextState* context) const noexcept; };

class Window {
public:
    [[nodiscard]] static std::expected<Window, std::string> create(const core::Settings& settings);
    Window(Window&&) noexcept = default;
    Window(const Window&) = delete;
    Window& operator=(Window&&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] SDL_Window* native() const noexcept { return window_.get(); }
    [[nodiscard]] SDL_GLContext context() const noexcept { return context_.get(); }
    [[nodiscard]] float display_scale() const noexcept;
    [[nodiscard]] bool vsync_enabled() const noexcept { return vsync_; }
    [[nodiscard]] bool minimized() const noexcept;
    [[nodiscard]] const std::string& warning() const noexcept { return warning_; }
    [[nodiscard]] std::expected<void, std::string> apply_video(
        const core::Settings& next, const core::Settings& previous);
    void show() const;
    void clear() const;
    void present() const;

private:
    Window() = default;
    // Reverse destruction order: GL context must die before its native window.
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_GLContextState, ContextDeleter> context_;
    bool vsync_{false};
    std::string warning_;
};

} // namespace strefa::platform
