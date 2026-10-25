#pragma once
#include <expected>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace strefa::platform {

class Texture {
public:
    [[nodiscard]] static std::expected<Texture, std::string> load_bmp(const std::filesystem::path& path);
    [[nodiscard]] static std::expected<Texture, std::string> create_rgba(int width, int height,
        std::span<const std::byte> pixels);
    [[nodiscard]] std::expected<void, std::string> update_rgba(std::span<const std::byte> pixels);
    ~Texture();
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    [[nodiscard]] unsigned int id() const noexcept { return id_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
private:
    Texture() = default;
    unsigned int id_{};
    int width_{};
    int height_{};
};

} // namespace strefa::platform
