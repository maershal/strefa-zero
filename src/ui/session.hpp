#pragma once
#include "ui/view.hpp"

#include <expected>
#include <filesystem>
#include <string>

struct ImGuiContext;
union SDL_Event;
namespace strefa::platform {
class Window;
} // namespace strefa::platform

namespace strefa::ui {

class Session {
public:
    [[nodiscard]] static std::expected<Session, std::string> create(const platform::Window& window,
                                                                    const std::filesystem::path& assets);
    ~Session();
    Session(Session&& other) noexcept;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session& operator=(Session&&) = delete;

    void process_event(const SDL_Event& event) const;
    void new_frame();
    void render() const;
    void set_scale(float display_scale, float user_scale);
    [[nodiscard]] float scale() const noexcept { return scale_; }
    [[nodiscard]] Fonts fonts() const noexcept { return fonts_; }

private:
    Session() = default;
    ImGuiContext* context_{};
    bool platform_ready_{false};
    bool renderer_ready_{false};
    Fonts fonts_;
    float scale_{};
};

} // namespace strefa::ui
