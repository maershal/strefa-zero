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
        if (options->smoke_test||options->laboratory_smoke_test) {
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
