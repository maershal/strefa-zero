#include "app/application.hpp"
#include "app/frame_profile.hpp"
#include "app/cpu_vfx.hpp"
#include "app/simulation_world.hpp"
#include "app/scenario_runner.hpp"
#include "app/diagnostic_scenarios.hpp"
#include "platform/settings_store.hpp"
#include "platform/texture.hpp"
#include "platform/window.hpp"
#include "platform/gl_debug.hpp"
#include "ui/session.hpp"
#include "ui/view.hpp"
#include "ui/laboratory.hpp"
#include "ui/arena.hpp"
#include "ui/gpu_laboratory.hpp"
#include "sim/fixed_clock.hpp"
#include "sim/recording.hpp"
#include "build_info.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace strefa::app {
namespace {

std::filesystem::path utf8_path(const char* text) {
    return std::filesystem::path{reinterpret_cast<const char8_t*>(text)};
}
std::string gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unknown";
}

class Application {
    friend class strefa::app::ScenarioRunner;
public:
    Application(platform::Window& window, ui::Session& gui, const platform::SettingsStore& store,
        core::Settings settings, std::array<ui::Image, 3> images, platform::Texture& canvas,
        ui::Image soldier, ui::Image parts, ui::Image weapons, std::string message, sim::PhysicsConfig physics,gpu::ProfileOptions gpu_profiling,gpu::ExecutionOptions execution,platform::GlDebugLog* gl_debug,bool shader_cache,gpu::RenderOptions render_options,gpu::VfxOptions vfx_options,bool unpaced,gpu::ThermalOptions thermal_options,unsigned thermal_scene,unsigned test_seconds)
        : window_{window}, gui_{gui}, store_{store}, images_{images}, canvas_{canvas}, soldier_{soldier},
          parts_{parts}, weapons_{weapons}, state_{ui::Screen::main_menu, settings, settings, settings.show_diagnostics},
          message_{std::move(message)}, renderer_{gl_string(GL_RENDERER)}, gl_version_{gl_string(GL_VERSION)},
          physics_{physics},gpu_profiling_{gpu_profiling},execution_{execution},gl_debug_{gl_debug},shader_cache_{shader_cache} {thermal_options_=thermal_options;initial_thermal_scene_=thermal_scene;window_seconds_=test_seconds;gpu_controls_.thermal_enabled=thermal_options.enabled;vfx_options_=vfx_options;gpu_controls_.vfx_quality=static_cast<int>(vfx_options.quality);unpaced_=unpaced;gpu_controls_.render_quality=static_cast<int>(render_options.quality);gpu_controls_.lighting=render_options.lighting;lighting_quality_=static_cast<int>(render_options.lighting.quality);

    }

    std::expected<int, std::string> run(bool smoke_test, bool start_in_laboratory, bool start_in_arena, bool lab_smoke,bool start_gpu,bool gpu_smoke,bool start_mission,bool mission_smoke,bool session_smoke) {
        if(smoke_test) {
            world_=std::make_unique<sim::World>(0x57EF'A023U,physics_);world_->load_demo();
            lab_snapshot_=sim::present_world(*world_,sim::RenderMode::materials);
        }
        if(lab_smoke)lab_controls_.scene=sim::Scene::dam_break;
        if (start_in_laboratory && !smoke_test) perform(ui::Action::open_laboratory);
        if (start_in_arena && !smoke_test) perform(ui::Action::open_arena);
        if(lab_smoke) {lab_controls_.scene=sim::Scene::dam_break;perform(ui::Action::open_laboratory);}
        if(start_gpu||gpu_smoke){if(!perform(ui::Action::open_gpu))return std::unexpected{message_};}
        if(gpu_smoke){simulation_.reset({false,gpu::Scene::sand_water});gpu_controls_.scene=static_cast<int>(gpu::Scene::sand_water);}
        if(start_mission||mission_smoke||session_smoke){if(!perform(ui::Action::open_mission))return std::unexpected{message_};}
        // Keep the historical 60 FPS run at 900 frames, but let faster displays
        // execute the entire tick-based mission route before its final capture.
        constexpr unsigned mission_smoke_min_frames=900;
        constexpr std::uint64_t mission_smoke_min_ticks=1700;
        ScenarioRunner scenario{smoke_test,lab_smoke,gpu_smoke,mission_smoke,gpu_profiling_.collect,session_smoke};
        auto& visited_screens=scenario.visited_screens;
        auto& visited_themes=scenario.visited_themes;
        auto& frames=scenario.frames;
        auto& mission_frames=scenario.mission_frames;
        auto& profile_frames=scenario.profile_frames;
        auto& iterations=scenario.iterations;
        auto append_frame_profile=[&](const FrameProfile& row){scenario.append(row);};
        if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::runtime);
        auto previous = SDL_GetTicksNS();const auto window_started=previous;
        while (running_) {
            const auto frame_start = SDL_GetTicksNS();
            const float frame_ms = static_cast<float>(frame_start - previous) / 1'000'000.0F;
            previous = frame_start;
            FrameProfile frame_profile;
            std::optional<std::array<double,12>> mission_frame;
            auto phase_started=frame_start;
            auto mark=[&](double& duration){
                if(profile_frames){const auto now=SDL_GetTicksNS();duration+=static_cast<double>(now-phase_started)/1e6;phase_started=now;}
            };
            if(profile_frames){
                frame_profile.iteration=iterations++;frame_profile.frame=frames;
                frame_profile.screen=static_cast<int>(state_.screen);frame_profile.start_ns=frame_start;
                frame_profile.previous_interval_ms=frame_ms;
                frame_profile.frame_limit=unpaced_?0U:static_cast<unsigned>(state_.applied.frame_limit);
            }
            mission_ticks_this_frame_=0;gpu_ticks_this_frame_=0;world_render_submit_ms_=0;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                gui_.process_event(event);
                if (event.type == SDL_EVENT_QUIT
                    || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                        && event.window.windowID == SDL_GetWindowID(window_.native()))) running_ = false;
            }
            if (!running_) break;
            mark(frame_profile.input_ms);
            if(profile_frames)frame_profile.presentation=window_.presentation_info();
            mark(frame_profile.metadata_ms);
            if (window_.minimized()) {
                if(lab_worker_)lab_worker_->set_active(false);
                clock_.reset();game_clock_.reset();simulation_.suspend();game_input_.clear();
                if(profile_frames){
                    frame_profile.mission_tick=simulation_?simulation_->metrics().steps:0;
                    frame_profile.mission_pending_seconds=simulation_.clock().pending_seconds();
                    frame_profile.mission_dropped_seconds=simulation_.clock().dropped_seconds();
                }
                mark(frame_profile.update_ms);
                SDL_Delay(30);mark(frame_profile.sleep_ms);
                if(profile_frames)append_frame_profile(frame_profile);
                continue;
            }

