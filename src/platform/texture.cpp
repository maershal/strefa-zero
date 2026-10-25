#include "platform/texture.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace strefa::platform {

Texture::Texture(Texture&& other) noexcept
    : id_{std::exchange(other.id_, 0)}, width_{other.width_}, height_{other.height_} {}
Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (id_) glDeleteTextures(1, &id_);
        id_ = std::exchange(other.id_, 0);
        width_ = other.width_;
        height_ = other.height_;
    }
    return *this;
}
Texture::~Texture() { if (id_) glDeleteTextures(1, &id_); }

std::expected<Texture, std::string> Texture::create_rgba(int width, int height,
    std::span<const std::byte> pixels) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192
        || pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return std::unexpected{"Nieprawidłowe wymiary tekstury RGBA."};
    Texture result;
    result.width_ = width; result.height_ = height;
    GLint previous{}, alignment{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glGenTextures(1, &result.id_);
    glBindTexture(GL_TEXTURE_2D, result.id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto error = glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (error != GL_NO_ERROR) return std::unexpected{"Nie można utworzyć tekstury symulacji."};
    return result;
}
std::expected<void, std::string> Texture::update_rgba(std::span<const std::byte> pixels) {
    if (!id_ || pixels.size() != static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4)
        return std::unexpected{"Nieprawidłowy bufor tekstury symulacji."};
    GLint previous{}, alignment{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto error = glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (error != GL_NO_ERROR) return std::unexpected{"Nie można odświeżyć tekstury symulacji."};
    return {};
}

std::expected<Texture, std::string> Texture::load_bmp(const std::filesystem::path& path) {
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    const auto utf8 = path.u8string();
    Surface original{SDL_LoadBMP(reinterpret_cast<const char*>(utf8.c_str())), SDL_DestroySurface};
    if (!original) return std::unexpected{"Nie można wczytać tła: " + std::string{SDL_GetError()}};
    Surface converted{SDL_ConvertSurface(original.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface};
    if (!converted) return std::unexpected{"Nie można przygotować tła: " + std::string{SDL_GetError()}};
    Texture result;
    result.width_ = converted->w;
    result.height_ = converted->h;
    glGenTextures(1, &result.id_);
    glBindTexture(GL_TEXTURE_2D, result.id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const auto row_bytes = static_cast<std::size_t>(converted->w) * 4;
    std::vector<unsigned char> tight(row_bytes * static_cast<std::size_t>(converted->h));
    for (int row = 0; row < converted->h; ++row)
        std::memcpy(tight.data() + static_cast<std::size_t>(row) * row_bytes,
            static_cast<const unsigned char*>(converted->pixels)
                + static_cast<std::size_t>(row) * static_cast<std::size_t>(converted->pitch), row_bytes);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, converted->w, converted->h, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, tight.data());
    const auto error = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);
    if (error != GL_NO_ERROR) return std::unexpected{"Sterownik odrzucił teksturę tła."};
    return result;
}

} // namespace strefa::platform
