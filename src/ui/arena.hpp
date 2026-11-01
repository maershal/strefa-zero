#pragma once
#include "game/session.hpp"
#include "ui/view.hpp"
#include "gpu/mission.hpp"

namespace strefa::ui {

struct ArenaInput { game::TickInput controls; bool reset{},toggle_pause{}; };
// Read-only presentation DTO. No UI dependency on the simulation owner/backend.
struct ArenaView {
    game::PlayerState actor;
    std::span<const game::Projectile> projectiles, thrown;
    std::span<const game::Target> objectives;
    std::span<const game::Effect> visuals;
    int ammo{},stock{},destroyed{};float reloading{},fuel_limit{1.5F};
    std::uint64_t tick{};bool gpu{},finished{};
    ArenaView(const game::GameSession& session)
        : actor(session.player()),projectiles(session.bullets()),thrown(session.grenades()),
          objectives(session.targets()),visuals(session.effects()),ammo(session.ammunition()),
          stock(session.grenade_stock()),destroyed(session.score()),reloading(session.reload_remaining()),
          fuel_limit(session.fuel_capacity()),tick(session.ticks()),finished(session.score()==3) {}
    ArenaView(const gpu::MissionSnapshot& state) : ammo(static_cast<int>(state.inventory[0])),
        stock(static_cast<int>(state.inventory[1])),destroyed(static_cast<int>(state.inventory[2])),
        reloading(state.timers[1]),tick(state.status[2]),gpu(true),finished(state.status[3]!=0) {
        actor.feet={state.actor[0],state.actor[1]};actor.aim={state.aim[0],state.aim[1]};actor.fuel=state.aim[2];
    }
    const game::PlayerState& player()const{return actor;}
    auto bullets()const{return projectiles;} auto grenades()const{return thrown;}
    auto targets()const{return objectives;} auto effects()const{return visuals;}
    int score()const{return destroyed;} int ammunition()const{return ammo;} int grenade_stock()const{return stock;}
    float reload_remaining()const{return reloading;} float fuel_capacity()const{return fuel_limit;}
    std::uint64_t ticks()const{return tick;}
};
struct ArenaFrame {
    Image world, soldier, background, parts, weapons;
    ArenaView game;
    ArenaInput& input;
    bool focused{},scene_mission{true},paused{};
    Image vfx_front{};bool shared_vfx{},effects_enabled{true};int* vfx_quality{};int* lighting_quality{};std::array<float,2> camera{};
};
[[nodiscard]] Action draw_arena(ArenaFrame& arena, const FrameInfo& frame, const Fonts& fonts);

} // namespace strefa::ui
