#include "ui/gpu_laboratory.hpp"

#include <imgui.h>

#include <algorithm>
#include <numeric>
namespace strefa::ui {
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
    ImGui::TextDisabled("Wspólna scena / I05");
    ImGui::Separator();
    if (ImGui::Button("Wróć do menu", {-1, 32 * s})) action = Action::back;
    if (ImGui::Button("Widok gry", {-1, 30 * s})) action = Action::open_mission;
    if (lab.mission) {
        ImGui::Text("Misja: %u/3 | amunicja %u", lab.state.inventory[2], lab.state.inventory[0]);
        ImGui::Text("Postać %.1f, %.1f", lab.state.actor[0], lab.state.actor[1]);
        ImGui::Text("HP tarcz: %.0f / %.0f / %.0f", lab.state.targets[0][2], lab.state.targets[1][2],
                    lab.state.targets[2][2]);
        ImGui::TextDisabled("HUD: generacja %u / krok %u", lab.state.status[1], lab.state.status[2]);
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo(
                "##GpuScene", &controls.scene,
                "Lawina\0Deszcz ziaren\0Mieszane materiały\0Pusta scena\0Zbiornik wody\0Przerwana zapora\0Naczynia połączone\0Woda i olej\0Piasek w wodzie\0Klepsydra\0Olej pod wodą\0Kropla oleju\0Misja treningowa\0"))
        lab.input.reset = true;
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
    ImGui::Separator();
    ImGui::TextUnformatted("OBRAZ WODY");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##RenderQuality", &controls.render_quality, "Podstawowy\0Standardowy\0Wysoki\0");
    ImGui::Separator();
    ImGui::TextUnformatted("ŚWIATŁO / I11");
    int lightQuality = static_cast<int>(controls.lighting.quality);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##LightingQuality", &lightQuality, "Wyłączone\0Niskie\0Wysokie\0"))
        controls.lighting.quality = static_cast<gpu::LightingQuality>(lightQuality);
    ImGui::SliderFloat("Bloom", &controls.lighting.bloom, 0.F, 1.F);
    ImGui::SliderFloat("Winieta", &controls.lighting.vignette, 0.F, 1.F);
    ImGui::SliderFloat("Wstrząs", &controls.lighting.shake, 0.F, 1.F);
    ImGui::SliderFloat("Aberracja", &controls.lighting.aberration, 0.F, 1.F);
    ImGui::TextUnformatted("EFEKTY VFX");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##VfxQuality", &controls.vfx_quality, "Wyłączone\0Niskie\0Wysokie\0");
    if (lab.vfx) {
        const auto& v = *lab.vfx;
        ImGui::Text("VFX: %u / 16384 | wolne %u", v.counts[0], v.counts[1]);
        ImGui::Text("Rysowane: %u | ograniczone emitery %u", v.layers[0] + v.layers[1] + v.layers[2], v.clock[2]);
        ImGui::Text("Szczyt: %u | odrzucone %llu", v.counts[3],
                    static_cast<unsigned long long>(
                            std::accumulate(v.rejected.begin(), v.rejected.end(), std::uint64_t{})));
        ImGui::Text("Pamięć VFX: %.2f MiB", static_cast<double>(lab.vfx_memory) / (1024 * 1024));
    }
    ImGui::Separator();
    ImGui::Checkbox("Termika i gazy I10", &controls.thermal_enabled);
    if (controls.thermal_enabled) {
        ImGui::Checkbox("Promieniowanie do otoczenia", &controls.thermal_radiation);
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ThermalView", &controls.thermal_view,
                     "Materiały / ogień\0Temperatura\0Ciśnienie projekcji\0Prędkość gazu\0Tlen\0Paliwo\0");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ThermalTool", &controls.thermal_tool,
                     "Zwykły pędzel\0Grzanie\0Chłodzenie\0Drewno\0Lód\0Lawa\0Gaz palny\0Dym\0Para\0");
        if (controls.thermal_tool >= 6)
            ImGui::SliderFloat("Masa / s / kom.", &controls.gas_rate, .001F, 1.F, "%.3f");
        else if (controls.thermal_tool == 1 || controls.thermal_tool == 2)
            ImGui::SliderFloat("kJ / s / kom.", &controls.thermal_rate, .01F, 2000.F, "%.2f");
        ImGui::TextWrapped("Źródła I10 działają w wykonanych krokach. Inspektor pokazuje ostatnią gotową próbkę GPU.");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ThermalScene", &controls.thermal_scene,
                     "Brak\0Drewno i dym\0Płonący olej\0Gaszenie wodą\0Przemiany fazowe\0");
        if (controls.thermal_scene && ImGui::Button("Wczytaj scenę I10", {-1, 28 * s})) lab.input.thermal_reset = true;
        if (lab.thermal) {
            const auto& t = *lab.thermal;
            ImGui::Text("Masa %.3f | H %.1f kJ", t.mass, t.enthalpy);
            ImGui::Text("Tlen %.4f | para %.4f", t.oxygen, t.steam);
            ImGui::Text("Dym %.4f | paliwo %.4f", t.smoke, t.fuel);
            ImGui::Text("Div RMS %.4f", t.divergence_l2);
            ImGui::Text("Stan: krok %llu | %.1f MiB", static_cast<unsigned long long>(t.tick),
                        static_cast<double>(lab.thermal_memory) / (1024 * 1024));
        }
    }
    ImGui::Text("Render GPU: %.3f ms", lab.metrics.render_ms);
    ImGui::Separator();
    ImGui::TextUnformatted("MATERIAŁ");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##GpuMaterial", &controls.material, "Gumka\0Piasek\0Popiół\0Metalowe drobiny\0Ściana\0Woda\0Olej\0");
    ImGui::SliderInt("Pędzel", &controls.radius, 1, 16);
    ImGui::TextWrapped("LPM: rysuj. PPM: usuń. Kółko: rozmiar pędzla.");
    ImGui::Separator();
    ImGui::Text("Cząstki: %u / %d", lab.metrics.grains, gpu::capacity);
    ImGui::Text("Woda: %u / Olej: %u", lab.metrics.water, lab.metrics.oil);
    ImGui::Text("Ściśnięcie maks.: %.1f%%", lab.metrics.max_compression * 100);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Błąd gęstości PBF, próbka co 30 kroków. Nie jest ciśnieniem w Pa.");
    ImGui::Text("Krok GPU: %.3f ms", lab.metrics.gpu_ms);
    ImGui::Text("Czas kroku: 1/120 s / %llu", static_cast<unsigned long long>(lab.metrics.steps));
    ImGui::Text("Okno: %.1f FPS", ImGui::GetIO().Framerate);
    if (controls.lost_seconds > .001) ImGui::Text("Pominięty czas: %.2f s", controls.lost_seconds);
    ImGui::Spacing();
    ImGui::TextWrapped(
            "Woda i olej: solver PBF na GPU. I10: zachowawczy transport gazów, ciepło i wybrane reakcje. Model CPU jest narzędziem referencyjnym.");
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
            controls.radius = std::clamp(controls.radius + (ImGui::GetIO().MouseWheel > 0 ? 1 : -1), 1, 16);
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
                const auto tool = static_cast<gpu::ThermalTool>(controls.thermal_tool);
                lab.input.thermal_brush = {{previous[0], previous[1], x, y},
                                           static_cast<float>(controls.radius),
                                           controls.thermal_tool >= 6 ? controls.gas_rate : controls.thermal_rate,
                                           tool};
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
