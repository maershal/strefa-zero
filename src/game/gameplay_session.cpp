#include "game/gameplay_session.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace strefa::game {
bool CommandRing::reserve(const WorldCommand& c) {
    if (!c.id || pending_ == entries_.size()) return false;
    for (auto& entry : entries_)
        if (!entry.id) {
            entry = c;
            ++pending_;
            return true;
        }
    return false;
}
bool CommandRing::acknowledge(unsigned id, bool accepted, GameplayState& state, unsigned value) {
    for (auto& c : entries_)
        if (c.id == id && id) {
            const auto kind = static_cast<CommandKind>(c.kind);
            if (!accepted) {
                if (kind == CommandKind::rifle) state.ammunition = std::min(rifle_magazine, state.ammunition + 1);
                if (kind == CommandKind::grenade) state.grenades = std::min(grenade_stock, state.grenades + 1);
                if (kind == CommandKind::shotgun) state.shells = std::min(shotgun_tube, state.shells + 1);
                if (kind == CommandKind::c4_place) state.charges = std::min(c4_stock, state.charges + 1);
            } else if (kind == CommandKind::rifle)
                ++state.shots;
            else if (kind == CommandKind::c4_place)
                ++state.placed;
            else if (kind == CommandKind::c4_detonate) // charges still in flight stay placed
                state.placed -= std::min(state.placed, value);
            c = {};
            --pending_;
            return true;
        }
    return false;
}
void GameplaySession::reset(unsigned generation, std::array<unsigned, 3> hp, unsigned tick) {
    *this = GameplaySession{};
    generation_ = generation;
    tick_ = acknowledged_tick_ = tick;
    state_.hp = hp;
    state_.score = static_cast<unsigned>(std::ranges::count(hp, 0U));
}
float GameplaySession::reload_remaining() const {
    return reload_end_ > tick_ ? static_cast<float>(reload_end_ - tick_) / 120.F : 0.F;
}
CommandBatch GameplaySession::prepare(GameplayInput input) {
    if (tick_ >= std::numeric_limits<unsigned>::max() - 256 || next_id_ >= std::numeric_limits<unsigned>::max() - 4)
        throw std::overflow_error{"Wyczerpano licznik rozgrywki: zresetuj generację świata."};
    ++tick_;
    if (reload_end_ && tick_ >= reload_end_) {
        (reloading_ == Weapon::shotgun ? state_.shells : state_.ammunition) =
                reloading_ == Weapon::shotgun ? shotgun_tube : rifle_magazine;
        reload_end_ = 0;
    }
    if (!state_.finished) { // a selection takes effect before this tick's shot
        if (input.buttons & 32U) state_.weapon = Weapon::rifle;
        if (input.buttons & 64U) state_.weapon = Weapon::shotgun;
        if (input.buttons & 128U) state_.weapon = Weapon::c4;
    }
    // Preserve the effective 0.7/I05 timing: repeated float subtraction in GLSL
    // expired after 13 / 181 ticks. Quantize once; do not change weapon tempo.
    const bool shotgun = state_.weapon == Weapon::shotgun;
    if (!state_.finished && (input.buttons & 8U) && !reload_end_ && state_.weapon != Weapon::c4
        && (shotgun ? state_.shells < shotgun_tube : state_.ammunition < rifle_magazine)) {
        reload_end_ = tick_ + 181;
        reloading_ = state_.weapon;
    }
    CommandBatch batch;
    batch.header = {generation_, tick_, 0, cancel_jump_ ? 1U : 0U};
    cancel_jump_ = false;
    const auto add = [&](CommandKind kind) {
        WorldCommand c;
        c.generation = generation_;
        c.tick = tick_;
        c.id = next_id_;
        c.kind = static_cast<unsigned>(kind);
        // Launch speed: rifle 600, shotgun pellets 420, grenade 90, C4 charge 60 (px/s).
        const float speed = kind == CommandKind::rifle    ? 600.F
                          : kind == CommandKind::shotgun  ? 420.F
                          : kind == CommandKind::c4_place ? 60.F
                                                          : 90.F;
        c.payload = {input.aim_x, input.aim_y, speed, 0};
        if (kind == CommandKind::jump) c.flags = tick_ + 12; // 100 ms buffer, inclusive execution tick
        if (batch.header[2] == commands_per_tick || !commands_.reserve(c)) return false;
        ++next_id_;
        batch.commands[batch.header[2]++] = c;
        return true;
    };
    if (!state_.finished) {
        const bool fire = (input.buttons & 4U) != 0;
        const bool reloading = reload_end_ && reloading_ == state_.weapon;
        if (fire && state_.weapon == Weapon::rifle && state_.ammunition && !reloading && tick_ >= rifle_ready_
            && add(CommandKind::rifle)) {
            --state_.ammunition;
            rifle_ready_ = tick_ + 13;
        }
        if (fire && shotgun && state_.shells && !reloading && tick_ >= shotgun_ready_ && add(CommandKind::shotgun)) {
            --state_.shells;
            shotgun_ready_ = tick_ + 60; // 0.5 s pump
        }
        if (fire && state_.weapon == Weapon::c4 && state_.charges && tick_ >= grenade_ready_
            && add(CommandKind::c4_place)) {
            --state_.charges;
            grenade_ready_ = tick_ + 30;
        }
        if ((input.buttons & 16U) && state_.grenades && tick_ >= grenade_ready_ && add(CommandKind::grenade)) {
            --state_.grenades;
            grenade_ready_ = tick_ + 30; // shared 250 ms throw cooldown
        }
        if (input.buttons & 1U) add(CommandKind::jump);
        // Fire, grenade and jump can fill the three slots; a detonation then waits for the next tick.
        detonate_ = (detonate_ || (input.buttons & 256U)) && state_.placed;
        if (detonate_ && add(CommandKind::c4_detonate)) detonate_ = false;
    }
    batch.inventory = {state_.ammunition, stock(), state_.score, state_.shots};
    batch.rules = {rifle_ready_ > tick_ ? static_cast<float>(rifle_ready_ - tick_) / 120.F : 0.F, reload_remaining(),
                   state_.finished ? 1.F : 0.F, 0};
    batch.targets = {state_.hp[0], state_.hp[1], state_.hp[2], 0};
    return batch;
}
void GameplaySession::consume(const EventBatch& batch) {
    if (batch.header[2] != generation_) return;
    const auto count = batch.header[0], tick = batch.header[1];
    if (tick <= acknowledged_tick_) {
        ++duplicate_batches_;
        return;
    }
    if (tick != acknowledged_tick_ + 1 || tick > tick_ || count > events_per_tick || batch.header[3])
        throw std::runtime_error{"Niepełny strumień zdarzeń rozgrywki."};
    // Validate a complete packet before applying anything; IDs are local ordinals.
    for (unsigned i = 0; i < count; ++i) {
        const auto& e = batch.events[i];
        if (e.generation != generation_ || e.tick != tick || e.id != i + 1 || e.kind < 1 || e.kind > 7)
            throw std::runtime_error{"Nieprawidłowy format lub kolejność zdarzeń rozgrywki."};
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto& e = batch.events[i];
        switch (static_cast<EventKind>(e.kind)) {
        case EventKind::accepted:
        case EventKind::rejected:
            commands_.acknowledge(e.command, e.kind == static_cast<unsigned>(EventKind::accepted), state_, e.value);
            break;
        case EventKind::hit:
            if (e.subject < state_.hp.size()) {
                auto& hp = state_.hp[e.subject];
                const auto before = hp;
                hp -= std::min(hp, e.value);
                if (before && hp == 0) ++state_.score;
            }
            break;
        case EventKind::explosion: ++state_.explosions; break;
        case EventKind::terrain: state_.destroyed_cells += e.value; break;
        case EventKind::exit:
            if (e.value && state_.score == 3) state_.finished = true;
            break;
        case EventKind::impact: state_.destroyed_cells += e.value; break;
        }
    }
    acknowledged_tick_ = tick;
}
std::uint32_t GameplaySession::stock() const noexcept {
    return Stock{state_.grenades, state_.shells, state_.charges, state_.placed, state_.weapon}.pack();
}
void GameplaySession::present(std::array<std::uint32_t, 4>& inventory, std::array<float, 4>& timers,
                              std::array<std::array<float, 4>, 3>& targets, std::uint32_t& finished) const {
    inventory = {state_.ammunition, stock(), state_.score, state_.shots};
    timers[0] = rifle_ready_ > tick_ ? static_cast<float>(rifle_ready_ - tick_) / 120.F : 0.F;
    timers[1] = reload_remaining();
    for (unsigned i = 0; i < 3; ++i) targets[i][2] = static_cast<float>(state_.hp[i]);
    finished = state_.finished ? 1U : 0U;
}
} // namespace strefa::game
