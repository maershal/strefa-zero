#include "app/application.hpp"
#include "game/session.hpp"
#include "gpu/vfx.hpp"

#include <iostream>
#include <stdexcept>
int main() {
    try {
        using namespace strefa;
        const auto check = [](bool ok, const char* text) {
            if (!ok) throw std::runtime_error{text};
        };
        const auto parse = [](std::initializer_list<const char*> args) {
            std::vector<char*> argv;
            for (auto text : args) argv.push_back(const_cast<char*>(text));
            return app::parse_options(static_cast<int>(argv.size()), argv.data());
        };
        check(!parse({"game", "--gpu-vfx-check"}), "VFX diagnostic needs a profile");
        check(!parse({"game", "--gpu-vfx-quality", "unknown"}), "Invalid VFX quality rejected");
        check(!parse({"game", "--gpu-vfx-quality"}), "Missing VFX quality rejected");
        check(!parse({"game", "--gpu-vfx-check", "--gpu-vfx-benchmark", "--profile", "fixture"}),
              "Exclusive diagnostics");
        check(!parse({"game", "--gpu-vfx-check", "--gpu-render-check", "--profile", "fixture"}),
              "VFX/render diagnostic conflict");
        auto options = parse({"game", "--gpu-vfx-quality", "high", "--gpu-render-quality", "basic"});
        check(options && options->gpu_vfx.quality == gpu::VfxQuality::high
                      && options->gpu_render.quality == gpu::RenderQuality::basic
                      && options->gpu_execution.fluid == gpu::FluidOptions{},
              "VFX independent of physical/render configuration");
        sim::PhysicsConfig config;
        config.thermal = config.fluids = config.air = config.reactions = config.motion = false;
        game::GameSession game{{}, {}, config};
        game::TickInput input;
        input.fire = true;
        input.aim = {200, 170};
        game.tick(input);
        check(!game.visual_events().empty(), "CPU accepted shot publishes presentation event");
        const auto event = game.visual_events().front();
        check(event.kind == game::EffectKind::muzzle && event.id != 0, "CPU VFX identity and kind");
        const auto checksum = game.checksum();
        for (const auto& e : game.visual_events())
            check(e.position.x >= 0 && e.position.y >= 0, "CPU VFX position in world units");
        check(game.checksum() == checksum, "Presentation inspection is read only");
        game.tick({});
        check(game.visual_events().empty(), "CPU presentation packet is per tick");
        for (const sim::Point p : std::array{sim::Point{0, 0}, sim::Point{100, 150}, sim::Point{100, 130},
                                             sim::Point{319, 191}, sim::Point{-1, 0}}) {
            const auto full = game.world().sample(p);
            const auto light = game.world().sample_fluid(p);
            check(full.material == light.material && full.liquid_velocity.x == light.velocity.x
                          && full.liquid_velocity.y == light.velocity.y
                          && static_cast<float>(full.liquid_fractions[0]) == light.water
                          && static_cast<float>(full.liquid_fractions[1]) == light.oil,
                  "Light fluid sample matches authoritative full sample");
        }
        game.reset();
        check(game.visual_events().empty(), "CPU reset discards old presentation");
        std::cout << "PASS: VFX CLI, configuration isolation, CPU presentation identity/lifecycle\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
