#include "sim/laboratory.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace strefa::sim;
using namespace std::chrono_literals;
void check(bool condition,const char* message){if(!condition) throw std::runtime_error{message};}
void snapshots_and_commands() {
    PhysicsConfig config;config.thermal=config.air=config.fluids=config.motion=config.reactions=false;
    LaboratoryRunner runner{{},Scene::sand_pile,config};
    check(runner.wait_for(0,5s),"Worker initializes without requiring a UI frame");
    auto initial=runner.snapshot();check(initial->statistics().tick==0,"Inactive lab does not simulate");
    LaboratoryControl control{true,true,1,RenderMode::materials,Point{150,30}};
    LaboratoryCommand command;command.manual_steps=3;
    const auto ticket=runner.submit(control,command);
    check(ticket!=0&&runner.wait_for(ticket,5s),"Queued manual ticks complete");
    auto stepped=runner.snapshot();
    check(stepped->statistics().tick==3,"Three requested steps advance exactly three ticks");
    check(initial->statistics().tick==0,"Published snapshots are immutable");
    check(stepped->sample({150,30})->material==Material::sand,"Worker publishes matching inspector sample");
    check(!stepped->sample({151,30}),"Inspector never labels a stale sample with new coordinates");
    const auto pixels=stepped->pixels;
    command={};command.toggle_boundary=true;
    const auto boundary=runner.submit(control,command);
    check(runner.wait_for(boundary,5s),"Paused worker accepts commands");
    auto changed=runner.snapshot();
    check(!changed->config().open_top,"Boundary command applied");
    check(changed->pixels==pixels,"Nonvisual commands reuse the image buffer");
    command={};command.clear=true;
    const auto cleared=runner.submit(control,command);
    check(runner.wait_for(cleared,5s),"Clear command acknowledged");
    check(runner.snapshot()->statistics().tick==0&&runner.snapshot()->statistics().particles==0,"Clear replaces world atomically");
    check(stepped->statistics().particles>0,"Old snapshot survives reset without mutation");
    runner.stop();
}
}
int main(){try{snapshots_and_commands();std::cout<<"PASS: worker, immutable snapshots, inspector, pause, steps, commands and shutdown\n";return 0;}
catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
