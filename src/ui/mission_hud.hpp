#pragma once
#include "game/session.hpp"
#include "gpu/mission.hpp"
#include "ui/view.hpp"

#include <cstdint>

namespace strefa::ui {

struct MissionHudInput {
    game::TickInput controls;
    bool reset{}, toggle_pause{};
};
// Read-only presentation DTO of the GPU mission. No UI dependency on the simulation owner.
struct MissionHudView {
    game::PlayerState actor;
    int ammo{}, stock{}, destroyed{};
    float reloading{}, fuel_limit{1.5F};
    std::uint64_t tick{};
    bool finished{};
    MissionHudView(const gpu::MissionSnapshot& state)
        : ammo(static_cast<int>(state.inventory[0])),
          stock(static_cast<int>(state.inventory[1])),
          destroyed(static_cast<int>(state.inventory[2])),
          reloading(state.timers[1]),
          tick(state.status[2]),
          finished(state.status[3] != 0) {
        actor.feet = {state.actor[0], state.actor[1]};
        actor.aim = {state.aim[0], state.aim[1]};
        actor.fuel = state.aim[2];
    }
    const game::PlayerState& player() const { return actor; }
    int score() const { return destroyed; }
    int ammunition() const { return ammo; }
    int grenade_stock() const { return stock; }
    float reload_remaining() const { return reloading; }
    float fuel_capacity() const { return fuel_limit; }
    std::uint64_t ticks() const { return tick; }
};
struct MissionHudFrame {
    Image world;
    MissionHudView game;
    MissionHudInput& input;
    bool focused{}, scene_mission{true}, paused{};
    int* vfx_quality{};
    int* lighting_quality{};
    std::array<float, 2> camera{};
    const ClockStats* clock{};
};
[[nodiscard]] Action draw_mission_hud(MissionHudFrame& hud, const FrameInfo& frame, const Fonts& fonts);

} // namespace strefa::ui
