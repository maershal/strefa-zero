#pragma once
#include "sim/world.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

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
struct PlayerTuning {
    float speed{45}, acceleration{360}, braking{480}, gravity{180}, jump_speed{85};
    float jet_acceleration{300}, fuel_seconds{1.5F}, max_fall{120}, max_rise{90};
};
struct WeaponConfig {
    int magazine{30};
    float interval{.1F}, reload_seconds{1.5F}, bullet_speed{600};
    float grenade_speed{90}, grenade_fuse{2}, bounce{.45F};
};
struct PlayerState {
    Vec2 feet{24, 188}, velocity{}, aim{1, 0};
    float fuel{1.5F};
    float stride{}, recoil{}, landing{}, airborne_time{}, stride_blend{};
    bool grounded{true}, jetting{};
};
struct Projectile {
    Vec2 position, previous, velocity;
    float remaining{};
};
struct Target {
    Vec2 feet;
    int hits_remaining{3};
    float flash{};
};
enum class EffectKind { impact, blast, muzzle };
struct VisualEvent {
    std::uint64_t id{};
    Vec2 position, velocity;
    EffectKind kind{};
};
struct Effect {
    Vec2 position;
    float remaining;
    EffectKind kind;
};

class GameSession {
public:
    static constexpr float tick_seconds = 1.F / 60.F;
    static constexpr float player_half_width = 4, player_height = 18;
    static constexpr std::size_t projectile_limit = 128, effect_limit = 128;
    explicit GameSession(PlayerTuning player = {}, WeaponConfig weapon = {}, sim::PhysicsConfig physics = {});
    void reset();
    void tick(const TickInput& input);
    [[nodiscard]] const sim::World& world() const noexcept { return world_; }
    [[nodiscard]] const PlayerState& player() const noexcept { return player_; }
    [[nodiscard]] std::span<const Projectile> bullets() const noexcept { return bullets_; }
    [[nodiscard]] std::span<const Projectile> grenades() const noexcept { return grenades_; }
    [[nodiscard]] std::span<const Target> targets() const noexcept { return targets_; }
    [[nodiscard]] std::span<const VisualEvent> visual_events() const noexcept { return visual_events_; }
    [[nodiscard]] std::span<const Effect> effects() const noexcept { return effects_; }
    [[nodiscard]] int ammunition() const noexcept { return ammunition_; }
    [[nodiscard]] int grenade_stock() const noexcept { return grenade_stock_; }
    [[nodiscard]] int score() const noexcept;
    [[nodiscard]] float reload_remaining() const noexcept { return reload_; }
    [[nodiscard]] float fuel_capacity() const noexcept { return tuning_.fuel_seconds; }
    [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] std::uint64_t shots_fired() const noexcept { return shots_fired_; }
    [[nodiscard]] std::uint64_t explosions() const noexcept { return explosions_; }
    [[nodiscard]] unsigned recovery_resets() const noexcept { return recovery_resets_; }
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] bool collides(Vec2 feet) const noexcept;
    [[nodiscard]] bool projectile_blocked(Vec2 point) const noexcept;
    void move_player(float dx, float dy);
    bool recover_player();
    void update_player(const TickInput& input);
    void update_weapons(const TickInput& input);
    void update_bullets();
    void update_grenades();
    void effect(Vec2 point, EffectKind kind);
    void rectangle(sim::Point a, sim::Point b, sim::Material material);

    sim::World world_;
    PlayerTuning tuning_;
    WeaponConfig weapon_;
    PlayerState player_;
    std::array<Target, 3> targets_{};
    std::vector<Projectile> bullets_, grenades_;
    std::vector<Effect> effects_;
    std::vector<VisualEvent> visual_events_;
    std::uint64_t visual_id_{};
    int ammunition_{30}, grenade_stock_{3};
    float cooldown_{}, reload_{};
    std::uint64_t ticks_{}, shots_fired_{}, explosions_{};
    unsigned recovery_resets_{};
    std::uint64_t last_blast_{};
};

} // namespace strefa::game
