#pragma once
#include "sim/world.hpp"

#include <filesystem>
#include <fstream>

namespace strefa::sim {
// CPU timing is supplied by the caller and excludes rendering/export.
class Recording {
public:
    explicit Recording(const std::filesystem::path& directory);
    void capture(const World& world, double physics_ms, RenderMode mode = RenderMode::materials);
    void finish();

private:
    std::filesystem::path directory_;
    std::ofstream log_;
    std::vector<std::string> frames_;
    std::vector<Color> pixels_ = std::vector<Color>(grid_cells);
};
} // namespace strefa::sim