            scenario.before_frame(*this,frame_ms);
            mark(frame_profile.scenario_ms);
            if(profile_frames)frame_profile.screen=static_cast<int>(state_.screen);
            gui_.set_scale(window_.display_scale(), state_.applied.ui_scale);
            gui_.new_frame();
            if(static_cast<std::size_t>(state_.screen)<visited_screens.size())visited_screens[static_cast<std::size_t>(state_.screen)] = true;
            visited_themes[static_cast<std::size_t>(state_.applied.map_theme)] = true;
            const ui::FrameInfo info{gui_.scale(), frame_ms, renderer_, gl_version_, message_};
            mark(frame_profile.ui_prepare_ms);
            // Render one coherent snapshot of actors and terrain. Input collected by
            // the UI advances the next snapshot; no GPU view can mutate the world.
            if (state_.screen == ui::Screen::arena) {
                const auto uploaded = upload_world(game_->world(), true);
                if (!uploaded) return std::unexpected{uploaded.error()};
                ++game_uploads_;
                if(cpu_vfx_enabled_){auto& vfx=render_device_->vfx;vfx.set_options(vfx_options_);vfx.poll();vfx.cpu_canvas(canvas_.id(),static_cast<unsigned>(images_[static_cast<std::size_t>(state_.applied.map_theme)].handle),static_cast<float>(game_clock_.fraction()));}
            }
            if(state_.screen==ui::Screen::laboratory) {
                if(lab_worker_) lab_snapshot_=lab_worker_->snapshot();
                if(!lab_snapshot_->error.empty()) return std::unexpected{lab_snapshot_->error};
                if(last_lab_pixels_!=lab_snapshot_->pixels) {
                    const auto uploaded=canvas_.update_rgba(std::as_bytes(std::span{*lab_snapshot_->pixels}));
                    if(!uploaded) return std::unexpected{uploaded.error()};
                    last_lab_pixels_=lab_snapshot_->pixels;++uploads_;
                }
                lab_controls_.recording=lab_snapshot_->recording;
            }
            mark(frame_profile.upload_ms);
            ui::LaboratoryInput input;
            std::optional<ui::LaboratoryFrame> lab;
            if(state_.screen==ui::Screen::laboratory)lab.emplace(ui::LaboratoryFrame{
                {canvas_.id(),canvas_.width(),canvas_.height()},*lab_snapshot_,lab_controls_,input,
                lab_snapshot_->step_ms,lab_snapshot_->overload_seconds});
            ui::ArenaInput arena_input;
            const bool focused = (SDL_GetWindowFlags(window_.native()) & SDL_WINDOW_INPUT_FOCUS) != 0;
            std::optional<ui::ArenaFrame> arena;
            if(state_.screen==ui::Screen::arena||state_.screen==ui::Screen::gpu_mission){
                arena.emplace(ui::ArenaFrame{{canvas_.id(),canvas_.width(),canvas_.height()},soldier_,
                    images_[static_cast<std::size_t>(state_.applied.map_theme)],parts_,weapons_,
                    state_.screen==ui::Screen::arena?ui::ArenaView{*game_}:ui::ArenaView{simulation_->mission()},arena_input,focused});
                arena->scene_mission=state_.screen==ui::Screen::arena||simulation_.scene().mission;
                arena->paused=gpu_controls_.paused;
                if(state_.screen==ui::Screen::arena&&cpu_vfx_enabled_){arena->shared_vfx=true;}
                if(state_.screen==ui::Screen::arena&&cpu_vfx_enabled_&&render_device_->vfx.enabled()){
                    arena->world={render_device_->vfx.cpu_back(),960,576};arena->vfx_front={render_device_->vfx.cpu_front(),960,576};
                }
                if(state_.screen==ui::Screen::gpu_mission){arena->lighting_quality=&lighting_quality_;arena->camera=simulation_->camera_offset();}arena->vfx_quality=&gpu_controls_.vfx_quality;arena->effects_enabled=gpu_controls_.vfx_quality!=0;
            }
            double ui_poll_ms=0;gpu::PollTimings ui_poll;
            if(state_.screen==ui::Screen::gpu_mission&&simulation_){
                const auto poll_started=gpu_profiling_.collect?SDL_GetTicksNS():std::uint64_t{0};
                simulation_->poll();arena->world={simulation_->texture(),gpu::width*3,gpu::height*3};arena->game=ui::ArenaView{simulation_->mission()};
                if(gpu_profiling_.collect){ui_poll_ms=static_cast<double>(SDL_GetTicksNS()-poll_started)/1e6;ui_poll=simulation_->poll_timings();}
            }
            ui::GpuInput gpu_input;std::optional<ui::GpuLaboratoryFrame> gpu_frame;
            if(state_.screen==ui::Screen::gpu_laboratory&&simulation_){
                simulation_->poll();
                gpu_frame.emplace(ui::GpuLaboratoryFrame{{simulation_->texture(),gpu::width*3,gpu::height*3},simulation_->metrics(),gpu_controls_,gpu_input,simulation_.scene().mission,simulation_->mission(),simulation_.recording()});
            }
            mark(frame_profile.poll_ms);
            if(gpu_frame){gpu_frame->camera=simulation_->camera_offset();gpu_frame->thermal=&simulation_->thermal_totals();gpu_frame->thermal_memory=simulation_->thermal_memory();gpu_frame->inspection=&simulation_->thermal_inspection();}
            if(gpu_frame&&render_device_){gpu_frame->vfx=&render_device_->vfx.stats();gpu_frame->vfx_memory=render_device_->vfx.memory_bytes();}
            const auto action = ui::draw(state_, info, images_, gui_.fonts(), lab?&*lab:nullptr, arena?&*arena:nullptr,gpu_frame?&*gpu_frame:nullptr);
            if(state_.screen==ui::Screen::gpu_mission)gpu_controls_.lighting.quality=static_cast<gpu::LightingQuality>(lighting_quality_);else lighting_quality_=static_cast<int>(gpu_controls_.lighting.quality);
            {auto saved=state_.applied;saved.lighting_quality=lighting_quality_;saved.bloom=gpu_controls_.lighting.bloom;saved.vignette=gpu_controls_.lighting.vignette;saved.camera_shake=gpu_controls_.lighting.shake;saved.aberration=gpu_controls_.lighting.aberration;if(saved!=state_.applied){state_.applied=saved;state_.draft=saved;const auto written=store_.save(saved);if(!written)message_=written.error();}}
            vfx_options_.quality=static_cast<gpu::VfxQuality>(gpu_controls_.vfx_quality);if(render_device_)render_device_->vfx.set_options(vfx_options_);
            mark(frame_profile.ui_build_ms);
            if(session_smoke){
                if(scenario.injected_stroke)gpu_input.stroke=std::exchange(scenario.injected_stroke,{});
                gpu_input.step=std::exchange(scenario.injected_step,false);
            }
            if(simulation_){thermal_options_.enabled=gpu_controls_.thermal_enabled;thermal_options_.radiation=gpu_controls_.thermal_radiation;simulation_.set_thermal_options(thermal_options_);simulation_->set_thermal_view(static_cast<gpu::ThermalView>(gpu_controls_.thermal_view));simulation_->set_render_options({static_cast<gpu::RenderQuality>(gpu_controls_.render_quality),false,gpu_controls_.lighting});simulation_->set_vfx_options({static_cast<gpu::VfxQuality>(gpu_controls_.vfx_quality),false});
                simulation_->set_background_texture(static_cast<unsigned>(images_[static_cast<std::size_t>(state_.applied.map_theme)].handle));}
            if(gpu_frame){
                update_gpu(gpu_input,static_cast<double>(frame_ms)/1000.0,focused||gpu_smoke||session_smoke);
            }
            if (state_.screen == ui::Screen::laboratory) {
                const auto updated = update_lab(input, static_cast<double>(frame_ms) / 1000.0);
                if (!updated) return std::unexpected{updated.error()};
            } else { if(lab_worker_) lab_worker_->set_active(false); clock_.reset(); lab_controls_.previous.reset(); }
            if(state_.screen==ui::Screen::gpu_mission&&action!=ui::Action::back){
                update_mission(arena_input,static_cast<double>(frame_ms)/1000,focused||mission_smoke||session_smoke,mission_smoke||session_smoke);
                if(mission_smoke){
                    const auto& metrics=simulation_->metrics();
                    const bool timing_available=gpu::effective_timer_mode(gpu_profiling_)!=gpu::TimerMode::off&&metrics.timing_samples>0;
                    mission_frame=std::array<double,12>{static_cast<double>(frames),frame_ms,metrics.gpu_ms,metrics.cpu_submit_ms,simulation_.clock().dropped_seconds(),ui_poll_ms,ui_poll.timers_ms,ui_poll.fence_ms,ui_poll.read_ms,mission_step_submit_ms_,timing_available?1.0:0.0,static_cast<double>(metrics.timing_samples)};
                }
            }else if(state_.screen!=ui::Screen::gpu_laboratory)simulation_.suspend();
            if (state_.screen == ui::Screen::arena && action != ui::Action::back)
                update_game(arena_input, static_cast<double>(frame_ms) / 1000.0, focused, smoke_test);
            else { game_clock_.reset(); game_input_.clear(); }
            mark(frame_profile.update_ms);
            if(profile_frames){
                frame_profile.world_render_ms=world_render_submit_ms_;
                frame_profile.update_ms-=world_render_submit_ms_;
                frame_profile.mission_ticks=mission_ticks_this_frame_;
                frame_profile.gpu_ticks=gpu_ticks_this_frame_;
                frame_profile.mission_tick=simulation_?simulation_->metrics().steps:0;
                frame_profile.mission_pending_seconds=simulation_.clock().pending_seconds();
                frame_profile.mission_dropped_seconds=simulation_.clock().dropped_seconds();
            }
            const bool duration_done=window_seconds_&&static_cast<double>(frame_start-window_started)/1e9>=window_seconds_;
            const bool mission_final_frame=mission_smoke&&(!window_seconds_||duration_done)&&frames>=mission_smoke_min_frames-1&&
                simulation_->metrics().steps>=mission_smoke_min_ticks;
            if(gpu_input.save_recording){
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::capture);
                const auto folder=store_.path().parent_path()/"recordings"/("gpu-"+std::to_string(SDL_GetTicksNS()));
                const auto saved=simulation_.save_recording(folder);
                if(saved)last_recording_=folder/"session.txt";
                message_=saved?"Zapisano nagranie w katalogu profilu: "+folder.filename().string():saved.error();
                mark(frame_profile.capture_ms);
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::runtime);
            }
            if(gpu_input.replay_recording){
                if(last_recording_.empty())message_="Najpierw zapisz nagranie w tej sesji.";
                else{
                    const auto replayed=simulation_.replay_recording(last_recording_);
                    message_=replayed?"Odtworzono nagranie. Scena jest zatrzymana.":replayed.error();
                    if(replayed){gpu_controls_.paused=true;gpu_controls_.scene=simulation_.scene().mission?12:static_cast<int>(simulation_.scene().laboratory);}
                }
                mark(frame_profile.capture_ms);
            }
            const bool capture_frame=(gpu_smoke&&(window_seconds_?duration_done:frames==230))||mission_final_frame;
            frame_profile.capture_performed=capture_frame||gpu_input.save_recording||gpu_input.replay_recording;
            mark(frame_profile.metadata_ms);
            if(capture_frame){
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::capture);
                if(gpu_smoke)simulation_->save_image(store_.path().parent_path()/"gpu-preview.bmp");
                if(mission_smoke)simulation_->save_image(store_.path().parent_path()/"mission-preview.bmp");
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::runtime);
                mark(frame_profile.capture_ms);
            }
            window_.clear();
            gui_.render();
            if(simulation_)simulation_->invalidate_gl_state();
            mark(frame_profile.gui_render_ms);
            if(mission_final_frame){
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::capture);
                int w{},h{};SDL_GetWindowSizeInPixels(window_.native(),&w,&h);std::vector<std::uint32_t> capture(static_cast<std::size_t>(w)*h);
                glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,capture.data());
                for(int y=0;y<h/2;++y)for(int x=0;x<w;++x)std::swap(capture[y*w+x],capture[(h-1-y)*w+x]);
                auto* surface=SDL_CreateSurfaceFrom(w,h,SDL_PIXELFORMAT_RGBA32,capture.data(),w*4);
                if(surface){const auto path=(store_.path().parent_path()/"mission-window.bmp").u8string();SDL_SaveBMP(surface,reinterpret_cast<const char*>(path.c_str()));SDL_DestroySurface(surface);}
            }
            if(mission_final_frame){
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::runtime);
                mark(frame_profile.capture_ms);
            }
            if ((smoke_test||gpu_smoke||mission_smoke||session_smoke) && glGetError() != GL_NO_ERROR)
                return std::unexpected{"Test renderowania wykrył błąd OpenGL."};
            mark(frame_profile.validation_ms);
            window_.present();
            mark(frame_profile.present_ms);
            frame_profile.rendered=true;
            if (action != ui::Action::none) perform(action);
            ++frames;
            if (smoke_test && frames >= 238) running_ = false;
            if(lab_smoke&&frames>=300) running_=false;
            if(gpu_smoke&&(window_seconds_?duration_done:frames>=240))running_=false;
            if(mission_final_frame||(session_smoke&&frames>=130))running_=false;

            mark(frame_profile.actions_ms);
            if (!window_.vsync_enabled()&&!unpaced_) {
                const auto budget = static_cast<Uint64>(1'000'000'000 / state_.applied.frame_limit);
                const auto elapsed = SDL_GetTicksNS() - frame_start;
                if (elapsed < budget) SDL_DelayPrecise(budget - elapsed);
            }
            mark(frame_profile.sleep_ms);
            if(mission_frame)mission_frames.push_back(*mission_frame);
            if(profile_frames)append_frame_profile(frame_profile);
        }
        if(auto exported=scenario.write_frames(*this);!exported)return std::unexpected{exported.error()};
        if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::validation);
        if(lab_worker_) {
            lab_worker_->stop();
            if(!lab_worker_->snapshot()->error.empty()) return std::unexpected{lab_worker_->snapshot()->error};
        }
        if (recording_) { recording_->finish(); recording_.reset(); }
        if(auto checked=scenario.finish(*this);!checked)return std::unexpected{checked.error()};
        if(simulation_){
            const auto cache=simulation_->shader_cache_stats();
            std::ofstream startup{store_.path().parent_path()/"gpu-startup.txt"};
            startup<<"world_creations="<<simulation_.creations()<<"\nprograms="<<cache.programs
                <<"\nsource_builds="<<cache.source_builds<<"\ncache_hits="<<cache.hits
                <<"\ncache_misses="<<cache.misses<<"\ncache_rejected="<<cache.rejected
                <<"\ncache_write_failures="<<cache.write_failures<<"\ncache_supported="<<cache.supported
                <<"\ncompile_ms="<<cache.compile_ms<<"\ncache_load_ms="<<cache.load_ms
                <<"\nfirst_world_ms="<<first_world_ms_<<"\nview_switches="<<view_switches_
                <<"\nview_switch_total_ms="<<view_switch_ms_<<"\nview_switch_max_ms="<<view_switch_max_ms_
                <<"\ncpu_game_created="<<bool(game_)<<"\ncpu_laboratory_created="<<bool(lab_worker_)<<'\n';
        }
        {std::ofstream vfx{store_.path().parent_path()/"vfx-state.txt"};vfx<<"quality="<<gpu::vfx_quality_name(vfx_options_.quality)<<"\nrender_device_created="<<bool(render_device_)<<"\ncpu_adapter="<<cpu_vfx_enabled_<<"\nworld_creations="<<simulation_.creations()<<'\n';if(render_device_){render_device_->vfx.poll();const auto& c=render_device_->vfx.stats();vfx<<"generation="<<c.clock[0]<<"\nsource_tick="<<c.clock[1]<<"\nbudget_limited_passes="<<c.clock[2]<<"\nmalformed="<<c.clock[3]<<"\n";for(unsigned kind=0;kind<gpu::vfx_kinds;++kind)vfx<<"emitted_"<<gpu::vfx_kind_names[kind]<<'='<<c.emitted[kind]<<"\nrejected_"<<gpu::vfx_kind_names[kind]<<'='<<c.rejected[kind]<<'\n';vfx<<"alive="<<c.counts[0]<<"\nfree="<<c.counts[1]<<"\npeak="<<c.counts[3]<<"\nmemory_bytes="<<render_device_->vfx.memory_bytes()<<'\n';}}
        return 0;
    }

