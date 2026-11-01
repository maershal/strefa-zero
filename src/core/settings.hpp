#pragma once

#include "core/metadata.hpp"

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
                   meta::Field<Settings, int>{"lighting_quality", "Oświetlenie", &Settings::lighting_quality, 0, 2},
                   meta::Field<Settings, float>{"bloom", "Bloom", &Settings::bloom, 0.F, 1.F},
                   meta::Field<Settings, float>{"vignette", "Winieta", &Settings::vignette, 0.F, 1.F},
                   meta::Field<Settings, float>{"camera_shake", "Wstrząs kamery", &Settings::camera_shake, 0.F, 1.F},
                   meta::Field<Settings, float>{"aberration", "Aberracja", &Settings::aberration, 0.F, 1.F},
                   meta::Field<Settings, bool>{"show_diagnostics", "Panel diagnostyczny", &Settings::show_diagnostics}};

[[nodiscard]] std::expected<void, std::string> validate(const Settings& settings);
[[nodiscard]] std::string serialize(const Settings& settings);
[[nodiscard]] std::expected<Settings, std::string> deserialize(std::string_view text);

} // namespace strefa::core
