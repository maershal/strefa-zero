#include "game/session.hpp"
#include "game/animation.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace strefa::game {
namespace {
constexpr float epsilon = .0001F;
sim::Point cell(Vec2 p) { return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))}; }
float length(Vec2 p) { return std::sqrt(p.x * p.x + p.y * p.y); }
float approach(float from, float to, float amount) { return from + std::clamp(to - from, -amount, amount); }

// Supercover traversal includes both sides of an exact grid corner. Fast bullets
// test every crossed cell, including their origin, instead of teleporting to an endpoint.
template<class Visitor> bool trace(Vec2 from, Vec2 to, Visitor&& visit) {
    auto p = cell(from);
    const auto end = cell(to);
    const Vec2 delta = to - from;
    const int sx = delta.x > 0 ? 1 : -1, sy = delta.y > 0 ? 1 : -1;
    const float infinity = std::numeric_limits<float>::infinity();
    const float step_x = delta.x == 0 ? infinity : std::abs(1.F / delta.x);
    const float step_y = delta.y == 0 ? infinity : std::abs(1.F / delta.y);
    float next_x = delta.x == 0 ? infinity : (static_cast<float>(p.x + (sx > 0 ? 1 : 0)) - from.x) / delta.x;
    float next_y = delta.y == 0 ? infinity : (static_cast<float>(p.y + (sy > 0 ? 1 : 0)) - from.y) / delta.y;
    float progress = 0;
    for (int guard = 0; guard < 2 * (sim::World::width + sim::World::height) + 8; ++guard) {
        if (visit(p, from + delta * progress)) return true;
        if (p.x == end.x && p.y == end.y) return false;
        if (next_x == next_y) {
            progress = next_x;
            if (visit({p.x + sx, p.y}, from + delta * progress)
                || visit({p.x, p.y + sy}, from + delta * progress)) return true;
            p.x += sx; p.y += sy; next_x += step_x; next_y += step_y;
        } else if (next_x < next_y) {
            progress = next_x; next_x += step_x; p.x += sx;
        } else {
            progress = next_y; next_y += step_y; p.y += sy;
        }
    }
    return false;
}
}

