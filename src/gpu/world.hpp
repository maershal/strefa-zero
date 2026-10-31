#pragma once
#include "gpu/mission.hpp"
#include "gpu/vfx.hpp"
#include "gpu/thermal.hpp"
#include "gpu/render_options.hpp"
#include "game/gameplay_session.hpp"
#include "gpu/shader_cache.hpp"
#include "gpu/profiling.hpp"
#include "gpu/diagnostics.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace strefa::gpu {
class RenderDevice;
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
struct Metrics { double gpu_ms{},render_ms{},cpu_submit_ms{},gameplay_ms{};std::uint64_t steps{};unsigned grains{},water{},oil{};float max_compression{};std::uint64_t timing_samples{}; };

struct NeighborSnapshot {
    float displacement{};unsigned builds{};
    std::vector<std::uint32_t> starts,counts,order,indices;
    std::vector<std::array<float,4>> anchors;
};
struct BodySnapshot {
    std::array<std::uint32_t,4> queue{},stats{};
    std::array<float,4> last_sum{},total_sum{};
    std::array<std::uint32_t,6> commands{};
};
struct HotAudit {bool indexed{};unsigned active{},mismatches{};};
static_assert(offsetof(BodySnapshot,commands)==64&&sizeof(BodySnapshot)==88);
// Owns GL resources; all methods (including destruction) require the owning
// context on its thread. No GL dependency leaks into the CPU reference model.
class ParticleWorld {
public:
    static std::expected<std::unique_ptr<ParticleWorld>,std::string> create(const std::filesystem::path& shaders,const ExecutionOptions& execution={},const ProfileOptions& profiling={},const std::filesystem::path& shader_cache={},std::shared_ptr<RenderDevice> device={});
    ~ParticleWorld();
    ParticleWorld(const ParticleWorld&)=delete;
    ParticleWorld& operator=(const ParticleWorld&)=delete;
    void reset(Scene scene);
    void paint(const Stroke& stroke);
    bool step(); // false: backpressure, no tick/input/resource consumed
    bool step(const MissionInput& input);
    bool ready_for_step();
    void cancel_mission_input();
    const game::GameplaySession& gameplay()const noexcept;
    std::array<std::uint64_t,7> event_stats()const noexcept;
    std::expected<void,std::string> validate_gameplay_protocol(const std::filesystem::path&);
    void reset_mission();
    void queue_explosion(const Explosion& event); // monotonically increasing nonzero ID per world generation
    void configure_mission(const MissionConfig& config); // attach to a loaded validation scene
    void restore_coupling_checkpoint(const MissionSnapshot&,std::uint64_t engine_tick); // validation only, after load(), no projectiles/events
    const MissionSnapshot& mission() const noexcept;
    MissionSnapshot read_mission() const; // synchronous validation only
    std::vector<std::uint32_t> read_terrain() const; // validation only
    void set_actor_textures(unsigned parts,unsigned weapons,int weapon_width,int weapon_height);
    void render(float alpha);
    std::expected<void,std::string> run_vfx_benchmark(const std::filesystem::path&);
    std::shared_ptr<RenderDevice> render_device()const;
    std::expected<void,std::string> run_thermal_benchmark(const std::filesystem::path&);
    void reset_thermal_scene(unsigned scene);
    void set_thermal_options(ThermalOptions);
    ThermalOptions thermal_options()const;
    void set_thermal_view(ThermalView);
    void set_thermal_brush(const ThermalBrush&);
    const ThermalTotals& thermal_totals()const;
    ThermalSnapshot read_thermal(); // Explicit synchronous diagnostics only.
    void request_thermal_inspection(int x,int y);
    const ThermalInspection& thermal_inspection()const;
    std::uint64_t thermal_memory()const;
    std::expected<void,std::string> validate_thermal(const std::filesystem::path&,bool benchmark=false);
    void set_vfx_options(VfxOptions);
    VfxOptions vfx_options()const noexcept;
    const VfxCounters& vfx_stats()const;
    std::expected<void,std::string> validate_vfx(const std::filesystem::path&,bool benchmark=false);
    std::expected<void,std::string> validate_lighting(const std::filesystem::path&,bool benchmark=false);
    std::array<float,2> camera_offset()const;
    void set_render_options(RenderOptions);
    RenderOptions render_options()const noexcept;
    void set_background_texture(unsigned);
    void invalidate_gl_state();
    std::vector<RenderSample> take_render_samples();
    std::expected<void,std::string> validate_renderer(const std::filesystem::path&,bool benchmark=false);

