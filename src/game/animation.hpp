#pragma once
#include "game/session.hpp"
#include <cmath>
#include <algorithm>

namespace strefa::game {
struct Limb { Vec2 root, joint, end; };
inline Limb two_bone(Vec2 root, Vec2 target, float upper, float lower, float bend) {
    Vec2 delta = target - root;
    const float distance = std::clamp(std::hypot(delta.x, delta.y), .001F, upper + lower - .001F);
    const float angle = std::atan2(delta.y, delta.x);
    const float cosine = std::clamp((upper * upper + distance * distance - lower * lower) / (2 * upper * distance), -1.F, 1.F);
    const float elbow = angle + bend * std::acos(cosine);
    const Vec2 joint = root + Vec2{std::cos(elbow), std::sin(elbow)} * upper;
    return {root, joint, root + Vec2{std::cos(angle), std::sin(angle)} * distance};
}
struct SoldierPose {
    Vec2 hip, shoulder, head, gun, muzzle, trigger_grip, support_grip;
    Limb near_arm, far_arm, near_leg, far_leg;
    float facing{};
};
inline SoldierPose pose(const PlayerState& player) {
    SoldierPose p;
    p.facing = player.aim.x < 0 ? -1.F : 1.F;
    const float gait = std::sin(player.stride), lift = std::cos(player.stride);
    const float compression = player.landing * 1.5F;
    const float bob = player.grounded ? std::abs(lift) * player.stride_blend * .4F : 0;
    p.hip = player.feet + Vec2{0, -7.5F + compression - bob};
    p.shoulder = p.hip + Vec2{player.velocity.x * .009F, -6.2F} - player.aim * (player.recoil * .6F);
    p.head = p.shoulder + Vec2{0, -3.3F};
    const Vec2 normal{-player.aim.y * p.facing, player.aim.x * p.facing};
    p.gun = p.shoulder + player.aim * (3 - player.recoil * .7F);
    p.muzzle = p.gun + player.aim * 6.5F;
    p.trigger_grip = p.gun - player.aim * 1.2F + normal;
    p.support_grip = p.gun + player.aim * 2.8F + normal * .7F;
    p.near_arm = two_bone(p.shoulder, p.trigger_grip, 3.8F, 3.4F, p.facing);
    p.far_arm = two_bone(p.shoulder + Vec2{-p.facing, 0}, p.support_grip, 3.8F, 3.4F, p.facing);
    const float stride = gait * 3 * player.stride_blend;
    Vec2 near_foot = player.feet + Vec2{stride, -std::max(0.F, lift) * 2 * player.stride_blend};
    Vec2 far_foot = player.feet + Vec2{-stride, -std::max(0.F, -lift) * 2 * player.stride_blend};
    if (!player.grounded) {
        const float fall = std::clamp(player.velocity.y / 90, -1.F, 1.F);
        near_foot = player.feet + Vec2{p.facing * (2 - fall), -2 + fall};
        far_foot = player.feet + Vec2{-p.facing * 2, -1 - fall};
    }
    p.near_leg = two_bone(p.hip, near_foot, 4.3F, 4.2F, -p.facing);
    p.far_leg = two_bone(p.hip + Vec2{-p.facing * .6F,0}, far_foot, 4.3F, 4.2F, -p.facing);
    return p;
}
} // namespace strefa::game
