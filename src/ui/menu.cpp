#include "build_info.hpp"
#include "ui/arena.hpp"
#include "ui/gpu_laboratory.hpp"
#include "ui/laboratory.hpp"
#include "ui/view.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <type_traits>

namespace strefa::ui {
namespace {

constexpr ImVec4 muted{0.59F, 0.66F, 0.55F, 1.0F};
constexpr ImVec4 accent{0.79F, 0.86F, 0.46F, 1.0F};
constexpr auto plain_window = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoSavedSettings;

class FontScope {
public:
    FontScope(ImFont* font, float size) { ImGui::PushFont(font, size); }
    ~FontScope() { ImGui::PopFont(); }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

void heading(const Fonts& fonts, const char* text, float size) {
    const FontScope font{fonts.heading, size};
    ImGui::TextUnformatted(text);
}

void background(const Image& image, bool preview, float scale) {
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 end{origin.x + viewport->Size.x, origin.y + viewport->Size.y};
    auto* draw = ImGui::GetBackgroundDrawList();
    if (image.handle && image.width > 0 && image.height > 0) {
        const float screen_aspect = viewport->Size.x / viewport->Size.y;
        const float image_aspect = static_cast<float>(image.width) / static_cast<float>(image.height);
        ImVec2 uv0{0.0F, 0.0F}, uv1{1.0F, 1.0F};
        if (screen_aspect > image_aspect) {
            const float visible = image_aspect / screen_aspect;
            uv0.y = (1.0F - visible) * 0.5F;
            uv1.y = 1.0F - uv0.y;
        } else {
            const float visible = screen_aspect / image_aspect;
            uv0.x = (1.0F - visible) * 0.5F;
            uv1.x = 1.0F - uv0.x;
        }
        draw->AddImage(ImTextureRef{static_cast<ImTextureID>(image.handle)}, origin, end, uv0, uv1);
    }
    draw->AddRectFilled(origin, end, IM_COL32(9, 17, 12, preview ? 28 : 92));
    if (!preview) {
        draw->AddRectFilledMultiColor(origin, end, IM_COL32(9, 16, 11, 248), IM_COL32(9, 16, 11, 12),
                                      IM_COL32(9, 16, 11, 30), IM_COL32(9, 16, 11, 250));
        const float spacing = 56.0F * scale;
        for (float x = origin.x + viewport->Size.x * 0.52F; x < end.x; x += spacing)
            draw->AddLine({x, origin.y}, {x, end.y}, IM_COL32(193, 207, 156, 8));
    }
    draw->AddRectFilledMultiColor({origin.x, origin.y + viewport->Size.y * 0.72F}, end, IM_COL32(0, 0, 0, 0),
                                  IM_COL32(0, 0, 0, 0), IM_COL32(5, 10, 7, 190), IM_COL32(5, 10, 7, 190));
    const float inset = 24.0F * scale, length = 22.0F * scale;
    const ImU32 color = IM_COL32(177, 195, 121, 125);
    draw->AddLine({origin.x + inset, origin.y + inset}, {origin.x + inset + length, origin.y + inset}, color);
    draw->AddLine({origin.x + inset, origin.y + inset}, {origin.x + inset, origin.y + inset + length}, color);
    draw->AddLine({end.x - inset, end.y - inset}, {end.x - inset - length, end.y - inset}, color);
    draw->AddLine({end.x - inset, end.y - inset}, {end.x - inset, end.y - inset - length}, color);
}

bool menu_button(const char* label, float height, bool primary = false) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.06F, 0.5F});
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.72F, 0.80F, 0.40F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.83F, 0.89F, 0.54F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.62F, 0.72F, 0.31F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.08F, 0.12F, 0.06F, 1.0F});
    }
    const bool pressed = ImGui::Button(label, {-FLT_MIN, height});
    if (primary) ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return pressed;
}

// Items that need the OpenGL 4.3 world stay visible but disabled, with the reason on hover.
bool gpu_menu_button(const char* label, float height, bool available, bool primary = false) {
    ImGui::BeginDisabled(!available);
    const bool pressed = menu_button(label, height, primary && available);
    ImGui::EndDisabled();
    if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Wymaga OpenGL 4.3 (compute shadery). Ten kontekst ma niższą wersję;\n"
                          "Laboratorium CPU i ustawienia działają normalnie.");
    return pressed;
}

