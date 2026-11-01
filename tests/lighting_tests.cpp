#include "app/application.hpp"
#include "core/settings.hpp"
#include "gpu/lighting.hpp"
#include "sim/fixed_clock.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error{message};
}
} // namespace
int main() {
    try {
        using namespace strefa;
        require(gpu::LightingOptions{}.valid(), "Defaults must be valid");
        for (float value : {-.1F, 1.1F, std::numeric_limits<float>::quiet_NaN()}) {
            gpu::LightingOptions o;
            o.bloom = value;
            require(!o.valid(), "Invalid style setting accepted");
        }
        float wet = 0;
        for (int i = 0; i < 30; ++i) wet = gpu::update_moisture(wet, true, 293.15F, 1.F / 120.F);
        require(wet > .9999F, "Contact must wet in 0.25s");
        for (int i = 0; i < 3600; ++i) {
            wet = gpu::update_moisture(wet, false, 293.15F, 1.F / 120.F);
        }
        require(wet < .0001F, "Ambient drying must take 30s");
        require(gpu::update_moisture(1, false, 1093.15F, 1.F) < gpu::update_moisture(1, false, 293.15F, 1.F),
                "Heat must speed drying");
        require(gpu::update_moisture(.5F, false, 293.15F, 0) == .5F, "Pause must preserve moisture");
        for (int fps : {30, 60, 144}) {
            sim::FixedStepClock<120, 4, 24> clock;
            float wet_at_tick = 0;
            unsigned tick = 0;
            for (int frame = 0; frame < fps * 10; ++frame) {
                const int count = clock.advance(1.0 / fps, false, 1);
                for (int step = 0; step < count; ++step) {
                    wet_at_tick = gpu::update_moisture(wet_at_tick, tick < 30, 293.15F, 1.F / 120.F);
                    ++tick;
                }
            }
            require(tick == 1200 && std::abs(wet_at_tick - .675F) < .001F, "Moisture depends on presentation FPS");
        }
        core::Settings settings;
        settings.lighting_quality = 2;
        settings.bloom = .31F;
        settings.camera_shake = .4F;
        settings.aberration = .2F;
        const auto saved = core::deserialize(core::serialize(settings));
        require(saved && *saved == settings, "Style settings lost on disk");
        require(core::deserialize("schema_version=1\nwidth=1280\n")->lighting_quality == 0,
                "Old settings must default to lighting off");
        require(!core::deserialize("lighting_quality=3"), "Unknown lighting quality accepted");
        require(!core::deserialize("bloom=nan"), "Nonfinite style accepted");
        const auto parse = [](std::initializer_list<const char*> args) {
            std::vector<char*> argv;
            for (const auto* a : args) argv.push_back(const_cast<char*>(a));
            return app::parse_options(static_cast<int>(argv.size()), argv.data());
        };
        auto options = parse({"game", "--gpu-lighting-quality", "high"});
        require(options && options->gpu_render.lighting.quality == gpu::LightingQuality::high, "CLI high missing");
        require(!parse({"game", "--gpu-lighting-quality", "ultra"}), "Unknown quality accepted");
        require(!parse({"game", "--gpu-lighting-quality"}), "Missing quality accepted");
        require(!parse({"game", "--gpu-lighting-check"}), "Diagnostic requires output");
        require(!parse({"game", "--gpu-lighting-check", "--gpu-render-check", "--profile", "output"}),
                "Conflicting checks accepted");
        std::cout << "I11 lighting/moisture/settings/CLI contract PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
