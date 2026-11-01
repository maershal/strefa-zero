#include "game/animation.hpp"
#include "game/session.hpp"
#include "sim/fixed_clock.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace strefa;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}
bool embedded(const game::GameSession& game) {
    const auto p = game.player().feet;
    for (int y = static_cast<int>(std::floor(p.y - 18 + .0001F)); y <= static_cast<int>(std::floor(p.y - .0001F)); ++y)
        for (int x = static_cast<int>(std::floor(p.x - 4 + .0001F));
             x <= static_cast<int>(std::floor(p.x + 4 - .0001F)); ++x)
            if (sim::blocks_actor(game.world().at({x, y}).material)) return true;
    return false;
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
void locomotion() {
    game::GameSession game;
    game::TickInput input;
    input.move = 1;
    for (int i = 0; i < 120; ++i) {
        game.tick(input);
        check(!embedded(game), "Walking must never penetrate terrain");
    }
    check(game.player().feet.x <= 56.01F && game.player().feet.x > 40, "A tall reservoir wall must stop the player");
    game.reset();
    input = {};
    input.jump = true;
    game.tick(input);
    check(game.player().feet.y < 188 && !game.player().grounded, "Jump must leave the ground");
    input.jump = false;
    for (int i = 0; i < 120; ++i) game.tick(input);
    check(game.player().grounded && std::abs(game.player().feet.y - 188) < .6F,
          "The player must land on the thin floor");
    game.reset();
    input.jet = true;
    for (int i = 0; i < 95; ++i) {
        game.tick(input);
        check(!embedded(game), "Jet flight must respect solid boundaries");
    }
    check(game.player().fuel < .001F && game.player().feet.y < 100,
          "Jet flight must climb and consume its finite fuel");
    input.jet = false;
    for (int i = 0; i < 360; ++i) game.tick(input);
    check(game.player().grounded && std::abs(game.player().fuel - game.fuel_capacity()) < .001F,
          "Fuel must refill after a grounded rest");
}
void weapons_and_targets() {
    game::GameSession game;
    game::TickInput input;
    input.fire = true;
    input.aim = {100, 178};
    game.tick(input);
    check(game.ammunition() == 29 && game.shots_fired() == 1, "One shot must consume one cartridge");
    input.fire = false;
    for (int i = 0; i < 30; ++i) game.tick(input);
    check(game.targets()[0].hits_remaining == 3 && game.bullets().empty(),
          "Terrain must occlude a target from fast bullets");
    input.reload = true;
    game.tick(input);
    input.reload = false;
    for (int i = 0; i < 90; ++i) game.tick(input);
    check(game.ammunition() == 30 && game.reload_remaining() == 0, "A completed reload must restore the magazine");
    game.reset();
    input = {};
    input.aim = {100, 178};
    for (int i = 0; i < 150; ++i) {
        input.jet = i < 90;
        input.fire = i >= 94 && i < 124;
        game.tick(input);
    }
    check(game.score() >= 1 && game.targets()[0].hits_remaining == 0,
          "A clear firing angle must damage and eventually destroy a target");
    input.jet = false;
    input.fire = true;
    input.aim = {184, 140};
    for (int i = 0; i < 30; ++i) game.tick(input);
    // Refuel on the ground, then shoot the elevated target from above its sand cover.
    input = {};
    for (int i = 0; i < 180; ++i) game.tick(input);
    input.aim = {282, 102};
    for (int i = 0; i < 150; ++i) {
        input.jet = i < 90;
        input.fire = i >= 90 && i < 120;
        game.tick(input);
    }
    check(game.score() == 3, "The training course must be completable by destroying all three targets");
    input = {};
    input.grenade = true;
    game.tick(input);
    input.grenade = false;
    for (int i = 0; i < 240; ++i) game.tick(input);
    check(game.grenade_stock() == 2 && game.grenades().empty() && game.explosions() == 1,
          "A grenade must leave stock once, bounce and explode exactly once");
    game.reset();
    check(game.ammunition() == 30 && game.grenade_stock() == 3 && game.score() == 0 && game.ticks() == 0
                  && game.bullets().empty() && game.effects().empty(),
          "Reset must restore the complete training session");
}
game::TickInput scripted(std::uint64_t tick) {
    game::TickInput input;
    input.aim = {184, 140};
    input.move = tick < 60 ? 1.F : tick < 100 ? -1.F : 0.F;
    input.jump = tick == 3;
    input.jet = tick > 10 && tick < 80;
    input.fire = tick > 20 && tick < 220;
    input.reload = tick == 230;
    input.grenade = tick == 100;
    return input;
}
void fps_independence() {
    std::uint64_t reference{};
    for (const int fps : {30, 60, 144}) {
        game::GameSession game;
        sim::FixedClock clock;
        for (int frame = 0; frame < fps * 6; ++frame) {
            const int steps = clock.advance(1.0 / fps, false, 1);
            for (int i = 0; i < steps; ++i) game.tick(scripted(game.ticks()));
        }
        check(game.ticks() == 360, "Every presentation rate must advance exactly 360 game ticks");
        if (reference == 0)
            reference = game.checksum();
        else
            check(game.checksum() == reference,
                  "Tick-indexed gameplay inputs must produce the same world and actor state at every FPS");
    }
}
void animation_constraints() {
    const auto arm = game::two_bone({0, 0}, {100, 0}, 4, 3, 1);
    check(std::abs(std::hypot(arm.joint.x, arm.joint.y) - 4) < 1e-4F, "IK upper bone length");
    check(std::abs(std::hypot(arm.end.x - arm.joint.x, arm.end.y - arm.joint.y) - 3) < 1e-4F, "IK lower bone length");
    game::GameSession g;
    game::TickInput input;
    input.aim = {200, 100};
    g.tick(input);
    check(g.player().recoil == 0 && g.player().stride == 0, "Idle does not advance gait or fire recoil");
    input.move = 1;
    for (int i = 0; i < 8; ++i) g.tick(input);
    check(g.player().stride > 0, "Travel advances gait");
    input.fire = true;
    g.tick(input);
    check(g.player().recoil > .9F, "Actual shot drives recoil");
    const auto pose = game::pose(g.player());
    check(std::isfinite(pose.near_arm.joint.x) && std::isfinite(pose.near_leg.joint.y), "Rig remains finite");
}
} // namespace
int main() {
    try {
        input_edges();
        locomotion();
        weapons_and_targets();
        fps_independence();
        animation_constraints();
        std::cout
                << "PASS: input edges, movement/collision, jump, jet fuel, weapons/occlusion, targets, reload, grenade, reset, 30/60/144 FPS determinism\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
