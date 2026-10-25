#include "core/settings.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <unordered_set>

namespace strefa::core {
namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view spaces{" \t\r"};
    const auto begin = text.find_first_not_of(spaces);
    if (begin == std::string_view::npos) return {};
    return text.substr(begin, text.find_last_not_of(spaces) - begin + 1);
}

template<class T>
std::string encode(T value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_enum_v<T>) {
        return encode(static_cast<std::underlying_type_t<T>>(value));
    } else {
        std::array<char, 64> buffer{};
        const auto result = [&] {
            if constexpr (std::is_floating_point_v<T>)
                return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                    std::chars_format::general, std::numeric_limits<T>::max_digits10);
            else
                return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        }();
        return {buffer.data(), result.ptr};
    }
}

template<class T>
bool decode(std::string_view text, T& destination) {
    if (text.empty()) return false;
    if constexpr (std::is_same_v<T, bool>) {
        if (text == "true") { destination = true; return true; }
        if (text == "false") { destination = false; return true; }
        return false;
    } else if constexpr (std::is_enum_v<T>) {
        std::underlying_type_t<T> value{};
        if (!decode(text, value)) return false;
        destination = static_cast<T>(value);
        return true;
    } else {
        T value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
        if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(value)) return false;
        destination = value;
        return true;
    }
}

} // namespace

std::expected<void, std::string> validate(const Settings& settings) {
    std::optional<std::string> error;
    meta::for_each_field(settings, settings_schema, [&](const auto& field, const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (!std::is_same_v<T, bool>) {
            bool invalid = value < field.minimum || value > field.maximum;
            if constexpr (std::is_floating_point_v<T>) invalid = invalid || !std::isfinite(value);
            if (invalid && !error) error = "Wartość poza zakresem: " + std::string{field.key};
        }
    });
    if (error) return std::unexpected{*error};
    return {};
}

std::string serialize(const Settings& settings) {
    std::string result{"schema_version=1\n"};
    meta::for_each_field(settings, settings_schema, [&](const auto& field, const auto& value) {
        result += field.key;
        result += '=';
        result += encode(value);
        result += '\n';
    });
    return result;
}

std::expected<Settings, std::string> deserialize(std::string_view text) {
    Settings result;
    std::unordered_set<std::string> seen;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.starts_with('#')) continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) return std::unexpected{"Brak '=' w ustawieniach."};
        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));
        if (!seen.emplace(key).second) return std::unexpected{"Powtórzony klucz: " + std::string{key}};
        if (key == "schema_version") {
            if (value != "1") return std::unexpected{"Nieobsługiwana wersja ustawień."};
            continue;
        }
        bool valid = true;
        meta::for_each_field(result, settings_schema, [&](const auto& field, auto& member) {
            if (field.key == key) valid = decode(value, member);
        });
        // Allow newer writers to add fields without breaking this reader.
        if (!valid) return std::unexpected{"Niepoprawna wartość: " + std::string{key}};
    }
    if (const auto checked = validate(result); !checked) return std::unexpected{checked.error()};
    return result;
}

} // namespace strefa::core
