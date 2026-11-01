#include "app/application.hpp"
#include "app/diagnostic_scenarios.hpp"

#include <SDL3/SDL_main.h>

// StrefaQA: the game plus every validation, benchmark, profile and tuning option.
int main(int argc, char** argv) {
    return strefa::app::run_main(argc, argv, strefa::app::OptionScope::all, strefa::app::run_diagnostic_scenario);
}
