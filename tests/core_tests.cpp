#include "core/charconv.hpp"
#include "core/settings.hpp"
#include "platform/settings_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

void round_trip() {
    strefa::core::Settings settings;
    settings.width = 1600;
    settings.height = 900;
    settings.vsync = false;
    settings.frame_limit = 144;
    settings.ui_scale = 1.234567F;
    settings.map_theme = strefa::core::MapTheme::winter;
    settings.show_diagnostics = true;
    const auto result = strefa::core::deserialize(strefa::core::serialize(settings));
    check(result.has_value() && *result == settings, "Settings must round-trip without precision loss");
    const auto partial = strefa::core::deserialize("# comment\r\nwidth=1600\r\nfuture_key=allowed\r\n");
    check(partial.has_value() && partial->width == 1600 && partial->vsync,
          "Partial files should use defaults and tolerate future fields");
}

void invalid_input() {
    for (const auto text : {"ui_scale=nan", "ui_scale=inf", "width=1600oops", "width=1", "width=1280\nwidth=1600",
                            "map_theme=99", "vsync=maybe", "schema_version=2", "missing_equals"})
        check(!strefa::core::deserialize(text).has_value(), "Malformed settings must be rejected");
    strefa::core::Settings settings;
    settings.frame_limit = 0;
    check(!strefa::core::validate(settings).has_value(), "Zero frame limit must not reach the frame pacer");
}

// On libc++ before LLVM 20 these exercise the floating-point fallback of core::from_chars.
void float_parsing() {
    const auto parse = [](std::string_view text, float& value) {
        const auto result = strefa::core::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    float value{};
    check(parse("1.234567", value) && value == 1.234567F, "Decimal floats must parse exactly");
    check(parse("-2.5e3", value) && value == -2500.0F, "Signed exponents must parse");
    check(parse(".5", value) && value == 0.5F && parse("7.", value) && value == 7.0F, "Bare fractions must parse");
    for (const auto text : {"", "+1", " 1", "1,5", "abc", "1e", "1e+", "0x10", "1.5f"})
        check(!parse(text, value), "Malformed floats must be rejected");
    const std::string_view huge{"1e999"};
    value = 3.0F;
    check(strefa::core::from_chars(huge.data(), huge.data() + huge.size(), value).ec == std::errc::result_out_of_range
              && value == 3.0F,
          "Overflow must report out of range and keep the value");
}

void quality_and_experimental() {
    using strefa::core::Settings;
    const auto old = strefa::core::deserialize("schema_version=1\nwidth=1280\nlighting_quality=1\n");
    check(old && old->water_quality == 0 && old->effects_quality == 1,
          "Old settings must keep the basic water and low effects defaults");
    Settings settings;
    settings.water_quality = 2;
    settings.effects_quality = 0;
    const auto saved = strefa::core::deserialize(strefa::core::serialize(settings));
    check(saved && saved->water_quality == 2 && saved->effects_quality == 0, "Water and effects quality round-trip");
    for (const auto text : {"water_quality=3", "effects_quality=-1"})
        check(!strefa::core::deserialize(text).has_value(), "Out-of-range quality must be rejected");
    Settings stored, wanted;
    wanted.lighting_quality = 2;
    wanted.bloom = .5F;
    wanted.water_quality = 1;
    const auto confirmed = strefa::core::confirmed_settings(wanted, stored);
    check(confirmed.lighting_quality == 0 && confirmed.bloom == .5F && confirmed.water_quality == 1,
          "Raising an experimental option needs confirmation; other changes are kept");
    stored.lighting_quality = 2;
    wanted.lighting_quality = 1;
    check(strefa::core::confirmed_settings(wanted, stored).lighting_quality == 1,
          "Lowering an experimental option is written without confirmation");
    check(strefa::core::confirmed_settings(stored, stored) == stored, "Confirmed settings stay unchanged");
}

void replace_file() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::current_path() / ("settings-test-" + std::to_string(nonce) + ".ini");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            path += ".tmp";
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};
    strefa::platform::SettingsStore store{path};
    check(store.load().has_value(), "A missing file should yield defaults");
    strefa::core::Settings first;
    check(store.save(first).has_value(), "Initial settings save must succeed");
    auto second = first;
    second.map_theme = strefa::core::MapTheme::industrial;
    second.width = 1920;
    check(store.save(second).has_value(), "Replacing an existing file must succeed");
    const auto loaded = store.load();
    check(loaded.has_value() && *loaded == second, "The replacement must be readable and complete");
    auto invalid = second;
    invalid.ui_scale = -1.0F;
    check(!store.save(invalid).has_value(), "Invalid data must not overwrite the file");
    check(store.load().value() == second, "Rejected save must preserve the previous file");
    {
        std::ofstream corrupt{path};
        corrupt << "width=broken";
    }
    check(!store.load().has_value(), "Corrupted settings must report failure");
}
} // namespace

int main() {
    try {
        round_trip();
        invalid_input();
        float_parsing();
        quality_and_experimental();
        replace_file();
        std::cout << "PASS: round-trip, invalid inputs, atomic replacement and corruption handling\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
