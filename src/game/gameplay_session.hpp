#pragma once
#include "game/world_protocol.hpp"

#include <span>

namespace strefa::game {
// Buttons: 1 jump, 2 jet, 4 fire (active weapon), 8 reload, 16 grenade, 32/64/128 select rifle/shotgun/C4,
// 256 detonate placed C4 charges (gpu::MissionButton).
struct GameplayInput {
    float move{}, aim_x{}, aim_y{};
    std::uint32_t buttons{};
};
struct GameplayState {
    unsigned ammunition{rifle_magazine}, grenades{grenade_stock}, score{}, shots{}, explosions{}, destroyed_cells{};
    unsigned shells{shotgun_tube}, charges{c4_stock}, placed{}; // placed: C4 charges accepted, not yet detonated
    Weapon weapon{Weapon::rifle};
    std::array<unsigned, 3> hp{3, 3, 3};
    bool finished{};
};
// Bounded journal: resources are reserved before publishing a command. A single
// acknowledgement commits the reservation or returns it. No physics model here.
class CommandRing {
public:
    bool reserve(const WorldCommand&);
    bool acknowledge(unsigned id, bool accepted, GameplayState&);
    void reset() {
        entries_ = {};
        pending_ = 0;
    }
    unsigned pending() const { return pending_; }

private:
    std::array<WorldCommand, command_capacity> entries_{};
    unsigned pending_{};
};
class GameplaySession {
public:
    void reset(unsigned generation, std::array<unsigned, 3> hp = {3, 3, 3}, unsigned tick = 0);
    CommandBatch prepare(GameplayInput); // only after transport has reserved a slot
    void consume(const EventBatch&);
    void cancel_input() { cancel_jump_ = true; }
    void present(std::array<std::uint32_t, 4>& inventory, std::array<float, 4>& timers,
                 std::array<std::array<float, 4>, 3>& targets, std::uint32_t& finished) const;
    const GameplayState& state() const { return state_; }
    unsigned tick() const { return tick_; }
    unsigned generation() const { return generation_; }
    unsigned pending() const { return commands_.pending(); }
    unsigned acknowledged_tick() const { return acknowledged_tick_; }
    unsigned duplicate_batches() const { return duplicate_batches_; }
    float reload_remaining() const;

private:
    GameplayState state_;
    CommandRing commands_;
    unsigned generation_{}, tick_{}, next_id_{1}, acknowledged_tick_{}, duplicate_batches_{};
    unsigned rifle_ready_{}, grenade_ready_{}, shotgun_ready_{}, reload_end_{};
    Weapon reloading_{Weapon::rifle};
    [[nodiscard]] std::uint32_t stock() const noexcept;
    bool cancel_jump_{}, detonate_{};
};
} // namespace strefa::game
