#include "gpu/world.hpp"

namespace strefa::gpu {
MissionInput scripted_mission_input(unsigned tick) {
    if (tick < 60) return {0, 54, 177, fire};
    if (tick < 400) return {1, 164, 177, (tick < 240 ? jet : 0U) | (tick == 60 ? jump : 0U)};
    if (tick < 460) return {0, 164, 177, fire};
    if (tick < 580) return {0, 132, 145, fire};
    if (tick < 790) return {1, 276, 177, 0};
    if (tick < 850) return {0, 276, 177, fire};
    if (tick < 880) return {0, 230, 125, reload};
    if (tick < 1100) return {0, 230, 125, fire};
    if (tick < 1340) return {1, 308, 177, (tick < 1260 ? jet : 0U) | (tick == 1100 ? jump : 0U)};
    return {0, 308, 177, 0};
}
} // namespace strefa::gpu
