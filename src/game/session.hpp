#pragma once

namespace strefa::game {

struct Vec2 {
    float x{}, y{};
    constexpr Vec2 operator+(Vec2 b) const noexcept { return {x + b.x, y + b.y}; }
    constexpr Vec2 operator-(Vec2 b) const noexcept { return {x - b.x, y - b.y}; }
    constexpr Vec2 operator*(float n) const noexcept { return {x * n, y * n}; }
};
struct TickInput {
    float move{};
    Vec2 aim{160, 96};
    bool jump{}, jet{}, fire{}, reload{}, grenade{};
};
// One-shot actions survive render frames with no simulation tick, then are consumed
// once. Held controls are replaced every frame, including focus-loss neutral input.
class InputBuffer {
public:
    void submit(const TickInput& input) noexcept;
    [[nodiscard]] TickInput consume() noexcept;
    [[nodiscard]] TickInput peek() const noexcept {
        auto input = state_;
        input.fire = input.fire || pending_fire_;
        return input;
    }
    void clear() noexcept {
        state_ = {};
        pending_fire_ = false;
    }

private:
    TickInput state_;
    bool pending_fire_{};
};
// Actor state shown by the HUD and posed by `animation.hpp` (shared with soldier_pose.glsl).
struct PlayerState {
    Vec2 feet{24, 188}, velocity{}, aim{1, 0};
    float fuel{1.5F};
    float stride{}, recoil{}, landing{}, airborne_time{}, stride_blend{};
    bool grounded{true}, jetting{};
};

} // namespace strefa::game
