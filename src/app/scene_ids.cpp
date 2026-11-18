#include "app/scene_ids.hpp"

#include <format>

namespace strefa::app {
namespace {
std::string joined(std::span<const char* const> ids) {
    std::string text;
    for (const auto* id : ids) {
        if (!text.empty()) text += ", ";
        text += id;
    }
    return text;
}
} // namespace

std::expected<unsigned, std::string> find_scene_id(std::span<const char* const> ids, std::string_view name,
                                                   std::string_view what) {
    for (unsigned i = 0; i < ids.size(); ++i)
        if (name == ids[i]) return i;
    return std::unexpected{std::format("Nieznana scena {} „{}”. Dostępne: {}.", what, name, joined(ids))};
}

} // namespace strefa::app
