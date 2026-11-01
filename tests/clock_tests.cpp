#include "game/session.hpp"
#include "sim/fixed_clock.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        auto check = [](bool ok) {
            if (!ok) throw std::runtime_error{"Fixed clock/input contract failed"};
        };
        strefa::sim::FixedStepClock<120, 4, 24> clock;
        int count = clock.advance(.1, false, 1);
        check(count == 4 && clock.dropped_seconds() == 0);
        check(std::abs(clock.pending_seconds() - 8.0 / 120) < 1e-9);
        count += clock.advance(0, false, 1);
        count += clock.advance(0, false, 1);
        check(count == 12 && clock.dropped_seconds() == 0 && clock.fraction() < 1);
        clock.reset();
        check(clock.pending_seconds() == 0);
        check(clock.advance(1, false, 1) == 4);
        check(std::abs(clock.pending_seconds() - 24.0 / 120) < 1e-9);
        check(std::abs(clock.dropped_seconds() - 92.0 / 120) < 1e-9);
        for (int i = 0; i < 6; ++i) check(clock.advance(0, false, 1) == 4);
        check(clock.advance(0, false, 1) == 0 && clock.pending_seconds() < 1e-9);
        clock.reset();
        clock.advance(.1, false, 1);
        check(clock.advance(0, true, 1) == 0 && clock.advance(0, false, 1) == 0);
        for (int fps : {30, 60, 144}) {
            clock.reset();
            int ticks = 0;
            for (int i = 0; i < fps * 10; ++i) ticks += clock.advance(1.0 / fps, false, 1);
            check(ticks == 1200 && clock.dropped_seconds() == 0);
        }
        strefa::sim::FixedStepClock<120, 4> legacy;
        check(legacy.advance(.1, false, 1) == 4 && std::abs(legacy.dropped_seconds() - 8.0 / 120) < 1e-9);
        strefa::game::InputBuffer input;
        strefa::game::TickInput press;
        press.jump = true;
        press.grenade = true;
        press.reload = true;
        press.fire = true;
        input.submit(press);
        check(input.consume().jump);
        const auto next = input.consume();
        check(!next.jump && !next.grenade && !next.reload && next.fire);
        input.submit(press);
        input.clear();
        check(!input.consume().grenade);
        std::cout << "PASS: bounded backlog, overflow accounting, pause, 30/60/144 FPS, single-shot input\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