private:
    void ensure_render_device(){
        if(render_device_)return;
        GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
        if(major<4||(major==4&&minor<3))return;
        render_device_=std::make_shared<gpu::RenderDevice>(utf8_path(SDL_GetBasePath())/"assets/shaders",shader_cache_?store_.path().parent_path()/"shader-cache":std::filesystem::path{});render_device_->vfx.set_options(vfx_options_);
    }
    void update_mission(const ui::ArenaInput& input,double elapsed,bool focused,bool scripted){
        if(input.toggle_pause)gpu_controls_.paused=!gpu_controls_.paused;
        if(input.reset){simulation_.reset({true});gpu_controls_.scene=12;gpu_controls_.paused=false;elapsed=0;}
        const auto result=simulation_.advance({input.controls,{},elapsed,focused,gpu_controls_.paused,false,scripted});
        mission_step_submit_ms_=result.cpu_ms;mission_ticks_this_frame_=result.ticks;
        if(gpu_profiling_.collect)world_render_submit_ms_+=result.render_ms;
    }
    void update_gpu(const ui::GpuInput& input,double elapsed,bool focused){
        if(input.inspect)simulation_->request_thermal_inspection((*input.inspect)[0],(*input.inspect)[1]);
        if(input.begin_recording){simulation_.begin_recording();elapsed=0;}
        if(input.thermal_reset){simulation_.reset({false,gpu::Scene::empty,static_cast<unsigned>(gpu_controls_.thermal_scene)});gpu_controls_.thermal_enabled=true;gpu_controls_.scene=3;elapsed=0;}
        if(input.reset){
            simulation_.reset({gpu_controls_.scene==12,static_cast<gpu::Scene>(std::min(gpu_controls_.scene,11))});
            gpu_controls_.lost_seconds=0;gpu_controls_.previous.reset();elapsed=0;
        }
        const auto result=simulation_.advance({{},input.stroke,elapsed,focused,gpu_controls_.paused,input.step,false,input.thermal_brush});
        gpu_controls_.lost_seconds=simulation_.clock().dropped_seconds();gpu_ticks_this_frame_=result.ticks;
        if(gpu_profiling_.collect)world_render_submit_ms_+=result.render_ms;
    }
    std::expected<void, std::string> upload_world(const sim::World& world, bool transparent = false) {
        last_lab_pixels_.reset();
        world.render(std::span<sim::Color, sim::World::cell_count>{pixels_.data(), pixels_.size()}, transparent,
            transparent ? sim::RenderMode::materials : lab_controls_.view);
        return canvas_.update_rgba(std::as_bytes(std::span{pixels_}));
    }
    void update_game(ui::ArenaInput input, double elapsed, bool focused, bool smoke) {
        if (input.reset) { game_->reset();if(cpu_vfx_enabled_)cpu_vfx_.reset(*render_device_); game_clock_.reset(); game_input_.clear(); elapsed = 0; }
        if (!focused && !smoke) { game_clock_.reset(); game_input_.clear(); return; }
        if (smoke) {
            input.controls = {};
            input.controls.aim = {184, 140};
            input.controls.move = smoke_game_frame_ < 40 ? 1.F : 0.F;
            input.controls.jump = smoke_game_frame_ == 3;
            input.controls.jet = smoke_game_frame_ > 10 && smoke_game_frame_ < 90;
            input.controls.fire = true;
            input.controls.grenade = smoke_game_frame_ == 2;
            input.controls.reload = smoke_game_frame_ == 120;
            ++smoke_game_frame_; elapsed = sim::FixedClock::tick_seconds;
        }
        game_input_.submit(input.controls);
        const int steps = game_clock_.advance(elapsed, false, 1);
        for (int i = 0; i < steps; ++i){game_->tick(game_input_.consume());if(cpu_vfx_enabled_)cpu_vfx_.tick(*render_device_,*game_);}
    }
    std::expected<void, std::string> update_lab(const ui::LaboratoryInput& input, double elapsed) {
        if(lab_worker_) {
            sim::LaboratoryControl control{true,lab_controls_.paused,lab_controls_.speed,lab_controls_.view,input.probe};
            sim::LaboratoryCommand command;
            if(input.reset) command.scene=lab_controls_.scene;
            command.clear=input.clear;command.toggle_boundary=input.boundary_toggle;command.toggle_recording=input.record_toggle;
            command.manual_steps=input.single_step?1:0;command.stroke=input.stroke;
            if(input.heat) command.heat=sim::HeatSource{*input.heat,lab_controls_.tool==1?100.0:-100.0};
            const bool event=!command.empty();
            const auto ticket=lab_worker_->submit(control,std::move(command));
            if(event&&ticket==0) message_="Kolejka edycji jest pełna. Zatrzymaj pędzel na chwilę.";
            return {};
        }
        if (input.reset || input.clear) {
            if (recording_) { recording_->finish(); recording_.reset(); lab_controls_.recording=false; }
            if (input.reset) world_->load_scene(lab_controls_.scene); else world_->clear();
            clock_.reset(); lab_controls_.previous.reset(); elapsed = 0;
        }
        if (input.boundary_toggle) world_->set_open_top(!world_->config().open_top);
        if (input.heat) world_->inject_heat(*input.heat, lab_controls_.tool == 1 ? 100 : -100);
        if (input.record_toggle) {
            try {
                if (recording_) { recording_->finish(); recording_.reset(); }
                else recording_.emplace(store_.path().parent_path()/"recordings"/std::to_string(SDL_GetTicksNS()));
                lab_controls_.recording=recording_.has_value();
            } catch(const std::exception& e) { return std::unexpected{e.what()}; }
        }
        if (input.stroke) {
            const auto& stroke = *input.stroke;
            world_->paint(stroke.from, stroke.to, stroke.radius, stroke.material, stroke.erase);
        }
        const int steps = clock_.advance(elapsed, lab_controls_.paused, lab_controls_.speed,
            input.single_step || std::exchange(smoke_single_step_, false));
        for (int i = 0; i < steps; ++i) {
            const auto start = SDL_GetPerformanceCounter(); world_->step();
            step_ms_ = static_cast<double>(SDL_GetPerformanceCounter()-start)*1000.0/static_cast<double>(SDL_GetPerformanceFrequency());
            if (recording_) {
                try { recording_->capture(*world_,step_ms_,lab_controls_.view); }
                catch(const std::exception& e) { return std::unexpected{e.what()}; }
            }
        }
        auto snapshot=sim::present_world(*world_,lab_controls_.view,input.probe);
        snapshot->step_ms=step_ms_;snapshot->recording=recording_.has_value();lab_snapshot_=std::move(snapshot);
        const auto uploaded = upload_world(*world_);
        if (uploaded) ++uploads_;
        return uploaded;
    }

    bool perform(ui::Action action) {
        switch (action) {
            case ui::Action::none: break;
            case ui::Action::open_mission:
            case ui::Action::open_gpu: {
                const bool mission=action==ui::Action::open_mission;
                const bool existing=bool(simulation_);
                const auto open_started=SDL_GetTicksNS();
                ensure_render_device();if(render_device_)render_device_->vfx.set_options(vfx_options_);
                if(state_.screen!=ui::Screen::gpu_laboratory&&state_.screen!=ui::Screen::gpu_mission&&render_device_)render_device_->vfx.reset();
                const auto opened=simulation_.open(utf8_path(SDL_GetBasePath())/"assets"/"shaders",execution_,gpu_profiling_,{mission},shader_cache_?store_.path().parent_path()/"shader-cache":std::filesystem::path{},render_device_);
                if(!opened){message_=opened.error();return false;}
                const double open_ms=static_cast<double>(SDL_GetTicksNS()-open_started)/1e6;
                if(existing){++view_switches_;view_switch_ms_+=open_ms;view_switch_max_ms_=std::max(view_switch_max_ms_,open_ms);}
                else first_world_ms_=open_ms;
                simulation_.set_thermal_options(thermal_options_);if(!existing&&initial_thermal_scene_){simulation_.reset({false,gpu::Scene::empty,initial_thermal_scene_});initial_thermal_scene_=0;}
                simulation_->set_actor_textures(static_cast<unsigned>(parts_.handle),static_cast<unsigned>(weapons_.handle),weapons_.width,weapons_.height);
                simulation_->set_render_options({static_cast<gpu::RenderQuality>(gpu_controls_.render_quality),false,gpu_controls_.lighting});simulation_->set_vfx_options({static_cast<gpu::VfxQuality>(gpu_controls_.vfx_quality),false});
                simulation_->set_background_texture(static_cast<unsigned>(images_[static_cast<std::size_t>(state_.applied.map_theme)].handle));
                gpu_controls_.scene=simulation_.scene().mission?12:static_cast<int>(simulation_.scene().laboratory);
                gpu_controls_.previous.reset();
                state_.screen=mission?ui::Screen::gpu_mission:ui::Screen::gpu_laboratory;message_.clear();break;
            }
            case ui::Action::open_arena:
                if(!game_)game_=std::make_unique<game::GameSession>(game::PlayerTuning{},game::WeaponConfig{},physics_);
                ensure_render_device();cpu_vfx_enabled_=bool(render_device_);if(cpu_vfx_enabled_)cpu_vfx_.reset(*render_device_);
                state_.screen=ui::Screen::arena;game_clock_.reset();game_input_.clear();message_.clear();break;
            case ui::Action::open_laboratory:
                if(!world_&&!lab_worker_){
                    lab_worker_=std::make_unique<sim::LaboratoryRunner>(store_.path().parent_path()/"recordings",lab_controls_.scene,physics_);
                    lab_snapshot_=lab_worker_->snapshot();
                }
                state_.screen=ui::Screen::laboratory;clock_.reset();message_.clear();break;
            case ui::Action::open_preview: state_.screen = ui::Screen::preview; message_.clear(); break;
            case ui::Action::open_settings:
                state_.draft = state_.applied; state_.screen = ui::Screen::settings; message_.clear(); break;
            case ui::Action::open_about: state_.screen = ui::Screen::about; message_.clear(); break;
            case ui::Action::back:
                simulation_.suspend();gpu_controls_.previous.reset();
                state_.draft = state_.applied; state_.screen = ui::Screen::main_menu; message_.clear(); break;
            case ui::Action::reset: state_.draft = core::Settings{}; message_.clear(); break;
            case ui::Action::quit: running_ = false; break;
            case ui::Action::apply: {
                if (const auto checked = core::validate(state_.draft); !checked) {
                    message_ = checked.error(); return false;
                }
                if (const auto changed = window_.apply_video(state_.draft, state_.applied); !changed) {
                    message_ = changed.error(); return false;
                }
                state_.applied = state_.draft;gpu_controls_.lighting.quality=static_cast<gpu::LightingQuality>(state_.applied.lighting_quality);lighting_quality_=state_.applied.lighting_quality;gpu_controls_.lighting.bloom=state_.applied.bloom;gpu_controls_.lighting.vignette=state_.applied.vignette;gpu_controls_.lighting.shake=state_.applied.camera_shake;gpu_controls_.lighting.aberration=state_.applied.aberration;
                state_.diagnostics = state_.applied.show_diagnostics;
                if (const auto saved = store_.save(state_.applied); !saved) {
                    message_ = saved.error() + " Zmiany działają w bieżącej sesji."; return false;
                }
                message_ = "Ustawienia zostały zapisane.";
                break;
            }
        }
        return true;
    }

    platform::Window& window_;
    ui::Session& gui_;
    const platform::SettingsStore& store_;
    std::array<ui::Image, 3> images_;
    platform::Texture& canvas_;
    ui::Image soldier_;
    ui::Image parts_, weapons_;
    ui::ViewState state_;
    bool running_{true},unpaced_{};
    std::string message_;
    std::string renderer_;
    std::string gl_version_;
    std::unique_ptr<sim::World> world_;
    std::unique_ptr<sim::LaboratoryRunner> lab_worker_;
    std::shared_ptr<const sim::LaboratorySnapshot> lab_snapshot_;
    std::shared_ptr<const std::vector<sim::Color>> last_lab_pixels_;
    sim::FixedClock clock_;
    ui::LaboratoryControls lab_controls_;
    std::optional<sim::Recording> recording_;
    std::vector<sim::Color> pixels_ = std::vector<sim::Color>(sim::World::cell_count);
    double step_ms_{};
    unsigned uploads_{};
    bool smoke_single_step_{}, smoke_lab_ok_{true};
    std::uint64_t smoke_paused_tick_{};
    sim::PhysicsConfig physics_;
    std::unique_ptr<game::GameSession> game_;
    game::InputBuffer game_input_;
    sim::FixedClock game_clock_;
    unsigned game_uploads_{}, smoke_game_frame_{};
    std::shared_ptr<gpu::RenderDevice> render_device_;
    CpuVfxAdapter cpu_vfx_;bool cpu_vfx_enabled_{};gpu::VfxOptions vfx_options_;
    SimulationWorld simulation_;
    gpu::ProfileOptions gpu_profiling_;
    gpu::ExecutionOptions execution_;
    platform::GlDebugLog* gl_debug_{};
    bool shader_cache_{};
    unsigned view_switches_{};
    double first_world_ms_{},view_switch_ms_{},view_switch_max_ms_{};
    std::filesystem::path last_recording_;
    double mission_step_submit_ms_{},world_render_submit_ms_{};
    unsigned mission_ticks_this_frame_{},gpu_ticks_this_frame_{};
    ui::GpuControls gpu_controls_;int lighting_quality_{};gpu::ThermalOptions thermal_options_;unsigned initial_thermal_scene_{},window_seconds_{};
};

} // namespace

