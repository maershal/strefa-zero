#include "sim/world.hpp"
#include "sim/fixed_clock.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace strefa::sim;
void check(bool value, const char* message) { if (!value) throw std::runtime_error{message}; }
void near(double a, double b, double tolerance, const char* message) {
    if (std::abs(a - b) > tolerance) {
        std::cerr << message << ": " << a << " vs " << b << '\n'; throw std::runtime_error{message};
    }
}
PhysicsConfig isolated() {
    PhysicsConfig c; c.open_top = c.air = c.fluids = c.reactions = c.motion = false; return c;
}
void fill(World& w, Point a, Point b, Material m) {
    for (int y = a.y; y <= b.y; ++y) for (int x = a.x; x <= b.x; ++x) w.set({x,y}, m);
}
void ledger(const World& w) {
    check(w.statistics().finite, "All fields must remain finite");
    near(w.statistics().mass_error, 0, 2e-6, "Mass ledger");
    near(w.statistics().energy_error, 0, 2e-4, "Enthalpy ledger");
}
void thermal() {
    auto config = isolated(); config.heat_contact = 0;
    World w{1, config}; w.set({15,30}, Material::metal, 400); w.set({16,30}, Material::metal, 300);
    for (int n = 0; n < 240; ++n) w.step();
    near(w.sample({15,30}).temperature, 350, .001, "Thermal equilibrium across chunk boundary");
    near(w.sample({16,30}).temperature, 350, .001, "Symmetric heat exchange"); ledger(w);
    World phase{2, config}; phase.set({20,20}, Material::ice, 273.15);
    const double mass = phase.mass_of(Material::ice);
    phase.inject_heat({20,20}, mass * 334 * .5);
    near(phase.mass_of(Material::water), mass * .5, 1e-9, "Latent heat must melt half the ice");
    near(phase.mass_of(Material::ice), mass * .5, 1e-9, "Partial phase coexistence");
    phase.inject_heat({20,20}, mass * 334 * .5 + 1);
    near(phase.mass_of(Material::water), mass, 1e-9, "Complete melting conserves mass"); ledger(phase);
    phase.set_temperature({20,20}, 200);
    for (int n = 0; n < 40; ++n) phase.step();
    check(phase.mass_of(Material::ice) > 0, "Cooling must freeze water"); ledger(phase);
    World metal{3, config}; metal.set({30,30}, Material::metal, 1811);
    const auto original = metal.mass_of(Material::metal);
    metal.inject_heat({30,30}, original * 270 + 10);
    near(metal.mass_of(Material::molten_metal), original, 1e-9, "Molten metal keeps identity");
    metal.set_temperature({30,30}, 1000);
    check(metal.mass_of(Material::metal) > 0, "Metal must solidify again"); ledger(metal);
    World cold{3,config}; cold.set({30,30},Material::water);
    cold.inject_heat({30,30},-1e9);
    check(cold.sample({30,30}).temperature>=1-1e-7,"Cooling clamps at a positive absolute temperature"); ledger(cold);
    World steam{4, config}; steam.set({40,40}, Material::steam, 600);
    const double vapor = steam.mass_of(Material::steam);
    for (int n = 0; n < 250; ++n) steam.step();
    near(steam.mass_of(Material::steam), vapor, 1e-10, "Hot steam has no four-second TTL");
    steam.set_temperature({40,40}, 280); steam.step();
    check(steam.mass_of(Material::water) > 0, "Cooling condenses vapor"); ledger(steam);
}
void liquids() {
    auto c = isolated(); c.thermal = false; c.fluids = true;
    World w{9,c}; fill(w,{80,140},{120,140},Material::metal);
    fill(w,{80,80},{80,139},Material::metal); fill(w,{120,80},{120,139},Material::metal);
    fill(w,{81,120},{119,139},Material::water);
    const double mass = w.mass_of(Material::water);
    for (int n=0;n<100;++n) w.step();
    near(w.mass_of(Material::water),mass,1e-7,"Conservative liquid transport"); ledger(w);
    std::cout << "Hydrostatic p(top/bottom)=" << w.sample({100,122}).liquid_pressure << '/'
        << w.sample({100,138}).liquid_pressure << " residual=" << w.statistics().pressure_residual << '\n';
    check(w.sample({100,138}).liquid_pressure > w.sample({100,122}).liquid_pressure + 1000,
        "Hydrostatic pressure must increase with depth");
    check(w.statistics().pressure_converged, "PCG must converge in a static tank");
    for(int y=0;y<192;++y) for(int x=121;x<320;++x)
        check(w.sample({x,y}).liquid_fill < 1e-8,"One-cell wall cannot leak liquid");
    World mix{10,c}; fill(mix,{150,140},{160,140},Material::metal);
    fill(mix,{150,80},{150,139},Material::metal); fill(mix,{160,80},{160,139},Material::metal);
    fill(mix,{151,130},{159,139},Material::oil); fill(mix,{151,120},{159,129},Material::water);
    const double water=mix.mass_of(Material::water), oil=mix.mass_of(Material::oil);
    for(int n=0;n<100;++n) mix.step();
    near(mix.mass_of(Material::water),water,1e-8,"Water component mass");
    near(mix.mass_of(Material::oil),oil,1e-8,"Oil component mass"); ledger(mix);
    double water_moment=0;
    for(int y=0;y<192;++y) for(int x=151;x<160;++x) water_moment+=y*mix.sample({x,y}).liquid_fractions[0];
    check(water_moment/water>125,"Dense water starts sinking through oil");
    World vessels{11,c}; vessels.load_scene(Scene::connected_vessels);
    double right_before=0;
    for(int y=80;y<163;++y) for(int x=202;x<229;++x) right_before+=vessels.sample({x,y}).liquid_fill;
    for(int n=0;n<160;++n) vessels.step();
    double right_after=0;
    for(int y=80;y<163;++y) for(int x=202;x<229;++x) right_after+=vessels.sample({x,y}).liquid_fill;
    std::cout<<"Connected vessels transfer="<<right_after-right_before<<'\n';
    check(right_after>right_before+20,"Pressure must raise the other connected vessel"); ledger(vessels);
}
void reactions_and_air() {
    auto c=isolated(); c.thermal=false; c.reactions=true;
    World fire{1,c}; fire.set({40,40},Material::wood,900);
    const auto fuel=fire.sample({40,40}).fuel;
    for(int n=0;n<20;++n) fire.step();
    check(fire.sample({40,40}).fuel<fuel,"Combustion consumes fuel");
    check(fire.at({40,40}).material==Material::wood,"Burning wood retains its structure"); ledger(fire);
    World starved{1,c}; starved.set({40,40},Material::wood,900); starved.set_oxygen(0);
    const auto starved_fuel=starved.sample({40,40}).fuel;
    for(int n=0;n<20;++n) starved.step();
    near(starved.sample({40,40}).fuel,starved_fuel,1e-12,"Oxygen starvation stops combustion"); ledger(starved);
    World acid{2,c}; acid.set({80,80},Material::acid); acid.set({81,80},Material::stone);
    const double stone=acid.mass_of(Material::stone);
    for(int n=0;n<160;++n) acid.step();
    check(acid.mass_of(Material::stone)<stone && acid.sample({80,80}).dissolved>0,"Acid retains dissolved products"); ledger(acid);
    c.reactions=false; c.air=true;
    World air{3,c}; air.queue_explosion(ExplosionCommand{{90,100},2,0,10,0});
    double free_peak=0;
    for(int n=0;n<25;++n) {air.step();free_peak=std::max(free_peak,std::abs(air.sample({94,100}).pressure));}
    check(std::abs(air.sample({100,100}).pressure)>1e-5,"Acoustic pulse propagates through air"); ledger(air);
    World shield{3,c}; fill(shield,{95,0},{95,191},Material::metal);
    shield.queue_explosion(ExplosionCommand{{90,100},2,0,10,0});
    double wall_peak=0;
    for(int n=0;n<25;++n) {shield.step();wall_peak=std::max(wall_peak,std::abs(shield.sample({94,100}).pressure));}
    check(wall_peak>free_peak*1.02,"Rigid wall reflects and amplifies incident pressure at the boundary");
    near(shield.sample({100,100}).pressure,0,1e-10,"Thin wall blocks direct pressure pulse"); ledger(shield);
    World extreme{4,c}; extreme.queue_explosion(ExplosionCommand{{40,40},4,100000,100000,100000});
    for(int n=0;n<20;++n) extreme.step(); ledger(extreme);
    check(extreme.statistics().finite,"Strong source stays finite without discarding enthalpy");
    c.air=false; c.reactions=true;
    World chain{5,c};
    fill(chain,{70,70},{72,72},Material::nitroglycerin);
    chain.projectile_hit({70,71});
    for(int n=0;n<5;++n) chain.step();
    near(chain.mass_of(Material::nitroglycerin),0,1e-9,"Detonation shock triggers a full liquid charge"); ledger(chain);
    World protected_charge{6,c}; fill(protected_charge,{80,50},{80,100},Material::metal);
    protected_charge.set({82,70},Material::nitroglycerin);
    protected_charge.queue_explosion(ExplosionCommand{{77,70},12,0,30,0});
    for(int n=0;n<5;++n) protected_charge.step();
    near(protected_charge.mass_of(Material::nitroglycerin),1.2,1e-9,"Armor shields the charge from direct shock"); ledger(protected_charge);
}
void reference_and_timing() {
    World active{123},reference{123}; active.load_scene(Scene::sand_water); reference.load_scene(Scene::sand_water);
    for(int n=0;n<45;++n) {
        active.step(); reference.step(ScanMode::full_grid);
        check(active.checksum()==reference.checksum(),"Active/reference state equivalence");
    }
    for(int fps:{30,60,144}) {
        FixedClock clock; int ticks=0;
        for(int frame=0;frame<fps*10;++frame) ticks+=clock.advance(1.0/fps,false,1);
        check(ticks==600,"Frame rate independent fixed clock");
        check(clock.advance(20,true,1)==0 && clock.advance(0,true,1,true)==1,"Pause and manual step");
    }
    World bounds; check(!bounds.set({-1,0},Material::sand),"Bounds reject invalid edits");
    const auto hash=bounds.checksum();
    bounds.paint({std::numeric_limits<int>::min(),0},{0,0},10000,Material::sand);
    check(bounds.checksum()==hash,"Unbounded brush rejected");
}
void pressure_reference() {
    auto optimized=isolated();optimized.thermal=false;optimized.fluids=true;optimized.modified_cholesky=false;
    auto slow=optimized;slow.reference_pressure=true;
    for(const auto scene:{Scene::falling_water,Scene::connected_vessels,Scene::oil_water}) {
        World a{123,optimized},b{123,slow};a.load_scene(scene);b.load_scene(scene);
        for(int n=0;n<24;++n){a.step();b.step();}
        for(int y=0;y<192;y+=3) for(int x=0;x<320;x+=3) {
            const auto actual=a.sample({x,y}),expected=b.sample({x,y});
            near(actual.liquid_pressure,expected.liquid_pressure,1e-6,"Compact/reference pressure");
            near(actual.liquid_fill,expected.liquid_fill,1e-8,"Compact/reference liquid transport");
            near(actual.liquid_velocity.x,expected.liquid_velocity.x,1e-6,"Compact/reference horizontal velocity");
            near(actual.liquid_velocity.y,expected.liquid_velocity.y,1e-6,"Compact/reference vertical velocity");
        }
        ledger(a);ledger(b);
    }
}
void benchmark() {
    World w; w.load_demo(); std::vector<double> times;
    for(int n=0;n<180;++n) { auto start=std::chrono::steady_clock::now(); w.step();
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()); }
    std::ranges::sort(times);
    std::cout<<"Physics 320x192: median="<<times[90]<<" p95="<<times[171]<<" p99="<<times[178]
        <<" max="<<times.back()<<" ms; residual="<<w.statistics().pressure_residual<<" overfill="<<w.statistics().max_overfill<<'\n';
    ledger(w);
    const auto& p=w.statistics();
    std::cout<<"Last tick heat="<<p.heat_ms<<" air="<<p.air_ms<<" liquids="<<p.liquid_ms<<" reactions="<<p.reaction_ms<<" motion="<<p.motion_ms<<" bookkeeping="<<p.bookkeeping_ms<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view{argv[1]}=="--benchmark") { benchmark(); return 0; }
        thermal(); std::cout<<"PASS thermal and latent heat\n";
        liquids(); std::cout<<"PASS liquid mass, walls, density and hydrostatics\n";
        reactions_and_air(); std::cout<<"PASS reactions, oxygen, acid, acoustics and shielding\n";
        reference_and_timing();pressure_reference(); std::cout<<"PASS reference, compact PCG and fixed clock\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}

