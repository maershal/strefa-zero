#include "ui/arena.hpp"
#include "game/animation.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace strefa::ui {
namespace {
ImTextureRef texture(const Image& image) { return ImTextureRef{static_cast<ImTextureID>(image.handle)}; }

void actors(ArenaFrame& arena, ImVec2 start, float zoom) {
    auto* draw = ImGui::GetWindowDrawList();
    auto point = [=](game::Vec2 p) { return ImVec2{start.x + p.x * zoom, start.y + p.y * zoom}; };
    for (const auto& target : arena.game.targets()) {
        const auto feet = point(target.feet);
        if (target.hits_remaining == 0) {
            draw->AddLine({feet.x - 3 * zoom, feet.y}, {feet.x + 3 * zoom, feet.y}, IM_COL32(105, 143, 81, 200), zoom);
            continue;
        }
        draw->AddLine({feet.x, feet.y}, {feet.x, feet.y - 16 * zoom}, IM_COL32(150, 136, 102, 255), zoom);
        draw->AddRectFilled({feet.x - 4 * zoom, feet.y - 16 * zoom}, {feet.x + 4 * zoom, feet.y - 3 * zoom},
            target.flash > 0 ? IM_COL32(255, 244, 174, 255) : IM_COL32(188, 192, 160, 255));
        draw->AddCircle({feet.x, feet.y - 10 * zoom}, 2.5F * zoom, IM_COL32(158, 49, 37, 255), 12, zoom);
        for (int i = 0; i < target.hits_remaining; ++i)
            draw->AddRectFilled({feet.x - 4 * zoom + i * 3 * zoom, feet.y - 19 * zoom},
                {feet.x - 2 * zoom + i * 3 * zoom, feet.y - 18 * zoom}, IM_COL32(196, 215, 114, 255));
    }
    const auto& player = arena.game.player();
    const auto rig=game::pose(player);
    struct Crop { float x,y,w,h; };
    constexpr std::array<Crop,16> crops{{{0,0,32,48},{11,16,10,15},{13,20,6,8},{12,20,7,8},
        {13,20,6,8},{10,21,11,6},{13,20,5,8},{10,21,11,6},
        {13,19,6,10},{13,18,6,12},{13,18,6,11},{12,18,7,12},
        {9,16,14,16},{12,20,8,8},{11,18,10,12},{13,21,5,6}}};
    auto sprite=[&](int index,game::Vec2 center,float width,float height,game::Vec2 down,ImU32 tint=IM_COL32_WHITE) {
        const auto c=crops[index];
        const game::Vec2 right{down.y,-down.x};
        const auto a=point(center-right*(width*.5F)-down*(height*.5F));
        const auto b=point(center+right*(width*.5F)-down*(height*.5F));
        const auto e=point(center+right*(width*.5F)+down*(height*.5F));
        const auto d=point(center-right*(width*.5F)+down*(height*.5F));
        float x0=(index%4*32+c.x)/128, x1=x0+c.w/128;
        const float y0=(index/4*48+c.y)/288,y1=y0+c.h/288;
        if(rig.facing<0) std::swap(x0,x1);
        if(index==5||index==7)
            draw->AddImageQuad(texture(arena.parts),a,b,e,d,{x0,y1},{x0,y0},{x1,y0},{x1,y1},tint);
        else draw->AddImageQuad(texture(arena.parts),a,b,e,d,{x0,y0},{x1,y0},{x1,y1},{x0,y1},tint);
    };
    auto segment=[&](int part,game::Vec2 a,game::Vec2 b,float width,ImU32 tint) {
        const auto delta=b-a; const float distance=std::max(.001F,std::hypot(delta.x,delta.y));
        sprite(part,(a+b)*.5F,width,distance+1,delta*(1/distance),tint);
    };
    auto limb=[&](const game::Limb& l,int upper,int lower,float width,ImU32 tint) {
        segment(upper,l.root,l.joint,width,tint); segment(lower,l.joint,l.end,width+.4F,tint);
    };
    constexpr ImU32 far_tint=IM_COL32(170,180,166,255);
    limb(rig.far_leg,10,11,2.3F,far_tint);
    limb(rig.far_arm,6,7,1.9F,far_tint);
    sprite(14,rig.shoulder+game::Vec2{-rig.facing*2.5F,1.5F},3.7F,6,{0,1});
    sprite(2,rig.hip,4,3,{0,1});
    limb(rig.near_leg,8,9,2.5F,IM_COL32_WHITE);
    sprite(1,(rig.hip+rig.shoulder)*.5F,5,7,{0,1});
    sprite(12,(rig.hip+rig.shoulder)*.5F,6,7.5F,{0,1});
    sprite(3,rig.head,3.5F,4,{0,1});
    sprite(13,rig.head+game::Vec2{0,-1.2F},4.5F,3,{0,1});
    const game::Vec2 down{-player.aim.y*rig.facing,player.aim.x*rig.facing};
    const auto a=point(rig.gun-player.aim*6.5F-down*2),b=point(rig.gun+player.aim*6.5F-down*2);
    const auto c=point(rig.gun+player.aim*6.5F+down*2),d=point(rig.gun-player.aim*6.5F+down*2);
    const float aw=static_cast<float>(arena.weapons.width),ah=static_cast<float>(arena.weapons.height);
    draw->AddImageQuad(texture(arena.weapons),a,b,c,d,{0,10/ah},{32/aw,10/ah},{32/aw,23/ah},{0,23/ah});
    limb(rig.near_arm,4,5,2,IM_COL32_WHITE);
    if (player.jetting) {
        const auto nozzle = point(player.feet + game::Vec2{-player.aim.x * 3, -8});
        const float flame = (4 + static_cast<int>(arena.game.ticks() % 4)) * zoom;
        draw->AddTriangleFilled({nozzle.x - zoom, nozzle.y}, {nozzle.x + zoom, nozzle.y},
            {nozzle.x, nozzle.y + flame}, IM_COL32(255, 173, 54, 255));
        draw->AddLine(nozzle, {nozzle.x, nozzle.y + flame * .6F}, IM_COL32(255, 245, 179, 255), zoom);
    }
    for (const auto& bullet : arena.game.bullets())
        draw->AddLine(point(bullet.previous), point(bullet.position), IM_COL32(255, 224, 124, 255), std::max(1.F, zoom * .55F));
    for (const auto& grenade : arena.game.grenades()) {
        const auto p = point(grenade.position);
        draw->AddRectFilled({p.x - zoom, p.y - zoom}, {p.x + zoom, p.y + zoom}, IM_COL32(155, 175, 77, 255));
    }
    for (const auto& visual : arena.game.effects()) {
        const auto p = point(visual.position);
        if (visual.kind == game::EffectKind::muzzle) {
            draw->AddCircleFilled(p, 1.6F * zoom, IM_COL32(255, 234, 151, 255), 8); continue;
        }
        const bool blast = visual.kind == game::EffectKind::blast;
        const float age = 1 - visual.remaining / (blast ? .45F : .18F);
        const int alpha = static_cast<int>(std::clamp(1 - age, 0.F, 1.F) * 255);
        if (blast) draw->AddCircle(p, (1 + age * 10) * zoom, IM_COL32(255, 191, 79, alpha), 24, zoom);
        // Short-lived visual debris is separate from persistent cellular materials.
        for (int i = 0; i < (blast ? 16 : 5); ++i) {
            const float angle = i * 2.399963F;
            const float distance = age * (blast ? 14.F : 4.F);
            const ImVec2 spark{p.x + std::cos(angle) * distance * zoom,
                p.y + (std::sin(angle) * distance + age * age * 5) * zoom};
            draw->AddRectFilled(spark, {spark.x + zoom, spark.y + zoom}, IM_COL32(255, 202, 105, alpha));
        }
    }
}
}

