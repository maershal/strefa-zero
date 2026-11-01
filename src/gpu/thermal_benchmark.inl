namespace strefa::gpu {
std::expected<void, std::string> ParticleWorld::run_thermal_benchmark(const std::filesystem::path& output) {
    try {
        auto& s = *impl_;
        auto& gl = s.gl;
        auto original = profile_options();
        auto profile = original;
        profile.gpu_timers = false;
        profile.timer_mode = TimerMode::off;
        set_profile_options(profile);
        set_vfx_options({VfxQuality::off, false});
        std::ofstream csv{output / "thermal-samples.csv"}, passes{output / "thermal-pass-samples.csv"},
                pools{output / "thermal-pool-segments.csv"};
        csv << std::setprecision(12)
            << "scene,stage,block,order,variant,tick,rendered,physics_gpu_ms,render_gpu_ms,frame_gpu_ms,cpu_submit_ms,memory_bytes\n";
        passes << "scene,tick,heat_ms,gas_ms,reaction_ms,cpu_ms\n";
        pools << "scene,stage,block,order,variant,mass,H,chemical,oxygen,fuel,steam,smoke,finite\n";
        const std::array<std::string_view, 4> names{"cold", "fire", "oil", "quench"},
                stages{"off", "heat", "heat_gas", "full"};
        for (unsigned scene = 0; scene < 4; ++scene) {
            for (unsigned stage = 0; stage < 4; ++stage)
                for (unsigned block = 1; block <= 3; ++block)
                    for (unsigned order = 0; order < 4; ++order) {
                        const bool candidate = order == 1 || order == 2;
                        const unsigned mode = candidate ? stage : 0;
                        if (scene == 0) {
                            ThermalOptions t;
                            t.enabled = true;
                            set_thermal_options(t);
                            reset(Scene::sand_water);
                        } else
                            reset_thermal_scene(scene);
                        ThermalOptions t;
                        t.enabled = mode != 0;
                        t.transport = mode >= 2;
                        t.reactions = mode >= 3;
                        set_thermal_options(t);
                        for (unsigned n = 0; n < 120; ++n) {
                            while (!step()) glFinish();
                            if (n % 2 == 1) render(1);
                            if (n % 16 == 15) {
                                glFinish();
                                poll();
                            }
                        }
                        glFinish();
                        poll();
                        struct Row {
                            std::array<GLuint, 3> q{};
                            double cpu{};
                        };
                        std::array<Row, 240> rows{};
                        for (auto& r : rows) gl.GenQueries(3, r.q.data());
                        for (unsigned n = 0; n < rows.size(); ++n) {
                            auto& row = rows[n];
                            const auto began = SDL_GetTicksNS();
                            gl.QueryCounter(row.q[0], GL_TIMESTAMP);
                            while (!step()) glFinish();
                            gl.QueryCounter(row.q[1], GL_TIMESTAMP);
                            if (n % 2 == 1) render(1);
                            gl.QueryCounter(row.q[2], GL_TIMESTAMP);
                            row.cpu = static_cast<double>(SDL_GetTicksNS() - began) / 1e6;
                            if (n % 16 == 15) {
                                glFinish();
                                poll();
                            }
                        }
                        glFinish();
                        poll();
                        GLuint64 previous_start = 0;
                        for (unsigned n = 0; n < rows.size(); ++n) {
                            const auto& row = rows[n];
                            std::array<GLuint64, 3> stamp{};
                            for (unsigned k = 0; k < 3; ++k)
                                gl.GetQueryObjectui64v(row.q[k], GL_QUERY_RESULT, &stamp[k]);
                            if (n % 2 == 0) previous_start = stamp[0];
                            csv << names[scene] << ',' << stages[stage] << ',' << block << ',' << order << ','
                                << stages[mode] << ',' << n << ',' << (n % 2) << ','
                                << static_cast<double>(stamp[1] - stamp[0]) / 1e6 << ','
                                << static_cast<double>(stamp[2] - stamp[1]) / 1e6 << ','
                                << (n % 2 ? static_cast<double>(stamp[2] - previous_start) / 1e6 : 0) << ',' << row.cpu
                                << ',' << thermal_memory() << '\n';
                            gl.DeleteQueries(3, row.q.data());
                        }
                        const auto stat = s.thermal->snapshot().totals;
                        pools << names[scene] << ',' << stages[stage] << ',' << block << ',' << order << ','
                              << stages[mode] << ',' << stat.mass << ',' << stat.enthalpy << ',' << stat.chemical << ','
                              << stat.oxygen << ',' << stat.fuel << ',' << stat.steam << ',' << stat.smoke << ','
                              << stat.finite << '\n';
                        csv.flush();
                        pools.flush();
                        if (block == 1 && order == 1) {
                            render(1);
                            save_image(output / (std::string{names[scene]} + "-" + std::string{stages[mode]} + ".bmp"));
                        }
                    }
            if (scene == 0)
                reset(Scene::sand_water);
            else
                reset_thermal_scene(scene);
            ThermalOptions detailed;
            detailed.enabled = detailed.profile = true;
            set_thermal_options(detailed);
            for (unsigned n = 0; n < 120; ++n) {
                while (!step()) glFinish();
                if (n % 2 == 1) render(1);
                if (n % 8 == 7) {
                    glFinish();
                    poll();
                }
            }
            glFinish();
            s.thermal->poll();
            for (const auto& row : s.thermal->take_timings())
                passes << names[scene] << ',' << row.tick << ',' << row.heat_ms << ',' << row.gas_ms << ','
                       << row.reaction_ms << ',' << row.cpu_ms << '\n';
        }
        set_profile_options(original);
        ThermalOptions off;
        set_thermal_options(off);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error{"Thermal benchmark GL error"};
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "thermal-benchmark-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