void InputBuffer::submit(const TickInput& input) noexcept {
    pending_fire_=pending_fire_||input.fire;
    const bool jump = state_.jump || input.jump, reload = state_.reload || input.reload,
        grenade = state_.grenade || input.grenade;
    state_ = input;
    state_.jump = jump; state_.reload = reload; state_.grenade = grenade;
}
TickInput InputBuffer::consume() noexcept {
    auto result = state_;result.fire=result.fire||pending_fire_;pending_fire_=false;
    state_.jump = state_.reload = state_.grenade = false;
    return result;
}
GameSession::GameSession(PlayerTuning player, WeaponConfig weapon, sim::PhysicsConfig physics)
    : world_{0x57EF'A023U,physics}, tuning_{player}, weapon_{weapon} {
    bullets_.reserve(projectile_limit); grenades_.reserve(3); effects_.reserve(effect_limit);
    reset();
}
void GameSession::rectangle(sim::Point a, sim::Point b, sim::Material material) {
    for (int y = a.y; y <= b.y; ++y) for (int x = a.x; x <= b.x; ++x) world_.set({x, y}, material);
}
void GameSession::reset() {
    using enum sim::Material;
    world_.clear();
    rectangle({0, 188}, {319, 191}, stone);
    rectangle({0, 0}, {1, 187}, metal); rectangle({318, 0}, {319, 187}, metal);
    rectangle({60, 165}, {62, 187}, stone); rectangle({88, 165}, {90, 187}, stone);
    rectangle({63, 178}, {87, 187}, water);
    rectangle({140, 150}, {205, 153}, wood);
    rectangle({145, 154}, {148, 187}, wood); rectangle({198, 154}, {201, 187}, wood);
    rectangle({246, 110}, {305, 113}, wood);
    rectangle({291, 114}, {294, 187}, metal);
    rectangle({252, 100}, {267, 109}, sand);
    rectangle({214, 184}, {225, 187}, gunpowder);
    rectangle({305, 170}, {314, 187}, metal);
    rectangle({307, 164}, {312, 169}, thermite);
    rectangle({106, 182}, {108, 187}, metal); rectangle({119, 182}, {121, 187}, metal);
    rectangle({109, 184}, {118, 187}, nitroglycerin);
    player_ = {}; player_.fuel = tuning_.fuel_seconds;
    targets_ = {{{{100, 188}, 3, 0}, {{184, 150}, 3, 0}, {{282, 110}, 3, 0}}};
    ammunition_ = weapon_.magazine; grenade_stock_ = 3;
    cooldown_ = reload_ = 0;
    ticks_ = shots_fired_ = explosions_ = 0; recovery_resets_ = 0;
    bullets_.clear(); grenades_.clear(); effects_.clear();visual_events_.clear();visual_id_=0;
    last_blast_ = 0;
}
int GameSession::score() const noexcept {
    return static_cast<int>(std::ranges::count_if(targets_, [](const auto& target) { return target.hits_remaining == 0; }));
}
bool GameSession::collides(Vec2 feet) const noexcept {
    const auto top = cell({feet.x - player_half_width + epsilon, feet.y - player_height + epsilon});
    const auto bottom = cell({feet.x + player_half_width - epsilon, feet.y - epsilon});
    for (int y = top.y; y <= bottom.y; ++y) for (int x = top.x; x <= bottom.x; ++x)
        if (sim::blocks_actor(world_.at({x, y}).material)) return true;
    return false;
}
bool GameSession::projectile_blocked(Vec2 p) const noexcept { return sim::blocks_projectile(world_.at(cell(p)).material); }
void GameSession::move_player(float dx, float dy) {
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)) / .5F)));
    const Vec2 increment{dx / steps, dy / steps};
    for (int i = 0; i < steps; ++i) {
        auto next = player_.feet + Vec2{increment.x, 0};
        if (!collides(next)) player_.feet = next;
        else {
            bool stepped = false;
            if (player_.grounded && increment.x != 0) for (int rise = 1; rise <= 2; ++rise) {
                const Vec2 lifted{next.x, next.y - rise};
                if (!collides(lifted)) { player_.feet = lifted; stepped = true; break; }
            }
            if (!stepped) player_.velocity.x = 0;
        }
        next = player_.feet + Vec2{0, increment.y};
        if (!collides(next)) player_.feet = next;
        else player_.velocity.y = 0;
    }
    player_.grounded = collides(player_.feet + Vec2{0, .05F});
}
bool GameSession::recover_player() {
    if (!collides(player_.feet)) return true;
    for (int radius = 1; radius <= 8; ++radius) for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            if (std::abs(x) + std::abs(y) != radius) continue;
            const Vec2 candidate = player_.feet + Vec2{static_cast<float>(x), static_cast<float>(y)};
            if (!collides(candidate)) {
                player_.feet = candidate; player_.velocity = {};
                player_.grounded = collides(candidate + Vec2{0, .05F}); return true;
            }
        }
    const auto recoveries = recovery_resets_ + 1;
    reset(); recovery_resets_ = recoveries;
    return false;
}
void GameSession::update_player(const TickInput& input) {
    const Vec2 old_feet = player_.feet;
    const bool was_grounded = player_.grounded;
    const float impact_speed = player_.velocity.y;
    player_.recoil *= std::exp(-18 * tick_seconds);
    player_.landing *= std::exp(-12 * tick_seconds);
    const auto wind = world_.sample(cell(player_.feet + Vec2{0,-10})).air_velocity;
    player_.velocity.x += std::clamp(wind.x * .04F, -2.F, 2.F) * tick_seconds;
    player_.velocity.y += std::clamp(wind.y * .04F, -2.F, 2.F) * tick_seconds;
    const auto aim = input.aim - (player_.feet + Vec2{0, -12});
    if (const float size = length(aim); std::isfinite(size) && size > epsilon) player_.aim = aim * (1.F / size);
    const float axis = std::isfinite(input.move) ? std::clamp(input.move, -1.F, 1.F) : 0;
    if (axis != 0) player_.velocity.x = approach(player_.velocity.x, axis * tuning_.speed, tuning_.acceleration * tick_seconds);
    else if (player_.grounded) player_.velocity.x = approach(player_.velocity.x, 0, tuning_.braking * tick_seconds);
    if (input.jump && player_.grounded) { player_.velocity.y = -tuning_.jump_speed; player_.grounded = false; }
    player_.jetting = input.jet && player_.fuel > epsilon;
    player_.velocity.y += tuning_.gravity * tick_seconds;
    if (player_.jetting) {
        player_.velocity.y -= tuning_.jet_acceleration * tick_seconds;
        player_.fuel = std::max(0.F, player_.fuel - tick_seconds);
    } else if (player_.grounded && !input.jet)
        player_.fuel = std::min(tuning_.fuel_seconds, player_.fuel + tick_seconds);
    player_.velocity.y = std::clamp(player_.velocity.y, -tuning_.max_rise, tuning_.max_fall);
    move_player(player_.velocity.x * tick_seconds, player_.velocity.y * tick_seconds);
    if (player_.grounded && !was_grounded) player_.landing = std::clamp(impact_speed / 100, 0.F, 1.F);
    player_.airborne_time = player_.grounded ? 0 : player_.airborne_time + tick_seconds;
    const float locomotion = player_.grounded ? std::clamp(std::abs(player_.velocity.x) / 25, 0.F, 1.F) : 0;
    player_.stride_blend = approach(player_.stride_blend, locomotion, tick_seconds * 8);
    if (player_.grounded) player_.stride = std::fmod(player_.stride + std::abs(player_.feet.x - old_feet.x) * .32F, 6.2831853F);
}
void GameSession::effect(Vec2 p, EffectKind kind) {
    visual_events_.push_back({++visual_id_,p,player_.aim*weapon_.bullet_speed,kind});
    if (effects_.size() < effect_limit) effects_.push_back({p, kind == EffectKind::blast ? .45F : kind == EffectKind::muzzle ? .06F : .18F, kind});
}
void GameSession::update_weapons(const TickInput& input) {
    cooldown_ = std::max(0.F, cooldown_ - tick_seconds);
    if (reload_ > 0) {
        reload_ = std::max(0.F, reload_ - tick_seconds);
        if (reload_ <= epsilon) { reload_ = 0; ammunition_ = weapon_.magazine; }
    }
    if (input.reload && reload_ == 0 && ammunition_ < weapon_.magazine) reload_ = weapon_.reload_seconds;
    const Vec2 shoulder = player_.feet + Vec2{0, -12};
    if (input.fire && reload_ == 0 && cooldown_ <= epsilon && ammunition_ > 0 && bullets_.size() < projectile_limit) {
        bullets_.push_back({shoulder, shoulder, player_.aim * weapon_.bullet_speed, 1});
        --ammunition_; ++shots_fired_; cooldown_ = weapon_.interval;
        player_.recoil = 1;
        effect(pose(player_).muzzle, EffectKind::muzzle);
    }
    if (input.grenade && grenade_stock_ > 0) {
        grenades_.push_back({shoulder, shoulder, player_.aim * weapon_.grenade_speed + player_.velocity, weapon_.grenade_fuse});
        --grenade_stock_;
    }
}
void GameSession::update_bullets() {
    for (auto& bullet : bullets_) {
        bullet.previous = bullet.position;
        const auto next = bullet.position + bullet.velocity * tick_seconds;
        const bool hit = trace(bullet.position, next, [&](sim::Point p, Vec2 position) {
            if (sim::blocks_projectile(world_.at(p).material)) {
                world_.projectile_hit(p); effect(position, EffectKind::impact); return true;
            }
            for (auto& target : targets_) if (target.hits_remaining > 0
                && p.x >= target.feet.x - 4 && p.x < target.feet.x + 4
                && p.y >= target.feet.y - 16 && p.y < target.feet.y) {
                --target.hits_remaining; target.flash = .12F; effect(position, EffectKind::impact); return true;
            }
            return false;
        });
        bullet.position = next;
        bullet.remaining = hit ? 0 : bullet.remaining - tick_seconds;
    }
    std::erase_if(bullets_, [](const auto& bullet) { return bullet.remaining <= epsilon; });
}
void GameSession::update_grenades() {
    for (auto& grenade : grenades_) {
        grenade.previous = grenade.position;
        grenade.remaining -= tick_seconds;
        grenade.velocity.y += tuning_.gravity * tick_seconds;
        const auto displacement = grenade.velocity * tick_seconds;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(displacement.x), std::abs(displacement.y)) / .5F)));
        for (int i = 0; i < steps; ++i) {
            Vec2 next = grenade.position + Vec2{grenade.velocity.x * tick_seconds / steps, 0};
            if (projectile_blocked(next)) {
                if (world_.at(cell(next)).material == sim::Material::nitroglycerin) { world_.projectile_hit(cell(next)); grenade.remaining = 0; }
                grenade.velocity.x *= -weapon_.bounce;
            } else grenade.position = next;
            next = grenade.position + Vec2{0, grenade.velocity.y * tick_seconds / steps};
            if (projectile_blocked(next)) {
                if (world_.at(cell(next)).material == sim::Material::nitroglycerin) { world_.projectile_hit(cell(next)); grenade.remaining = 0; }
                grenade.velocity.y *= -weapon_.bounce;
                if (std::abs(grenade.velocity.y) < 5) grenade.velocity.y = 0;
                grenade.velocity.x *= .9F;
            } else grenade.position = next;
        }
        if (grenade.remaining <= epsilon) {
            world_.queue_explosion(cell(grenade.position)); ++explosions_;
            for (auto& target : targets_) {
                const Vec2 nearest{std::clamp(grenade.position.x, target.feet.x - 4, target.feet.x + 4),
                    std::clamp(grenade.position.y, target.feet.y - 16, target.feet.y)};
                bool occluded = false;
                trace(grenade.position, nearest, [&](sim::Point p, Vec2) {
                    if (sim::blocks_actor(world_.at(p).material)) { occluded = true; return true; } return false;
                });
                if (!occluded && length(nearest - grenade.position) <= 9) target.hits_remaining = 0;
            }
        }
    }
    std::erase_if(grenades_, [](const auto& grenade) { return grenade.remaining <= epsilon; });
}
void GameSession::tick(const TickInput& input) {
    ++ticks_;visual_events_.clear();
    for (auto& target : targets_) target.flash = std::max(0.F, target.flash - tick_seconds);
    for (auto& visual : effects_) visual.remaining -= tick_seconds;
    std::erase_if(effects_, [](const auto& visual) { return visual.remaining <= 0; });
    if (!recover_player()) return;
    update_player(input); update_weapons(input); update_bullets(); update_grenades();
    world_.step();
    for (const auto& event : world_.events()) if (event.kind == sim::PhysicsEventKind::explosion && event.id != last_blast_) {
        last_blast_ = event.id;
        const Vec2 source{static_cast<float>(event.position.x), static_cast<float>(event.position.y)};
        effect(source, EffectKind::blast);
        const Vec2 delta = player_.feet + Vec2{0,-9} - source;
        const float distance = length(delta);
        bool occluded = false;
        trace(source, player_.feet + Vec2{0,-9}, [&](sim::Point p, Vec2) {
            if (sim::blocks_actor(world_.at(p).material)) { occluded = true; return true; } return false;
        });
        if (!occluded && distance < 24 && distance > .1F) {
            const float strength = std::min(30.F, event.strength) * (1 - distance / 24);
            player_.velocity = player_.velocity + delta * (strength / distance);
            player_.velocity.x = std::clamp(player_.velocity.x, -70.F, 70.F);
            player_.velocity.y = std::clamp(player_.velocity.y, -90.F, 120.F);
        }
    }
    recover_player();
}
std::uint64_t GameSession::checksum() const noexcept {
    std::uint64_t hash = world_.checksum();
    auto add = [&hash](std::uint64_t value) { hash ^= value; hash *= 1099511628211ULL; };
    auto number = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    add(ticks_); add(shots_fired_); add(explosions_); add(ammunition_); add(grenade_stock_);
    number(player_.feet.x); number(player_.feet.y); number(player_.velocity.x); number(player_.velocity.y);
    number(player_.fuel); number(cooldown_); number(reload_);
    number(player_.aim.x); number(player_.aim.y); add(player_.grounded); add(player_.jetting);
    number(player_.stride); number(player_.recoil); number(player_.landing); number(player_.airborne_time); number(player_.stride_blend);
    for (const auto& target : targets_) { add(target.hits_remaining); number(target.flash); }
    for (const auto& list : {std::span<const Projectile>{bullets_}, std::span<const Projectile>{grenades_}})
        for (const auto& p : list) { number(p.position.x); number(p.position.y); number(p.velocity.x); number(p.velocity.y); number(p.remaining); }
    return hash;
}

} // namespace strefa::game
