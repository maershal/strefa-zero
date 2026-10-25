#pragma once
#include "game/session.hpp"
#include "ui/view.hpp"

namespace strefa::ui {

struct ArenaInput { game::TickInput controls; bool reset{}; };
struct ArenaFrame {
    Image world, soldier, background, parts, weapons;
    const game::GameSession& game;
    ArenaInput& input;
    bool focused{};
};
[[nodiscard]] Action draw_arena(ArenaFrame& arena, const FrameInfo& frame, const Fonts& fonts);

} // namespace strefa::ui
