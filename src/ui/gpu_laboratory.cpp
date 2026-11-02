#include "ui/gpu_laboratory.hpp"

#include "core/settings.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <string>
namespace strefa::ui {
namespace {
constexpr std::array scene_names{"Lawina",          "Deszcz ziaren",    "Mieszane materiały", "Pusta scena",
                                 "Zbiornik wody",   "Przerwana zapora", "Naczynia połączone", "Woda i olej",
                                 "Piasek w wodzie", "Klepsydra",        "Olej pod wodą",      "Kropla oleju",
                                 "Misja treningowa"};
constexpr std::array thermal_scene_names{"Brak", "Drewno i dym", "Płonący olej", "Gaszenie wodą", "Przemiany fazowe"};
void tooltip(std::string_view text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%.*s", static_cast<int>(text.size()), text.data());
}
} // namespace
Action draw_gpu_laboratory(GpuLaboratoryFrame& lab, const FrameInfo& info) {
    const auto* viewport = ImGui::GetMainViewport();
    const float s = info.scale;
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("GranularGPU", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    auto action = Action::none;
    auto& controls = lab.controls;
    ImGui::BeginChild("GpuControls", {265 * s, 0}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("LABORATORIUM GPU");
    ImGui::TextDisabled("Świat wspólny z Misją GPU");
    tooltip("Laboratorium GPU i Misja GPU pokazują ten sam świat. Przełączanie nie resetuje sceny.");
    ImGui::Separator();
    if (ImGui::Button("Wróć do menu", {-1, 32 * s})) action = Action::back;
    if (ImGui::Button("Widok gry (Misja GPU)", {-1, 30 * s})) action = Action::open_mission;
    if (lab.mission) {
        ImGui::Text("Misja: %u/3 | amunicja %u", lab.state.inventory[2], lab.state.inventory[0]);
        ImGui::Text("Postać %.1f, %.1f", lab.state.actor[0], lab.state.actor[1]);
        ImGui::Text("Wytrzymałość tarcz: %.0f / %.0f / %.0f", lab.state.targets[0][2], lab.state.targets[1][2],
                    lab.state.targets[2][2]);
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    const auto scene_index =
            static_cast<std::size_t>(std::clamp(controls.scene, 0, static_cast<int>(scene_names.size()) - 1));
    const std::string scene_label = lab.thermal_scene && lab.thermal_scene < thermal_scene_names.size()
                                          ? std::string{"Termika: "} + thermal_scene_names[lab.thermal_scene]
                                          : scene_names[scene_index];
    if (ImGui::BeginCombo("##GpuScene", scene_label.c_str())) {
        for (std::size_t i = 0; i < scene_names.size(); ++i) {
            const bool current = i == scene_index && !lab.thermal_scene;
            if (ImGui::Selectable(scene_names[i], current)) {
                controls.scene = static_cast<int>(i);
                lab.input.select_scene = true;
            }
            if (current) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Reset sceny", {-1, 30 * s})) lab.input.reset = true;
    ImGui::Checkbox("Pauza [Spacja]", &controls.paused);
    if (ImGui::Button("Jeden krok [F]", {-1, 28 * s})) lab.input.step = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) controls.paused = !controls.paused;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) lab.input.step = true;
    if (ImGui::Button("Nowe nagranie (reset sceny)", {-1, 28 * s})) lab.input.begin_recording = true;
    if (ImGui::Button("Zapisz nagranie + obraz", {-1, 28 * s})) lab.input.save_recording = true;
    if (ImGui::Button("Odtwórz ostatnie nagranie", {-1, 28 * s})) lab.input.replay_recording = true;
    if (lab.recording) ImGui::TextColored({.9F, .45F, .2F, 1}, "Nagrywanie aktywne");
    if (!info.message.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(info.message.data(), info.message.data() + info.message.size());
        ImGui::PopTextWrapPos();
    }
    if (experimental_notice(info)) action = Action::confirm_settings;
    ImGui::Separator();
    ImGui::TextUnformatted("OBRAZ WODY");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##RenderQuality", &controls.render_quality, "Podstawowy\0Standardowy\0Wysoki\0");
    ImGui::Separator();
    ImGui::TextUnformatted("ŚWIATŁO (eksperymentalne)");
    tooltip(core::lighting_cost);
    int lightQuality = static_cast<int>(controls.lighting.quality);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##LightingQuality", &lightQuality, "Wyłączone\0Niskie\0Wysokie\0"))
        controls.lighting.quality = static_cast<gpu::LightingQuality>(lightQuality);
    tooltip(core::lighting_cost);
    ImGui::SliderFloat("Bloom", &controls.lighting.bloom, 0.F, 1.F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Winieta", &controls.lighting.vignette, 0.F, 1.F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Wstrząs", &controls.lighting.shake, 0.F, 1.F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Aberracja", &controls.lighting.aberration, 0.F, 1.F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextUnformatted("EFEKTY");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##VfxQuality", &controls.vfx_quality, "Wyłączone\0Niskie\0Wysokie\0");
    ImGui::Separator();
    ImGui::Checkbox("Termika i gazy (eksperymentalne)", &controls.thermal_enabled);
    tooltip(std::string{core::thermal_cost} + "\nWłączenie działa tylko w tej sesji i nie trafia do ustawień.");
    if (controls.thermal_enabled) {
        ImGui::Checkbox("Promieniowanie do otoczenia", &controls.thermal_radiation);
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ThermalView", &controls.thermal_view,
                     "Materiały / ogień\0Temperatura\0Ciśnienie projekcji\0Prędkość gazu\0Tlen\0Paliwo\0");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ThermalTool", &controls.thermal_tool,
                     "Zwykły pędzel\0Grzanie\0Chłodzenie\0Drewno\0Lód\0Lawa\0Gaz palny\0Dym\0Para\0");
        if (controls.thermal_tool >= 6)
            ImGui::SliderFloat("Masa / s / kom.", &controls.gas_rate, gas_rate_min, gas_rate_max, "%.3f",
                               ImGuiSliderFlags_AlwaysClamp);
        else if (controls.thermal_tool == 1 || controls.thermal_tool == 2)
            ImGui::SliderFloat("kJ / s / kom.", &controls.thermal_rate, heat_rate_min, heat_rate_max, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextWrapped("Źródła ciepła i gazu działają tylko w wykonanych krokach symulacji. Podgląd pod kursorem "
                           "pokazuje ostatnią gotową próbkę.");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ThermalScene", &controls.thermal_scene, thermal_scene_names.data(),
                     static_cast<int>(thermal_scene_names.size()));
        if (controls.thermal_scene && ImGui::Button("Wczytaj scenę termiki", {-1, 28 * s}))
            lab.input.thermal_reset = true;
        if (lab.thermal) {
            const auto& t = *lab.thermal;
            ImGui::Text("Masa %.3f | entalpia %.1f kJ", t.mass, t.enthalpy);
            ImGui::Text("Tlen %.4f | para %.4f", t.oxygen, t.steam);
            ImGui::Text("Dym %.4f | paliwo %.4f", t.smoke, t.fuel);
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("MATERIAŁ");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##GpuMaterial", &controls.material, "Gumka\0Piasek\0Popiół\0Metalowe drobiny\0Ściana\0Woda\0Olej\0");
    ImGui::SliderInt("Pędzel", &controls.radius, gpu_brush_min, gpu_brush_max, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped("LPM: rysuj. PPM: usuń. Kółko: rozmiar pędzla.");
    ImGui::Separator();
    ImGui::Text("Cząstki: %u / %d", lab.metrics.grains, gpu::capacity);
    ImGui::Text("Woda: %u / Olej: %u", lab.metrics.water, lab.metrics.oil);
    ImGui::Text("Okno: %.1f FPS", ImGui::GetIO().Framerate);
    if (lab.clock) clock_stats(*lab.clock);
    if (ImGui::CollapsingHeader("Diagnostyka")) {
        ImGui::Text("Krok GPU: %.3f ms (fizyka %.3f, prezentacja %.3f) | render %.3f ms", lab.metrics.gpu_ms,
                    lab.metrics.physics_ms, lab.metrics.presentation_ms, lab.metrics.render_ms);
        ImGui::Text("Kroki: %llu (1/120 s)", static_cast<unsigned long long>(lab.metrics.steps));
        ImGui::Text("Ściśnięcie cieczy maks.: %.1f%%", lab.metrics.max_compression * 100);
        tooltip("Błąd gęstości cieczy (PBF), próbka co 30 kroków. Nie jest ciśnieniem w Pa.");
        if (lab.mission) {
            ImGui::Text("HUD: generacja %u / krok %u", lab.state.status[1], lab.state.status[2]);
            tooltip("Numer ostatniej odczytanej próbki stanu misji z GPU i krok, z którego pochodzi.");
        }
        if (controls.thermal_enabled && lab.thermal) {
            const auto& t = *lab.thermal;
            ImGui::Text("Div RMS %.4f", t.divergence_l2);
            tooltip("Średnia kwadratowa dywergencji pola gazu po projekcji ciśnienia: im mniej, tym dokładniej.");
            ImGui::Text("Termika: krok %llu | %.1f MiB", static_cast<unsigned long long>(t.tick),
                        static_cast<double>(lab.thermal_memory) / (1024 * 1024));
        }
        if (lab.vfx) {
            const auto& v = *lab.vfx;
            ImGui::Text("Efekty: %u / 16384 | wolne %u", v.counts[0], v.counts[1]);
            ImGui::Text("Rysowane: %u | ograniczone emitery %u", v.layers[0] + v.layers[1] + v.layers[2], v.clock[2]);
            ImGui::Text("Szczyt: %u | odrzucone %llu", v.counts[3],
                        static_cast<unsigned long long>(
                                std::accumulate(v.rejected.begin(), v.rejected.end(), std::uint64_t{})));
            ImGui::Text("Pamięć efektów: %.2f MiB", static_cast<double>(lab.vfx_memory) / (1024 * 1024));
        }
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Woda i olej: cząstki cieczy na GPU (PBF). Termika: transport ciepła i gazów z wybranymi "
                       "reakcjami. Laboratorium CPU jest modelem referencyjnym.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("GpuCanvas", {0, 0});
    const auto room = ImGui::GetContentRegionAvail();
    const float zoom = std::max(.1F, std::min(room.x / gpu::width, (room.y - 28 * s) / gpu::height));
    const ImVec2 size{gpu::width * zoom, gpu::height * zoom};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (room.x - size.x) * .5F));
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef{static_cast<ImTextureID>(lab.image.handle)}, size);
    bool painting = false;
    if (ImGui::IsItemHovered()) {
        const auto mouse = ImGui::GetIO().MousePos;
        const float x = std::clamp((mouse.x - origin.x) / zoom + lab.camera[0], 0.F, gpu::width - 1.F),
                    y = std::clamp((mouse.y - origin.y) / zoom + lab.camera[1], 0.F, gpu::height - 1.F);
        if (controls.thermal_enabled) {
            lab.input.inspect = std::array{static_cast<int>(x), static_cast<int>(y)};
            if (lab.inspection && lab.inspection->valid) {
                const auto& sample = *lab.inspection;
                const auto& c = sample.value;
                const double cp = c.gas0[0] + c.gas0[1] + 2 * c.gas0[2] + c.gas0[3] + 1.5 * c.gas1[0] + c.gas1[1];
                const double gasT =
                        cp > 0 ? (c.gas1[2] - c.gas0[2] * sim::enthalpy_offset(sim::Material::steam)) / cp : 293.15;
                ImGui::BeginTooltip();
                ImGui::Text("Próbka (%d,%d), krok %u", sample.cell[0], sample.cell[1], sample.tick);
                ImGui::Text("Gaz %.1f K | cząstki %.1f K", gasT, static_cast<double>(c.extra[2]));
                ImGui::Text("O2 %.5f | paliwo %.5f", c.gas0[1], c.gas1[0] + c.matter[2]);
                ImGui::Text("Gaz u/v %.2f / %.2f", c.flow[0], c.flow[1]);
                ImGui::EndTooltip();
            }
        }
        if (ImGui::GetIO().MouseWheel != 0)
            controls.radius = std::clamp(controls.radius + (ImGui::GetIO().MouseWheel > 0 ? 1 : -1), gpu_brush_min,
                                         gpu_brush_max);
        ImGui::GetWindowDrawList()->AddCircle(
                {origin.x + (x - lab.camera[0]) * zoom, origin.y + (y - lab.camera[1]) * zoom}, controls.radius * zoom,
                IM_COL32(225, 228, 168, 220));
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            const auto previous = controls.previous.value_or(std::array{x, y});
            lab.input.stroke = gpu::Stroke{previous[0],
                                           previous[1],
                                           x,
                                           y,
                                           static_cast<float>(controls.radius),
                                           ImGui::IsMouseDown(ImGuiMouseButton_Right)
                                                   ? gpu::Material::erase
                                                   : static_cast<gpu::Material>(controls.material)};
            if (controls.thermal_enabled && controls.thermal_tool > 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                lab.input.thermal_brush = make_thermal_brush(controls, {previous[0], previous[1], x, y});
                const auto tool = lab.input.thermal_brush.tool;
                if (tool == gpu::ThermalTool::wood)
                    lab.input.stroke->material = gpu::Material::wall;
                else if (tool == gpu::ThermalTool::ice)
                    lab.input.stroke->material = gpu::Material::metal;
                else if (tool == gpu::ThermalTool::lava)
                    lab.input.stroke->material = gpu::Material::oil;
                else
                    lab.input.stroke.reset();
            }
            controls.previous = std::array{x, y};
            painting = true;
        }
    }
    if (!painting) controls.previous.reset();
    ImGui::TextDisabled("Ciągłe pozycje / powierzchnia cieczy / interpolacja ruchu");
    ImGui::EndChild();
    ImGui::End();
    return action;
}
} // namespace strefa::ui
