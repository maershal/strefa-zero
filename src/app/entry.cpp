#include "app/application.hpp"

#include <SDL3/SDL.h>

#include <exception>
#include <fstream>
#include <iostream>

namespace strefa::app {

int run_main(int argc, char** argv, OptionScope scope, const DiagnosticRunner& diagnostics) {
    const auto options = parse_options(argc, argv, scope);
    if (!options) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strefa Zero", options.error().c_str(), nullptr);
        return 1;
    }
    if (options->help) {
        const auto text = help_text(scope);
        std::cout << text;
#ifdef _WIN32
        // StrefaZero is a GUI-subsystem executable without a console on Windows.
        if (scope == OptionScope::player)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Strefa Zero", text.c_str(), nullptr);
#endif
        return 0;
    }
    const auto report_error = [&](const std::string& error) {
        if (options->is_test()) {
            std::error_code ignored;
            std::filesystem::create_directories(options->profile, ignored);
            std::ofstream{options->profile / "smoke-error.txt"} << error;
        } else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strefa Zero — błąd", error.c_str(), nullptr);
    };
    try {
        const auto result = run(*options, diagnostics);
        if (!result) {
            report_error(result.error());
            return 1;
        }
        return *result;
    } catch (const std::exception& error) {
        report_error(error.what());
        return 1;
    }
}

} // namespace strefa::app
