#include "game/animation.hpp"
#include "game/input.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace strefa;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}
void input_edges() {
    game::InputBuffer buffer;
    game::TickInput press;
    press.jump = press.grenade = press.reload = press.fire = true;
    buffer.submit(press);
    press.jump = press.grenade = press.reload = false;
    buffer.submit(press);
    const auto first = buffer.consume(), second = buffer.consume();
    check(first.jump && first.grenade && first.reload && !second.jump && !second.grenade && !second.reload,
          "Action edges must survive zero-tick frames and be consumed exactly once");
    check(first.fire && second.fire, "Held fire must survive catch-up ticks");
    buffer.clear();
    check(!buffer.consume().fire, "Focus loss must clear held controls");
}
// soldier_pose.glsl compiled as C++: bone lengths hold and the rig stays finite in motion.
void animation_constraints() {
    const auto arm = game::two_bone({0, 0}, {100, 0}, 4, 3, 1);
    check(std::abs(std::hypot(arm.joint.x, arm.joint.y) - 4) < 1e-4F, "IK upper bone length");
    check(std::abs(std::hypot(arm.end.x - arm.joint.x, arm.end.y - arm.joint.y) - 3) < 1e-4F, "IK lower bone length");
    game::PlayerState player;
    player.velocity = {40, 0};
    player.aim = {.8F, -.6F};
    player.stride = 2.5F;
    player.recoil = 1;
    player.stride_blend = 1;
    const auto pose = game::pose(player);
    check(std::isfinite(pose.near_arm.joint.x) && std::isfinite(pose.near_leg.joint.y), "Rig remains finite");
}
} // namespace
int main() {
    try {
        input_edges();
        animation_constraints();
        std::cout << "PASS: input edges, pose/IK constraints\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
