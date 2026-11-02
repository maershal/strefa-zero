#pragma once

#include "core/metadata.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <tuple>

namespace strefa::core {

enum class MapTheme { woodland, industrial, winter };
inline constexpr std::array<std::string_view, 3> theme_names{"Leśne bunkry", "Strefa przemysłowa", "Zimowe ruiny"};

struct Settings {
    int width{1280};
    int height{800};
    bool vsync{true};
    int frame_limit{120};
    float ui_scale{1.0F};
    MapTheme map_theme{MapTheme::woodland};
    bool show_diagnostics{false};
    int lighting_quality{};
    float bloom{.08F}, vignette{.10F}, camera_shake{}, aberration{};
    int water_quality{};    // gpu::RenderQuality: basic
    int effects_quality{1}; // gpu::VfxQuality: low

    bool operator==(const Settings&) const = default;
};

inline constexpr auto settings_schema =
        std::tuple{meta::Field<Settings, int>{"width", "Szerokość okna", &Settings::width, 960, 2560},
                   meta::Field<Settings, int>{"height", "Wysokość okna", &Settings::height, 540, 1440},
                   meta::Field<Settings, bool>{"vsync", "Synchronizacja pionowa", &Settings::vsync},
                   meta::Field<Settings, int>{"frame_limit", "Limit FPS bez VSync", &Settings::frame_limit, 30, 240},
                   meta::Field<Settings, float>{"ui_scale", "Skala interfejsu", &Settings::ui_scale, 0.85F, 1.5F},
                   meta::Field<Settings, MapTheme>{"map_theme", "Motyw pola walki", &Settings::map_theme,
                                                   MapTheme::woodland, MapTheme::winter},
                   meta::Field<Settings, int>{"water_quality", "Obraz wody", &Settings::water_quality, 0, 2},
                   meta::Field<Settings, int>{"effects_quality", "Efekty", &Settings::effects_quality, 0, 2},
                   meta::Field<Settings, int>{"lighting_quality", "Oświetlenie (eksperymentalne)",
                                              &Settings::lighting_quality, 0, 2},
                   meta::Field<Settings, float>{"bloom", "Bloom", &Settings::bloom, 0.F, 1.F},
                   meta::Field<Settings, float>{"vignette", "Winieta", &Settings::vignette, 0.F, 1.F},
                   meta::Field<Settings, float>{"camera_shake", "Wstrząs kamery", &Settings::camera_shake, 0.F, 1.F},
                   meta::Field<Settings, float>{"aberration", "Aberracja", &Settings::aberration, 0.F, 1.F},
                   meta::Field<Settings, bool>{"show_diagnostics", "Panel diagnostyczny", &Settings::show_diagnostics}};

// Options that exceed their measured budget stay off by default; raising one is
// written to settings.ini only after explicit confirmation (lowering always is).
[[nodiscard]] constexpr Settings confirmed_settings(Settings wanted, const Settings& stored) {
    wanted.lighting_quality = std::min(wanted.lighting_quality, std::max(stored.lighting_quality, 0));
    return wanted;
}
// Cost of the experimental options, from docs/reports/I10_GPU_THERMAL.md and I11_LIGHTING.md.
inline constexpr std::string_view lighting_cost =
        "Eksperymentalne: +0,65–1,03 ms na krok (niskie) i +0,75–1,10 ms (wysokie) przy budżecie 0,35 ms, GTX 1650.";
inline constexpr std::string_view thermal_cost =
        "Eksperymentalne: +1,4–9,7 ms na krok zależnie od sceny przy budżecie 0,70 ms, GTX 1650.";

[[nodiscard]] std::expected<void, std::string> validate(const Settings& settings);
[[nodiscard]] std::string serialize(const Settings& settings);
[[nodiscard]] std::expected<Settings, std::string> deserialize(std::string_view text);

} // namespace strefa::core
