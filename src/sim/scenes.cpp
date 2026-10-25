#include "sim/world.hpp"
#include <algorithm>

namespace strefa::sim {
void World::load_scene(Scene scene) {
    clear();
    rectangle({0, 184}, {319, 191}, Material::stone);
    auto box = [&](int left, int right, int bottom) {
        rectangle({left, 80}, {left, bottom}, Material::metal);
        rectangle({right, 80}, {right, bottom}, Material::metal);
        rectangle({left, bottom}, {right, bottom}, Material::metal);
    };
    switch (scene) {
    case Scene::demonstration:
        rectangle({8, 145}, {92, 148}, Material::stone);
        rectangle({16, 30}, {45, 45}, Material::sand);
        box(100, 162, 182); rectangle({102, 145}, {160, 180}, Material::water);
        rectangle({102, 132}, {160, 142}, Material::oil);
        rectangle({180, 165}, {225, 180}, Material::wood);
        set({181, 164}, Material::fire);
        set({180,175},Material::wood,800);
        rectangle({252, 170}, {285, 183}, Material::metal);
        rectangle({255, 164}, {263, 169}, Material::thermite);
        set_temperature({258, 168}, 1250);
        break;
    case Scene::falling_water:
        box(70, 245, 183); rectangle({124, 20}, {172, 54}, Material::water); break;
    case Scene::dam_break:
        box(60, 260, 183); rectangle({62, 85}, {122, 181}, Material::water);
        rectangle({124, 80}, {124, 142}, Material::metal); break;
    case Scene::connected_vessels:
        box(90, 230, 183); rectangle({120, 80}, {200, 163}, Material::metal);
        rectangle({92, 100}, {118, 181}, Material::water);
        rectangle({119, 165}, {228, 181}, Material::water);
        rectangle({202, 165}, {228, 181}, Material::water); break;
    case Scene::oil_water:
        box(100, 220, 183); rectangle({102, 146}, {218, 181}, Material::oil);
        rectangle({102, 110}, {218, 145}, Material::water); break;
    case Scene::sand_pile:
        rectangle({139, 15}, {179, 70}, Material::sand); break;
    case Scene::sand_water:
        box(100, 220, 183); rectangle({102, 130}, {218, 181}, Material::water);
        rectangle({140, 65}, {175, 94}, Material::sand); break;
    case Scene::smoke_ceiling: case Scene::smoke_vent:
        rectangle({70, 65}, {250, 66}, Material::metal);
        rectangle({70, 65}, {70, 183}, Material::metal);
        rectangle({250, 65}, {250, 183}, Material::metal);
        if (scene == Scene::smoke_vent) set({160, 65}, Material::empty), set({160, 66}, Material::empty);
        rectangle({145, 150}, {175, 172}, Material::smoke); break;
    case Scene::wood_fire: case Scene::extinguish:
        rectangle({140, 145}, {180, 183}, Material::wood);
        set({140,175},Material::wood,800);
        for (int y = 157; y < 182; y += 3) set({139, y}, Material::fire);
        if (scene == Scene::extinguish) rectangle({135, 70}, {187, 100}, Material::water);
        break;
    case Scene::melting_metal:
        // A preheated crucible makes the latent-heat transition visible during
        // a short recording; all authored heat is part of the initial ledger.
        for(int y=180;y<=183;++y) for(int x=130;x<=193;++x) set({x,y},Material::metal,1700);
        for(int y=176;y<=179;++y) for(int x=140;x<=180;++x) set({x,y},Material::thermite,2000);
        break;
    case Scene::quench_metal:
        box(110, 210, 183); rectangle({112, 135}, {208, 173}, Material::water);
        rectangle({140, 98}, {177, 115}, Material::molten_metal); break;
    case Scene::ice_fire:
        rectangle({135, 148}, {180, 183}, Material::ice);
        for (int y = 148; y < 181; y += 3) set({134, y}, Material::fire); break;
    case Scene::shielded_explosion:
        rectangle({160, 120}, {163, 183}, Material::metal);
        rectangle({146, 150}, {153, 177}, Material::wood);
        rectangle({169, 150}, {176, 177}, Material::wood);
        queue_explosion(ExplosionCommand{{153, 162}, 24, 4000, 50, 45}); break;
    default: break;
    }
    detail::refresh_thermal(state_); detail::update_report(state_);
}
} // namespace strefa::sim

