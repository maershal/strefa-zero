#pragma once
#include "platform/window.hpp"
#include <cstdint>
#include <ostream>

namespace strefa::app {

// All durations belong to this loop iteration, including presentation and the
// explicit frame limiter. Legacy frame_ms is the preceding start-to-start gap.
struct FrameProfile {
    std::uint64_t iteration{},frame{},start_ns{},mission_tick{};
    int screen{};
    bool rendered{},capture_performed{};
    unsigned mission_ticks{},gpu_ticks{},frame_limit{};
    double previous_interval_ms{},total_ms{},input_ms{},metadata_ms{},scenario_ms{},
        ui_prepare_ms{},upload_ms{},poll_ms{},ui_build_ms{},update_ms{},
        world_render_ms{},gui_render_ms{},capture_ms{},validation_ms{},present_ms{},
        actions_ms{},sleep_ms{},other_ms{},mission_pending_seconds{},mission_dropped_seconds{};
    platform::PresentationInfo presentation;

    void finish(std::uint64_t end_ns) noexcept {
        total_ms=static_cast<double>(end_ns-start_ns)/1e6;
        other_ms=total_ms-(input_ms+metadata_ms+scenario_ms+ui_prepare_ms+upload_ms+
            poll_ms+ui_build_ms+update_ms+world_render_ms+gui_render_ms+capture_ms+
            validation_ms+present_ms+actions_ms+sleep_ms);
    }
};

inline void write_frame_profile_header(std::ostream& out){
    out<<"iteration,frame,screen,rendered,start_ns,previous_interval_ms,frame_total_ms,"
        "input_ms,metadata_ms,scenario_ms,ui_prepare_ms,upload_ms,poll_ms,ui_build_ms,"
        "update_ms,world_render_ms,gui_render_ms,capture_ms,validation_ms,present_ms,"
        "actions_ms,sleep_ms,other_ms,mission_ticks_this_frame,gpu_ticks_this_frame,"
        "mission_tick,mission_pending_seconds,mission_dropped_seconds,focused,minimized,"
        "window_flags,display_id,window_pixel_width,window_pixel_height,display_mode_available,"
        "display_mode_width,display_mode_height,refresh_hz,refresh_numerator,refresh_denominator,"
        "vsync_enabled,swap_interval_available,swap_interval,frame_limit,capture_performed\n";
}

inline void write_frame_profile(std::ostream& out,const FrameProfile& f){
    const auto& p=f.presentation;
    out<<f.iteration<<','<<f.frame<<','<<f.screen<<','<<f.rendered<<','<<f.start_ns<<','
        <<f.previous_interval_ms<<','<<f.total_ms<<','<<f.input_ms<<','<<f.metadata_ms<<','
        <<f.scenario_ms<<','<<f.ui_prepare_ms<<','<<f.upload_ms<<','<<f.poll_ms<<','
        <<f.ui_build_ms<<','<<f.update_ms<<','<<f.world_render_ms<<','<<f.gui_render_ms<<','
        <<f.capture_ms<<','<<f.validation_ms<<','<<f.present_ms<<','<<f.actions_ms<<','
        <<f.sleep_ms<<','<<f.other_ms<<','<<f.mission_ticks<<','<<f.gpu_ticks<<','
        <<f.mission_tick<<','<<f.mission_pending_seconds<<','<<f.mission_dropped_seconds<<','
        <<p.focused<<','<<p.minimized<<','<<p.window_flags<<','<<p.display_id<<','
        <<p.pixel_width<<','<<p.pixel_height<<','<<p.mode_available<<','<<p.mode_width<<','
        <<p.mode_height<<','<<p.refresh_hz<<','<<p.refresh_numerator<<','<<p.refresh_denominator<<','
        <<p.vsync_enabled<<','<<p.swap_interval_available<<','<<p.swap_interval<<','<<f.frame_limit<<','<<f.capture_performed<<'\n';
}

} // namespace strefa::app
