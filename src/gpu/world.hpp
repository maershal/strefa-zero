#pragma once
#include "gpu/mission.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace strefa::gpu {
inline constexpr int width=320,height=192,capacity=32768;
inline constexpr double step_seconds=1.0/120.0;
enum class Scene { avalanche, rain, mixed, empty, water_tank, dam_break, connected_vessels, oil_water, sand_water, hourglass, inverted_oil, oil_drop };
enum class Material { erase, sand, ash, metal, wall, water, oil };
// Explicit std430 ABI. State stays on the GPU between edits, simulation and drawing.
struct alignas(16) Grain {
    std::array<float,4> position{}; // current xy, previous contact-substep xy
    std::array<float,4> motion{};   // velocity xy, inverse mass, material
    std::array<float,4> contact{};  // static normal xy, previous presentation-tick xy
    std::array<float,4> residual{}; // compensated position xy, previous substep residual zw
};
static_assert(sizeof(Grain)==64);
struct Stroke { float x0{},y0{},x1{},y1{},radius{4};Material material{Material::sand}; };
struct Metrics { double gpu_ms{},render_ms{},cpu_submit_ms{};std::uint64_t steps{};unsigned grains{},water{},oil{};float max_compression{};std::uint64_t timing_samples{}; };

// Owns GL resources; all methods (including destruction) require the owning
// context on its thread. No GL dependency leaks into the CPU reference model.
class ParticleWorld {
public:
    static std::expected<std::unique_ptr<ParticleWorld>,std::string> create(const std::filesystem::path& shaders);
    ~ParticleWorld();
    ParticleWorld(const ParticleWorld&)=delete;
    ParticleWorld& operator=(const ParticleWorld&)=delete;
    void reset(Scene scene);
    void paint(const Stroke& stroke);
    void step();
    void step(const MissionInput& input);
    void reset_mission();
    void queue_explosion(const Explosion& event); // monotonically increasing nonzero ID per world generation
    void configure_mission(const MissionConfig& config); // attach to a loaded validation scene
    const MissionSnapshot& mission() const noexcept;
    MissionSnapshot read_mission() const; // synchronous validation only
    std::vector<std::uint32_t> read_terrain() const; // validation only
    void set_actor_textures(unsigned parts,unsigned weapons,int weapon_width,int weapon_height);
    void render(float alpha);
    void poll(); // nonblocking timer/fence collection; no full-state readback
    unsigned texture() const noexcept;
    const Metrics& metrics() const noexcept;
    // Explicit validation/scene-loading boundary, never used by the frame loop.
    void load(std::span<const Grain> grains,std::span<const std::uint32_t> walls);
    std::vector<Grain> readback() const;
    void save_image(const std::filesystem::path& path) const;
    std::vector<double> take_timings();
    std::vector<double> take_render_timings();
    std::vector<double> take_cpu_timings();
    std::vector<std::array<float,4>> read_fluid_terms();
    void project_liquids_for_test(unsigned iterations);
private:
    struct Impl;
    ParticleWorld();
    std::unique_ptr<Impl> impl_;
};
MissionInput scripted_mission_input(unsigned tick); // regression route, also used by UI smoke test
std::expected<void,std::string> validate_mission(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> validate_granular(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> validate_liquids(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> validate_flow(ParticleWorld&,const std::filesystem::path& output);
}
