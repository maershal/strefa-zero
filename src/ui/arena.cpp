#include "ui/arena.hpp"

#include "sim/world.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace strefa::ui {
namespace {
ImTextureRef texture(const Image& image) {
    return ImTextureRef{static_cast<ImTextureID>(image.handle)};
}
} // namespace

Action draw_arena(ArenaFrame& arena, const FrameInfo& frame, const Fonts& fonts) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("TrainingArena", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    // Space belongs to jumping in this screen, not to activating a focused menu button.
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool back = ImGui::Button("Menu [Esc]");
    ImGui::SameLine();
    const bool tools = ImGui::Button("Laboratorium GPU");
    ImGui::SameLine();
    arena.input.toggle_pause = ImGui::Button(arena.paused ? "Wznów" : "Pauza");
    ImGui::SameLine();
    arena.input.reset = ImGui::Button(arena.scene_mission ? "Od nowa [F5]" : "Nowa misja [F5]");
    ImGui::SameLine();
    ImGui::Text("Tarcze %d/3  |  %d/30  |  Granaty %d", arena.game.score(), arena.game.ammunition(),
                arena.game.grenade_stock());
    if (viewport->WorkSize.x / frame.scale >= 700) ImGui::SameLine();
    const float fuel = arena.game.player().fuel / arena.game.fuel_capacity();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4{.72F, .82F, .40F, 1});
    ImGui::ProgressBar(fuel, {90 * frame.scale, 0}, "Paliwo");
    ImGui::PopStyleColor();
    if (arena.game.reload_remaining() > 0)
        ImGui::Text("Przeładowanie %.1f s", static_cast<double>(arena.game.reload_remaining()));
    else
        ImGui::TextDisabled("%s",
                            !arena.scene_mission
                                    ? "SCENA LABORATORYJNA / F5 rozpoczyna nową misję"
                                    : "MISJA: 3 tarcze + zielone wyjście. Pomarańczowe przegrody są zniszczalne.");
    if (arena.game.finished) {
        ImGui::PushFont(fonts.heading, 22);
        ImGui::TextWrapped("MISJA UKOŃCZONA  /  F5 — kolejna próba");
        ImGui::PopFont();
    }
    if (arena.lighting_quality) {
        ImGui::SetNextItemWidth(120 * frame.scale);
        ImGui::Combo("Światło (eksp.)", arena.lighting_quality, "Wyłączone\0Niskie\0Wysokie\0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", core::lighting_cost.data());
    }
    bool confirm = false;
    if (arena.lighting_quality) confirm = experimental_notice(frame);
    if (arena.vfx_quality) {
        ImGui::SetNextItemWidth(140 * frame.scale);
        ImGui::Combo("Efekty", arena.vfx_quality, "Wyłączone\0Niskie\0Wysokie\0");
    }
    if (arena.clock) {
        if (arena.vfx_quality) ImGui::SameLine();
        clock_stats(*arena.clock);
    }
    const auto room = ImGui::GetContentRegionAvail();
    const float fit = std::min(room.x / sim::World::width,
                               (room.y - 2 * ImGui::GetTextLineHeightWithSpacing()) / sim::World::height);
    const float zoom = fit >= 1 ? std::floor(fit) : std::max(.1F, fit);
    const ImVec2 size{sim::World::width * zoom, sim::World::height * zoom};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (room.x - size.x) * .5F));
    const auto start = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ArenaCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hover = ImGui::IsItemHovered(), canvas_active = ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end{start.x + size.x, start.y + size.y};
    draw->PushClipRect(start, end, true);
    draw->AddImage(texture(arena.world), start, end);
    arena.input.controls.aim = arena.game.player().feet + arena.game.player().aim * 40 + game::Vec2{0, -12};
    if (arena.focused && !ImGui::GetIO().WantTextInput
        && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        auto& input = arena.input.controls;
        input.move = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow))
                   - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow));
        input.jump = ImGui::IsKeyPressed(ImGuiKey_Space, false);
        input.reload = ImGui::IsKeyPressed(ImGuiKey_R, false);
        arena.input.reset = arena.input.reset || ImGui::IsKeyPressed(ImGuiKey_F5, false);
        input.grenade = ImGui::IsKeyPressed(ImGuiKey_G, false);
        const auto mouse = ImGui::GetIO().MousePos;
        // Retain the direction outside the arena; clamp the target to world bounds.
        // Active/hovered controls in this window continue to own mouse input.
        const bool pointer_controls =
                hover || canvas_active || (!ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive());
        if (pointer_controls) {
            input.aim = {std::clamp((mouse.x - start.x) / zoom + arena.camera[0], 0.F, 320.F),
                         std::clamp((mouse.y - start.y) / zoom + arena.camera[1], 0.F, 192.F)};
            input.fire = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            input.jet = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            draw->AddCircle(mouse, 3 * zoom, IM_COL32(226, 237, 170, 240), 16, std::max(1.F, zoom * .5F));
            draw->AddLine({mouse.x - 5 * zoom, mouse.y}, {mouse.x + 5 * zoom, mouse.y}, IM_COL32(226, 237, 170, 160));
            draw->AddLine({mouse.x, mouse.y - 5 * zoom}, {mouse.x, mouse.y + 5 * zoom}, IM_COL32(226, 237, 170, 160));
        } else
            input.aim = arena.game.player().feet + arena.game.player().aim * 40 + game::Vec2{0, -12};
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
