#include "game/session.hpp"
#include "sim/recording.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>

int main(int argc, char** argv) {
    using namespace strefa;
    try {
        int ticks = 180;
        std::string scene = "all";
        std::filesystem::path output;
        bool record = false, reference = false;
        sim::PhysicsConfig config;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            if (arg == "--ticks" && i + 1 < argc)
                ticks = std::stoi(argv[++i]);
            else if (arg == "--scene" && i + 1 < argc)
                scene = argv[++i];
            else if (arg == "--output" && i + 1 < argc)
                output = std::filesystem::path{reinterpret_cast<const char8_t*>(argv[++i])};
            else if (arg == "--record")
                record = true;
            else if (arg == "--reference")
                reference = true;
            else if (arg == "--threads" && i + 1 < argc) {
                const int count = std::stoi(argv[++i]);
                if (count < 1 || count > 4) throw std::runtime_error{"Threads must be 1..4"};
                config.worker_threads = static_cast<unsigned>(count);
            } else if (arg == "--ic0")
                config.modified_cholesky = false;
            else if (arg == "--mic-only")
                config.coarse_pressure = false;
            else if (arg == "--pressure-reference") {
                config.reference_pressure = true;
                config.modified_cholesky = false;
            } else
                throw std::runtime_error{
                        "Usage: StrefaPhysics --scene all|arena|<scene-id> --ticks N --output DIR [--record] [--reference] [--threads 1..4] [--ic0] [--mic-only] [--pressure-reference]"};
        }
        if (ticks < 3 || ticks > 36000) throw std::runtime_error{"Ticks must be 3..36000"};
        if (record && output.empty()) throw std::runtime_error{"Recording requires --output DIR"};
        std::ofstream summary, html;
        if (!output.empty()) {
            std::filesystem::create_directories(output);
            summary.open(output / "summary.csv");
            html.open(output / "index.html");
            summary << "scene,ticks,median_ms,p95_ms,p99_ms,max_ms,max_mass_error,max_energy_error,max_pcg_residual,nonconverged_ticks,max_overfill,finite\n";
            html << "<!doctype html><meta charset=\"utf-8\"><title>Strefa Zero 0.4 — sceny</title><style>body{font:16px system-ui;background:#111c22;color:#e0e9d4;margin:4vw}a{color:#c2df8b}td{padding:10px;border-bottom:1px solid #425242}</style><h1>Strefa Zero 0.4 — sceny regresyjne</h1><p>Krok fizyki 60 Hz; obrazy co 3 kroki (20 FPS). Pomiar nie obejmuje renderowania ani zapisu. Jedna jednostka siatki = jeden piksel świata.</p><table><tr><th>Scena</th><th>Mediana / p95 / p99 / max [ms]</th><th>Bilans masy / energii</th><th>PCG: brak zbieżności</th></tr>";
        }
        bool valid = true, selected = false;
        for (unsigned index = 0; index <= sim::scene_ids.size(); ++index) {
            const bool arena = index == sim::scene_ids.size();
            const std::string_view id = arena ? "arena" : sim::scene_ids[index];
            if (scene != "all" && scene != id) continue;
            if (scene == "all" && index == 0) continue;
            selected = true;
            sim::World world{0x57EF'A023U, config};
            game::GameSession game{{}, {}, config};
            if (!arena) world.load_scene(static_cast<sim::Scene>(index));
            std::optional<sim::Recording> recorder;
            if (record) recorder.emplace(output / id);
            std::vector<double> times;
            std::ofstream profile;
            if (!output.empty()) {
                profile.open(output / (std::string{id} + "-profile.csv"));
                profile << "tick,total_ms,heat_ms,air_ms,liquid_ms,reaction_ms,motion_ms,bookkeeping_ms,pressure_ms,transport_ms,pcg_iterations,liquid_substeps,air_substeps,mass_error,energy_error,residual,converged,overfill,air_wave_ms,air_transport_ms,thermal_refresh_ms\n";
            }
            double mass_error = 0, energy_error = 0, residual = 0, overfill = 0;
            int failed = 0;
            bool scene_finite = true;
            for (int n = 0; n < ticks; ++n) {
                const auto start = std::chrono::steady_clock::now();
                if (arena) {
                    game::TickInput input;
                    input.aim = {184, 140};
                    input.move = n < 40 ? 1.F : 0.F;
                    input.jump = n == 3;
                    input.fire = n > 30 && n < 150;
                    input.grenade = n == 45;
                    game.tick(input);
                } else
                    world.step(reference ? sim::ScanMode::full_grid : sim::ScanMode::active_chunks);
                const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                times.push_back(ms);
                const auto& current = arena ? game.world() : world;
                const auto& s = current.statistics();
                mass_error = std::max(mass_error, std::abs(s.mass_error));
                energy_error = std::max(energy_error, std::abs(s.energy_error));
                residual = std::max(residual, s.pressure_residual);
                overfill = std::max(overfill, s.max_overfill);
                if (!s.pressure_converged) ++failed;
                valid = valid && s.finite && s.pressure_converged && std::abs(s.mass_error) < 1e-5
                     && std::abs(s.energy_error) < 1e-3;
                scene_finite = scene_finite && s.finite;
                if (profile)
                    profile << s.tick << ',' << ms << ',' << s.heat_ms << ',' << s.air_ms << ',' << s.liquid_ms << ','
                            << s.reaction_ms << ',' << s.motion_ms << ',' << s.bookkeeping_ms << ',' << s.pressure_ms
                            << ',' << s.transport_ms << ',' << s.pressure_iterations << ',' << s.liquid_substeps << ','
                            << s.air_substeps << ',' << s.mass_error << ',' << s.energy_error << ','
                            << s.pressure_residual << ',' << s.pressure_converged << ',' << s.max_overfill << ','
                            << s.air_wave_ms << ',' << s.air_transport_ms << ',' << s.thermal_refresh_ms << '\n';
                if (recorder) recorder->capture(current, ms);
            }
            if (recorder) recorder->finish();
            std::ranges::sort(times);
            auto percentile = [&](double q) {
                return times[std::min(times.size() - 1, static_cast<std::size_t>(q * times.size()))];
            };
            std::cout << id << ": median=" << percentile(.5) << " p95=" << percentile(.95) << " p99=" << percentile(.99)
                      << " max=" << times.back() << " ms; dm=" << mass_error << " dH=" << energy_error
                      << " residual=" << residual << " failed=" << failed << " overfill=" << overfill << std::endl;
            const auto& last = (arena ? game.world() : world).statistics();
            std::cout << "  last tick: heat=" << last.heat_ms << " air=" << last.air_ms << " liquid=" << last.liquid_ms
                      << " chemistry=" << last.reaction_ms << " other=" << last.bookkeeping_ms
                      << " PCG=" << last.pressure_iterations << " pressure=" << last.pressure_ms
                      << " transport=" << last.transport_ms << " checksum=" << (arena ? game.world() : world).checksum()
                      << std::endl;
            if (summary)
                summary << id << ',' << ticks << ',' << percentile(.5) << ',' << percentile(.95) << ','
                        << percentile(.99) << ',' << times.back() << ',' << mass_error << ',' << energy_error << ','
                        << residual << ',' << failed << ',' << overfill << ',' << scene_finite << '\n';
            if (html)
                html << "<tr><td><a href=\"" << (record ? std::string{id} + "/index.html" : "summary.csv") << "\">"
                     << id << "</a></td><td>" << percentile(.5) << " / " << percentile(.95) << " / " << percentile(.99)
                     << " / " << times.back() << "</td><td>" << mass_error << " / " << energy_error << "</td><td>"
                     << failed << "</td></tr>";
        }
        if (html)
            html << "</table><p>Pełne bilanse i residua: summary.csv oraz steps.csv w katalogach scen. Model akustyczny ma ograniczoną amplitudę; nie opisuje detonacji w jednostkach SI.</p>";
        if (!selected) throw std::runtime_error{"Unknown scene identifier"};
        return valid ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
