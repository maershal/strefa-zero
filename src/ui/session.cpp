#include "ui/session.hpp"
#include "platform/window.hpp"
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <cmath>
#include <utility>

namespace strefa::ui {

Session::Session(Session&& other) noexcept
    : context_{std::exchange(other.context_, nullptr)},
      platform_ready_{std::exchange(other.platform_ready_, false)},
      renderer_ready_{std::exchange(other.renderer_ready_, false)},
      fonts_{other.fonts_}, scale_{other.scale_} {}
Session::~Session() {
    if (!context_) return;
    ImGui::SetCurrentContext(context_);
    if (renderer_ready_) ImGui_ImplOpenGL3_Shutdown();
    if (platform_ready_) ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context_);
}

std::expected<Session, std::string> Session::create(
    const platform::Window& window, const std::filesystem::path& assets) {
    IMGUI_CHECKVERSION();
    Session result;
    result.context_ = ImGui::CreateContext();
    if (!result.context_) return std::unexpected{"Nie można utworzyć kontekstu ImGui."};
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    result.platform_ready_ = ImGui_ImplSDL3_InitForOpenGL(window.native(), window.context());
    if (!result.platform_ready_) return std::unexpected{"Nie można uruchomić backendu SDL3 ImGui."};
    result.renderer_ready_ = ImGui_ImplOpenGL3_Init("#version 330 core");
    if (!result.renderer_ready_) return std::unexpected{"Nie można uruchomić backendu OpenGL ImGui."};
    const auto regular = (assets / "fonts" / "Barlow-Regular.ttf").string();
    const auto bold = (assets / "fonts" / "Barlow-Bold.ttf").string();
    if (!std::filesystem::exists(regular) || !std::filesystem::exists(bold))
        return std::unexpected{"Brak czcionek w assets/fonts. Rozpakuj pełną paczkę aplikacji."};
    result.fonts_.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), 18.0F);
    result.fonts_.heading = io.Fonts->AddFontFromFileTTF(bold.c_str(), 18.0F);
    if (!result.fonts_.body || !result.fonts_.heading)
        return std::unexpected{"Nie można wczytać czcionek interfejsu."};
    io.FontDefault = result.fonts_.body;
    result.set_scale(window.display_scale(), 1.0F);
    return result;
}

void Session::process_event(const SDL_Event& event) const { ImGui_ImplSDL3_ProcessEvent(&event); }
void Session::new_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}
void Session::render() const {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
void Session::set_scale(float display_scale, float user_scale) {
    const float next = display_scale * user_scale;
    if (std::abs(next - scale_) < 0.001F) return;
    scale_ = next;
    ImGuiStyle style;
    style.WindowRounding = 4.0F;
    style.ChildRounding = 4.0F;
    style.FrameRounding = 3.0F;
    style.FramePadding = {12.0F, 9.0F};
    style.ItemSpacing = {12.0F, 12.0F};
    style.WindowPadding = {24.0F, 22.0F};
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.ScaleAllSizes(next);
    style.FontSizeBase = 18.0F;
    style.FontScaleDpi = display_scale;
    style.FontScaleMain = user_scale;
    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = {0.91F, 0.93F, 0.85F, 1.0F};
    colors[ImGuiCol_TextDisabled] = {0.52F, 0.59F, 0.51F, 1.0F};
    colors[ImGuiCol_WindowBg] = {0.055F, 0.075F, 0.060F, 0.97F};
    colors[ImGuiCol_ChildBg] = {0.07F, 0.09F, 0.07F, 0.95F};
    colors[ImGuiCol_Border] = {0.24F, 0.30F, 0.22F, 0.75F};
    colors[ImGuiCol_FrameBg] = {0.12F, 0.16F, 0.11F, 1.0F};
    colors[ImGuiCol_FrameBgHovered] = {0.19F, 0.25F, 0.16F, 1.0F};
    colors[ImGuiCol_FrameBgActive] = {0.25F, 0.33F, 0.20F, 1.0F};
    colors[ImGuiCol_Button] = {0.14F, 0.19F, 0.12F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = {0.25F, 0.34F, 0.18F, 1.0F};
    colors[ImGuiCol_ButtonActive] = {0.32F, 0.40F, 0.22F, 1.0F};
    colors[ImGuiCol_CheckMark] = {0.75F, 0.83F, 0.40F, 1.0F};
    colors[ImGuiCol_SliderGrab] = colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_SliderGrabActive] = {0.88F, 0.92F, 0.58F, 1.0F};
    colors[ImGuiCol_Header] = colors[ImGuiCol_Button];
    colors[ImGuiCol_HeaderHovered] = colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_ButtonActive];
    ImGui::GetStyle() = style;
}

} // namespace strefa::ui
