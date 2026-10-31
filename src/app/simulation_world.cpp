#include "app/simulation_world.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <thread>

namespace strefa::app {
std::expected<void,std::string> SimulationWorld::open(const std::filesystem::path& shaders,
    const gpu::ExecutionOptions& execution,const gpu::ProfileOptions& profile,SceneConfiguration initial,const std::filesystem::path& shader_cache,std::shared_ptr<gpu::RenderDevice> device){
    if(world_) {suspend();return {};}
    auto created=gpu::ParticleWorld::create(shaders,execution,profile,shader_cache,std::move(device));
    if(!created)return std::unexpected{created.error()};
    world_=std::move(*created);++creations_;
    reset(initial);
    return {};
}
void SimulationWorld::reset(SceneConfiguration scene){
    if(scene.thermal_scene)world_->reset_thermal_scene(scene.thermal_scene);else if(scene.mission)world_->reset_mission();else world_->reset(scene.laboratory);
    scene_=scene;suspend();recording_.append(scene);
}
void SimulationWorld::set_thermal_options(gpu::ThermalOptions value){if(value!=world_->thermal_options()){world_->set_thermal_options(value);recording_.append(value);}}
void SimulationWorld::suspend(){thermal_pending_=false;clock_.reset();input_.clear();pending_single_step_=false;if(world_){world_->cancel_mission_input();world_->set_thermal_brush({});if(thermal_brush_!=gpu::ThermalBrush{}){recording_.append(gpu::ThermalBrush{});thermal_brush_={};}}}
SimulationSubmission SimulationWorld::advance(const SimulationFrame& frame){
    SimulationSubmission result;
    if(!frame.focused){
        suspend();world_->render(1.F);return result;
    }
    pending_single_step_=pending_single_step_||frame.single_step;
    if(frame.paused&&!pending_single_step_)thermal_pending_=false;
    auto brush=frame.paused&&!pending_single_step_?gpu::ThermalBrush{}:frame.thermal_brush;
    if(brush.tool!=gpu::ThermalTool::none&&thermal_brush_.tool==gpu::ThermalTool::none)thermal_pending_=true;
    if(brush.tool==gpu::ThermalTool::none&&thermal_pending_)brush=thermal_brush_;
    if(brush!=thermal_brush_){recording_.append(brush);thermal_brush_=brush;}world_->set_thermal_brush(brush);
    if(frame.stroke){world_->paint(*frame.stroke);recording_.append(*frame.stroke);}
    if(frame.paused){input_.clear();world_->cancel_mission_input();}else input_.submit(frame.controls);
    pending_single_step_=pending_single_step_||frame.single_step;
    const int steps=clock_.advance(frame.elapsed,frame.paused,1,frame.paused&&pending_single_step_);
    for(int i=0;i<steps;++i){
        const auto c=input_.peek();
        const unsigned buttons=(c.jump?gpu::jump:0U)|(c.jet?gpu::jet:0U)|(c.fire?gpu::fire:0U)
            |(c.reload?gpu::reload:0U)|(c.grenade?gpu::grenade:0U);
        // Both views execute the mission if that is the active scene. Laboratory
        // tools supply neutral actor input, preserving goals, score and events.
        const auto input=frame.scripted?gpu::scripted_mission_input(static_cast<unsigned>(world_->metrics().steps))
            :gpu::MissionInput{c.move,c.aim.x,c.aim.y,buttons};
        if(!(scene_.mission?world_->step(input):world_->step())){clock_.defer(steps-i);break;}
        static_cast<void>(input_.consume());pending_single_step_=false;thermal_pending_=false;
        recording_.append(input);
        result.cpu_ms+=world_->metrics().cpu_submit_ms;++result.ticks;
    }

    const auto began=std::chrono::steady_clock::now();
    world_->render(frame.paused?1.F:static_cast<float>(clock_.fraction()));
    result.render_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    return result;
}
void SimulationWorld::begin_recording(){recording_.begin(world_->thermal_options());reset(scene_);}
std::expected<void,std::string> SimulationWorld::save_recording(const std::filesystem::path& folder) const{
    try{
        std::filesystem::create_directories(folder);
        if(auto saved=recording_.save(folder/"session.txt",world_->execution_options(),world_->profile_options());!saved)return saved;
        // This export uses the latest asynchronous HUD sample. No synchronous
        // particle/mission readback is introduced into the production step.
        const auto& m=world_->metrics();const auto& hud=world_->mission();
        std::ofstream csv{folder/"metrics.csv"};
        csv<<"submitted_tick,hud_generation,hud_tick,score,ammo,particles,water,oil,gpu_ms,cpu_submit_ms\n"
            <<m.steps<<','<<hud.status[1]<<','<<hud.status[2]<<','<<hud.inventory[2]<<','<<hud.inventory[0]<<','
            <<m.grains<<','<<m.water<<','<<m.oil<<','<<m.gpu_ms<<','<<m.cpu_submit_ms<<'\n';
        if(!csv)return std::unexpected{"Niepełny eksport CSV."};
        // Explicit image capture is a separate UI action, outside physics timing.
        world_->save_image(folder/"preview.bmp");return {};
    }catch(const std::exception& e){return std::unexpected{e.what()};}
}
std::expected<void,std::string> SimulationWorld::replay_recording(const std::filesystem::path& path){
    auto commands=SessionRecording::load(path,world_->execution_options(),world_->profile_options());
    if(!commands)return std::unexpected{commands.error()};
    recording_=SessionRecording{};
    if(!std::ranges::any_of(*commands,[](const SessionCommand& c){return std::holds_alternative<gpu::ThermalOptions>(c);}))world_->set_thermal_options({});
    for(const auto& command:*commands){
        if(const auto* scene=std::get_if<SceneConfiguration>(&command))reset(*scene);
        else if(const auto* stroke=std::get_if<gpu::Stroke>(&command))world_->paint(*stroke);
        else if(const auto* config=std::get_if<gpu::ThermalOptions>(&command))world_->set_thermal_options(*config);
        else if(const auto* brush=std::get_if<gpu::ThermalBrush>(&command))world_->set_thermal_brush(*brush);
        else {
            // Explicit offline replay can wait for capacity. The live frame loop never waits.
            while(!(scene_.mission?world_->step(std::get<gpu::MissionInput>(command)):world_->step()))std::this_thread::yield();
        }
        world_->poll();
    }
    suspend();world_->render(1.F);return {};
}
}
