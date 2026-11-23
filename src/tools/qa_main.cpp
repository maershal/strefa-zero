#include "app/application.hpp"
#include "app/diagnostic_scenarios.hpp"
#include "app/window_scenarios.hpp"
#include "tools/renderdoc_capture.hpp"

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
    strefa::tools::attach_renderdoc();
    // The runner is called once the window and its GL context exist, so SDL events work from here on.
    // Window scenarios (scene benchmark, frame profiles) observe the application's frame loop instead.
    const strefa::app::QaHooks qa{.options = strefa::app::check_qa_options,
                                  .diagnostics =
                                          [](const strefa::app::LaunchOptions& options,
                                             const std::filesystem::path& assets,
                                             const std::filesystem::path& profile) {
                                              strefa::tools::watch_renderdoc_hotkey(profile);
                                              return strefa::app::run_diagnostic_scenario(options, assets, profile);
                                          },
                                  .window = strefa::app::run_window_scenario};
    return strefa::app::run_main(argc, argv, strefa::app::OptionScope::all, qa);
}
