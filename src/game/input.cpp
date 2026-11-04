#include "game/input.hpp"

namespace strefa::game {

void InputBuffer::submit(const TickInput& input) noexcept {
    pending_fire_ = pending_fire_ || input.fire;
    const bool jump = state_.jump || input.jump, reload = state_.reload || input.reload,
               grenade = state_.grenade || input.grenade;
    state_ = input;
    state_.jump = jump;
    state_.reload = reload;
    state_.grenade = grenade;
}
TickInput InputBuffer::consume() noexcept {
    auto result = state_;
    result.fire = result.fire || pending_fire_;
    pending_fire_ = false;
    state_.jump = state_.reload = state_.grenade = false;
    return result;
}

} // namespace strefa::game
