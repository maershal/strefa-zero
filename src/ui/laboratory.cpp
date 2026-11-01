#include "ui/laboratory.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace strefa::ui {
namespace {
void canvas(LaboratoryFrame& lab, float scale) {
    auto& controls = lab.controls;
    const auto room = ImGui::GetContentRegionAvail();
    const float fit = std::min(room.x / sim::World::width, (room.y - 38.0F * scale) / sim::World::height);
    const float zoom = fit >= 1.0F ? std::floor(fit) : std::max(.1F, fit);
    const ImVec2 size{sim::World::width * zoom, sim::World::height * zoom};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (room.x - size.x) * .5F));
    const auto start = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef{static_cast<ImTextureID>(lab.image.handle)}, size);
    const bool hovered = ImGui::IsItemHovered();
    const auto end = ImVec2{start.x + size.x, start.y + size.y};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRect(start, end, IM_COL32(132, 155, 104, 180));
    draw->PushClipRect(start, end, true);
    if (controls.show_chunks) {
        for (int cy = 0; cy < sim::World::rows; ++cy) for (int cx = 0; cx < sim::World::columns; ++cx) {
            const ImVec2 a{start.x + cx * sim::World::chunk_size * zoom, start.y + cy * sim::World::chunk_size * zoom};
            const ImVec2 b{a.x + sim::World::chunk_size * zoom, a.y + sim::World::chunk_size * zoom};
            if (lab.world.chunk_awake(cy * sim::World::columns + cx))
                draw->AddRectFilled(a, b, IM_COL32(171, 214, 88, 18));
            draw->AddRect(a, b, IM_COL32(151, 174, 126, 45));
        }
    }
    bool drew = false;
    if (hovered) {
        const auto mouse = ImGui::GetIO().MousePos;
        const sim::Point point{std::clamp(static_cast<int>((mouse.x - start.x) / zoom), 0, sim::World::width - 1),
            std::clamp(static_cast<int>((mouse.y - start.y) / zoom), 0, sim::World::height - 1)};
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) controls.radius = std::clamp(controls.radius + (wheel > 0 ? 1 : -1), 1, 16);
        const bool erase = ImGui::IsMouseDown(ImGuiMouseButton_Right) || controls.material == sim::Material::empty;
        draw->AddCircle({start.x + (point.x + .5F) * zoom, start.y + (point.y + .5F) * zoom},
            (controls.radius + .5F) * zoom, erase ? IM_COL32(255, 165, 130, 220) : IM_COL32(232, 241, 203, 220), 32);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            const auto from = controls.previous && controls.previous_material == controls.material
                && controls.previous_erase == erase ? *controls.previous : point;
            if (controls.tool != 0 && !erase) lab.input.heat = point;
            else lab.input.stroke = Stroke{from, point, controls.radius, controls.material, erase};
            controls.previous = point; controls.previous_material = controls.material;
            controls.previous_erase = erase; drew = true;
        }
        lab.input.probe=point;
        const auto inspected = lab.world.sample(point);
        if(inspected) {
        const auto& sample=*inspected;
        ImGui::SetTooltip("%s [%d, %d]\nT: %.2f °C  /  gaz: %.2f °C\np gazu: %.3f  /  cieczy: %.2f\nMasa: %.4f  /  ciecz: %.3f\nu gazu: (%.2f, %.2f)\nu cieczy: (%.2f, %.2f)\nTlen: %.5f  /  paliwo: %.4f\nProdukty rozpuszczone: %.4f",
            sim::definition(sample.material).name.data(), point.x, point.y,
            sample.temperature - 273.15, sample.air_temperature - 273.15, sample.pressure, sample.liquid_pressure,
            sample.mass, sample.liquid_fill, sample.air_velocity.x, sample.air_velocity.y,
            sample.liquid_velocity.x, sample.liquid_velocity.y, sample.oxygen, sample.fuel, sample.dissolved);
        } else ImGui::SetTooltip("Odczyt pól [%d, %d]…",point.x,point.y);
    }
    if (!drew) controls.previous.reset();
    draw->PopClipRect();
    ImGui::Spacing();
    ImGui::TextDisabled("LPM: rysuj   PPM: usuń   Kółko: pędzel   Spacja: pauza   F: krok");
}

