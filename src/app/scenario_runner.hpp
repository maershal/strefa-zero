#pragma once
#include "app/frame_profile.hpp"
#include "ui/laboratory.hpp"
#include "ui/gpu_laboratory.hpp"
#include "sim/recording.hpp"
#include "build_info.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <vector>

namespace strefa::app {
// Uses the production lifecycle/actions and scheduler. Scripted input and
// synchronous validation/export stay outside the interactive frame controller.
class ScenarioRunner {
public:
    static constexpr unsigned mission_smoke_min_frames=900;
    static constexpr std::uint64_t mission_smoke_min_ticks=1700;
    ScenarioRunner(bool smoke,bool laboratory,bool gpu,bool mission,bool profile,bool session)
        :session_smoke{session},smoke_test{smoke},lab_smoke{laboratory},gpu_smoke{gpu},mission_smoke{mission}{
        profile_frames=profile;
        if(profile){
            frame_profiles.reserve(1024);mission_frames.reserve(1024);
            profile_anchor_ns=SDL_GetTicksNS();
            profile_anchor_utc_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            profile_anchor_after_ns=SDL_GetTicksNS();
        }
    }
    void append(const FrameProfile& row){
        if(frame_profiles.size()<frame_profile_capacity){frame_profiles.push_back(row);frame_profiles.back().finish(SDL_GetTicksNS());}
        else ++frame_profiles_dropped;
    }
    template<class App> void before_frame(App& app,float frame_ms){
        if(session_smoke)session_step(app);
            if (smoke_test) smoke_ok = smoke_step(app,frames) && smoke_ok;
            if(lab_smoke) {
                if(frames>=30) ui_frame_times.push_back(frame_ms);
                if(frames==120) app.lab_controls_.paused=true;
                if(frames==150) app.lab_controls_.paused=false;
                if(frames==200) app.perform(ui::Action::back);
                if(frames==220) app.perform(ui::Action::open_laboratory);
            }
            if(mission_smoke&&frames>=30)ui_frame_times.push_back(frame_ms);
            if(gpu_smoke){
                if(frames>=30)ui_frame_times.push_back(frame_ms);
                if(!app.window_seconds_&&frames==80){app.gpu_controls_.paused=true;gpu_paused_step=app.simulation_->metrics().steps;}
                if(!app.window_seconds_&&frames==100){gpu_pause_ok=app.simulation_->metrics().steps==gpu_paused_step;app.gpu_controls_.paused=false;}
                if(!app.window_seconds_&&frames==140)app.perform(ui::Action::back);
                if(!app.window_seconds_&&frames==155)app.perform(ui::Action::open_gpu);
            }
    }
    template<class App> std::expected<void,std::string> write_frames(App& app){
        if(profile_frames){
            std::ofstream timing_out{app.store_.path().parent_path()/"frame-timings.csv"};
            std::ofstream info_out{app.store_.path().parent_path()/"frame-timings-info.txt"};
            if(!timing_out||!info_out)return std::unexpected{"Nie można zapisać pełnego profilu klatek."};
            timing_out<<std::setprecision(12);write_frame_profile_header(timing_out);
            for(const auto& row:frame_profiles)write_frame_profile(timing_out,row);
            info_out<<"render_width=960\nrender_height=576\nrender_quality="<<gpu::render_quality_name(app.simulation_?app.simulation_->render_options().quality:gpu::RenderQuality::basic)<<'\n';
            info_out<<"schema=1\nrows_capacity="<<frame_profile_capacity<<"\nrows_dropped="<<frame_profiles_dropped
                <<"\nanchor_sdl_ns="<<profile_anchor_ns<<"\nanchor_utc_unix_ns="<<profile_anchor_utc_ns
                <<"\nanchor_sdl_after_ns="<<profile_anchor_after_ns
                <<"\nanchor_pair_order=sdl_before_utc_then_sdl_after\nanchor_precision=bracketed_cpu_samples\n"
                <<"mission_smoke="<<mission_smoke<<"\nmission_smoke_min_frames="<<mission_smoke_min_frames
                <<"\nmission_smoke_min_ticks="<<mission_smoke_min_ticks<<"\nrendered_frames="<<frames
                <<"\nloop_iterations="<<iterations<<"\nmission_submitted_ticks="<<(app.simulation_?app.simulation_->metrics().steps:0)<<'\n'
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
                <<"timer_mode="<<gpu::timer_mode_name(gpu::effective_timer_mode(app.gpu_profiling_))
                <<"\ngpu_timers_enabled="<<app.gpu_profiling_.gpu_timers
                <<"\nphase_gating="<<app.execution_.phase_gating
                <<"\nphase_gating_effective="<<(app.execution_.phase_gating&&app.execution_.neighbors!=gpu::NeighborMode::linked)
                <<"\nneighbor_reads_requested="<<gpu::neighbor_reads_name(app.execution_.neighbor_reads)<<"\nneighbor_reads_effective="<<gpu::neighbor_reads_name(gpu::effective_neighbor_reads(app.execution_.neighbor_reads,app.execution_.neighbors))<<"\nstorage_requested="<<gpu::device_storage_name(app.execution_.storage)<<"\nreadback_period="<<app.gpu_profiling_.readback_period<<"\nskip_zero_projection="<<app.execution_.skip_zero_projection<<'\n';
            if(!timing_out||!info_out)return std::unexpected{"Niepełny zapis profilu klatek."};
        }
        return {};
    }
    template<class App> std::expected<void,std::string> finish(App& app){
        if(session_smoke){
            const auto cache=app.simulation_->shader_cache_stats();
            session_ok=session_ok&&app.simulation_.creations()==1&&!app.game_&&!app.lab_worker_&&cache.programs==session_programs;
            std::ofstream report{app.store_.path().parent_path()/"session-smoke.txt"};
            report<<"frames="<<frames<<"\nchecks="<<session_checks<<"\npassed="<<session_ok
                <<"\nworld_creations="<<app.simulation_.creations()<<"\nprograms="<<cache.programs
                <<"\ncpu_game_created="<<bool(app.game_)<<"\ncpu_laboratory_created="<<bool(app.lab_worker_)<<'\n';
            const auto folder=app.store_.path().parent_path()/"recording-check";
            if(auto saved=app.simulation_.save_recording(folder);!saved)return saved;
            const auto tick=app.simulation_->metrics().steps;
            if(auto replayed=app.simulation_.replay_recording(folder/"session.txt");!replayed)return replayed;
            if(app.simulation_->metrics().steps!=tick||app.simulation_.scene().mission)return std::unexpected{"Odtwarzanie nie zachowało sceny i kroku."};
            if(!session_ok||session_checks!=12)return std::unexpected{"Wspólna scena: utrata stanu lub niepełny test przełączeń."};
        }
        if(lab_smoke) {
            const auto snapshot=app.lab_worker_->snapshot();
            std::ranges::sort(ui_frame_times);
            auto percentile=[&](double q){return ui_frame_times[std::min(ui_frame_times.size()-1,static_cast<std::size_t>(ui_frame_times.size()*q))];};
            std::ofstream report{app.store_.path().parent_path()/"laboratory-performance.txt"};
            if(!report) return std::unexpected{"Nie można zapisać raportu laboratorium."};
            report<<"frames="<<frames<<"\nui_median_ms="<<percentile(.5)<<"\nui_p95_ms="<<percentile(.95)
                <<"\nui_p99_ms="<<percentile(.99)<<"\nui_max_ms="<<ui_frame_times.back()
                <<"\nphysics_last_ms="<<snapshot->step_ms<<"\nphysics_ticks="<<snapshot->report.tick
                <<"\nphysics_steps_per_second="<<snapshot->steps_per_second<<"\nphysics_threads="<<snapshot->report.worker_threads
                <<"\nfinite="<<snapshot->report.finite<<'\n';
            if(!snapshot->ready||!snapshot->report.finite||snapshot->report.tick<5||percentile(.95)>100
                ||(app.physics_.worker_threads&&snapshot->report.worker_threads!=app.physics_.worker_threads))
                return std::unexpected{"Test reaktywności laboratorium nie przeszedł."};
        }
        if(gpu_smoke||mission_smoke){
            std::ranges::sort(ui_frame_times);
            auto percentile=[&](double q){return ui_frame_times[std::min(ui_frame_times.size()-1,static_cast<std::size_t>(ui_frame_times.size()*q))];};
            std::ofstream report{app.store_.path().parent_path()/(mission_smoke?"mission-ui.txt":"gpu-ui.txt")};
            auto& measured=mission_smoke?*app.simulation_:*app.simulation_;
            const auto async_mission=measured.mission();
            // Final validation is outside the measured window, including readback-off ablations.
            const auto final_mission=mission_smoke?measured.read_mission():gpu::MissionSnapshot{};
            if(app.gpu_profiling_.collect){measured.poll();gpu::write_profile_samples(measured,app.store_.path().parent_path());}
            if(mission_smoke){std::ofstream frames_out{app.store_.path().parent_path()/"mission-frames.csv"};frames_out<<"frame,frame_ms,gpu_physics_ms,cpu_submit_ms,dropped_seconds,ui_poll_ms,ui_timer_poll_ms,ui_fence_poll_ms,ui_read_ms,frame_step_submit_ms,gpu_timing_available,gpu_timing_samples\n";
                for(const auto& row:mission_frames){for(std::size_t i=0;i<row.size();++i){if(i)frames_out<<',';frames_out<<row[i];}frames_out<<'\n';}}
            report<<"scene="<<(mission_smoke?"mission":"sand_water")<<"\nframes="<<frames<<"\nmedian_ms="<<percentile(.5)<<"\np95_ms="<<percentile(.95)<<"\np99_ms="<<percentile(.99)
                <<"\nmax_ms="<<ui_frame_times.back()<<"\nsteps="<<measured.metrics().steps<<"\ngpu_step_ms="<<measured.metrics().gpu_ms<<"\ngpu_render_ms="<<measured.metrics().render_ms<<"\ncpu_submit_ms="<<measured.metrics().cpu_submit_ms
                <<"\npaused_ok="<<gpu_pause_ok<<"\ndropped_seconds="<<(mission_smoke?app.simulation_.clock().dropped_seconds():app.gpu_controls_.lost_seconds)<<"\nrenderer="<<app.renderer_<<'\n';
            report<<"phase_gating="<<app.execution_.phase_gating
                <<"\nphase_gating_effective="<<(app.execution_.phase_gating&&app.execution_.neighbors!=gpu::NeighborMode::linked)
                <<"\nneighbor_reads_requested="<<gpu::neighbor_reads_name(app.execution_.neighbor_reads)<<"\nneighbor_reads_effective="<<gpu::neighbor_reads_name(gpu::effective_neighbor_reads(app.execution_.neighbor_reads,app.execution_.neighbors))<<"\nstorage_requested="<<gpu::device_storage_name(app.execution_.storage)<<"\nreadback_period="<<app.gpu_profiling_.readback_period<<"\nskip_zero_projection="<<app.execution_.skip_zero_projection<<'\n';
            report<<"storage_effective="<<gpu::device_storage_name(measured.upload_stats().mode)<<'\n';
            report<<"conditional_work_requested="<<app.execution_.conditional_work
                <<"\nconditional_work_effective="<<measured.execution_options().conditional_work<<'\n';
            report<<"gpu_timer_mode="<<gpu::timer_mode_name(gpu::effective_timer_mode(app.gpu_profiling_))
                <<"\ngpu_timing_available="<<(gpu::effective_timer_mode(app.gpu_profiling_)!=gpu::TimerMode::off&&measured.metrics().timing_samples>0)
                <<"\ngpu_timing_samples="<<measured.metrics().timing_samples<<'\n';
            if(mission_smoke)report<<"mission_min_frames="<<mission_smoke_min_frames<<"\nmission_min_ticks="<<mission_smoke_min_ticks
                <<"\nhud_epoch="<<async_mission.status[1]<<"\nhud_tick="<<async_mission.status[2]<<"\nvalidation_tick="<<final_mission.status[2]
                <<"\nshots="<<final_mission.inventory[3]<<"\nexplosions="<<final_mission.counts[0]<<"\nmission_complete="<<final_mission.status[3]<<"\nscore="<<final_mission.inventory[2]<<'\n';
            if(mission_smoke&&(frames<mission_smoke_min_frames||measured.metrics().steps<mission_smoke_min_ticks||
                app.simulation_.clock().dropped_seconds()!=0||(app.gpu_profiling_.body_coupling&&(final_mission.status[3]!=1||final_mission.inventory[2]!=3))))
                return std::unexpected{"Misja GPU: niepełny przebieg lub pominięte kroki czasu."};
            if(!gpu_pause_ok||measured.metrics().steps<100||percentile(.95)>100)return std::unexpected{"Nieudany test okna GPU."};
        }
        if (smoke_test) {
            const bool all_screens = std::ranges::all_of(visited_screens, [](bool value) { return value; });
            const bool all_themes = std::ranges::all_of(visited_themes, [](bool value) { return value; });
            const bool all_images = std::ranges::all_of(app.images_, [](const auto& image) { return image.handle != 0; });
            std::ofstream report{app.store_.path().parent_path() / "smoke-report.txt"};
            report << "frames=" << frames << "\nall_screens=" << all_screens
                << "\nall_themes=" << all_themes << "\nall_backgrounds=" << all_images
                << "\nsettings_applied=" << smoke_ok << "\nrenderer=" << app.renderer_
                << "\nopengl=" << app.gl_version_ << "\ncompiler=" << build::compiler
                << "\ncpp=" << __cplusplus << "\nsimulation_uploads=" << app.uploads_
                << "\nsimulation_pause_step=" << app.smoke_lab_ok_ << "\nactor_atlas=" << (app.soldier_.handle != 0)
                << "\ngame_uploads=" << app.game_uploads_ << "\ngame_ticks=" << app.game_->ticks()
                << "\nshots_fired=" << app.game_->shots_fired() << "\ngrenade_explosions=" << app.game_->explosions() << '\n';
            report<<"layered_parts="<<(app.parts_.handle!=0)<<"\nweapon_atlas="<<(app.weapons_.handle!=0)
                <<"\nphysics_finite="<<app.world_->statistics().finite<<"\ngame_physics_finite="<<app.game_->world().statistics().finite<<'\n';
            if (!all_screens || !all_themes || !all_images || !smoke_ok || app.uploads_ == 0 || !app.smoke_lab_ok_
                || !app.soldier_.handle || !app.parts_.handle || !app.weapons_.handle || !app.world_->statistics().finite || !app.game_->world().statistics().finite
                || app.game_uploads_ == 0 || app.game_->shots_fired() == 0 || app.game_->explosions() != 1)
                return std::unexpected{"Niepełny wynik testu aplikacji. Sprawdź smoke-report.txt."};
        }
        return {};
    }
    template<class App> bool smoke_step(App& app,unsigned frame) {
        switch (frame) {
            case 16: return app.perform(ui::Action::open_settings);
            case 24:
                app.state_.draft.map_theme = core::MapTheme::industrial;
                app.state_.draft.width = 1100; app.state_.draft.height = 720;
                app.state_.draft.vsync = false; app.state_.draft.ui_scale = 1.15F;
                return app.perform(ui::Action::apply);
            case 32: return app.perform(ui::Action::open_preview);
            case 40: return app.perform(ui::Action::open_settings);
            case 41: app.state_.draft.map_theme = core::MapTheme::winter; return app.perform(ui::Action::apply);
            case 48: return app.perform(ui::Action::open_about);
            case 56: return app.perform(ui::Action::open_laboratory);
            case 57:
                app.world_->paint({105, 100}, {109, 115}, 3, sim::Material::fire);
                app.world_->paint({240, 120}, {255, 120}, 4, sim::Material::lava);
                break;
            case 58: app.lab_controls_.paused = true; app.smoke_paused_tick_ = app.world_->statistics().tick; break;
            case 59: app.smoke_lab_ok_ = app.world_->statistics().tick == app.smoke_paused_tick_; break;
            case 60: app.smoke_single_step_ = true; break;
            case 61: app.smoke_lab_ok_ = app.smoke_lab_ok_ && app.world_->statistics().tick == app.smoke_paused_tick_ + 1; break;
            case 62: app.lab_controls_.paused = false; break;
            case 78: return app.perform(ui::Action::back);
            case 80: return app.perform(ui::Action::open_arena);
            case 230: return app.perform(ui::Action::back);
            default: return true;
        }
        return true;
    }

