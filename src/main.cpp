#include "app/application.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <exception>
#include <fstream>

int main(int argc, char** argv) {
    const auto options = strefa::app::parse_options(argc, argv);
    if (!options) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strefa Zero", options.error().c_str(), nullptr);
        return 1;
    }
    const auto report_error = [&](const std::string& error) {
        if (options->smoke_test||options->laboratory_smoke_test||options->gpu_test||options->gpu_liquid_test||options->gpu_flow_test||options->gpu_smoke_test||options->mission_test||options->mission_smoke||options->gpu_profile||options->stabilization_profile||options->work_test||options->transport_test||options->hot_test||options->cost_profile||options->cost_check||options->thermal_check||options->thermal_benchmark||options->vfx_check||options->vfx_benchmark||options->lighting_check||options->lighting_benchmark||options->render_check||options->render_benchmark||options->fluid_check||options->fluid_benchmark||options->gpu_profile_check||options->readback_test||options->readback_profile||options->execution_test||options->execution_profile||options->neighbor_test||options->neighbor_profile||options->coupling_test||options->coupling_profile||options->session_smoke||options->cache_check) {
            std::error_code ignored;
            std::filesystem::create_directories(options->profile, ignored);
            std::ofstream{options->profile / "smoke-error.txt"} << error;
        } else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strefa Zero — błąd", error.c_str(), nullptr);
    };
    try {
        const auto result = strefa::app::run(*options);
        if (!result) { report_error(result.error()); return 1; }
        return *result;
    } catch (const std::exception& error) {
        report_error(error.what());
        return 1;
    }
}
