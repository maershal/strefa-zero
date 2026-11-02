#include "app/application.hpp"
#include "app/diagnostic_scenarios.hpp"

#include <SDL3/SDL_main.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// StrefaQA: the game plus every validation, benchmark, profile and tuning option.
int main(int argc, char** argv) {
#ifdef _WIN32
    // Summary lines and errors are Polish UTF-8 text; the default OEM code page garbles them.
    SetConsoleOutputCP(CP_UTF8);
#endif
    return strefa::app::run_main(argc, argv, strefa::app::OptionScope::all, strefa::app::run_diagnostic_scenario);
}
