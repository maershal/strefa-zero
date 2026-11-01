#include "app/application.hpp"

#include <SDL3/SDL_main.h>

int main(int argc, char** argv) {
    return strefa::app::run_main(argc, argv, strefa::app::OptionScope::player);
}
