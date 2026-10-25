#include "platform/settings_store.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace strefa::platform {

std::expected<core::Settings, std::string> SettingsStore::load() const {
    std::error_code error;
    const bool exists = std::filesystem::exists(file_, error);
    if (error) return std::unexpected{"Nie można sprawdzić pliku ustawień: " + error.message()};
    if (!exists) return core::Settings{};
    if (std::filesystem::file_size(file_, error) > 64 * 1024 || error)
        return std::unexpected{"Plik ustawień jest zbyt duży albo niedostępny."};
    std::ifstream stream{file_, std::ios::binary};
    if (!stream) return std::unexpected{"Nie można otworzyć ustawień."};
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    if (stream.bad()) return std::unexpected{"Nie można odczytać ustawień."};
    return core::deserialize(contents);
}

std::expected<void, std::string> SettingsStore::save(const core::Settings& settings) const {
    if (const auto checked = core::validate(settings); !checked) return checked;
    std::error_code error;
    if (!file_.parent_path().empty()) std::filesystem::create_directories(file_.parent_path(), error);
    if (error) return std::unexpected{"Nie można utworzyć katalogu ustawień: " + error.message()};
    auto temporary = file_;
    temporary += ".tmp";
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) return std::unexpected{"Nie można zapisać ustawień."};
        stream << core::serialize(settings);
        stream.flush();
        if (!stream) return std::unexpected{"Błąd zapisu ustawień."};
        stream.close();
        if (!stream) return std::unexpected{"Błąd zamknięcia pliku ustawień."};
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return std::unexpected{"Nie można zastąpić pliku ustawień."};
#else
    std::filesystem::rename(temporary, file_, error);
    if (error) return std::unexpected{"Nie można zastąpić pliku ustawień: " + error.message()};
#endif
    return {};
}

} // namespace strefa::platform
