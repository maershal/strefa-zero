#include "core/settings.hpp"
#include "platform/settings_store.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

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
    for (const auto text : {"ui_scale=nan", "ui_scale=inf", "width=1600oops", "width=1",
        "width=1280\nwidth=1600", "map_theme=99", "vsync=maybe", "schema_version=2", "missing_equals"})
        check(!strefa::core::deserialize(text).has_value(), "Malformed settings must be rejected");
    strefa::core::Settings settings;
    settings.frame_limit = 0;
    check(!strefa::core::validate(settings).has_value(), "Zero frame limit must not reach the frame pacer");
}

void replace_file() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::current_path() / ("settings-test-" + std::to_string(nonce) + ".ini");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); path += ".tmp"; std::filesystem::remove(path, ignored); }
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
    { std::ofstream corrupt{path}; corrupt << "width=broken"; }
    check(!store.load().has_value(), "Corrupted settings must report failure");
}
}

int main() {
    try {
        round_trip(); invalid_input(); replace_file();
        std::cout << "PASS: round-trip, invalid inputs, atomic replacement and corruption handling\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
