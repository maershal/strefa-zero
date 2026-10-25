#pragma once
#include "sim/material.hpp"
#include "sim/executor.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace strefa::sim {
struct Point { int x{}, y{}; bool operator==(const Point&) const = default; };
struct Vector { float x{}, y{}; };
inline constexpr int grid_width = 320, grid_height = 192, grid_cells = grid_width * grid_height;
inline constexpr int horizontal_faces = (grid_width + 1) * grid_height;
inline constexpr int vertical_faces = grid_width * (grid_height + 1);
inline constexpr double tick_duration = 1.0 / 60.0, ambient_temperature = 293.15;
constexpr bool contains(Point p) noexcept { return p.x >= 0 && p.y >= 0 && p.x < grid_width && p.y < grid_height; }
constexpr int cell_index(Point p) noexcept { return p.y * grid_width + p.x; }
constexpr int u_index(int x, int y) noexcept { return y * (grid_width + 1) + x; }
constexpr int v_index(int x, int y) noexcept { return y * grid_width + x; }
inline constexpr std::array liquid_materials{Material::water, Material::oil, Material::acid,
    Material::lava, Material::nitroglycerin, Material::molten_metal};
constexpr int liquid_index(Material m) noexcept {
    for (int i = 0; i < static_cast<int>(liquid_materials.size()); ++i) if (liquid_materials[i] == m) return i;
    return -1;
}
enum class Gas : unsigned { inert, oxygen, steam, smoke, fuel, products, count };
inline constexpr unsigned gas_count = static_cast<unsigned>(Gas::count);
constexpr unsigned gi(Gas g) noexcept { return static_cast<unsigned>(g); }
constexpr double gas_cp(Gas g) noexcept { return g == Gas::steam ? 2 : g == Gas::fuel ? 1.5 : 1; }
constexpr double gas_offset(Gas g) noexcept { return g == Gas::steam ? enthalpy_offset(Material::steam) : 0; }

struct PhysicsConfig {
    bool open_top{true};
    bool thermal{true}, fluids{true}, air{true}, reactions{true}, motion{true};
    bool reference_pressure{};
    bool modified_cholesky{true};
    bool coarse_pressure{true};
    unsigned worker_threads{}; // 0 = automatic (1..2 total), 1 = serial, max 4.
    double gravity{180};
    double sound_speed{40}; // Cells/s, deliberately slowed acoustic model.
    double air_density{1}, pressure_damping{.8}, velocity_damping{1.2};
    double heat_contact{.35};
    double contact_resistance{.05};
    double cfl{.45}, liquid_cfl{.9}, pressure_tolerance{1e-4};
    int pressure_iterations{200};
};
struct Budget {
    double initial_mass{}, initial_energy{};
    double edited_mass{}, edited_energy{}, injected_heat{}, chemical_heat{};
    double escaped_mass{}, escaped_energy{}, incoming_mass{}, incoming_energy{}, radiation{};
    double injected_mass{}, source_energy{};
};
struct Statistics {
    std::uint64_t tick{};
    std::size_t particles{}, pending_explosions{};
    int active_chunks{}, thermal_chunks{}, reactive_chunks{}, visited_cells{}, moves{};
    int pressure_iterations{}, air_substeps{}, liquid_substeps{};
    unsigned worker_threads{1};
    double pressure_residual{}, max_divergence{}, max_overfill{};
    double mass{}, energy{}, mass_error{}, energy_error{};
    double escaped_mass{}, escaped_energy{}, radiation{};
    bool pressure_converged{true}, finite{true};
    double heat_ms{}, air_ms{}, liquid_ms{}, reaction_ms{}, motion_ms{}, bookkeeping_ms{};
    double pressure_ms{}, transport_ms{},air_wave_ms{},air_transport_ms{},thermal_refresh_ms{};
};
struct PhysicalSample {
    Material material{Material::empty};
    double temperature{ambient_temperature}, air_temperature{ambient_temperature};
    double pressure{}, liquid_pressure{}, liquid_fill{}, mass{}, fuel{}, oxygen{}, dissolved{};
    Vector air_velocity{};
    Vector liquid_velocity{};
    std::array<double, liquid_materials.size()> liquid_fractions{};
};
enum class RenderMode { materials, temperature, pressure, velocity, oxygen, activity };
enum class PhysicsEventKind { explosion, ignition, phase_change };
struct PhysicsEvent { std::uint64_t id{}, tick{}; PhysicsEventKind kind{}; Point position{}; float strength{}; };
struct ExplosionCommand {
    Point center{};
    int radius{9};
    double energy{1800}, pressure{14}, impulse{32};
    std::uint64_t id{};
    bool chemical{};
    double gas_mass{.15};
};

