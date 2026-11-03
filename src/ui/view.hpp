#pragma once
#include "core/settings.hpp"

#include <array>
#include <cstdint>
#include <string_view>

struct ImFont;

namespace strefa::ui {

enum class Screen { main_menu, settings, preview, about, laboratory, gpu_laboratory, gpu_mission };
enum class Action {
    none,
    open_preview,
    open_settings,
    open_about,
    open_laboratory,
    open_gpu,
    open_mission,
    back,
    apply,
    apply_session, // apply, but keep unconfirmed experimental options out of settings.ini
    confirm_settings,
    reset,
    quit
};
struct LaboratoryFrame;
struct ArenaFrame;
struct GpuLaboratoryFrame;

struct Image {
    std::uintptr_t handle{};
    int width{};
    int height{};
};
struct Fonts {
    ImFont* body{};
    ImFont* heading{};
};
struct FrameInfo {
    float scale{1.0F};
    float frame_ms{};
    std::string_view renderer;
    std::string_view gl_version;
    std::string_view message;
    bool gpu_available{true};    // OpenGL 4.3 compute: GPU world, its tools and the mission
    bool unconfirmed_settings{}; // experimental options active in this session only
};
// Simulation pacing shown in the mission and GPU tool HUDs.
struct ClockStats {
    double speed{-1};      // simulated / wall seconds over the last second, < 0 = not known yet
    double lost_seconds{}; // wall time the interactive clock discarded since the scene started
    double gpu_tick_ms{};  // GPU time of the last timed tick, 0 = timers unavailable
    bool gpu_slowdown{};   // GPU time per tick > 2x the median of the first 10 s of the scene
};
struct ViewState {
    Screen screen{Screen::main_menu};
    core::Settings applied;
    core::Settings draft;
    bool diagnostics{false};
    core::Settings stored; // what settings.ini holds
};

// Shown while experimental options are active only in this session; true = "save permanently".
[[nodiscard]] bool experimental_notice(const FrameInfo& frame);
// One HUD line with simulation speed, lost time and GPU time per tick, plus the slowdown warning.
void clock_stats(const ClockStats& clock);

[[nodiscard]] Action draw(ViewState& state, const FrameInfo& frame, const std::array<Image, 3>& backgrounds,
                          const Fonts& fonts, LaboratoryFrame* laboratory = nullptr, ArenaFrame* arena = nullptr,
                          GpuLaboratoryFrame* gpu = nullptr);

} // namespace strefa::ui
