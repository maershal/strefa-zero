#pragma once
#include "core/settings.hpp"
#include <filesystem>

namespace strefa::platform {

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path file) : file_{std::move(file)} {}
    [[nodiscard]] std::expected<core::Settings, std::string> load() const;
    [[nodiscard]] std::expected<void, std::string> save(const core::Settings& settings) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return file_; }
private:
    std::filesystem::path file_;
};

} // namespace strefa::platform
