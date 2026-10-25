#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace strefa::app {

struct LaunchOptions {
    bool smoke_test{false};
    bool laboratory_smoke_test{false};
    bool start_in_laboratory{false};
    bool start_in_arena{false};
    unsigned physics_threads{};
    std::filesystem::path profile;
};

[[nodiscard]] std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv);
[[nodiscard]] std::expected<int, std::string> run(const LaunchOptions& options);

} // namespace strefa::app
