#pragma once
#include "game/session.hpp"

#include <algorithm>
#include <cmath>
namespace strefa::game {
namespace pose_shared {
using vec2 = Vec2;
inline float length(Vec2 v) {
    return std::hypot(v.x, v.y);
}
inline float clamp(float x, float lo, float hi) {
    return std::clamp(x, lo, hi);
}
inline float max(float a, float b) {
    return std::max(a, b);
}
using std::abs;
using std::acos;
using std::atan2;
using std::cos;
using std::sin;
#include "../../assets/shaders/soldier_pose.glsl"
} // namespace pose_shared
using Limb = pose_shared::PoseLimb;
using SoldierPose = pose_shared::SoldierPose;
inline Limb two_bone(Vec2 root, Vec2 target, float upper, float lower, float bend) {
    return pose_shared::poseLimb(root, target, upper, lower, bend);
}
inline SoldierPose pose(const PlayerState& p) {
    return pose_shared::soldierPose(p.feet, p.velocity, p.aim, p.stride, p.recoil, p.landing, p.stride_blend,
                                    p.grounded);
}
} // namespace strefa::game
