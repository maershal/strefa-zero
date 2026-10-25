#pragma once
#include "core/settings.hpp"
#include <array>
#include <cstdint>
#include <string_view>

struct ImFont;

namespace strefa::ui {

enum class Screen { main_menu, settings, preview, about, laboratory, arena };
enum class Action { none, open_preview, open_settings, open_about, open_laboratory, open_arena, back, apply, reset, quit };
struct LaboratoryFrame;
struct ArenaFrame;

struct Image { std::uintptr_t handle{}; int width{}; int height{}; };
struct Fonts { ImFont* body{}; ImFont* heading{}; };
struct FrameInfo {
    float scale{1.0F};
    float frame_ms{};
    std::string_view renderer;
    std::string_view gl_version;
    std::string_view message;
};
struct ViewState {
    Screen screen{Screen::main_menu};
    core::Settings applied;
    core::Settings draft;
    bool diagnostics{false};
};

[[nodiscard]] Action draw(ViewState& state, const FrameInfo& frame,
    const std::array<Image, 3>& backgrounds, const Fonts& fonts, LaboratoryFrame* laboratory = nullptr,
    ArenaFrame* arena = nullptr);

} // namespace strefa::ui