    template<class App> void session_step(App& app){
        auto check=[&](bool condition){session_ok=condition&&session_ok;++session_checks;};
        auto snapshot=[&]{
            retained_mission=app.simulation_->read_mission();
            retained_grains=app.simulation_->readback();retained_terrain=app.simulation_->read_terrain();
            retained_tick=app.simulation_->metrics().steps;
        };
        auto unchanged=[&]{
            const auto mission=app.simulation_->read_mission();const auto grains=app.simulation_->readback();
            return app.simulation_->metrics().steps==retained_tick&&std::memcmp(&mission,&retained_mission,sizeof(mission))==0
                &&grains.size()==retained_grains.size()&&std::memcmp(grains.data(),retained_grains.data(),grains.size()*sizeof(gpu::Grain))==0
                &&app.simulation_->read_terrain()==retained_terrain;
        };
        switch(frames){
        case 0: app.simulation_.begin_recording();session_programs=app.simulation_->shader_cache_stats().programs;check(session_programs>0&&!app.game_&&!app.lab_worker_);break;
        case 30: app.gpu_controls_.paused=true;snapshot();app.perform(ui::Action::open_gpu);break;
        case 31: check(unchanged()&&app.simulation_.scene().mission);break;
        case 32: injected_stroke=gpu::Stroke{8,8,8,8,2,gpu::Material::sand};break;
        case 33: check(app.simulation_->metrics().steps==retained_tick&&app.simulation_->read_terrain()==retained_terrain);snapshot();break;
        case 34: app.perform(ui::Action::open_mission);break;
        case 35: check(unchanged());break;
        case 36: app.gpu_controls_.render_quality=2;break;
        case 37: check(unchanged());break;
        case 38: app.gpu_controls_.render_quality=0;break;
        case 39: check(unchanged());break;
        case 40: app.perform(ui::Action::open_gpu);injected_step=true;break;
        case 41: check(app.simulation_->metrics().steps==retained_tick+1);snapshot();break;
        case 50: app.perform(ui::Action::back);break;
        case 60: check(unchanged());app.perform(ui::Action::open_mission);break;
        case 61: check(unchanged());break;
        case 70: app.gpu_controls_.paused=false;break;
        case 90: app.gpu_controls_.paused=true;app.perform(ui::Action::open_gpu);snapshot();break;
        case 91: check(unchanged());break;
        case 100: app.simulation_.reset({false,gpu::Scene::empty});snapshot();break;
        case 101: check(!app.simulation_.scene().mission&&retained_tick==0);app.perform(ui::Action::open_mission);break;
        case 102: check(unchanged()&&!app.simulation_.scene().mission);break;
        default:break;
        }
    }
    bool session_smoke{},session_ok{true},injected_step{};
    std::optional<gpu::Stroke> injected_stroke;
    unsigned session_checks{},session_programs{};
    std::uint64_t retained_tick{};
    gpu::MissionSnapshot retained_mission;
    std::vector<gpu::Grain> retained_grains;
    std::vector<std::uint32_t> retained_terrain;
    bool smoke_test{},lab_smoke{},gpu_smoke{},mission_smoke{};
        std::array<bool, 6> visited_screens{};
        std::array<bool, 3> visited_themes{};
        bool smoke_ok = true;
        unsigned frames{};
        std::vector<double> ui_frame_times;
        std::vector<std::array<double,12>> mission_frames;
        bool profile_frames{};
        std::vector<FrameProfile> frame_profiles;
        std::uint64_t profile_anchor_ns{};
        std::uint64_t profile_anchor_utc_ns{};
        std::uint64_t profile_anchor_after_ns{};
        std::uint64_t iterations{},frame_profiles_dropped{};
        static constexpr std::size_t frame_profile_capacity=250000;
        std::uint64_t gpu_paused_step{};bool gpu_pause_ok=true;
};
}