void palette(LaboratoryFrame& lab, float scale) {
    auto& controls = lab.controls;
    ImGui::TextUnformatted("SCENA I POLA");
    if (ImGui::BeginCombo("##scene", sim::scene_names[static_cast<unsigned>(controls.scene)])) {
        for (unsigned i=0; i < static_cast<unsigned>(sim::Scene::count); ++i)
            if (ImGui::Selectable(sim::scene_names[i], static_cast<unsigned>(controls.scene)==i)) {
                controls.scene=static_cast<sim::Scene>(i); lab.input.reset=true;
            }
        ImGui::EndCombo();
    }
    constexpr const char* views[]{"Materiały", "Temperatura", "Ciśnienie", "Prędkość", "Tlen", "Aktywność: ciepło/ruch/reakcje"};
    int view=static_cast<int>(controls.view);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##view", &view, views, 6)) controls.view=static_cast<sim::RenderMode>(view);
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##tool", &controls.tool, "Materiał\0Ogrzewanie +100\0Chłodzenie -100\0");
    bool open=lab.world.config().open_top;
    if (ImGui::Checkbox("Otwarta góra (gazy)", &open)) lab.input.boundary_toggle=true;
    if (ImGui::Button(controls.recording ? "Zatrzymaj zapis" : "Nagrywaj co 3 kroki")) lab.input.record_toggle=true;
    ImGui::TextDisabled("Nagrania: profil/recordings");
    const auto& physics=lab.world.statistics();
    if(!lab.world.ready) ImGui::TextDisabled("Ładowanie sceny…");
    ImGui::Text("Symulacja: %.1f / %d kroków/s",lab.world.steps_per_second,60*controls.speed);
    ImGui::TextDisabled("Okno działa niezależnie od fizyki");
    ImGui::Text("Krok %.2f ms | PCG %d", lab.step_ms, physics.pressure_iterations);
    if(ImGui::TreeNode("Koszt obliczeń")) {
        ImGui::Text("Wątki fizyki: %u",physics.worker_threads);
        ImGui::Text("Ciecze %.2f ms (ciśnienie %.2f)",physics.liquid_ms,physics.pressure_ms);
        ImGui::Text("Gazy %.2f ms (transport %.2f)",physics.air_ms,physics.air_transport_ms);
        ImGui::Text("Ciepło %.2f ms | reakcje %.2f",physics.heat_ms,physics.reaction_ms);
        ImGui::Text("Podkroki: ciecz %d | gaz %d",physics.liquid_substeps,physics.air_substeps);
        ImGui::TreePop();
    }
    ImGui::Text("Błąd PCG %.2e",physics.pressure_residual);
    if (!physics.pressure_converged) ImGui::TextColored({1,.5F,.3F,1},"PCG: brak zbieżności");
    if (!physics.finite) ImGui::TextColored({1,.2F,.2F,1},"BŁĄD: niefinitywny stan");
    ImGui::Text("Bilans m: %.2e  H: %.2e",physics.mass_error,physics.energy_error);
    ImGui::Text("Ujście m: %.3f  H: %.1f",physics.escaped_mass,physics.escaped_energy);
    ImGui::Separator();
    ImGui::TextUnformatted("MATERIAŁY");
    ImGui::Separator();
    for (std::size_t i = 0; i < sim::materials.size(); ++i) {
        const auto material = static_cast<sim::Material>(i);
        const auto& def = sim::materials[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::ColorButton("##swatch", {def.color.r / 255.F, def.color.g / 255.F, def.color.b / 255.F, 1},
            ImGuiColorEditFlags_NoTooltip, {16.F * scale, 16.F * scale});
        ImGui::SameLine();
        if (ImGui::Selectable(def.name.data(), controls.material == material)) controls.material = material;
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", sim::definition(controls.material).description.data());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##brush", &controls.radius, 1, 16, "Pędzel: %d px");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##speed", &controls.speed, 1, 4, "Tempo: %dx");
    ImGui::Checkbox("Siatka aktywnych obszarów", &controls.show_chunks);
    if (ImGui::CollapsingHeader("Statystyki")) {
        const auto& stats = lab.world.statistics();
        ImGui::Text("Cząstki: %zu / %d", stats.particles, sim::World::cell_count);
        ImGui::Text("Obszary: %d / %d", stats.active_chunks, sim::World::chunk_count);
        ImGui::Text("Krok: %llu  /  %.3f ms", static_cast<unsigned long long>(stats.tick), lab.step_ms);
        ImGui::Text("Ruchy: %d", stats.moves);
        ImGui::Text("Pominięty czas: %.2f s", lab.dropped_seconds);
        ImGui::TextDisabled("CPU  /  60 kroków/s  /  320 × 192");
    }
}
}

Action draw_laboratory(LaboratoryFrame& lab, const FrameInfo& frame, const Fonts& fonts) {
    const auto* viewport = ImGui::GetMainViewport();
    const float s = frame.scale;
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("Laboratory", nullptr, flags);
    ImGui::PushFont(fonts.heading, 26);
    ImGui::TextUnformatted("LABORATORIUM MATERIAŁÓW");
    ImGui::PopFont();
    if(!frame.message.empty()) ImGui::TextColored({1,.7F,.35F,1},"%s",frame.message.data());
    const bool back = ImGui::Button("Menu [Esc]");
    ImGui::SameLine();
    if (!ImGui::GetIO().WantTextInput) ImGui::SetNextItemShortcut(ImGuiKey_Space);
    if (ImGui::Button(lab.controls.paused ? "Wznów [Spacja]" : "Pauza [Spacja]"))
        lab.controls.paused = !lab.controls.paused;
    ImGui::SameLine();
    ImGui::BeginDisabled(!lab.controls.paused);
    if (!ImGui::GetIO().WantTextInput) ImGui::SetNextItemShortcut(ImGuiKey_F);
    lab.input.single_step = ImGui::Button("Krok [F]");
    ImGui::EndDisabled();
    ImGui::SameLine();
    lab.input.reset = ImGui::Button("Uruchom scenę");
    ImGui::SameLine();
    lab.input.clear = ImGui::Button("Wyczyść");
    ImGui::Separator();
    const float side = std::min(240.0F * s, ImGui::GetContentRegionAvail().x * .3F);
    ImGui::BeginChild("Canvas", {-(side + ImGui::GetStyle().ItemSpacing.x), 0}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    canvas(lab, s);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Materials", {0, 0});
    palette(lab, s);
    ImGui::EndChild();
    ImGui::End();
    return back ? Action::back : Action::none;
}

} // namespace strefa::ui