std::expected<int, std::string> run(const LaunchOptions& options) {
    SDL_SetAppMetadata("Strefa Zero", build::version.data(), "local.strefazero.prototype");
    auto system = platform::SdlSystem::create();
    if (!system) return std::unexpected{system.error()};
    auto profile = options.profile;
    if (profile.empty()) {
        using SdlString = std::unique_ptr<char, decltype(&SDL_free)>;
        SdlString path{SDL_GetPrefPath("StrefaZero", "Prototype"), SDL_free};
        if (!path) return std::unexpected{"Nie można ustalić katalogu ustawień."};
        profile = utf8_path(path.get());
    }
    platform::SettingsStore store{profile / "settings.ini"};
    const bool testing=options.smoke_test||options.laboratory_smoke_test||options.gpu_test||options.gpu_liquid_test||options.gpu_flow_test||options.gpu_smoke_test||options.mission_test||options.mission_smoke||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.thermal_check||options.thermal_benchmark||options.vfx_check||options.vfx_benchmark||options.lighting_check||options.lighting_benchmark||options.render_check||options.render_benchmark||options.fluid_check||options.fluid_benchmark||options.cost_check||options.session_smoke||options.cache_check;
    if(testing) std::filesystem::create_directories(profile);
    auto loaded = store.load();
    std::string message = loaded ? std::string{} : loaded.error() + " Użyto ustawień domyślnych.";
    // A smoke run starts from known defaults and only writes its dedicated profile.
    auto settings = testing ? core::Settings{} : loaded.value_or(core::Settings{});
    auto render_options=options.gpu_render;if(!options.lighting_quality_set)render_options.lighting.quality=static_cast<gpu::LightingQuality>(settings.lighting_quality);render_options.lighting.bloom=settings.bloom;render_options.lighting.vignette=settings.vignette;render_options.lighting.shake=settings.camera_shake;render_options.lighting.aberration=settings.aberration;
    if(options.laboratory_smoke_test||options.gpu_smoke_test||options.mission_test||options.mission_smoke||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.thermal_check||options.thermal_benchmark||options.vfx_check||options.vfx_benchmark||options.lighting_check||options.lighting_benchmark||options.render_check||options.render_benchmark||options.fluid_check||options.fluid_benchmark||options.cost_check||options.session_smoke||options.cache_check) {settings.vsync=false;settings.frame_limit=60;}
    if(options.test_vsync.has_value())settings.vsync=*options.test_vsync;
    if(options.test_frame_limit.has_value())settings.frame_limit=static_cast<int>(*options.test_frame_limit);
    if(options.test_window_size){settings.width=(*options.test_window_size)[0];settings.height=(*options.test_window_size)[1];}
    auto window = platform::Window::create(settings,!options.legacy_graphics,options.gl_debug);
    if (!window) return std::unexpected{window.error()};
    if(testing&&options.show_test_window){window->show();SDL_RaiseWindow(window->native());}
    bool debug_log=options.gl_debug;
#ifndef NDEBUG
    debug_log=true;
#endif
    auto gl_debug=debug_log?platform::GlDebugLog::create(profile/"gl-debug.txt"):nullptr;
    settings.vsync = window->vsync_enabled();
    if (!window->warning().empty()) message = window->warning();
    const char* base = SDL_GetBasePath();
    if (!base) return std::unexpected{"Nie można ustalić katalogu aplikacji."};
    const auto assets = utf8_path(base) / "assets";
    if(options.gpu_test||options.gpu_liquid_test||options.gpu_flow_test||options.gpu_smoke_test||options.mission_test||options.mission_smoke||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.thermal_check||options.thermal_benchmark||options.vfx_check||options.vfx_benchmark||options.lighting_check||options.lighting_benchmark||options.render_check||options.render_benchmark||options.fluid_check||options.fluid_benchmark||options.cost_check||options.session_smoke||options.cache_check){
        GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
        if(major<4||(major==4&&minor<3)){std::ofstream{profile/"gpu-skipped.txt"}<<"OpenGL 4.3 unavailable; GPU tests skipped, CPU mode remains supported.\n";return 77;}
    }
    if(testing&&!(options.smoke_test||options.laboratory_smoke_test||options.gpu_smoke_test||options.mission_smoke||options.session_smoke)&&gl_debug)
        gl_debug->set_phase(platform::DebugPhase::validation);
    if(auto diagnostic=run_diagnostic_scenario(options,assets,profile))return *diagnostic;
    auto gui = ui::Session::create(*window, assets);
    if (!gui) return std::unexpected{gui.error()};
    std::array<std::optional<platform::Texture>, 3> textures;
    std::array<ui::Image, 3> images{};
    constexpr std::array filenames{"forest.bmp", "industrial.bmp", "winter.bmp"};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        auto loaded_texture = platform::Texture::load_bmp(assets / "backgrounds" / filenames[i]);
        if (loaded_texture) {
            textures[i].emplace(std::move(*loaded_texture));
            images[i] = {textures[i]->id(), textures[i]->width(), textures[i]->height()};
        } else message = loaded_texture.error();
    }
    std::vector<sim::Color> blank(sim::World::cell_count);
    auto canvas = platform::Texture::create_rgba(sim::World::width, sim::World::height,
        std::as_bytes(std::span{blank}));
    if (!canvas) return std::unexpected{canvas.error()};
    auto soldier = platform::Texture::load_bmp(assets / "actors" / "soldier_pl.bmp");
    if (!soldier) return std::unexpected{soldier.error()};
    if (soldier->width() != 128 || soldier->height() != 192)
        return std::unexpected{"Atlas żołnierza wymaga rozmiaru 128 × 192."};
    auto parts = platform::Texture::load_bmp(assets/"actors"/"parts.bmp");
    auto weapons = platform::Texture::load_bmp(assets/"weapons"/"weapons.bmp");
    if (!parts || !weapons) return std::unexpected{"Brak atlasu części postaci lub broni."};
    if (!testing) window->show();
    sim::PhysicsConfig physics;physics.worker_threads=options.physics_threads;
    Application application{*window, *gui, store, settings, images, *canvas,
        {soldier->id(), soldier->width(), soldier->height()}, {parts->id(),parts->width(),parts->height()},
        {weapons->id(),weapons->width(),weapons->height()}, std::move(message),physics,options.gpu_profiling,options.gpu_execution,gl_debug.get(),options.shader_cache,render_options,options.gpu_vfx,options.test_unpaced,options.gpu_thermal,options.thermal_scene,options.test_seconds};
    auto result=application.run(options.smoke_test, options.start_in_laboratory, options.start_in_arena,options.laboratory_smoke_test,options.start_in_gpu,options.gpu_smoke_test,options.start_in_mission,options.mission_smoke,options.session_smoke);
    if(gl_debug)gl_debug->set_phase(platform::DebugPhase::shutdown);
    return result;
}

} // namespace strefa::app