void notice(std::string_view message) {
    if (message.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.90F, 0.78F, 0.48F, 1.0F});
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(message.data(), message.data() + message.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

Action main_menu(const ViewState& state, const FrameInfo& frame, const Fonts& fonts) {
    const auto* viewport = ImGui::GetMainViewport();
    const float s = frame.scale;
    const bool compact = viewport->Size.y < 760.0F * s;
    const bool tight = viewport->Size.y < 620.0F * s;
    const float margin = (tight ? 24.0F : compact ? 40.0F : 60.0F) * s;
    const float width = std::min(430.0F * s, viewport->Size.x * 0.46F);
    ImGui::SetNextWindowPos({viewport->Pos.x + margin, viewport->Pos.y + margin});
    ImGui::SetNextWindowSize({width, viewport->Size.y - margin * 2.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::Begin("MainMenu", nullptr, plain_window | ImGuiWindowFlags_NoBackground);
    ImGui::TextColored(accent, "PROTOTYP 07   /   OFFLINE");
    ImGui::Dummy({0.0F, (tight ? 4.0F : compact ? 12.0F : 22.0F) * s});
    {
        const FontScope font{fonts.heading, tight ? 42.0F : compact ? 58.0F : 76.0F};
        ImGui::TextUnformatted("STREFA");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 16.0F * s);
        ImGui::TextUnformatted("ZERO");
    }
    ImGui::Dummy({0.0F, 6.0F * s});
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    ImGui::TextWrapped("Demo silnika symulacji materiałów. Wybierz tryb.");
    ImGui::PopStyleColor();
    ImGui::Dummy({0.0F, (tight ? 8.0F : compact ? 16.0F : 28.0F) * s});
    Action action = Action::none;
    const float button_height = (tight ? 29.0F : compact ? 34.0F : 40.0F) * s;
    // Mode names match README.md, docs/QUICKSTART.md and docs/LINUX_WSL.md.
    if (gpu_menu_button("01    Misja GPU", button_height, frame.gpu_available, true)) action = Action::open_mission;
    if (gpu_menu_button("02    Laboratorium GPU", button_height, frame.gpu_available)) action = Action::open_gpu;
    if (menu_button("03    Laboratorium CPU", button_height)) action = Action::open_laboratory;
    if (menu_button("04    Podgląd areny", button_height)) action = Action::open_preview;
    if (menu_button("05    Ustawienia", button_height)) action = Action::open_settings;
    if (menu_button("06    O projekcie", button_height)) action = Action::open_about;
    if (menu_button("07    Zakończ", button_height)) action = Action::quit;
    ImGui::Dummy({0.0F, 14.0F * s});
    notice(frame.message);
    ImGui::TextDisabled("v%s  /  F1 — diagnostyka", build::version.data());
    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(
            {viewport->Pos.x + viewport->Size.x - 310.0F * s, viewport->Pos.y + viewport->Size.y - 112.0F * s});
    ImGui::SetNextWindowSize({270.0F * s, 76.0F * s});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::Begin("MapCaption", nullptr, plain_window | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    ImGui::TextColored(accent, "WYBRANY SEKTOR");
    const auto theme = core::theme_names[static_cast<std::size_t>(state.applied.map_theme)];
    ImGui::TextUnformatted(theme.data(), theme.data() + theme.size());
    ImGui::End();
    ImGui::PopStyleVar();
    return action;
}

void begin_card(const char* id, float width, float height, float scale) {
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 size{std::min(width * scale, viewport->Size.x - 48.0F * scale),
                      std::min(height * scale, viewport->Size.y - 48.0F * scale)};
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + viewport->Size.y * 0.5F},
                            ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(id, nullptr, plain_window);
}

template<class T> void setting_widget(const meta::Field<core::Settings, T>& field, T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        ImGui::Checkbox("##value", &value);
    } else if constexpr (std::is_same_v<T, int>) {
        if (field.key == "lighting_quality" || field.key == "effects_quality")
            ImGui::Combo("##value", &value, "Wyłączone\0Niskie\0Wysokie\0");
        else if (field.key == "water_quality")
            ImGui::Combo("##value", &value, "Podstawowy\0Standardowy\0Wysoki\0");
        else
            ImGui::SliderInt("##value", &value, field.minimum, field.maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
    } else if constexpr (std::is_same_v<T, float>) {
        ImGui::SliderFloat("##value", &value, field.minimum, field.maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    } else if constexpr (std::is_same_v<T, core::MapTheme>) {
        const auto index = static_cast<std::size_t>(value);
        if (ImGui::BeginCombo("##value", core::theme_names[index].data())) {
            for (std::size_t i = 0; i < core::theme_names.size(); ++i) {
                if (ImGui::Selectable(core::theme_names[i].data(), i == index)) value = static_cast<T>(i);
                if (i == index) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
}

Action settings(ViewState& state, const FrameInfo& frame, const Fonts& fonts) {
    const float s = frame.scale;
    begin_card("Settings", 720.0F, 600.0F, s);
    ImGui::TextColored(accent, "KONFIGURACJA");
    heading(fonts, "Ustawienia", 34.0F);
    ImGui::TextColored(muted, "Zmiany zostaną zastosowane po zapisaniu.");
    ImGui::Dummy({0.0F, 12.0F * s});
    ImGui::Separator();
    const float footer = 110.0F * s;
    ImGui::BeginChild("Fields", {0.0F, -footer}, ImGuiChildFlags_None);
    if (ImGui::BeginTable("SettingsSchema", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.48F);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.52F);
        meta::for_each_field(state.draft, core::settings_schema, [&](const auto& field, auto& value) {
            ImGui::PushID(field.key.data());
            ImGui::TableNextRow(0, 52.0F * s);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(field.label.data(), field.label.data() + field.label.size());
            if (field.key == "lighting_quality" && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", core::lighting_cost.data());
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::BeginDisabled(field.key == "frame_limit" && state.draft.vsync);
            setting_widget(field, value);
            ImGui::EndDisabled();
            ImGui::PopID();
        });
        ImGui::EndTable();
    }
    ImGui::EndChild();
    notice(frame.message);
    ImGui::Separator();
    Action action = Action::none;
    // Raising an experimental option asks before it goes to settings.ini.
    if (ImGui::Button("Zastosuj", {144.0F * s, 42.0F * s})) {
        if (core::confirmed_settings(state.draft, state.stored) == state.draft)
            action = Action::apply;
        else
            ImGui::OpenPopup("Opcje eksperymentalne");
    }
    if (ImGui::BeginPopupModal("Opcje eksperymentalne", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(420.0F * s);
        ImGui::TextUnformatted("Oświetlenie przekracza zmierzony budżet wydajności.");
        ImGui::TextColored(muted, "%s", core::lighting_cost.data());
        ImGui::TextUnformatted("Zapisać je na stałe, czy włączyć tylko w tej sesji?");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Zapisz na stałe")) {
            action = Action::apply;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Tylko w tej sesji")) {
            action = Action::apply_session;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Anuluj")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Domyślne", {120.0F * s, 42.0F * s})) action = Action::reset;
    ImGui::SameLine();
    if (ImGui::Button("Wróć", {100.0F * s, 42.0F * s})) action = Action::back;
    ImGui::End();
    return action;
}

Action about(const FrameInfo& frame, const Fonts& fonts) {
    begin_card("About", 640.0F, 470.0F, frame.scale);
    ImGui::TextColored(accent, "STREFA ZERO   /   DEMO SILNIKA");
    heading(fonts, "O projekcie", 34.0F);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted("Prototyp silnika 2D z symulacją materiałów: ciecze i ziarna liczone na GPU oraz "
                           "referencyjny model cieplno-chemiczny na CPU. Misja pokazuje silnik w ruchu.");
    ImGui::Spacing();
    ImGui::TextColored(muted, "Tryby");
    ImGui::BulletText("Misja GPU: tarcze, wyjście, niszczone przegrody, woda i piasek");
    ImGui::BulletText("Laboratorium GPU: ciecze PBF, ziarna PBD, nagrania, termika eksperymentalna");
    ImGui::BulletText("Laboratorium CPU: 19 materiałów, temperatura, ciśnienie i reakcje");
    ImGui::Spacing();
    ImGui::TextColored(muted, "Ograniczenia");
    ImGui::BulletText("Tryby GPU wymagają OpenGL 4.3; na 3.3 działa Laboratorium CPU");
    ImGui::BulletText("Oświetlenie i termika są eksperymentalne (poza budżetem wydajności)");
    ImGui::Spacing();
    ImGui::TextDisabled("C++23  /  SDL3  /  Dear ImGui  /  OpenGL");
    ImGui::TextDisabled("Czcionki: Barlow — SIL Open Font License.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0.0F, 18.0F * frame.scale});
    const bool back = ImGui::Button("Wróć do menu", {180.0F * frame.scale, 42.0F * frame.scale});
    ImGui::End();
    return back ? Action::back : Action::none;
}

Action preview(const ViewState& state, const FrameInfo& frame, const Fonts& fonts) {
    const auto* viewport = ImGui::GetMainViewport();
    const float s = frame.scale;
    ImGui::SetNextWindowPos({viewport->Pos.x + 40.0F * s, viewport->Pos.y + 40.0F * s});
    ImGui::SetNextWindowSize({std::min(420.0F * s, viewport->Size.x - 80.0F * s), 180.0F * s});
    ImGui::Begin("PreviewControls", nullptr, plain_window);
    ImGui::TextColored(accent, "PODGLĄD OPRAWY");
    heading(fonts, core::theme_names[static_cast<std::size_t>(state.applied.map_theme)].data(), 28.0F);
    ImGui::TextColored(muted, "ESC — powrót do menu");
    const bool back = ImGui::Button("Wróć do menu", {180.0F * s, 40.0F * s});
    ImGui::End();
    return back ? Action::back : Action::none;
}

void diagnostics(ViewState& state, const FrameInfo& frame) {
    if (!state.diagnostics) return;
    ImGui::SetNextWindowSize({430.0F * frame.scale, 0.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Diagnostyka [F1]", &state.diagnostics, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%.1f FPS   /   %.2f ms", static_cast<double>(ImGui::GetIO().Framerate),
                    static_cast<double>(frame.frame_ms));
        ImGui::Separator();
        ImGui::Text("Kompilator: %s", build::compiler.data());
        ImGui::Text("SDL %s  /  ImGui %s", build::sdl.data(), build::imgui.data());
        ImGui::Text("C++: %ld", static_cast<long>(__cplusplus));
        ImGui::Text("Skala: %.2f", static_cast<double>(frame.scale));
        ImGui::TextWrapped("GPU: %.*s", static_cast<int>(frame.renderer.size()), frame.renderer.data());
        ImGui::TextWrapped("OpenGL: %.*s", static_cast<int>(frame.gl_version.size()), frame.gl_version.data());
        ImGui::TextDisabled("Schemat ustawień: typowane metadane C++23");
    }
    ImGui::End();
}

} // namespace

bool experimental_notice(const FrameInfo& frame) {
    if (!frame.unconfirmed_settings) return false;
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextColored({0.90F, 0.78F, 0.48F, 1.0F}, "Opcje eksperymentalne działają tylko w tej sesji.");
    ImGui::PopTextWrapPos();
    const bool save = ImGui::Button("Zapisz na stałe");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", core::lighting_cost.data());
    return save;
}

void clock_stats(const ClockStats& clock) {
    if (clock.speed >= 0)
        ImGui::Text("Tempo ×%.2f", clock.speed);
    else
        ImGui::TextDisabled("Tempo ×–");
    ImGui::SameLine();
    ImGui::Text("| utracony czas %.2f s |", clock.lost_seconds);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Czas pominięty po przestoju lub przeciążeniu, zamiast przyspieszać symulację.");
    ImGui::SameLine();
    if (clock.gpu_tick_ms > 0)
        ImGui::Text("GPU/krok %.2f ms", clock.gpu_tick_ms);
    else
        ImGui::TextDisabled("GPU/krok –");
    if (clock.gpu_slowdown) ImGui::TextColored({0.95F, 0.55F, 0.30F, 1.0F}, "GPU zwolnił – zasilanie lub temperatura?");
}

Action draw(ViewState& state, const FrameInfo& frame, const std::array<Image, 3>& backgrounds, const Fonts& fonts,
            LaboratoryFrame* laboratory, ArenaFrame* arena, GpuLaboratoryFrame* gpu) {
    const auto theme = static_cast<std::size_t>(state.applied.map_theme);
    background(backgrounds[theme], state.screen == Screen::preview, frame.scale);
    Action result = Action::none;
    switch (state.screen) {
    case Screen::main_menu: result = main_menu(state, frame, fonts); break;
    case Screen::settings: result = settings(state, frame, fonts); break;
    case Screen::preview: result = preview(state, frame, fonts); break;
    case Screen::about: result = about(frame, fonts); break;
    case Screen::laboratory:
        if (laboratory) result = draw_laboratory(*laboratory, frame, fonts);
        break;
    case Screen::gpu_mission:
        if (arena) result = draw_arena(*arena, frame, fonts);
        break;
    case Screen::gpu_laboratory:
        if (gpu) result = draw_gpu_laboratory(*gpu, frame);
        break;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) state.diagnostics = !state.diagnostics;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && state.screen != Screen::main_menu) result = Action::back;
    diagnostics(state, frame);
    return result;
}

} // namespace strefa::ui
