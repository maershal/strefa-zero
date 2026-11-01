#include "gpu/world.hpp"
#include "platform/window.hpp"
#include "platform/gl_debug.hpp"
#include <SDL3/SDL_opengl.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const std::filesystem::path output{argv[2]};std::filesystem::create_directories(output);
    try{
        auto sdl=strefa::platform::SdlSystem::create();if(!sdl)throw std::runtime_error{sdl.error()};
        strefa::core::Settings settings;auto window=strefa::platform::Window::create(settings,true,true);if(!window)throw std::runtime_error{window.error()};
        auto debug=strefa::platform::GlDebugLog::create(output/"gl-debug.txt");if(debug)debug->set_phase(strefa::platform::DebugPhase::validation);
        strefa::gpu::ExecutionOptions execution;execution.soa=false;execution.neighbors=strefa::gpu::NeighborMode::sorted;
        auto created=strefa::gpu::ParticleWorld::create(argv[1],execution);if(!created)throw std::runtime_error{created.error()};auto& world=**created;
        if(auto result=world.validate_thermal(output/"aos");!result)throw std::runtime_error{result.error()};
        std::ofstream report{output/"thermal-edges-tests.txt"},csv{output/"thermal-door.csv"};csv<<std::setprecision(12)<<"phase,mass,H,chemical,external_mass,external_H,external_chemical,right_smoke\n";
        const auto require=[&](bool ok,const char* text){report<<text<<'='<<(ok?"PASS":"FAIL")<<'\n';report.flush();if(!ok)throw std::runtime_error{text};};
        std::vector<std::uint32_t> walls(strefa::gpu::width*strefa::gpu::height);for(int y=0;y<strefa::gpu::height;++y)walls[y*strefa::gpu::width+160]=1;
        world.load({},walls);strefa::gpu::ThermalOptions options;options.enabled=true;options.reactions=false;world.set_thermal_options(options);const auto initial=world.read_thermal().totals;
        world.set_thermal_brush({{158.5F,90.5F,158.5F,90.5F},1,.05F,strefa::gpu::ThermalTool::smoke});
        const auto steps=[&]{for(unsigned n=0;n<600;++n){while(!world.step())glFinish();if(n%8==7){glFinish();world.poll();}}glFinish();};
        const auto sample=[&](const char* phase){const auto snapshot=world.read_thermal();double right=0;for(int y=0;y<strefa::gpu::height;++y)for(int x=161;x<strefa::gpu::width;++x)right+=snapshot.cells[y*strefa::gpu::width+x].gas0[3];const auto& t=snapshot.totals;csv<<phase<<','<<t.mass<<','<<t.enthalpy<<','<<t.chemical<<','<<t.external_mass<<','<<t.external_enthalpy<<','<<t.external_chemical<<','<<right<<'\n';return std::pair{t,right};};
        steps();const auto closed=sample("closed");require(closed.second<1e-8,"closed_thin_wall_blocks_smoke");
        world.paint({160.5F,90.5F,160.5F,90.5F,2,strefa::gpu::Material::erase});steps();const auto opened=sample("open");require(opened.second>1e-8,"edited_door_admits_smoke");
        const auto& end=opened.first;const double massError=(end.mass-initial.mass)-(end.external_mass-initial.external_mass),energyError=(end.enthalpy+end.chemical-initial.enthalpy-initial.chemical)-(end.external_enthalpy+end.external_chemical-initial.external_enthalpy-initial.external_chemical);
        require(end.finite&&std::abs(massError)/initial.mass<.001&&std::abs(energyError)/(initial.enthalpy+initial.chemical)<.001,"edited_world_balances_sources_and_removal");
        world.request_thermal_inspection(160,90);glFinish();world.render(1);require(world.thermal_inspection().valid&&world.thermal_inspection().cell==std::array{160,90},"asynchronous_inspector_returns_requested_cell");
        world.reset(strefa::gpu::Scene::empty);glFinish();world.render(1);require(!world.thermal_inspection().valid,"generation_reset_discards_old_inspection");
        require(glGetError()==GL_NO_ERROR,"opengl_no_error");report<<"result=PASS\n";return 0;
    }catch(const std::exception& e){std::ofstream{output/"thermal-edges-error.txt"}<<e.what()<<'\n';std::cerr<<e.what()<<'\n';return 1;}
}