    void poll(bool collect_hud=true); // frame-level HUD collection; false polls only GPU timers
    unsigned texture() const noexcept;
    const Metrics& metrics() const noexcept;
    // Explicit validation/scene-loading boundary, never used by the frame loop.
    void load(std::span<const Grain> grains,std::span<const std::uint32_t> walls);
    std::vector<Grain> readback() const;
    NeighborSnapshot read_neighbors(); // synchronous validation only
    std::array<std::uint32_t,240> read_phase_mask(); // same anchor generation, validation only
    HotAudit audit_hot_view(); // no rebuild: compare current cache bank against authoritative ID state
    void start_diagnostic_trace(const TraceConfig& config); // CLI only, off by default
    TraceReport read_diagnostic_trace(); // explicit GPU drain required; no index rebuild
    void capture_diagnostic_snapshot(std::string label); // current generation only, no rebuild
    std::vector<std::array<float,4>> audit_neighbors(bool pairs=true); // <=256 stable IDs for pair audit
    void displace_for_test(std::span<const std::array<float,4>> deltas);
    BodySnapshot read_body(); // synchronous validation only
    void inject_body_for_test(std::span<const std::array<float,4>> reactions,bool apply=true);
    void save_image(const std::filesystem::path& path) const;
    std::vector<double> take_timings();
    std::vector<double> take_render_timings();
    std::vector<double> take_cpu_timings();
    void set_profile_options(const ProfileOptions& options);
    ProfileOptions profile_options() const noexcept;
    const StepProfile& step_profile() const noexcept;
    PollTimings poll_timings() const noexcept;
    CallCounters counters() const noexcept;
    ReadbackStats readback_stats() const noexcept;
    UploadStats upload_stats() const noexcept;
    ShaderCacheStats shader_cache_stats() const noexcept;
    ExecutionOptions execution_options() const noexcept;
    std::array<bool,5> pbf_restrict_programs() const; // validation only
    std::array<std::string,5> pbf_program_sources_for_test() const; // source retrieved from attached GL shaders
    void probe_pbf_alias_for_test(unsigned operation,unsigned source,unsigned binding); // must reject before GL, source 8 = unbound
    std::array<std::uint32_t,37> read_dispatch_commands() const; // validation only
    std::vector<StepProfile> take_step_profiles();
    std::vector<GroupProfile> take_group_profiles();
    std::vector<std::array<float,4>> read_fluid_terms();
    void project_liquids_for_test(unsigned iterations);
    // Explicit synchronous diagnostics, never used by the interactive loop.
    std::vector<float> read_boundary_weights() const;
    std::vector<std::array<float,4>> fluid_fields_for_test();
    void apply_fluid_velocities_for_test();
private:
    struct Impl;
    explicit ParticleWorld(std::shared_ptr<RenderDevice>);
    std::unique_ptr<Impl> impl_;
};
MissionInput scripted_mission_input(unsigned tick); // regression route, also used by UI smoke test
std::expected<void,std::string> validate_mission(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> validate_granular(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> validate_fluid_quality(const std::filesystem::path& shaders,const std::filesystem::path& output,const ExecutionOptions&,bool benchmark);
std::expected<void,std::string> validate_liquids(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> validate_flow(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> profile_gpu(ParticleWorld&,const std::filesystem::path& output,bool check_only=false);
void write_profile_samples(ParticleWorld&,const std::filesystem::path& output);
std::expected<void,std::string> profile_stabilization(ParticleWorld&,const std::filesystem::path& output,const ProfileOptions&);
std::expected<void,std::string> validate_transport(const std::filesystem::path& output);
std::expected<void,std::string> validate_hot_reads(const std::filesystem::path& output,const ExecutionOptions& layout={});
std::expected<void,std::string> profile_cost(const std::filesystem::path& output,const ExecutionOptions&,CostMode,bool check_only=false);
std::expected<void,std::string> validate_work(const std::filesystem::path& output,const ExecutionOptions& layout={});
std::expected<void,std::string> validate_coupling(const std::filesystem::path& output,bool benchmark=false,bool reference_control=false,bool legacy_check=false,const ExecutionOptions& layout={});
void validate_coupling_replay(const std::filesystem::path& output,std::ostream& checks,const ExecutionOptions& layout);
std::expected<void,std::string> validate_neighbors(const std::filesystem::path& output,bool benchmark=false);
std::expected<void,std::string> validate_execution(const std::filesystem::path& output,bool benchmark=false);
std::expected<void,std::string> validate_pbf_restrict(const std::filesystem::path& output);
std::expected<void,std::string> validate_readback(const std::filesystem::path& output);
std::expected<void,std::string> profile_readback(const std::filesystem::path& output);
}