Action draw_arena(ArenaFrame& arena, const FrameInfo& frame, const Fonts& fonts) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("TrainingArena", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    // Space belongs to jumping in this screen, not to activating a focused menu button.
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool back = ImGui::Button("Menu [Esc]");
    ImGui::SameLine();
    arena.input.reset = ImGui::Button("Od nowa [F5]");
    ImGui::SameLine();
    ImGui::Text("Tarcze %d/3  |  %d/30  |  Granaty %d", arena.game.score(), arena.game.ammunition(), arena.game.grenade_stock());
    if (viewport->WorkSize.x / frame.scale >= 700) ImGui::SameLine();
    const float fuel = arena.game.player().fuel / arena.game.fuel_capacity();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4{.72F, .82F, .40F, 1});
    ImGui::ProgressBar(fuel, {90 * frame.scale, 0}, "Paliwo");
    ImGui::PopStyleColor();
    if (arena.game.reload_remaining() > 0) ImGui::Text("Przeładowanie %.1f s", static_cast<double>(arena.game.reload_remaining()));
    else ImGui::TextDisabled("%s",arena.game.gpu?"MISJA: 3 tarcze + zielone wyjście. Pomarańczowe przegrody są zniszczalne.":"POLIGON CPU / żołnierz niewrażliwy na obrażenia");
    if (arena.game.finished) {
        ImGui::PushFont(fonts.heading, 22);
        ImGui::TextWrapped("POLIGON UKOŃCZONY  /  F5 — kolejna próba");
        ImGui::PopFont();
    }
    const auto room = ImGui::GetContentRegionAvail();
    const float fit = std::min(room.x / sim::World::width,
        (room.y - 2 * ImGui::GetTextLineHeightWithSpacing()) / sim::World::height);
    const float zoom = fit >= 1 ? std::floor(fit) : std::max(.1F, fit);
    const ImVec2 size{sim::World::width * zoom, sim::World::height * zoom};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (room.x - size.x) * .5F));
    const auto start = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ArenaCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hover = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end{start.x + size.x, start.y + size.y};
    draw->PushClipRect(start, end, true);
    draw->AddImage(texture(arena.background), start, end);
    draw->AddRectFilled(start, end, IM_COL32(7, 14, 17, 165));
    draw->AddImage(texture(arena.world), start, end);
    if(!arena.game.gpu)actors(arena, start, zoom);
    arena.input.controls.aim = arena.game.player().feet + arena.game.player().aim * 40 + game::Vec2{0, -12};
    if (arena.focused && !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        auto& input = arena.input.controls;
        input.move = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow))
            - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow));
        input.jump = ImGui::IsKeyPressed(ImGuiKey_Space, false);
        input.reload = ImGui::IsKeyPressed(ImGuiKey_R, false);
        arena.input.reset = arena.input.reset || ImGui::IsKeyPressed(ImGuiKey_F5, false);
        if (hover) {
            const auto mouse = ImGui::GetIO().MousePos;
            input.aim = {(mouse.x - start.x) / zoom, (mouse.y - start.y) / zoom};
            input.fire = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            input.jet = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            input.grenade = ImGui::IsKeyPressed(ImGuiKey_G, false);
            draw->AddCircle(mouse, 3 * zoom, IM_COL32(226, 237, 170, 240), 16, std::max(1.F, zoom * .5F));
            draw->AddLine({mouse.x - 5 * zoom, mouse.y}, {mouse.x + 5 * zoom, mouse.y}, IM_COL32(226, 237, 170, 160));
            draw->AddLine({mouse.x, mouse.y - 5 * zoom}, {mouse.x, mouse.y + 5 * zoom}, IM_COL32(226, 237, 170, 160));
        } else input.aim = arena.game.player().feet + arena.game.player().aim * 40 + game::Vec2{0, -12};
    }
    draw->PopClipRect();
    draw->AddRect(start, end, IM_COL32(141, 158, 117, 160));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("A/D: ruch   Spacja: skok   PPM: plecak   LPM: strzał   R: przeładuj   G: granat");
    ImGui::PopStyleColor();
    ImGui::PopItemFlag();
    ImGui::End();
    return back || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ? Action::back : Action::none;
}

} // namespace strefa::ui

