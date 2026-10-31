#include "app/application.hpp"
#include "app/frame_profile.hpp"
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
public:
    Application(platform::Window& window, ui::Session& gui, const platform::SettingsStore& store,
        core::Settings settings, std::array<ui::Image, 3> images, platform::Texture& canvas,
        ui::Image soldier, ui::Image parts, ui::Image weapons, std::string message, sim::PhysicsConfig physics,gpu::ProfileOptions gpu_profiling,gpu::ExecutionOptions execution,platform::GlDebugLog* gl_debug)
        : window_{window}, gui_{gui}, store_{store}, images_{images}, canvas_{canvas}, soldier_{soldier},
          parts_{parts}, weapons_{weapons}, state_{ui::Screen::main_menu, settings, settings, settings.show_diagnostics},
          message_{std::move(message)}, renderer_{gl_string(GL_RENDERER)}, gl_version_{gl_string(GL_VERSION)},
          physics_{physics},game_{{},{},physics},gpu_profiling_{gpu_profiling},execution_{execution},gl_debug_{gl_debug} {

    }

    std::expected<int, std::string> run(bool smoke_test, bool start_in_laboratory, bool start_in_arena, bool lab_smoke,bool start_gpu,bool gpu_smoke,bool start_mission,bool mission_smoke) {
        if(smoke_test) {
            world_=std::make_unique<sim::World>(0x57EF'A023U,physics_);world_->load_demo();
            lab_snapshot_=sim::present_world(*world_,sim::RenderMode::materials);
        } else {
            lab_worker_=std::make_unique<sim::LaboratoryRunner>(store_.path().parent_path()/"recordings",
                lab_smoke?sim::Scene::dam_break:sim::Scene::demonstration,physics_);
            lab_snapshot_=lab_worker_->snapshot();
        }
        if (start_in_laboratory && !smoke_test) perform(ui::Action::open_laboratory);
        if (start_in_arena && !smoke_test) perform(ui::Action::open_arena);
        if(lab_smoke) {lab_controls_.scene=sim::Scene::dam_break;perform(ui::Action::open_laboratory);}
        if(start_gpu||gpu_smoke){if(!perform(ui::Action::open_gpu))return std::unexpected{message_};}
        if(gpu_smoke){gpu_world_->reset(gpu::Scene::sand_water);gpu_controls_.scene=static_cast<int>(gpu::Scene::sand_water);}
        if(start_mission||mission_smoke){if(!perform(ui::Action::open_mission))return std::unexpected{message_};}
        // Keep the historical 60 FPS run at 900 frames, but let faster displays
        // execute the entire tick-based mission route before its final capture.
        constexpr unsigned mission_smoke_min_frames=900;
        constexpr std::uint64_t mission_smoke_min_ticks=1700;
        std::array<bool, 6> visited_screens{};
        std::array<bool, 3> visited_themes{};
        bool smoke_ok = true;
        unsigned frames{};
        std::vector<double> ui_frame_times;
        std::vector<std::array<double,12>> mission_frames;
        const bool profile_frames=gpu_profiling_.collect;
        std::vector<FrameProfile> frame_profiles;
        if(profile_frames){frame_profiles.reserve(1024);mission_frames.reserve(1024);}
        const auto profile_anchor_ns=profile_frames?SDL_GetTicksNS():std::uint64_t{0};
        const auto profile_anchor_utc_ns=profile_frames?std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count():std::int64_t{0};
        const auto profile_anchor_after_ns=profile_frames?SDL_GetTicksNS():std::uint64_t{0};
        std::uint64_t iterations{},frame_profiles_dropped{};
        constexpr std::size_t frame_profile_capacity=10000;
        auto append_frame_profile=[&](const FrameProfile& row){
            if(frame_profiles.size()<frame_profile_capacity){
                frame_profiles.push_back(row);frame_profiles.back().finish(SDL_GetTicksNS());
            }else ++frame_profiles_dropped;
        };
        std::uint64_t gpu_paused_step{};bool gpu_pause_ok=true;
        if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::runtime);
        auto previous = SDL_GetTicksNS();
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
                frame_profile.frame_limit=static_cast<unsigned>(state_.applied.frame_limit);
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
                clock_.reset();game_clock_.reset();gpu_clock_.reset();game_input_.clear();
                mission_clock_.reset();mission_input_.clear();
                if(profile_frames){
                    frame_profile.mission_tick=mission_world_?mission_world_->metrics().steps:0;
                    frame_profile.mission_pending_seconds=mission_clock_.pending_seconds();
                    frame_profile.mission_dropped_seconds=mission_clock_.dropped_seconds();
                }
                mark(frame_profile.update_ms);
                SDL_Delay(30);mark(frame_profile.sleep_ms);
                if(profile_frames)append_frame_profile(frame_profile);
                continue;
            }

            if (smoke_test) smoke_ok = smoke_step(frames) && smoke_ok;
            if(lab_smoke) {
                if(frames>=30) ui_frame_times.push_back(frame_ms);
                if(frames==120) lab_controls_.paused=true;
                if(frames==150) lab_controls_.paused=false;
                if(frames==200) perform(ui::Action::back);
                if(frames==220) perform(ui::Action::open_laboratory);
            }
            if(mission_smoke&&frames>=30)ui_frame_times.push_back(frame_ms);
            if(gpu_smoke){
                if(frames>=30)ui_frame_times.push_back(frame_ms);
                if(frames==80){gpu_controls_.paused=true;gpu_paused_step=gpu_world_->metrics().steps;}
                if(frames==100){gpu_pause_ok=gpu_world_->metrics().steps==gpu_paused_step;gpu_controls_.paused=false;}
                if(frames==140)perform(ui::Action::back);
                if(frames==155)perform(ui::Action::open_gpu);
            }
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
                const auto uploaded = upload_world(game_.world(), true);
                if (!uploaded) return std::unexpected{uploaded.error()};
                ++game_uploads_;
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
            ui::LaboratoryFrame lab{{canvas_.id(), canvas_.width(), canvas_.height()}, *lab_snapshot_,
                lab_controls_, input, lab_snapshot_->step_ms, lab_snapshot_->overload_seconds};
            ui::ArenaInput arena_input;
            const bool focused = (SDL_GetWindowFlags(window_.native()) & SDL_WINDOW_INPUT_FOCUS) != 0;
            ui::ArenaFrame arena{{canvas_.id(), canvas_.width(), canvas_.height()}, soldier_,
                images_[static_cast<std::size_t>(state_.applied.map_theme)], parts_, weapons_, game_, arena_input, focused};
            double ui_poll_ms=0;gpu::PollTimings ui_poll;
            if(state_.screen==ui::Screen::gpu_mission&&mission_world_){
                const auto poll_started=gpu_profiling_.collect?SDL_GetTicksNS():std::uint64_t{0};
                mission_world_->poll();arena.world={mission_world_->texture(),gpu::width*3,gpu::height*3};arena.game=ui::ArenaView{mission_world_->mission()};
                if(gpu_profiling_.collect){ui_poll_ms=static_cast<double>(SDL_GetTicksNS()-poll_started)/1e6;ui_poll=mission_world_->poll_timings();}
            }
            ui::GpuInput gpu_input;std::optional<ui::GpuLaboratoryFrame> gpu_frame;
            if(state_.screen==ui::Screen::gpu_laboratory&&gpu_world_){
                gpu_world_->poll();
                gpu_frame.emplace(ui::GpuLaboratoryFrame{{gpu_world_->texture(),gpu::width*3,gpu::height*3},gpu_world_->metrics(),gpu_controls_,gpu_input});
            }
            mark(frame_profile.poll_ms);
            const auto action = ui::draw(state_, info, images_, gui_.fonts(), &lab, &arena,gpu_frame?&*gpu_frame:nullptr);
            mark(frame_profile.ui_build_ms);
            if(gpu_frame){
                update_gpu(gpu_input,static_cast<double>(frame_ms)/1000.0,focused||gpu_smoke);
            }else gpu_clock_.reset();
            if (state_.screen == ui::Screen::laboratory) {
                const auto updated = update_lab(input, static_cast<double>(frame_ms) / 1000.0);
                if (!updated) return std::unexpected{updated.error()};
            } else { if(lab_worker_) lab_worker_->set_active(false); clock_.reset(); lab_controls_.previous.reset(); }
            if(state_.screen==ui::Screen::gpu_mission&&action!=ui::Action::back){
                update_mission(arena_input,static_cast<double>(frame_ms)/1000,focused||mission_smoke,mission_smoke);
                if(mission_smoke){
                    const auto& metrics=mission_world_->metrics();
                    const bool timing_available=gpu::effective_timer_mode(gpu_profiling_)!=gpu::TimerMode::off&&metrics.timing_samples>0;
                    mission_frame=std::array<double,12>{static_cast<double>(frames),frame_ms,metrics.gpu_ms,metrics.cpu_submit_ms,mission_clock_.dropped_seconds(),ui_poll_ms,ui_poll.timers_ms,ui_poll.fence_ms,ui_poll.read_ms,mission_step_submit_ms_,timing_available?1.0:0.0,static_cast<double>(metrics.timing_samples)};
                }
            }else{mission_clock_.reset();mission_input_.clear();}
            if (state_.screen == ui::Screen::arena && action != ui::Action::back)
                update_game(arena_input, static_cast<double>(frame_ms) / 1000.0, focused, smoke_test);
            else { game_clock_.reset(); game_input_.clear(); }
            mark(frame_profile.update_ms);
            if(profile_frames){
                frame_profile.world_render_ms=world_render_submit_ms_;
                frame_profile.update_ms-=world_render_submit_ms_;
                frame_profile.mission_ticks=mission_ticks_this_frame_;
                frame_profile.gpu_ticks=gpu_ticks_this_frame_;
                frame_profile.mission_tick=mission_world_?mission_world_->metrics().steps:0;
                frame_profile.mission_pending_seconds=mission_clock_.pending_seconds();
                frame_profile.mission_dropped_seconds=mission_clock_.dropped_seconds();
            }
            const bool mission_final_frame=mission_smoke&&frames>=mission_smoke_min_frames-1&&
                mission_world_->metrics().steps>=mission_smoke_min_ticks;
            const bool capture_frame=(gpu_smoke&&frames==230)||mission_final_frame;
            frame_profile.capture_performed=capture_frame;
            mark(frame_profile.metadata_ms);
            if(capture_frame){
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::capture);
                if(gpu_smoke)gpu_world_->save_image(store_.path().parent_path()/"gpu-preview.bmp");
                if(mission_smoke)mission_world_->save_image(store_.path().parent_path()/"mission-preview.bmp");
                if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::runtime);
                mark(frame_profile.capture_ms);
            }
            window_.clear();
            gui_.render();
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
            if ((smoke_test||gpu_smoke||mission_smoke) && glGetError() != GL_NO_ERROR)
                return std::unexpected{"Test renderowania wykrył błąd OpenGL."};
            mark(frame_profile.validation_ms);
            window_.present();
            mark(frame_profile.present_ms);
            frame_profile.rendered=true;
            if (action != ui::Action::none) perform(action);
            ++frames;
            if (smoke_test && frames >= 238) running_ = false;
            if(lab_smoke&&frames>=300) running_=false;
            if(gpu_smoke&&frames>=240)running_=false;
            if(mission_final_frame)running_=false;

            mark(frame_profile.actions_ms);
            if (!window_.vsync_enabled()) {
                const auto budget = static_cast<Uint64>(1'000'000'000 / state_.applied.frame_limit);
                const auto elapsed = SDL_GetTicksNS() - frame_start;
                if (elapsed < budget) SDL_DelayPrecise(budget - elapsed);
            }
            mark(frame_profile.sleep_ms);
            if(mission_frame)mission_frames.push_back(*mission_frame);
            if(profile_frames)append_frame_profile(frame_profile);
        }
        if(profile_frames){
            std::ofstream timing_out{store_.path().parent_path()/"frame-timings.csv"};
            std::ofstream info_out{store_.path().parent_path()/"frame-timings-info.txt"};
            if(!timing_out||!info_out)return std::unexpected{"Nie można zapisać pełnego profilu klatek."};
            timing_out<<std::setprecision(12);write_frame_profile_header(timing_out);
            for(const auto& row:frame_profiles)write_frame_profile(timing_out,row);
            info_out<<"schema=1\nrows_capacity="<<frame_profile_capacity<<"\nrows_dropped="<<frame_profiles_dropped
                <<"\nanchor_sdl_ns="<<profile_anchor_ns<<"\nanchor_utc_unix_ns="<<profile_anchor_utc_ns
                <<"\nanchor_sdl_after_ns="<<profile_anchor_after_ns
                <<"\nanchor_pair_order=sdl_before_utc_then_sdl_after\nanchor_precision=bracketed_cpu_samples\n"
                <<"mission_smoke="<<mission_smoke<<"\nmission_smoke_min_frames="<<mission_smoke_min_frames
                <<"\nmission_smoke_min_ticks="<<mission_smoke_min_ticks<<"\nrendered_frames="<<frames
                <<"\nloop_iterations="<<iterations<<"\nmission_submitted_ticks="<<(mission_world_?mission_world_->metrics().steps:0)<<'\n'
                <<"durations=current_loop_iteration_after_present_and_limiter\n"
                <<"previous_interval_ms=preceding_frame_start_to_current_frame_start\n"
                <<"phase_partition=input+metadata+scenario+ui_prepare+upload+poll+ui_build+update+world_render+gui_render+capture+validation+present+actions+sleep+other\n"
                <<"rendered_0=minimized_loop_no_swap_or_simulation\n"
                <<"input_ms=SDL_event_poll_and_gui_event_dispatch\n"
                <<"ui_build_ms=UI_widgets_and_game_input_collection\n"
                <<"world_render_ms=CPU_submission_in_update_not_GPU_execution\n"
                <<"capture_ms=synchronous_test_capture_excluded_from_runtime_comparisons\n"
                <<"capture_performed=explicit_capture_marker_not_a_duration_threshold\n"
                <<"validation_ms=per_frame_glGetError_not_full_snapshot_validation\n"
                <<"full_snapshot_validation=after_loop_not_in_frame_timings_csv\n"
                <<"presentation_snapshot=after_event_poll_before_UI_actions\n"
                <<"frame_limit_snapshot=before_UI_actions_may_change_limiter_for_same_iteration\n"
                <<"mission_tick=last_submitted_tick_not_GPU_completion\n"
                <<"mission_pending_seconds=clock_accumulator_including_fractional_tick\n"
                <<"timer_mode="<<gpu::timer_mode_name(gpu::effective_timer_mode(gpu_profiling_))
                <<"\ngpu_timers_enabled="<<gpu_profiling_.gpu_timers
                <<"\nphase_gating="<<execution_.phase_gating
                <<"\nphase_gating_effective="<<(execution_.phase_gating&&execution_.neighbors!=gpu::NeighborMode::linked)
                <<"\nneighbor_reads_requested="<<gpu::neighbor_reads_name(execution_.neighbor_reads)<<"\nneighbor_reads_effective="<<gpu::neighbor_reads_name(gpu::effective_neighbor_reads(execution_.neighbor_reads,execution_.neighbors))<<"\nstorage_requested="<<gpu::device_storage_name(execution_.storage)<<"\nreadback_period="<<gpu_profiling_.readback_period<<"\nskip_zero_projection="<<execution_.skip_zero_projection<<'\n';
            if(!timing_out||!info_out)return std::unexpected{"Niepełny zapis profilu klatek."};
        }
        if(gl_debug_)gl_debug_->set_phase(platform::DebugPhase::validation);
        if(lab_worker_) {
            lab_worker_->stop();
            if(!lab_worker_->snapshot()->error.empty()) return std::unexpected{lab_worker_->snapshot()->error};
        }
        if(lab_smoke) {
            const auto snapshot=lab_worker_->snapshot();
            std::ranges::sort(ui_frame_times);
            auto percentile=[&](double q){return ui_frame_times[std::min(ui_frame_times.size()-1,static_cast<std::size_t>(ui_frame_times.size()*q))];};
            std::ofstream report{store_.path().parent_path()/"laboratory-performance.txt"};
            if(!report) return std::unexpected{"Nie można zapisać raportu laboratorium."};
            report<<"frames="<<frames<<"\nui_median_ms="<<percentile(.5)<<"\nui_p95_ms="<<percentile(.95)
                <<"\nui_p99_ms="<<percentile(.99)<<"\nui_max_ms="<<ui_frame_times.back()
                <<"\nphysics_last_ms="<<snapshot->step_ms<<"\nphysics_ticks="<<snapshot->report.tick
                <<"\nphysics_steps_per_second="<<snapshot->steps_per_second<<"\nphysics_threads="<<snapshot->report.worker_threads
                <<"\nfinite="<<snapshot->report.finite<<'\n';
            if(!snapshot->ready||!snapshot->report.finite||snapshot->report.tick<5||percentile(.95)>100
                ||(physics_.worker_threads&&snapshot->report.worker_threads!=physics_.worker_threads))
                return std::unexpected{"Test reaktywności laboratorium nie przeszedł."};
        }
        if (recording_) { recording_->finish(); recording_.reset(); }
        if(gpu_smoke||mission_smoke){
            std::ranges::sort(ui_frame_times);
            auto percentile=[&](double q){return ui_frame_times[std::min(ui_frame_times.size()-1,static_cast<std::size_t>(ui_frame_times.size()*q))];};
            std::ofstream report{store_.path().parent_path()/(mission_smoke?"mission-ui.txt":"gpu-ui.txt")};
            auto& measured=mission_smoke?*mission_world_:*gpu_world_;
            const auto async_mission=measured.mission();
            // Final validation is outside the measured window, including readback-off ablations.
            const auto final_mission=mission_smoke?measured.read_mission():gpu::MissionSnapshot{};
            if(gpu_profiling_.collect){measured.poll();gpu::write_profile_samples(measured,store_.path().parent_path());}
            if(mission_smoke){std::ofstream frames_out{store_.path().parent_path()/"mission-frames.csv"};frames_out<<"frame,frame_ms,gpu_physics_ms,cpu_submit_ms,dropped_seconds,ui_poll_ms,ui_timer_poll_ms,ui_fence_poll_ms,ui_read_ms,frame_step_submit_ms,gpu_timing_available,gpu_timing_samples\n";
                for(const auto& row:mission_frames){for(std::size_t i=0;i<row.size();++i){if(i)frames_out<<',';frames_out<<row[i];}frames_out<<'\n';}}
            report<<"scene="<<(mission_smoke?"mission":"sand_water")<<"\nframes="<<frames<<"\nmedian_ms="<<percentile(.5)<<"\np95_ms="<<percentile(.95)<<"\np99_ms="<<percentile(.99)
                <<"\nmax_ms="<<ui_frame_times.back()<<"\nsteps="<<measured.metrics().steps<<"\ngpu_step_ms="<<measured.metrics().gpu_ms<<"\ngpu_render_ms="<<measured.metrics().render_ms<<"\ncpu_submit_ms="<<measured.metrics().cpu_submit_ms
                <<"\npaused_ok="<<gpu_pause_ok<<"\ndropped_seconds="<<(mission_smoke?mission_clock_.dropped_seconds():gpu_controls_.lost_seconds)<<"\nrenderer="<<renderer_<<'\n';
            report<<"phase_gating="<<execution_.phase_gating
                <<"\nphase_gating_effective="<<(execution_.phase_gating&&execution_.neighbors!=gpu::NeighborMode::linked)
                <<"\nneighbor_reads_requested="<<gpu::neighbor_reads_name(execution_.neighbor_reads)<<"\nneighbor_reads_effective="<<gpu::neighbor_reads_name(gpu::effective_neighbor_reads(execution_.neighbor_reads,execution_.neighbors))<<"\nstorage_requested="<<gpu::device_storage_name(execution_.storage)<<"\nreadback_period="<<gpu_profiling_.readback_period<<"\nskip_zero_projection="<<execution_.skip_zero_projection<<'\n';
            report<<"storage_effective="<<gpu::device_storage_name(measured.upload_stats().mode)<<'\n';
            report<<"gpu_timer_mode="<<gpu::timer_mode_name(gpu::effective_timer_mode(gpu_profiling_))
                <<"\ngpu_timing_available="<<(gpu::effective_timer_mode(gpu_profiling_)!=gpu::TimerMode::off&&measured.metrics().timing_samples>0)
                <<"\ngpu_timing_samples="<<measured.metrics().timing_samples<<'\n';
            if(mission_smoke)report<<"mission_min_frames="<<mission_smoke_min_frames<<"\nmission_min_ticks="<<mission_smoke_min_ticks
                <<"\nhud_epoch="<<async_mission.status[1]<<"\nhud_tick="<<async_mission.status[2]<<"\nvalidation_tick="<<final_mission.status[2]
                <<"\nshots="<<final_mission.inventory[3]<<"\nexplosions="<<final_mission.counts[0]<<"\nmission_complete="<<final_mission.status[3]<<"\nscore="<<final_mission.inventory[2]<<'\n';
            if(mission_smoke&&(frames<mission_smoke_min_frames||measured.metrics().steps<mission_smoke_min_ticks||
                mission_clock_.dropped_seconds()!=0||(gpu_profiling_.body_coupling&&(final_mission.status[3]!=1||final_mission.inventory[2]!=3))))
                return std::unexpected{"Misja GPU: niepełny przebieg lub pominięte kroki czasu."};
            if(!gpu_pause_ok||measured.metrics().steps<100||percentile(.95)>100)return std::unexpected{"Nieudany test okna GPU."};
        }
        if (smoke_test) {
            const bool all_screens = std::ranges::all_of(visited_screens, [](bool value) { return value; });
            const bool all_themes = std::ranges::all_of(visited_themes, [](bool value) { return value; });
            const bool all_images = std::ranges::all_of(images_, [](const auto& image) { return image.handle != 0; });
            std::ofstream report{store_.path().parent_path() / "smoke-report.txt"};
            report << "frames=" << frames << "\nall_screens=" << all_screens
                << "\nall_themes=" << all_themes << "\nall_backgrounds=" << all_images
                << "\nsettings_applied=" << smoke_ok << "\nrenderer=" << renderer_
                << "\nopengl=" << gl_version_ << "\ncompiler=" << build::compiler
                << "\ncpp=" << __cplusplus << "\nsimulation_uploads=" << uploads_
                << "\nsimulation_pause_step=" << smoke_lab_ok_ << "\nactor_atlas=" << (soldier_.handle != 0)
                << "\ngame_uploads=" << game_uploads_ << "\ngame_ticks=" << game_.ticks()
                << "\nshots_fired=" << game_.shots_fired() << "\ngrenade_explosions=" << game_.explosions() << '\n';
            report<<"layered_parts="<<(parts_.handle!=0)<<"\nweapon_atlas="<<(weapons_.handle!=0)
                <<"\nphysics_finite="<<world_->statistics().finite<<"\ngame_physics_finite="<<game_.world().statistics().finite<<'\n';
            if (!all_screens || !all_themes || !all_images || !smoke_ok || uploads_ == 0 || !smoke_lab_ok_
                || !soldier_.handle || !parts_.handle || !weapons_.handle || !world_->statistics().finite || !game_.world().statistics().finite
                || game_uploads_ == 0 || game_.shots_fired() == 0 || game_.explosions() != 1)
                return std::unexpected{"Niepełny wynik testu aplikacji. Sprawdź smoke-report.txt."};
        }
        return 0;
    }

