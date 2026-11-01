#include "app/simulation_world.hpp"
#include "gpu/render_device.hpp"

#include <SDL3/SDL_opengl.h>

#include <fstream>
namespace strefa::app {
std::expected<void, std::string> validate_thermal_session(const std::filesystem::path& shaders,
                                                          const std::filesystem::path& output,
                                                          std::shared_ptr<gpu::RenderDevice> device) {
    try {
        std::ofstream report{output / "thermal-session-tests.txt"};
        double reference = -1;
        const auto require = [&](bool ok, const char* name) {
            report << name << '=' << (ok ? "PASS" : "FAIL") << '\n';
            report.flush();
            if (!ok) throw std::runtime_error{name};
        };
        for (unsigned fps : {30u, 60u, 144u}) {
            SimulationWorld session;
            auto opened = session.open(shaders, {}, {}, {false, gpu::Scene::empty}, {}, device);
            if (!opened) return std::unexpected{opened.error()};
            gpu::ThermalOptions options;
            options.enabled = true;
            session.set_thermal_options(options);
            gpu::ThermalBrush source{{60.5F, 60.5F, 60.5F, 60.5F}, 1, 120, gpu::ThermalTool::heat};
            while (session->metrics().steps < 120) {
                SimulationFrame f;
                f.elapsed = 1.0 / fps;
                f.focused = true;
                f.thermal_brush = source;
                session.advance(f);
            }
            glFinish();
            session->render(1);
            const auto dose = session->thermal_totals().external_enthalpy;
            if (reference < 0) reference = dose;
            require(session->metrics().steps == 120 && dose == reference, "energy_dose_independent_of_render_fps");
            SimulationFrame pause;
            pause.focused = pause.paused = true;
            pause.elapsed = 1;
            pause.thermal_brush = source;
            session.advance(pause);
            glFinish();
            session->render(1);
            require(session->metrics().steps == 120 && session->thermal_totals().external_enthalpy == dose,
                    "pause_applies_no_heat");
            SimulationFrame focus;
            focus.elapsed = 1;
            focus.thermal_brush = source;
            session.advance(focus);
            require(session->metrics().steps == 120, "unfocused_frame_applies_no_heat");
            SimulationFrame single;
            single.focused = single.paused = single.single_step = true;
            single.thermal_brush = source;
            session.advance(single);
            require(session->metrics().steps == 121, "single_step_advances_exactly_one_tick");
            SimulationFrame release;
            release.focused = true;
            session.advance(release);
            SimulationFrame short_click;
            short_click.focused = true;
            short_click.thermal_brush = source;
            session.advance(short_click);
            short_click.thermal_brush = {};
            short_click.elapsed = gpu::step_seconds;
            session.advance(short_click);
            require(session->metrics().steps == 122 && session->read_thermal().totals.external_enthalpy == dose + 10,
                    "short_heat_click_survives_zero_tick_frame");
            session.suspend();
            session.begin_recording();
            SimulationFrame held;
            held.focused = true;
            held.elapsed = 1.0 / 60;
            held.thermal_brush = source;
            for (unsigned frame = 0; frame < 30; ++frame) session.advance(held);
            glFinish();
            session->render(1);
            const auto recorded = session->thermal_totals().external_enthalpy;
            const auto folder = output / ("thermal-recording-" + std::to_string(fps));
            if (auto save = session.save_recording(folder); !save) return save;
            if (auto replay = session.replay_recording(folder / "session.txt"); !replay) return replay;
            glFinish();
            session->render(1);
            require(session->metrics().steps == 60 && session->thermal_totals().external_enthalpy == recorded,
                    "v3_replay_preserves_energy_commands");
            report << "fps=" << fps << ",dose=" << dose << '\n';
        }
        report << "result=PASS\n";
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "thermal-session-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::app
