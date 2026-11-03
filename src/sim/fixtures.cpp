#include "sim/fixtures.hpp"

namespace strefa::sim {
void load_powder_chain(World& world) {
    for (int y = 180; y <= 183; ++y)
        for (int x = 96; x <= 223; ++x) world.set({x, y}, Material::stone);
    for (int y = 147; y <= 179; ++y)
        for (int x = 100; x <= 219; ++x) world.set({x, y}, Material::gunpowder);
    world.projectile_hit({100, 179});
}
void load_wood_slab(World& world) {
    for (int y = 180; y <= 183; ++y)
        for (int x = 96; x <= 223; ++x) world.set({x, y}, Material::stone);
    for (int y = 160; y <= 179; ++y)
        for (int x = 140; x <= 179; ++x) world.set({x, y}, Material::wood);
}
void load_wood_chip(World& world) {
    load_wood_slab(world);
    world.projectile_hit({160, 160});
}
} // namespace strefa::sim