namespace detail {
struct PressureRow {
    int left{},right{},up{},down{};
    double a_left{},a_right{},a_up{},a_down{},diagonal{},inverse_factor{};
};
struct CompactPressure {
    struct Coarse {
        std::vector<int> group;
        std::vector<double> factor,rhs,solution;
        int count{},band{};
        bool ready{};
    } coarse;
    std::vector<int> index;
    std::vector<PressureRow> rows;
    std::vector<double> x,b,r,z,direction,product;
    void allocate();
};
struct LiquidFields {
    struct Transfer { int from{},to{};double volume{}; };
    std::vector<Transfer> transfers;
    std::vector<int> component;
    std::vector<int> closed_cells,closed_ends;
    std::array<bool,liquid_materials.size()> present{};
    bool solutes{};
    int min_x{},max_x{},min_y{},max_y{};
    CompactPressure compact;
    std::array<std::vector<double>, liquid_materials.size()> volume, energy;
    std::vector<float> u, v, next_u, next_v;
    std::vector<unsigned char> velocity_valid,velocity_next_valid;
    std::vector<double> pressure, density, fill, capacity, viscosity;
    std::vector<double> diagonal, right, down, rhs, r, z, direction, product, inverse_factor;
    std::vector<int> wet;
    std::vector<unsigned char> mask, anchor;
    std::vector<double> delta_volume, delta_energy, outflow;
    void allocate();
};
struct AirFields {
    struct Flow { int from{},to{};double fraction{}; };
    std::vector<Flow> flows;
    std::array<std::vector<double>,gas_count+1> delta;
    std::array<std::vector<double>,gas_count> diffusion_energy;
    std::vector<unsigned char> open;
    std::array<std::vector<double>, gas_count> mass;
    std::vector<double> energy, temperature, capacity, pressure_source;
    std::vector<float> pressure, next_pressure, u, v, next_u, next_v;
    std::vector<float> buoyancy;
    std::vector<double> scalar_delta, outflow;
    void allocate();
};
struct State {
    Executor executor;
    struct HeatFlux {int a{},b{};double heat{};};
    std::vector<HeatFlux> heat_fluxes;
    std::vector<double> heat_load,heat_delta;
    std::vector<Material> material;
    std::vector<std::uint8_t> shade, loose, burning;
    std::vector<double> mass, energy, fuel, acid_capacity, dissolved, temperature, capacity;
    std::vector<float> vx, vy, offset_x, offset_y;
    std::vector<float> shock; // One-tick condensed-phase shock load; not hydrostatic pressure.
    std::vector<std::uint64_t> updated;
    std::array<bool, 240> movement_awake{}, thermal_awake{}, reaction_awake{}, next_awake{};
    LiquidFields liquid;
    AirFields air;
    PhysicsConfig config;
    Budget budget;
    Statistics report;
    std::vector<PhysicsEvent> events;
    std::vector<ExplosionCommand> explosions;
    std::uint64_t next_event{1};
    std::uint32_t seed{};
    bool air_awake{}, heat_awake{}, chemistry_awake{};
    bool thermal_dirty{true}, cached_finite{true};
    double cached_mass{}, cached_energy{};
    std::size_t cached_particles{};
    void allocate();
};
double matter_mass(const State& s, int i) noexcept;
double matter_energy(const State& s, int i) noexcept;
double gas_mass(const State& s, int i) noexcept;
double total_mass(const State& s) noexcept;
double total_energy(const State& s) noexcept;
double solid_fraction(const State& s, int i) noexcept;
double liquid_fill(const State& s, int i) noexcept;
bool barrier(const State& s, int i) noexcept;
Material dominant(const State& s, int i) noexcept;
void wake(State& s, int i) noexcept;
void refresh_thermal(State& s);
void add_matter_heat(State& s, int i, double heat);
void phase_changes(State& s);
void conduct_heat(State& s, double dt);
void solve_liquids(State& s, double dt);
void solve_compact_pressure(State& s);
void solve_air(State& s, double dt);
void react_materials(State& s, double dt);
void move_powders(State& s, double dt, bool full_scan);
void detonate(State& s, const ExplosionCommand& explosion);
void update_report(State& s);
double visibility(const State& s, Point from, Point to) noexcept;
void convert_solid(State& s, int i, Material material);
std::uint32_t random(const State& s, int i, unsigned salt) noexcept;
} // namespace detail
} // namespace strefa::sim
