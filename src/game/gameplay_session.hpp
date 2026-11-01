#pragma once
#include "game/world_protocol.hpp"

#include <span>

namespace strefa::game {
struct GameplayInput {
    float move{}, aim_x{}, aim_y{};
    std::uint32_t buttons{};
};
struct GameplayState {
    unsigned ammunition{30}, grenades{3}, score{}, shots{}, explosions{}, destroyed_cells{};
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
    unsigned rifle_ready_{}, grenade_ready_{}, reload_end_{};
    bool cancel_jump_{};
};
} // namespace strefa::game
