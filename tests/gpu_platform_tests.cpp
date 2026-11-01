#include "gpu/world.hpp"
#include "platform/window.hpp"

#include <SDL3/SDL_opengl.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path output{argv[2]};
    std::filesystem::create_directories(output);
    try {
        auto sdl = strefa::platform::SdlSystem::create();
        if (!sdl) throw std::runtime_error{sdl.error()};
        strefa::core::Settings settings;
        auto window = strefa::platform::Window::create(settings);
        if (!window) throw std::runtime_error{window.error()};
        // A driver may have rejected requested VSync during window creation.
        // Applying unrelated settings must preserve that working fallback.
        const bool actual_vsync = window->vsync_enabled();
        auto resized = settings;
        resized.width = 1100;
        if (!window->apply_video(resized, settings) || window->vsync_enabled() != actual_vsync)
            throw std::runtime_error{"Unrelated video settings broke the VSync fallback"};
        auto unsynced = resized;
        unsynced.vsync = false;
        if (!window->apply_video(unsynced, resized) || window->vsync_enabled())
            throw std::runtime_error{"Cannot retain or select disabled VSync"};
        GLint major{}, minor{};
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3)) return 77;

        auto created = strefa::gpu::ParticleWorld::create(argv[1]);
        if (!created) throw std::runtime_error{created.error()};
        auto& world = **created;
        std::ofstream report{output / "platform-dispatch.txt"};
        report << "renderer=" << glGetString(GL_RENDERER) << "\nversion=" << glGetString(GL_VERSION)
               << "\nconditional_work_effective=" << world.execution_options().conditional_work
               << "\nvideo_settings=PASS\n";
        for (const bool shielded : {false, true}) {
            std::vector<std::uint32_t> walls(strefa::gpu::width * strefa::gpu::height);
            for (int y = 187; y < strefa::gpu::height; ++y)
                for (int x = 0; x < strefa::gpu::width; ++x) walls[y * strefa::gpu::width + x] = 1;
            if (shielded)
                for (int y = 100; y < 187; ++y) walls[y * strefa::gpu::width + 62] = 1;
            strefa::gpu::Grain particle;
            particle.position = {68.5F, 160, 68.5F, 160};
            particle.motion = {0, 0, 1, 1};
            world.load(std::span{&particle, 1}, walls);
            strefa::gpu::MissionConfig mission;
            mission.spawn_x = 100;
            world.configure_mission(mission);
            world.queue_explosion({1, 54, 160, 24, 70});
            if (!world.step()) throw std::runtime_error{"Fresh fixture refused its first tick"};
            glFinish();
            world.poll();
            const auto state = world.readback();
            const auto events = world.read_mission();
            const float velocity = state.front().motion[0];
            report << (shielded ? "shielded" : "exposed") << "_vx=" << velocity << std::endl;
            if (!std::isfinite(velocity) || (shielded ? std::abs(velocity) >= .01F : velocity <= 10.F))
                throw std::runtime_error{"Blast dispatch skipped or ignored its terrain shield"};
            if (events.counts[0] != 1) throw std::runtime_error{"Explosion event lost or duplicated"};
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error{"OpenGL error in blast fixture"};
        }
        report << "result=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::ofstream{output / "platform-dispatch-error.txt"} << error.what() << '\n';
        std::cerr << error.what() << '\n';
        return 1;
    }
}
