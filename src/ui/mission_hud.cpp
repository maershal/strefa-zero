#include "ui/mission_hud.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace strefa::ui {
namespace {
ImTextureRef texture(const Image& image) {
    return ImTextureRef{static_cast<ImTextureID>(image.handle)};
}
} // namespace

Action draw_mission_hud(MissionHudFrame& hud, const FrameInfo& frame, const Fonts& fonts) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("MissionHud", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    // Space belongs to jumping in this screen, not to activating a focused menu button.
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool back = ImGui::Button("Menu [Esc]");
    ImGui::SameLine();
    const bool tools = ImGui::Button("Laboratorium GPU");
    ImGui::SameLine();
    hud.input.toggle_pause = ImGui::Button(hud.paused ? "Wznów" : "Pauza");
    ImGui::SameLine();
    hud.input.reset = ImGui::Button(hud.scene_mission ? "Od nowa [F5]" : "Nowa misja [F5]");
    ImGui::SameLine();
    ImGui::Text("Tarcze %d/3  |  %d/30  |  Granaty %d", hud.game.score(), hud.game.ammunition(),
                hud.game.grenade_stock());
    if (viewport->WorkSize.x / frame.scale >= 700) ImGui::SameLine();
    const float fuel = hud.game.player().fuel / hud.game.fuel_capacity();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4{.72F, .82F, .40F, 1});
    ImGui::ProgressBar(fuel, {90 * frame.scale, 0}, "Paliwo");
    ImGui::PopStyleColor();
    if (hud.game.reload_remaining() > 0)
        ImGui::Text("Przeładowanie %.1f s", static_cast<double>(hud.game.reload_remaining()));
    else
        ImGui::TextDisabled(
                "%s", !hud.scene_mission ? "SCENA LABORATORYJNA / F5 rozpoczyna nową misję"
                                         : "MISJA: 3 tarcze + zielone wyjście. Pomarańczowe przegrody są zniszczalne.");
    if (hud.game.finished) {
        ImGui::PushFont(fonts.heading, 22);
        ImGui::TextWrapped("MISJA UKOŃCZONA  /  F5 — kolejna próba");
        ImGui::PopFont();
    }
    // Quality combos and clock share one row so the canvas keeps the vertical space.
    const bool wide = viewport->WorkSize.x / frame.scale >= 900;
    if (hud.lighting_quality) {
        ImGui::SetNextItemWidth(120 * frame.scale);
        ImGui::Combo("Światło (eksp.)", hud.lighting_quality, "Wyłączone\0Niskie\0Wysokie\0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", core::lighting_cost.data());
    }
    if (hud.vfx_quality) {
        if (hud.lighting_quality && wide) ImGui::SameLine();
        ImGui::SetNextItemWidth(120 * frame.scale);
        ImGui::Combo("Efekty", hud.vfx_quality, "Wyłączone\0Niskie\0Wysokie\0");
    }
    if (hud.clock) {
        if ((hud.lighting_quality || hud.vfx_quality) && wide) ImGui::SameLine();
        clock_stats(*hud.clock);
    }
    bool confirm = false;
    if (hud.lighting_quality) confirm = experimental_notice(frame);
    const auto room = ImGui::GetContentRegionAvail();
    // Fill the room (fractional zoom): integer-only zoom left a 1x canvas on a 1080p window.
    const float fit = std::min(room.x / gpu::width, (room.y - 2 * ImGui::GetTextLineHeightWithSpacing()) / gpu::height);
    const float zoom = std::max(.1F, fit);
    const ImVec2 size{gpu::width * zoom, gpu::height * zoom};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (room.x - size.x) * .5F));
    const auto start = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("MissionCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hover = ImGui::IsItemHovered(), canvas_active = ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end{start.x + size.x, start.y + size.y};
    draw->PushClipRect(start, end, true);
    draw->AddImage(texture(hud.world), start, end);
    hud.input.controls.aim = hud.game.player().feet + hud.game.player().aim * 40 + game::Vec2{0, -12};
    if (hud.focused && !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        auto& input = hud.input.controls;
        input.move = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow))
                   - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow));
        input.jump = ImGui::IsKeyPressed(ImGuiKey_Space, false);
        input.reload = ImGui::IsKeyPressed(ImGuiKey_R, false);
        hud.input.reset = hud.input.reset || ImGui::IsKeyPressed(ImGuiKey_F5, false);
        input.grenade = ImGui::IsKeyPressed(ImGuiKey_G, false);
        const auto mouse = ImGui::GetIO().MousePos;
        // Retain the direction outside the canvas; clamp the target to world bounds.
        // Active/hovered controls in this window continue to own mouse input.
        const bool pointer_controls =
                hover || canvas_active || (!ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive());
        if (pointer_controls) {
            input.aim = {std::clamp((mouse.x - start.x) / zoom + hud.camera[0], 0.F, static_cast<float>(gpu::width)),
                         std::clamp((mouse.y - start.y) / zoom + hud.camera[1], 0.F, static_cast<float>(gpu::height))};
            input.fire = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            input.jet = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            draw->AddCircle(mouse, 3 * zoom, IM_COL32(226, 237, 170, 240), 16, std::max(1.F, zoom * .5F));
            draw->AddLine({mouse.x - 5 * zoom, mouse.y}, {mouse.x + 5 * zoom, mouse.y}, IM_COL32(226, 237, 170, 160));
            draw->AddLine({mouse.x, mouse.y - 5 * zoom}, {mouse.x, mouse.y + 5 * zoom}, IM_COL32(226, 237, 170, 160));
        } else
            input.aim = hud.game.player().feet + hud.game.player().aim * 40 + game::Vec2{0, -12};
    }
    draw->PopClipRect();
    draw->AddRect(start, end, IM_COL32(141, 158, 117, 160));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("A/D: ruch   Spacja: skok   PPM: plecak   LPM: strzał   R: przeładuj   G: granat");
    ImGui::PopStyleColor();
    ImGui::PopItemFlag();
    ImGui::End();
    if (back || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return Action::back;
    return tools ? Action::open_gpu : confirm ? Action::confirm_settings : Action::none;
}

} // namespace strefa::ui
