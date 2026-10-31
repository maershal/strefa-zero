#pragma once
#include "sim/physics.hpp"
#include <span>

namespace strefa::sim {
struct Cell { Material material{Material::empty}; std::uint8_t shade{}; };
enum class ScanMode { active_chunks, full_grid };
enum class Scene {
    demonstration, falling_water, dam_break, connected_vessels, oil_water, sand_pile,
    sand_water, smoke_ceiling, smoke_vent, wood_fire, extinguish, melting_metal,
    quench_metal, ice_fire, shielded_explosion, count
};
inline constexpr std::array scene_names{"Demonstracja", "Spadająca woda", "Przerwana zapora",
    "Naczynia połączone", "Olej i woda", "Usyp piasku", "Piasek w wodzie", "Dym pod sufitem",
    "Dym i otwór", "Spalanie drewna", "Gaszenie", "Topienie metalu", "Chłodzenie metalu",
    "Lód i ogień", "Wybuch za osłoną"};
inline constexpr std::array scene_ids{"demo","falling-water","dam-break","connected-vessels","oil-water",
    "sand-pile","sand-water","smoke-ceiling","smoke-vent","wood-fire","extinguish","melting-metal",
    "quench-metal","ice-fire","shielded-explosion"};
class World {
public:
    static constexpr int width = grid_width, height = grid_height, chunk_size = 16;
    static constexpr int columns = width / chunk_size, rows = height / chunk_size;
    static constexpr int chunk_count = columns * rows, cell_count = grid_cells;
    explicit World(std::uint32_t seed = 0x57EF'A023U, PhysicsConfig config = {});
    void clear();
    void load_demo() { load_scene(Scene::demonstration); }
    void load_scene(Scene scene);
    void step(ScanMode mode = ScanMode::active_chunks);
    void paint(Point from, Point to, int radius, Material material, bool erase = false);
    bool set(Point position, Material material);
    bool set(Point position, Material material, double kelvin, double fill = 1);
    double inject_heat(Point position, double energy);
    void set_temperature(Point position, double kelvin);
    void projectile_hit(Point position);
    std::uint64_t queue_explosion(Point center, int radius = 9);
    std::uint64_t queue_explosion(ExplosionCommand command);
    [[nodiscard]] Cell at(Point position) const noexcept;
    [[nodiscard]] FluidSample sample_fluid(Point position)const noexcept;
    [[nodiscard]] PhysicalSample sample(Point position) const noexcept;
    [[nodiscard]] const Statistics& statistics() const noexcept { return state_.report; }
    [[nodiscard]] const Budget& budget() const noexcept { return state_.budget; }
    [[nodiscard]] const PhysicsConfig& config() const noexcept { return state_.config; }
    [[nodiscard]] std::span<const PhysicsEvent> events() const noexcept { return state_.events; }
    [[nodiscard]] std::size_t count(Material material) const noexcept;
    [[nodiscard]] double mass_of(Material material) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] bool chunk_awake(int index) const noexcept;
    void render(std::span<Color, cell_count> pixels, bool transparent_empty = false,
        RenderMode mode = RenderMode::materials) const;
    // Test/preset authoring controls; no UI or clock is required by the model.
    void set_oxygen(double fraction);
    void set_open_top(bool open) noexcept { state_.config.open_top = open; }
private:
    void rectangle(Point a, Point b, Material material);
    void erase_cell(int index);
    detail::State state_;
};
} // namespace strefa::sim
