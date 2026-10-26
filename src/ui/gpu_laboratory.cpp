#include "ui/gpu_laboratory.hpp"
#include <imgui.h>
#include <algorithm>
namespace strefa::ui {
Action draw_gpu_laboratory(GpuLaboratoryFrame& lab,const FrameInfo& info){
    const auto* viewport=ImGui::GetMainViewport();const float s=info.scale;
    ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("GranularGPU",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    auto action=Action::none;auto& controls=lab.controls;
    ImGui::BeginChild("GpuControls",{265*s,0},ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("LABORATORIUM GPU");ImGui::TextDisabled("Klepsydra i ciecze / 0.6.1");ImGui::Separator();
    if(ImGui::Button("Wróć do menu",{-1,32*s}))action=Action::back;
    ImGui::Spacing();ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##GpuScene",&controls.scene,"Lawina\0Deszcz ziaren\0Mieszane materiały\0Pusta scena\0Zbiornik wody\0Przerwana zapora\0Naczynia połączone\0Woda i olej\0Piasek w wodzie\0Klepsydra\0Olej pod wodą\0Kropla oleju\0"))lab.input.reset=true;
    if(ImGui::Button("Reset sceny",{-1,30*s}))lab.input.reset=true;
    ImGui::Checkbox("Pauza [Spacja]",&controls.paused);
    if(ImGui::Button("Jeden krok [F]",{-1,28*s}))lab.input.step=true;
    if(ImGui::IsKeyPressed(ImGuiKey_Space,false))controls.paused=!controls.paused;
    if(ImGui::IsKeyPressed(ImGuiKey_F,false))lab.input.step=true;
    ImGui::Separator();ImGui::TextUnformatted("MATERIAŁ");ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##GpuMaterial",&controls.material,"Gumka\0Piasek\0Popiół\0Metalowe drobiny\0Ściana\0Woda\0Olej\0");
    ImGui::SliderInt("Pędzel",&controls.radius,1,16);
    ImGui::TextWrapped("LPM: rysuj. PPM: usuń. Kółko: rozmiar pędzla.");
    ImGui::Separator();ImGui::Text("Cząstki: %u / %d",lab.metrics.grains,gpu::capacity);
    ImGui::Text("Woda: %u / Olej: %u",lab.metrics.water,lab.metrics.oil);
    ImGui::Text("Ściśnięcie maks.: %.1f%%",lab.metrics.max_compression*100);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Błąd gęstości PBF, próbka co 30 kroków. Nie jest ciśnieniem w Pa.");
    ImGui::Text("Krok GPU: %.3f ms",lab.metrics.gpu_ms);
    ImGui::Text("Czas kroku: 1/120 s / %llu",static_cast<unsigned long long>(lab.metrics.steps));
    ImGui::Text("Okno: %.1f FPS",ImGui::GetIO().Framerate);
    if(controls.lost_seconds>.001)ImGui::Text("Pominięty czas: %.2f s",controls.lost_seconds);
    ImGui::Spacing();ImGui::TextWrapped("Woda i olej: solver PBF na GPU. Ogień, temperatura i reakcje pozostają w laboratorium CPU.");
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("GpuCanvas",{0,0});
    const auto room=ImGui::GetContentRegionAvail();const float zoom=std::max(.1F,std::min(room.x/gpu::width,(room.y-28*s)/gpu::height));
    const ImVec2 size{gpu::width*zoom,gpu::height*zoom};
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.F,(room.x-size.x)*.5F));const auto origin=ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef{static_cast<ImTextureID>(lab.image.handle)},size);
    bool painting=false;
    if(ImGui::IsItemHovered()){
        const auto mouse=ImGui::GetIO().MousePos;const float x=std::clamp((mouse.x-origin.x)/zoom,0.F,gpu::width-1.F),y=std::clamp((mouse.y-origin.y)/zoom,0.F,gpu::height-1.F);
        if(ImGui::GetIO().MouseWheel!=0)controls.radius=std::clamp(controls.radius+(ImGui::GetIO().MouseWheel>0?1:-1),1,16);
        ImGui::GetWindowDrawList()->AddCircle({origin.x+x*zoom,origin.y+y*zoom},controls.radius*zoom,IM_COL32(225,228,168,220));
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left)||ImGui::IsMouseDown(ImGuiMouseButton_Right)){
            const auto previous=controls.previous.value_or(std::array{x,y});
            lab.input.stroke=gpu::Stroke{previous[0],previous[1],x,y,static_cast<float>(controls.radius),ImGui::IsMouseDown(ImGuiMouseButton_Right)?gpu::Material::erase:static_cast<gpu::Material>(controls.material)};
            controls.previous=std::array{x,y};painting=true;
        }
    }
    if(!painting)controls.previous.reset();
    ImGui::TextDisabled("Ciągłe pozycje / powierzchnia cieczy / interpolacja ruchu");
    ImGui::EndChild();ImGui::End();return action;
}
}
