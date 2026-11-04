#pragma once

namespace strefa::gpu {
// Size of the GPU world in cells. Kept apart from world.hpp so presentation code (the mission HUD) can size itself
// without the ParticleWorld declaration.
inline constexpr int width = 320, height = 192;
} // namespace strefa::gpu
