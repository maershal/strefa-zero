#include "ui/shader_progress.hpp"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace strefa::ui {
void draw_shader_progress(unsigned done, unsigned total, float scale) {
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(420.0F * scale, viewport->Size.x - 32.0F * scale);
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + viewport->Size.y * 0.5F},
                            ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({width, 0.0F});
    ImGui::Begin("##shader_progress", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    const auto label = std::format("Kompilacja shaderów… {}/{}", done, total);
    ImGui::TextUnformatted(label.c_str());
    ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 0.0F, {-1.0F, 0.0F}, "");
    ImGui::TextDisabled("Pierwsze uruchomienie trybu GPU na tym sterowniku trwa dłużej.");
    ImGui::End();
}
} // namespace strefa::ui