private:
    void update_mission(const ui::ArenaInput& input,double elapsed,bool focused,bool scripted){
        mission_step_submit_ms_=0;
        if(input.reset){mission_world_->reset_mission();mission_clock_.reset();mission_input_.clear();elapsed=0;}
        if(!focused){mission_clock_.reset();mission_input_.clear();return;}
        mission_input_.submit(input.controls);const int steps=mission_clock_.advance(elapsed,false,1);
        for(int i=0;i<steps;++i){const auto c=mission_input_.consume();
            const unsigned buttons=(c.jump?gpu::jump:0U)|(c.jet?gpu::jet:0U)|(c.fire?gpu::fire:0U)|(c.reload?gpu::reload:0U)|(c.grenade?gpu::grenade:0U);
            mission_world_->step(scripted?gpu::scripted_mission_input(static_cast<unsigned>(mission_world_->metrics().steps)):gpu::MissionInput{c.move,c.aim.x,c.aim.y,buttons});
            mission_step_submit_ms_+=mission_world_->metrics().cpu_submit_ms;}
        mission_ticks_this_frame_=static_cast<unsigned>(steps);
        const auto render_started=gpu_profiling_.collect?SDL_GetTicksNS():std::uint64_t{0};
        mission_world_->render(static_cast<float>(mission_clock_.fraction()));
        if(gpu_profiling_.collect)world_render_submit_ms_+=static_cast<double>(SDL_GetTicksNS()-render_started)/1e6;
    }
    void update_gpu(const ui::GpuInput& input,double elapsed,bool focused){
        if(input.reset){gpu_world_->reset(static_cast<gpu::Scene>(gpu_controls_.scene));gpu_clock_.reset();gpu_controls_.lost_seconds=0;}
        if(input.stroke)gpu_world_->paint(*input.stroke);
        const int steps=gpu_clock_.advance(elapsed,gpu_controls_.paused||!focused,1,input.step&&gpu_controls_.paused);
        for(int i=0;i<steps;++i)gpu_world_->step();
        gpu_controls_.lost_seconds=gpu_clock_.dropped_seconds();
        gpu_ticks_this_frame_=static_cast<unsigned>(steps);
        const auto render_started=gpu_profiling_.collect?SDL_GetTicksNS():std::uint64_t{0};
        gpu_world_->render(gpu_controls_.paused||!focused?1.F:static_cast<float>(gpu_clock_.fraction()));
        if(gpu_profiling_.collect)world_render_submit_ms_+=static_cast<double>(SDL_GetTicksNS()-render_started)/1e6;
    }
    std::expected<void, std::string> upload_world(const sim::World& world, bool transparent = false) {
        last_lab_pixels_.reset();
        world.render(std::span<sim::Color, sim::World::cell_count>{pixels_.data(), pixels_.size()}, transparent,
            transparent ? sim::RenderMode::materials : lab_controls_.view);
        return canvas_.update_rgba(std::as_bytes(std::span{pixels_}));
    }
    void update_game(ui::ArenaInput input, double elapsed, bool focused, bool smoke) {
        if (input.reset) { game_.reset(); game_clock_.reset(); game_input_.clear(); elapsed = 0; }
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
        for (int i = 0; i < steps; ++i) game_.tick(game_input_.consume());
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
                if(!mission_world_){
                    auto created=gpu::ParticleWorld::create(utf8_path(SDL_GetBasePath())/"assets"/"shaders",execution_,gpu_profiling_);
                    if(!created){message_=created.error();return false;}mission_world_=std::move(*created);
                    mission_world_->set_profile_options(gpu_profiling_);
                    mission_world_->set_actor_textures(static_cast<unsigned>(parts_.handle),static_cast<unsigned>(weapons_.handle),weapons_.width,weapons_.height);mission_world_->reset_mission();
                }
                state_.screen=ui::Screen::gpu_mission;mission_clock_.reset();mission_input_.clear();message_.clear();break;
            case ui::Action::open_gpu:
                if(!gpu_world_){
                    auto created=gpu::ParticleWorld::create(utf8_path(SDL_GetBasePath())/"assets"/"shaders",execution_,gpu_profiling_);
                    if(!created){message_=created.error();return false;}
                    // Move the successfully initialized owner only after capability/shader checks.
                    gpu_world_=std::move(*created);
                    gpu_world_->set_profile_options(gpu_profiling_);
                }
                state_.screen=ui::Screen::gpu_laboratory;gpu_clock_.reset();message_.clear();break;
            case ui::Action::open_arena:
                state_.screen = ui::Screen::arena; game_clock_.reset(); game_input_.clear(); message_.clear(); break;
            case ui::Action::open_laboratory:
                state_.screen = ui::Screen::laboratory; clock_.reset(); message_.clear(); break;
            case ui::Action::open_preview: state_.screen = ui::Screen::preview; message_.clear(); break;
            case ui::Action::open_settings:
                state_.draft = state_.applied; state_.screen = ui::Screen::settings; message_.clear(); break;
            case ui::Action::open_about: state_.screen = ui::Screen::about; message_.clear(); break;
            case ui::Action::back:
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
                state_.applied = state_.draft;
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

    bool smoke_step(unsigned frame) {
        switch (frame) {
            case 16: return perform(ui::Action::open_settings);
            case 24:
                state_.draft.map_theme = core::MapTheme::industrial;
                state_.draft.width = 1100; state_.draft.height = 720;
                state_.draft.vsync = false; state_.draft.ui_scale = 1.15F;
                return perform(ui::Action::apply);
            case 32: return perform(ui::Action::open_preview);
            case 40: return perform(ui::Action::open_settings);
            case 41: state_.draft.map_theme = core::MapTheme::winter; return perform(ui::Action::apply);
            case 48: return perform(ui::Action::open_about);
            case 56: return perform(ui::Action::open_laboratory);
            case 57:
                world_->paint({105, 100}, {109, 115}, 3, sim::Material::fire);
                world_->paint({240, 120}, {255, 120}, 4, sim::Material::lava);
                break;
            case 58: lab_controls_.paused = true; smoke_paused_tick_ = world_->statistics().tick; break;
            case 59: smoke_lab_ok_ = world_->statistics().tick == smoke_paused_tick_; break;
            case 60: smoke_single_step_ = true; break;
            case 61: smoke_lab_ok_ = smoke_lab_ok_ && world_->statistics().tick == smoke_paused_tick_ + 1; break;
            case 62: lab_controls_.paused = false; break;
            case 78: return perform(ui::Action::back);
            case 80: return perform(ui::Action::open_arena);
            case 230: return perform(ui::Action::back);
            default: return true;
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
    bool running_{true};
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
    game::GameSession game_;
    game::InputBuffer game_input_;
    sim::FixedClock game_clock_;
    unsigned game_uploads_{}, smoke_game_frame_{};
    std::unique_ptr<gpu::ParticleWorld> gpu_world_,mission_world_;
    gpu::ProfileOptions gpu_profiling_;
    gpu::ExecutionOptions execution_;
    platform::GlDebugLog* gl_debug_{};
    double mission_step_submit_ms_{},world_render_submit_ms_{};
    unsigned mission_ticks_this_frame_{},gpu_ticks_this_frame_{};
    sim::FixedStepClock<120,4,24> mission_clock_;game::InputBuffer mission_input_;
    ui::GpuControls gpu_controls_;
    sim::FixedStepClock<120,4> gpu_clock_;
};

} // namespace

std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv) {
    LaunchOptions result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--smoke-test") result.smoke_test = true;
        else if(argument=="--lab-smoke-test") result.laboratory_smoke_test=true;
        else if (argument == "--laboratory") result.start_in_laboratory = true;
        else if (argument == "--arena") result.start_in_arena = true;
        else if(argument=="--gpu-laboratory")result.start_in_gpu=true;
        else if(argument=="--mission")result.start_in_mission=true;
        else if(argument=="--mission-test")result.mission_test=true;
        else if(argument=="--mission-smoke-test")result.mission_smoke=true;
        else if(argument=="--gpu-test")result.gpu_test=true;
        else if(argument=="--gpu-liquid-test")result.gpu_liquid_test=true;
        else if(argument=="--gpu-flow-test")result.gpu_flow_test=true;
        else if(argument=="--gpu-smoke-test")result.gpu_smoke_test=true;
        else if(argument=="--opengl33")result.legacy_graphics=true;
        else if(argument=="--gpu-work-check")result.work_test=true;
        else if(argument=="--gpu-transport-check")result.transport_test=true;
        else if(argument=="--gpu-hot-check")result.hot_test=true;
        else if(argument=="--gpu-cost-profile")result.cost_profile=true;
        else if(argument=="--gpu-cost-check")result.cost_check=true;
        else if(argument=="--gpu-cost-mode"&&i+1<argc){
            const std::string_view value{argv[++i]};bool found=false;
            for(const auto mode:{gpu::CostMode::off,gpu::CostMode::passes,gpu::CostMode::bounds,gpu::CostMode::snapshots})
                if(value==gpu::cost_mode_name(mode)){result.cost_mode=mode;found=true;}
            if(!found)return std::unexpected{"Diagnostyka kosztu GPU: off, passes, bounds lub snapshots."};
        }
        else if(argument=="--gpu-neighbor-reads"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="full")result.gpu_execution.neighbor_reads=gpu::NeighborReads::full;
            else if(value=="narrow")result.gpu_execution.neighbor_reads=gpu::NeighborReads::narrow;
            else if(value=="sorted")result.gpu_execution.neighbor_reads=gpu::NeighborReads::sorted;
            else return std::unexpected{"Odczyty sąsiadów: full, narrow lub sorted."};
        }
        else if(argument=="--gpu-storage"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="direct")result.gpu_execution.storage=gpu::DeviceStorageMode::direct;
            else if(value=="staged")result.gpu_execution.storage=gpu::DeviceStorageMode::staged;
            else if(value=="immutable")result.gpu_execution.storage=gpu::DeviceStorageMode::immutable;
            else if(value=="auto")result.gpu_execution.storage=gpu::DeviceStorageMode::automatic;
            else return std::unexpected{"Pamięć GPU: direct, staged, immutable lub auto."};
        }
        else if((argument=="--gpu-phase-gating"||argument=="--gpu-zero-projection")&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="on"&&value!="off")return std::unexpected{"Optymalizacja pracy GPU: on lub off."};
            if(argument=="--gpu-phase-gating")result.gpu_execution.phase_gating=value=="on";
            else result.gpu_execution.skip_zero_projection=value=="on";
        }
        else if(argument=="--gpu-coupling-check")result.coupling_test=true;
        else if(argument=="--gpu-coupling-control"){result.coupling_test=true;result.coupling_reference_control=true;}
        else if(argument=="--gpu-coupling-legacy-check"){result.coupling_test=true;result.coupling_legacy_check=true;}
        else if(argument=="--gpu-coupling-profile")result.coupling_profile=true;
        else if(argument=="--gpu-coupling"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="reference")result.gpu_execution.coupling=gpu::CouplingMode::reference;
            else if(value=="local")result.gpu_execution.coupling=gpu::CouplingMode::local;
            else if(value=="combined")result.gpu_execution.coupling=gpu::CouplingMode::combined;
            else return std::unexpected{"Sprzężenie GPU: reference, local lub combined."};
        }
        else if(argument=="--gpu-neighbor-check")result.neighbor_test=true;
        else if(argument=="--gpu-neighbor-profile")result.neighbor_profile=true;
        else if(argument=="--gpu-neighbors"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="linked")result.gpu_execution.neighbors=gpu::NeighborMode::linked;
            else if(value=="sorted")result.gpu_execution.neighbors=gpu::NeighborMode::sorted;
            else if(value=="reused")result.gpu_execution.neighbors=gpu::NeighborMode::reused;
            else return std::unexpected{"Sąsiedztwo GPU: linked, sorted lub reused."};
        }
        else if(argument=="--gpu-layout"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="aos"&&value!="soa")return std::unexpected{"Układ cząstek GPU: aos lub soa."};
            result.gpu_execution.soa=value=="soa";
        }
        else if(argument=="--gpu-execution-check")result.execution_test=true;
        else if(argument=="--gpu-execution-profile")result.execution_profile=true;
        else if(argument=="--gpu-execution"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="reference")result.gpu_execution=gpu::ExecutionOptions::reference();
            else if(value=="optimized")result.gpu_execution={};
            else return std::unexpected{"Harmonogram GPU: reference lub optimized."};
        }
        else if((argument=="--gpu-local-size"||argument=="--gpu-reduction-size"||argument=="--gpu-pbf-size")&&i+1<argc){
            const std::string_view value{argv[++i]};unsigned size{};
            if(value=="64")size=64;else if(value=="128")size=128;else if(value=="256")size=256;
            else return std::unexpected{"Rozmiar grupy GPU: 64, 128 lub 256."};
            if(argument=="--gpu-local-size")result.gpu_execution.local_size=size;
            else if(argument=="--gpu-pbf-size")result.gpu_execution.pbf_size=size;
            else result.gpu_execution.reduction_size=size;
        }
        else if(argument=="--gpu-readback-check")result.readback_test=true;
        else if(argument=="--gpu-readback-profile")result.readback_profile=true;
        else if(argument=="--gpu-readback-mode"&&i+1<argc){
            const std::string_view value{argv[++i]};bool found=false;
            for(const auto mode:{gpu::ReadbackMode::automatic,gpu::ReadbackMode::legacy,gpu::ReadbackMode::stream,gpu::ReadbackMode::staging,gpu::ReadbackMode::persistent})
                if(value==gpu::readback_mode_name(mode)){result.gpu_profiling.readback_mode=mode;found=true;}
            if(!found)return std::unexpected{"Tryb odczytu GPU: auto, legacy, stream, staging lub persistent."};
        }
        else if(argument=="--gpu-profile")result.gpu_profile=true;
        else if(argument=="--gpu-stabilization-profile")result.stabilization_profile=true;
        else if(argument=="--gpu-profile-check")result.gpu_profile_check=true;
        else if(argument=="--gl-debug")result.gl_debug=true;
        else if(argument=="--show-test-window")result.show_test_window=true;
        else if(argument=="--gpu-counters")result.gpu_profiling.collect=true;
        else if(argument=="--gpu-timer-mode"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="elapsed")result.gpu_profiling.timer_mode=gpu::TimerMode::elapsed;
            else if(value=="timestamp")result.gpu_profiling.timer_mode=gpu::TimerMode::timestamp;
            else if(value=="off")result.gpu_profiling.timer_mode=gpu::TimerMode::off;
            else return std::unexpected{"Tryb timerów GPU: elapsed, timestamp lub off."};
            result.gpu_profiling.gpu_timers=value!="off";
        }
        else if(argument=="--test-vsync"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="on"&&value!="off")return std::unexpected{"Test VSync: on lub off."};
            result.test_vsync=value=="on";
        }
        else if(argument=="--test-frame-limit"&&i+1<argc){
            const std::string_view value{argv[++i]};unsigned limit{};
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),limit);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||limit<30||limit>240)
                return std::unexpected{"Testowy limit FPS musi wynosić 30–240."};
            result.test_frame_limit=limit;
        }
        else if(argument=="--gpu-group-timers"){result.gpu_profiling.collect=true;result.gpu_profiling.group_timers=true;}
        else if(argument=="--gpu-no-timers"){result.gpu_profiling.gpu_timers=false;result.gpu_profiling.timer_mode=gpu::TimerMode::off;}
        else if(argument=="--gpu-no-coupling")result.gpu_profiling.body_coupling=false;
        else if(argument=="--gpu-no-diagnostics"){result.gpu_profiling.gpu_timers=false;result.gpu_profiling.periodic_diagnostics=false;}
        else if(argument=="--gpu-readback-period"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="0"&&value!="1"&&value!="2"&&value!="4")return std::unexpected{"Okres odczytu GPU musi wynosić 0, 1, 2 lub 4."};
            result.gpu_profiling.readback_period=static_cast<unsigned>(value.front()-'0');
        }
        else if(argument=="--physics-threads"&&i+1<argc) {
            const std::string_view value{argv[++i]};
            if(value.size()!=1||value.front()<'1'||value.front()>'4')
                return std::unexpected{"Liczba wątków fizyki musi wynosić 1–4."};
            result.physics_threads=static_cast<unsigned>(value.front()-'0');
        }
        else if (argument == "--profile" && i + 1 < argc) result.profile = utf8_path(argv[++i]);
        else return std::unexpected{"Nieznany albo niepełny argument: " + std::string{argument}};
    }
    if ((result.smoke_test||result.laboratory_smoke_test||result.gpu_test||result.gpu_liquid_test||result.gpu_flow_test||result.gpu_smoke_test||result.mission_test||result.mission_smoke||result.gpu_profile||result.stabilization_profile||result.gpu_profile_check||result.readback_test||result.readback_profile||result.execution_test||result.execution_profile||result.neighbor_test||result.neighbor_profile||result.coupling_test||result.coupling_profile||result.work_test||result.transport_test||result.hot_test||result.cost_profile||result.cost_check) && result.profile.empty())
        return std::unexpected{"Test wymaga osobnego katalogu: --smoke-test --profile <katalog>."};
    if (static_cast<int>(result.start_in_arena)+result.start_in_laboratory+result.start_in_gpu+result.start_in_mission>1)
        return std::unexpected{"Wybierz jeden tryb startowy."};
    if(result.coupling_reference_control&&result.coupling_legacy_check)return std::unexpected{"Wybierz jedną kontrolę sprzężenia: reference control albo legacy."};
    if(result.cost_profile&&(!result.gpu_profiling.gpu_timers||result.gpu_profiling.timer_mode!=gpu::TimerMode::elapsed||result.gpu_profiling.group_timers
        ||!result.gpu_profiling.periodic_diagnostics||!result.gpu_profiling.body_coupling||result.gpu_profiling.readback_period!=2||result.gpu_profiling.readback_mode!=gpu::ReadbackMode::automatic))
        return std::unexpected{"Profil kosztu ma stały protokół: elapsed, diagnostyka/sprzężenie ON, HUD auto/2, bez timerów grupowych."};
    if(static_cast<int>(result.smoke_test)+result.laboratory_smoke_test+result.gpu_test+result.gpu_liquid_test+result.gpu_flow_test+result.gpu_smoke_test+result.mission_test+result.mission_smoke+result.gpu_profile+result.stabilization_profile+result.gpu_profile_check+result.readback_test+result.readback_profile+result.execution_test+result.execution_profile+result.neighbor_test+result.neighbor_profile+result.coupling_test+result.coupling_profile+result.work_test+result.transport_test+result.hot_test+result.cost_profile+result.cost_check>1) return std::unexpected{"Wybierz jeden rodzaj testu."};
    if((result.test_vsync.has_value()||result.test_frame_limit.has_value())&&
        !(result.smoke_test||result.laboratory_smoke_test||result.gpu_smoke_test||result.mission_smoke))
        return std::unexpected{"Opcje --test-vsync i --test-frame-limit wymagają testu okna."};
    return result;
}

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
    const bool testing=options.smoke_test||options.laboratory_smoke_test||options.gpu_test||options.gpu_liquid_test||options.gpu_flow_test||options.gpu_smoke_test||options.mission_test||options.mission_smoke||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.cost_check;
    if(testing) std::filesystem::create_directories(profile);
    auto loaded = store.load();
    std::string message = loaded ? std::string{} : loaded.error() + " Użyto ustawień domyślnych.";
    // A smoke run starts from known defaults and only writes its dedicated profile.
    auto settings = testing ? core::Settings{} : loaded.value_or(core::Settings{});
    if(options.laboratory_smoke_test||options.gpu_smoke_test||options.mission_test||options.mission_smoke||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.cost_check) {settings.vsync=false;settings.frame_limit=60;}
    if(options.test_vsync.has_value())settings.vsync=*options.test_vsync;
    if(options.test_frame_limit.has_value())settings.frame_limit=static_cast<int>(*options.test_frame_limit);
    auto window = platform::Window::create(settings,!options.legacy_graphics,options.gl_debug);
    if (!window) return std::unexpected{window.error()};
    if(testing&&options.show_test_window)window->show();
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
    if(options.gpu_test||options.gpu_liquid_test||options.gpu_flow_test||options.gpu_smoke_test||options.mission_test||options.mission_smoke||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.cost_check){
        GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
        if(major<4||(major==4&&minor<3)){std::ofstream{profile/"gpu-skipped.txt"}<<"OpenGL 4.3 unavailable; GPU tests skipped, CPU mode remains supported.\n";return 77;}
    }
    if(testing&&!(options.smoke_test||options.laboratory_smoke_test||options.gpu_smoke_test||options.mission_smoke)&&gl_debug)
        gl_debug->set_phase(platform::DebugPhase::validation);
    if(options.work_test){
        const auto result=gpu::validate_work(profile,options.gpu_execution);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.transport_test){
        const auto result=gpu::validate_transport(profile);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.cost_profile||options.cost_check){
        const auto result=gpu::profile_cost(profile,options.gpu_execution,options.cost_mode,options.cost_check);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.hot_test){
        const auto result=gpu::validate_hot_reads(profile,options.gpu_execution);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.coupling_test||options.coupling_profile){
        const auto result=gpu::validate_coupling(profile,options.coupling_profile,options.coupling_reference_control,options.coupling_legacy_check,options.gpu_execution);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.neighbor_test||options.neighbor_profile){
        const auto result=gpu::validate_neighbors(profile,options.neighbor_profile);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.execution_test||options.execution_profile){
        const auto result=gpu::validate_execution(profile,options.execution_profile);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.readback_test||options.readback_profile){
        const auto result=options.readback_test?gpu::validate_readback(profile):gpu::profile_readback(profile);
        if(!result)return std::unexpected{result.error()};return 0;
    }
    if(options.gpu_test||options.gpu_liquid_test||options.gpu_flow_test||options.mission_test||options.gpu_profile||options.stabilization_profile||options.gpu_profile_check||options.readback_test||options.readback_profile||options.execution_test||options.execution_profile||options.neighbor_test||options.neighbor_profile||options.coupling_test||options.coupling_profile||options.work_test||options.transport_test||options.hot_test||options.cost_profile||options.cost_check){
        auto world=gpu::ParticleWorld::create(assets/"shaders",options.gpu_execution,options.gpu_profiling);if(!world)return std::unexpected{world.error()};
        if(options.stabilization_profile){const auto result=gpu::profile_stabilization(**world,profile,options.gpu_profiling);if(!result)return std::unexpected{result.error()};return 0;}
        if(options.gpu_profile||options.gpu_profile_check){const auto result=gpu::profile_gpu(**world,profile,options.gpu_profile_check);if(!result)return std::unexpected{result.error()};return 0;}
        const auto validated=options.mission_test?gpu::validate_mission(**world,profile):options.gpu_flow_test?gpu::validate_flow(**world,profile):options.gpu_liquid_test?gpu::validate_liquids(**world,profile):gpu::validate_granular(**world,profile);if(!validated)return std::unexpected{validated.error()};return 0;
    }
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
        {weapons->id(),weapons->width(),weapons->height()}, std::move(message),physics,options.gpu_profiling,options.gpu_execution,gl_debug.get()};
    auto result=application.run(options.smoke_test, options.start_in_laboratory, options.start_in_arena,options.laboratory_smoke_test,options.start_in_gpu,options.gpu_smoke_test,options.start_in_mission,options.mission_smoke);
    if(gl_debug)gl_debug->set_phase(platform::DebugPhase::shutdown);
    return result;
}

} // namespace strefa::app

