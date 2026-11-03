#pragma once
#include "sim/world.hpp"

namespace strefa::sim {
// Fixed layouts shared by StrefaPhysics and the CPU tests; not laboratory scenes.
// A ~120x33 gunpowder slab on a stone floor, lit at one end, so every tick detonates a batch of queued explosions.
void load_powder_chain(World& world);
// A wood slab on a stone floor; load_wood_chip adds one projectile hit on its top face, where the "chip" branch of
// projectile_hit loosens the hit cell and its wood neighbours instead of igniting anything.
void load_wood_slab(World& world);
void load_wood_chip(World& world);
} // namespace strefa::sim
