#include "gpu/world_impl.hpp"
#include "gpu/world_test_access.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace strefa::gpu {
namespace {
using detail::Api, detail::WorldAccess, detail::ParticleAliasError, detail::hash, detail::seeded_position;
} // namespace
ParticleWorld::ParticleWorld(std::shared_ptr<RenderDevice> device) : impl_{std::make_unique<Impl>(std::move(device))} {}
ParticleWorld::~ParticleWorld() = default;
std::expected<std::unique_ptr<ParticleWorld>, std::string>
ParticleWorld::create(const std::filesystem::path& shaders, const ExecutionOptions& execution,
                      const ProfileOptions& profiling, const std::filesystem::path& shader_cache,
                      std::shared_ptr<RenderDevice> device) {
    GLint major{}, minor{};
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3))
        return std::unexpected{"Laboratorium GPU wymaga OpenGL 4.3. Model referencyjny (CPU) jest dostępny."};
    try {
        const bool standalone = !device;
        auto resources = device ? std::move(device) : std::make_shared<RenderDevice>(shaders, shader_cache);
        if (standalone)
            resources->vfx.set_options(
                    {VfxQuality::off, false}); // Legacy physics callers opt into presentation explicitly.
        auto world = std::unique_ptr<ParticleWorld>{new ParticleWorld{std::move(resources)}};
        auto& s = *world->impl_;
        s.device.require_current();
        // Apply transport/timer policy before the first reset or staging copy.
        // Group query objects are allocated by set_profile_options below.
        s.profile_options = profiling;
        s.profile_options.group_timers = false;
        s.gl.observe(profiling.collect ? &s.calls : nullptr);
        s.uploads.configure(execution.storage);
        s.debug_groups = s.device.debug_groups;
        const auto valid_size = [](unsigned size) { return size == 64 || size == 128 || size == 256; };
        if (!valid_size(execution.local_size) || !valid_size(execution.reduction_size)
            || !valid_size(execution.effective_pbf_size())
            || (!execution.specialized && execution.local_size != execution.reduction_size))
            throw std::invalid_argument{
                    "Rozmiar grupy obliczeń musi wynosić 64, 128 lub 256, a połączone shadery wymagają zgodnych rozmiarów."};
        // The shared device configures its program cache before VFX and world programs.
        if (!execution.fluid.valid()) throw std::invalid_argument{"Nieprawidłowa konfiguracja cieczy."};
        s.execution = execution;
        // Mesa D3D12 in WSL can skip a nonzero conditional blast dispatch.
        // Keep active-particle indirect dispatch, but use the existing direct
        // path for optional work. The same fixture passes on native GL/Mesa
        // llvmpipe and with conditional_work=false (see LINUX_WSL_QA.md).
        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (renderer && std::string_view{renderer}.starts_with("D3D12 (") && s.execution.conditional_work) {
            s.execution.conditional_work = false;
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Mesa D3D12: using direct dispatch for conditional GPU work.");
        }
        s.compile_kernels(shaders);
        s.shader_folder = shaders;
        s.renderer.initialize(s.shader_library, shaders, s.layout_defines());
        s.renderer.vfx = &s.resources->vfx;
        s.gl.GenBuffers(static_cast<GLsizei>(s.buffers.size()), s.buffers.data());
        const std::array<std::size_t, 8> sizes{
                capacity * sizeof(Grain),
                capacity * sizeof(Grain),
                s.cell_buffer_size(),
                s.index_buffer_size(),
                static_cast<std::size_t>(width * height * (execution.fluid.boundary == FluidBoundary::volume ? 20 : 16)
                                         + (execution.fluid.boundary == FluidBoundary::volume ? 144 * 4 : 0)),
                capacity * 8 + detail::command_words * 4,
                mission_buffer_size,
                s.terms_buffer_size()};
        for (unsigned i = 0; i < s.buffers.size(); ++i)
            s.uploads.allocate(s.buffers[i], static_cast<GLsizeiptr>(sizes[i]));
        if (s.debug_groups) {
            constexpr std::array names{"particles A",
                                       "particles B",
                                       "cell counts and prefixes",
                                       "sorted IDs, reverse mapping and anchors",
                                       "terrain",
                                       "active/free IDs",
                                       "mission state",
                                       "fluid terms"};
            for (std::size_t i = 0; i < names.size(); ++i) s.gl.ObjectLabel(GL_BUFFER, s.buffers[i], -1, names[i]);
        }
        // Capsule samples translate with the actor. Their shape/volumes are fixed.
        if (execution.fluid.boundary == FluidBoundary::volume) {
            std::array<float, 144> weights{};
            const auto inside = [](int x, int y) {
                const double px = x + .5, py = y + .5;
                const double dy = py - std::clamp(py, -14.0, -4.0);
                return px * px + dy * dy < 16;
            };
            for (int y = -18; y < 0; ++y)
                for (int x = -4; x < 4; ++x)
                    if (inside(x, y)) {
                        double sum = 0;
                        for (int dy = -2; dy <= 2; ++dy)
                            for (int dx = -2; dx <= 2; ++dx)
                                if (inside(x + dx, y + dy)) {
                                    const double q = std::max(0.0, 4.0 - dx * dx - dy * dy);
                                    sum += q * q * q / (64 * 3.141592653589793);
                                }
                        weights[(y + 18) * 8 + x + 4] = static_cast<float>(1.014612762210833 / sum);
                    }
            s.upload(4, width * height * 20, sizeof(weights), weights.data());
        }
        s.readbacks.configure(profiling.readback_mode, s.debug_groups);
        s.event_ring.initialize();
        s.reset_timer_queries(effective_timer_mode(s.profile_options));
        world->set_profile_options(profiling);
        world->reset(Scene::hourglass);
        return world;
    } catch (const std::exception& e) {
        return std::unexpected{e.what()};
    }
}
void ParticleWorld::load(std::span<const Grain> grains, std::span<const std::uint32_t> walls) {
    if (grains.size() > capacity || walls.size() != width * height)
        throw std::invalid_argument{"Nieprawidłowy rozmiar sceny GPU."};
    std::vector<Grain> all(capacity);
    std::ranges::copy(grains, all.begin());
    auto& s = *impl_;
    s.invalidate_bindings();
    for (auto& p : all) {
        p.contact[2] = p.position[0];
        p.contact[3] = p.position[1];
    }
    ++s.readback_revision;
    s.readback_refresh = false;
    s.mission_enabled = false;
    s.snapshot = {};
    s.explosions.clear();
    s.next_generation();
    s.gameplay.reset(s.epoch);
    const std::vector<std::byte> empty(mission_buffer_size);
    s.upload(6, 0, mission_buffer_size, empty.data());
    s.reset_body();
    s.current = 0;
    s.bins_valid = false;
    const std::array<GLuint, 16> neighbor_zero{};
    s.upload(2, 0, sizeof(neighbor_zero), neighbor_zero.data());
    // A loaded scene must not inherit list state from the previous one: free/active slot lists, the sorted
    // order with its reverse map and reuse anchors (or the linked lists). rebuild_active() below refills the
    // slot lists; the index is rebuilt before its first use.
    const std::vector<std::byte> list_zero(std::max<std::size_t>(capacity * 8, s.body_offset()));
    s.upload(5, 0, static_cast<GLsizeiptr>(capacity * 8), list_zero.data());
    s.upload(3, 0, static_cast<GLsizeiptr>(s.body_offset()), list_zero.data());
    s.renderer.reset_profiles();
    s.presented_hp = {3, 3, 3};
    s.hit_started = {};
    s.metrics.steps = 0;
    s.readback_age = 0;
    s.metrics.grains =
            static_cast<unsigned>(std::ranges::count_if(all, [](const Grain& p) { return p.motion[3] > .5F; }));
    s.metrics.gpu_ms = s.metrics.physics_ms = s.metrics.presentation_ms = 0;
    s.metrics.render_ms = 0;
    s.metrics.timing_samples = 0;
    s.metrics.water =
            static_cast<unsigned>(std::ranges::count_if(all, [](const Grain& p) { return p.motion[3] == 5; }));
    s.metrics.oil = static_cast<unsigned>(std::ranges::count_if(all, [](const Grain& p) { return p.motion[3] == 6; }));
    s.metrics.max_compression = 0;
    s.fluids_enabled = s.metrics.water + s.metrics.oil > 0;
    s.grains_enabled = s.metrics.grains > s.metrics.water + s.metrics.oil;
    const std::array<GLuint, 8> counters{0, 0, s.metrics.grains, s.metrics.water, s.metrics.oil, 0, 0, 0};
    s.upload(6, 0, 32, counters.data());
    std::vector<std::array<float, 4>> packed(capacity * 4);
    if (s.execution.soa)
        for (unsigned i = 0; i < capacity; ++i) {
            packed[i] = all[i].position;
            packed[capacity + i] = all[i].motion;
            packed[2 * capacity + i] = all[i].contact;
            packed[3 * capacity + i] = all[i].residual;
        }
    for (unsigned i = 0; i < 2; ++i)
        s.upload(i, 0, static_cast<GLsizeiptr>(all.size() * sizeof(Grain)),
                 s.execution.soa ? static_cast<const void*>(packed.data()) : static_cast<const void*>(all.data()));
    s.upload(4, 0, static_cast<GLsizeiptr>(walls.size_bytes()), walls.data());
    std::vector<std::uint32_t> strength(walls.size());
    for (std::size_t i = 0; i < walls.size(); ++i) strength[i] = walls[i] >= 2 ? 3U : 0U;
    s.upload(4, width * height * 12, static_cast<GLsizeiptr>(strength.size() * 4), strength.data());
    s.dispatch(9, width * height);
    s.rebuild_active();
    if (s.thermal) s.thermal->reset(s.buffers, s.current, s.epoch);
    render(1);
}
void ParticleWorld::reset(Scene scene) {
    std::vector<std::uint32_t> walls(width * height);
    std::vector<Grain> grains;
    auto box = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) walls[y * width + x] = 1;
    };
    box(0, 187, 319, 191);
    box(0, 0, 0, 191);
    box(319, 0, 319, 191);
    if (scene == Scene::hourglass) {
        for (int y = 28; y < 64; ++y)
            for (int x = 100; x < 220; ++x) {
                if (x < 92 + (y - 20) * .87 + 3 || x > 228 - (y - 20) * .87 - 3) continue;
                const auto [px, py] = seeded_position(x, y);
                Grain p;
                p.position = {px, py, px, py};
                p.motion = {0, 0, 1, 1};
                grains.push_back(p);
            }
        load(grains, walls);
        // Exactly the public brush path; the regression exercises user-drawn
        // walls and their edits, not special analytic hourglass collision code.
        for (const auto stroke :
             std::array{Stroke{92, 20, 155, 94, 1, Material::wall}, Stroke{228, 20, 165, 94, 1, Material::wall},
                        Stroke{155, 94, 155, 102, 1, Material::wall}, Stroke{165, 94, 165, 102, 1, Material::wall},
                        Stroke{155, 102, 92, 176, 1, Material::wall}, Stroke{165, 102, 228, 176, 1, Material::wall},
                        Stroke{92, 176, 228, 176, 1, Material::wall}})
            paint(stroke);
        return;
    }
    if (scene == Scene::inverted_oil || scene == Scene::oil_drop) {
        box(111, 100, 111, 164);
        box(208, 100, 208, 164);
        box(111, 164, 208, 164);
        for (int y = 124; y < 164; ++y)
            for (int x = 112; x < 208; ++x) {
                const auto [px, py] = seeded_position(x, y);
                const bool oil = scene == Scene::inverted_oil ? py >= 144 + 2 * std::sin((px - 112) * 6.2831853F / 96)
                                                              : std::hypot(px - 160, py - 150) < 8;
                Grain p;
                p.position = {px, py, px, py};
                p.motion = {0, 0, oil ? 3.3125F : 2.65F, oil ? 6.F : 5.F};
                grains.push_back(p);
            }
        load(grains, walls);
        return;
    }
    if (scene >= Scene::water_tank) {
        auto fill = [&](int x0, int y0, int nx, int ny, int type) {
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    const float px = x0 + x + .5F, py = y0 + y + .5F;
                    if (walls[static_cast<int>(py) * width + static_cast<int>(px)]) continue;
                    Grain p;
                    p.position = {px, py, px, py};
                    p.motion = {0, 0, type == 6 ? 3.3125F : (type == 5 ? 2.65F : 1.F), static_cast<float>(type)};
                    grains.push_back(p);
                }
        };
        box(40, 85, 40, 186);
        box(279, 85, 279, 186);
        if (scene == Scene::water_tank) fill(41, 167, 238, 20, 5);
        if (scene == Scene::dam_break) {
            fill(42, 100, 54, 86, 5);
            box(210, 158, 211, 186);
        }
        if (scene == Scene::connected_vessels) {
            box(156, 80, 157, 173);
            fill(41, 133, 115, 54, 5);
            fill(158, 175, 121, 12, 5);
        }
        if (scene == Scene::oil_water) {
            fill(41, 171, 238, 16, 5);
            fill(41, 159, 238, 12, 6);
        }
        if (scene == Scene::sand_water) {
            fill(41, 169, 238, 18, 5);
            fill(120, 100, 50, 20, 1);
        }
    } else if (scene != Scene::empty) {
        box(68, 118, 142, 119);
        box(191, 148, 265, 149);
        for (int x = 30; x < 92; ++x) {
            const int y = 135 + (x - 30) / 3;
            box(x, y, x, y + 1);
        }
        const int columns = scene == Scene::rain ? 220 : 190, rows = scene == Scene::rain ? 20 : 50;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                const auto seed = hash(static_cast<unsigned>(y * columns + x + 1));
                const float px = 45.F + x * 1.04F + static_cast<float>(seed & 255U) / 2550.F;
                const float py = 8.F + y * 1.04F + static_cast<float>((seed >> 8) & 255U) / 2550.F;
                const int type = scene == Scene::mixed ? 1 + (x / 64) % 3 : 1;
                Grain p;
                p.position = {px, py, px, py};
                p.motion = {0, 0, type == 1 ? 1.F : (type == 2 ? 4.F : .35F), static_cast<float>(type)};
                grains.push_back(p);
            }
    }
    load(grains, walls);
}
void ParticleWorld::paint(const Stroke& stroke) {
    auto& s = *impl_;
    if (s.thermal && s.thermal_config.enabled
        && (stroke.material == Material::erase || stroke.material == Material::wall))
        s.thermal->erase(s.buffers, s.current,
                         {{stroke.x0, stroke.y0, stroke.x1, stroke.y1}, stroke.radius, 0, ThermalTool::none});
    s.invalidate_bindings();
    s.bins_valid = false;
    ++s.readback_revision;
    s.brush = stroke;
    const std::array<GLuint, 8> zero{};
    s.upload(6, 0, 32, zero.data());
    // Conservative flags: an asynchronous counter must never disable a solver
    // after a newer edit. Reset/load can clear them from known scene state.
    if (stroke.material == Material::water || stroke.material == Material::oil) s.fluids_enabled = true;
    if (stroke.material >= Material::sand && stroke.material <= Material::metal) s.grains_enabled = true;
    s.dispatch(5, capacity, true);
    s.rebuild_active();
    if (stroke.material == Material::wall || stroke.material == Material::erase) {
        s.dispatch(7, width * height);
        s.dispatch(9, width * height);
    } else {
        s.bins();
        s.dispatch(8, width * height, false, true);
    }
    s.rebuild_active();
    s.bins_valid = false;
    s.stage_count(true);
}
bool ParticleWorld::ready_for_step() {
    auto& s = *impl_;
    if (!s.hold_events) s.event_ring.poll(s.gameplay);
    if (!s.mission_enabled || s.event_ring.available()) return true;
    glFlush();
    return false;
}
void ParticleWorld::cancel_mission_input() {
    impl_->controls = {};
    impl_->gameplay.cancel_input();
}
const game::GameplaySession& ParticleWorld::gameplay() const noexcept {
    return impl_->gameplay;
}
std::array<std::uint64_t, 7> ParticleWorld::event_stats() const noexcept {
    const auto& s = impl_->event_ring.stats();
    return {s.submitted, s.acknowledged, s.stale, s.pending, s.max_pending, s.backpressure, s.max_age_ticks};
}
bool ParticleWorld::step(const MissionInput& input) {
    impl_->controls = input;
    return step();
}
bool ParticleWorld::step() {
    STREFA_ZONE("ParticleWorld::step");
    const auto started = SDL_GetTicksNS();
    auto& s = *impl_;
    const auto initial_calls = s.calls;
    const auto logic_before = s.event_ring.stats().logic_ms;
    if (!ready_for_step()) return false;
    STREFA_GPU_ZONE("physics tick");
    s.metrics.gameplay_ms = 0;
    s.invalidate_bindings();
    s.trace_step = true;
    s.trace.begin_step(s.metrics.steps + 1);
    if (s.debug_groups) s.gl.PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, -1, "Physics tick");
    const auto stamp = [&]() { return s.profile_options.collect ? SDL_GetTicksNS() : std::uint64_t{0}; };
    const auto elapsed = [](std::uint64_t a, std::uint64_t b) { return static_cast<double>(b - a) / 1e6; };
    // Physics consumes GPU state directly. Only diagnostic legacy modes retain
    // per-tick HUD polling; normal HUD collection is once per UI frame.
    const auto mode = s.readbacks.stats().mode;
    const auto poll_start = stamp();
    poll(mode == ReadbackMode::legacy || mode == ReadbackMode::stream);
    const auto poll_end = stamp();
    const bool timers_enabled = effective_timer_mode(s.profile_options) != TimerMode::off;
    auto& timer = s.timers[s.timer_cursor];
    const bool measure = timers_enabled && !timer.pending;
    auto& group = s.group_timers[s.group_cursor];
    const bool grouped = timers_enabled && s.profile_options.group_timers && !group.pending;
    if (grouped) {
        if (s.profile_options.collect) ++s.calls.timer_submissions;
        group.tick = s.metrics.steps + 1;
        group.generation = s.epoch;
        s.gl.QueryCounter(group.ids[0], GL_TIMESTAMP);
    }
    if (s.profile_options.collect && timers_enabled) {
        if (measure)
            ++s.calls.timer_submissions;
        else
            ++s.calls.timer_skips;
    }
    const detail::ElapsedQueryGuard elapsed_guard{s.gl, s.elapsed_active};
    if (measure) s.begin_timer(timer);
    double upload_ms = 0;
    const auto prepare_start = stamp();
    if (s.mission_enabled) {
        STREFA_ZONE("prepare mission");
        const auto logic_started = SDL_GetTicksNS();
        const auto commands =
                s.gameplay.prepare({s.controls.move, s.controls.aim_x, s.controls.aim_y, s.controls.buttons});
        s.metrics.gameplay_ms = static_cast<double>(SDL_GetTicksNS() - logic_started) / 1e6;
        s.upload(6, command_offset, sizeof(commands), &commands);
    }
    const auto tick = s.mission_enabled ? s.gameplay.tick() : static_cast<unsigned>(s.metrics.steps + 1);
    const detail::BlastTick blast{s.mission_enabled, s.execution.batched_blasts, s.epoch, tick, s.execution.local_size};
    detail::PassContext context{s.buffers, s.current, s.execution.conditional_work};
    if (s.explosions.pending()) {
        const auto upload_start = stamp();
        s.explosions.upload(context, s.uploads, blast);
        upload_ms = elapsed(upload_start, stamp());
    }
    if (s.mission_enabled) s.mission_dispatch(0);
    const bool events = s.explosions.apply(context, s, blast);
    const auto prepare_end = stamp();
    if (grouped) s.gl.QueryCounter(group.ids[1], GL_TIMESTAMP);
    // At |v|<=80 each half-tick travels <=1/3 cell. Two approaching,
    // nonoverlapping radius-.46 grains cannot exchange centers undetected.
    for (int half = 0; half < static_cast<int>(s.execution.fluid.substeps); ++half) {
        STREFA_ZONE("solver substep");
        s.trace.context(half + 1, -1);
        s.bins_valid = false;
        s.half_step = half;
        if (s.mission_enabled) s.mission_dispatch(1);
        s.dispatch(1, capacity, true);
        const int iterations = static_cast<int>(s.grains_enabled ? s.execution.fluid.contact_iterations
                                                                 : s.execution.fluid.pbf_iterations);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            s.trace.context(half + 1, iteration);
            s.bins();
            if (s.fluids_enabled && iteration < static_cast<int>(s.execution.fluid.pbf_iterations)) {
                s.liquid_dispatch(0);
                s.liquid_dispatch(1, true);
                if (s.mission_enabled && (s.execution.coupling != CouplingMode::combined || !s.grains_enabled))
                    s.mission_dispatch(3);
            }
            if (s.grains_enabled) {
                if (s.fluids_enabled && iteration < static_cast<int>(s.execution.fluid.pbf_iterations)) s.bins();
                s.dispatch(3, capacity, true);
            }
            if (s.mission_enabled) s.mission_dispatch(3);
        }
        s.trace.context(half + 1, iterations);
        s.bins();
        s.dispatch(4, capacity, true);
        if (s.fluids_enabled) {
            if (s.execution.fluid.fields()) {
                s.liquid_dispatch(5);
                s.liquid_dispatch(6);
            }
            s.liquid_dispatch(4, true);
            // Apply adhesion before contact drag. The reference reducer shares
            // one reaction channel; consuming here prevents it being overwritten.
            if (s.mission_enabled && s.execution.fluid.velocity_reaction()) s.mission_dispatch(3);
        }
        if (s.mission_enabled) {
            if (s.fluids_enabled) {
                s.mission_dispatch(8, capacity, true, true);
                s.mission_dispatch(3);
            }
            s.mission_dispatch(4);
        }
        if (grouped && (half == 0 || half == static_cast<int>(s.execution.fluid.substeps) - 1))
            s.gl.QueryCounter(group.ids[half == 0 ? 2 : 3], GL_TIMESTAMP);
    }
    const auto solver_end = stamp();
    s.trace.context(3, -1);
    const bool diagnostic_due = s.profile_options.periodic_diagnostics && s.fluids_enabled && s.metrics.steps % 30 == 0;
    if (diagnostic_due) {
        s.bins();
        s.liquid_dispatch(3);
        s.liquid_dispatch(2);
    }
    if (s.thermal && s.thermal_config.enabled) {
        if (events) s.thermal->consume_events(s.buffers, s.epoch, tick);
        s.thermal->tick(s.buffers, s.current, static_cast<unsigned>(s.metrics.steps + 1), s.thermal_brush);
        s.invalidate_bindings();
        s.rebuild_active();
        s.renderer.lighting.prepare_birth(s.buffers, s.current);
        s.invalidate_bindings();
        s.thermal->condense(s.buffers, s.current);
        s.invalidate_bindings();
        s.rebuild_active();
        s.dispatch(9, width * height);
        s.bins_valid = false;
        s.grains_enabled = s.fluids_enabled = true;
    }
    const auto diagnostics_end = stamp();
    if (grouped) s.gl.QueryCounter(group.ids[4], GL_TIMESTAMP);
    if (measure) s.end_timer(timer);
    const auto timer_closed = stamp();
    if (s.thermal && s.thermal_config.enabled) s.thermal->presentation(s.renderer.lighting.thermal_image);
    s.renderer.lighting.tick(s.buffers, s.current, s.epoch, tick, events, s.renderer.terrain_image,
                             s.thermal && s.thermal_config.enabled ? s.renderer.lighting.thermal_image : 0,
                             s.thermal && s.thermal_config.enabled ? s.thermal->presentation_particles() : 0);
    s.invalidate_bindings();
    if (s.resources->vfx.enabled()) {
        auto& vfx = s.resources->vfx;
        vfx.begin_tick(s.renderer.terrain_image, s.epoch, tick, 1.F / 120.F);
        s.renderer.build_vfx_field({s.buffers, s.current, s.fluids_enabled, s.mission_enabled});
        vfx.sand_sources(s.renderer.get_options().lighting.quality != LightingQuality::off ? s.renderer.lighting.field
                                                                                           : 0);
        vfx.end_tick(events ? s.buffers[6] : 0, {}, s.fluids_enabled);
        s.invalidate_bindings();
    }
    if (s.mission_enabled) s.event_ring.submit(s.buffers[6], event_offset, s.epoch, s.gameplay.tick());
    ++s.metrics.steps;
    if (s.mission_enabled || diagnostic_due) s.stage_count(!s.mission_enabled);
    if (measure) {
        s.end_presentation_timer(timer);
        s.timer_cursor = (s.timer_cursor + 1) % static_cast<unsigned>(s.timers.size());
    }
    if (grouped) {
        s.gl.QueryCounter(group.ids[5], GL_TIMESTAMP);
        group.pending = true;
        s.group_cursor = (s.group_cursor + 1) % static_cast<unsigned>(s.group_timers.size());
    }
    const auto finished = SDL_GetTicksNS();
    if (s.debug_groups) s.gl.PopDebugGroup();
    s.metrics.gameplay_ms += s.event_ring.stats().logic_ms - logic_before;
    s.metrics.cpu_submit_ms = static_cast<double>(finished - started) / 1e6;
    if (s.cpu_timings.size() < 10000) s.cpu_timings.push_back(s.metrics.cpu_submit_ms);
    if (s.profile_options.collect) {
        auto& p = s.last_profile;
        p = {};
        p.tick = s.metrics.steps;
        p.generation = s.epoch;
        p.calls = s.calls - initial_calls;
        p.total_ms = s.metrics.cpu_submit_ms;
        p.poll_ms = elapsed(poll_start, poll_end);
        p.upload_ms = upload_ms;
        p.prepare_ms = elapsed(prepare_start, prepare_end) - upload_ms;
        p.solver_ms = elapsed(prepare_end, solver_end);
        p.diagnostics_ms = elapsed(solver_end, diagnostics_end);
        p.staging_ms = elapsed(timer_closed, finished);
        p.timer_begin_ms = elapsed(poll_end, prepare_start);
        p.timer_end_ms = elapsed(diagnostics_end, timer_closed);
        p.timer_mode = effective_timer_mode(s.profile_options);
        p.timer_poll_ms = s.poll_timer_ms;
        p.fence_poll_ms = s.poll_fence_ms;
        p.read_ms = s.poll_read_ms;
        p.readback_age_ticks = s.mission_enabled ? s.metrics.steps - s.snapshot.status[2] : s.readback_age;
        if (s.profiles.size() < 10000) s.profiles.push_back(p);
    }
    s.trace_step = false;
    s.trace.end_step();
    return true;
}
void ParticleWorld::poll(bool collect_hud) {
    auto& s = *impl_;
    const auto stamp = [&]() { return s.profile_options.collect ? SDL_GetTicksNS() : std::uint64_t{0}; };
    if (collect_hud && !s.hold_events) s.event_ring.poll(s.gameplay);
    const auto timer_start = stamp();
    s.poll_fence_ms = 0;
    s.poll_read_ms = 0;
    const auto collect = [&](auto& queue, unsigned& head, auto consume) {
        const auto ready = [&](auto& timer, GLuint id) {
            GLint available{};
            s.gl.GetQueryObjectiv(id, GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available) return false;
            consume(timer);
            timer.pending = false;
            return true;
        };
        const auto id = [](const auto& timer) {
            if constexpr (requires { timer.presentation; })
                return timer.presentation ? timer.presentation : timer.id;
            else
                return timer.ids.back();
        };
        if (s.execution.fifo_queries) {
            while (queue[head].pending) {
                if (!ready(queue[head], id(queue[head]))) break;
                head = (head + 1) % static_cast<unsigned>(queue.size());
            }
        } else
            for (auto& timer : queue)
                if (timer.pending) ready(timer, id(timer));
    };
    const auto scalar = [&](auto& queue, unsigned& head, bool rendering) {
        collect(queue, head, [&](auto& timer) {
            GLuint64 ns{}, presentation_ns{};
            s.gl.GetQueryObjectui64v(timer.id, GL_QUERY_RESULT, &ns);
            if (timer.presentation) s.gl.GetQueryObjectui64v(timer.presentation, GL_QUERY_RESULT, &presentation_ns);
            if (timer.mode == TimerMode::timestamp) {
                GLuint64 began{};
                s.gl.GetQueryObjectui64v(timer.start, GL_QUERY_RESULT, &began);
                if (timer.presentation) presentation_ns -= ns;
                ns -= began;
            }
            if (timer.generation != s.epoch) {
                if (s.profile_options.collect) ++s.calls.timer_stale;
                return;
            }
            const auto ms = static_cast<double>(ns) * 1e-6;
            if (rendering) {
                s.metrics.render_ms = ms;
                if (s.render_timings.size() < 10000)
                    s.render_timings.push_back(ms);
                else
                    ++s.metrics.render_timings_dropped;
            } else {
                const auto presentation_ms = static_cast<double>(presentation_ns) * 1e-6;
                s.metrics.physics_ms = ms;
                s.metrics.presentation_ms = presentation_ms;
                s.metrics.gpu_ms = ms + presentation_ms;
                STREFA_PLOT("physics_ms", ms);
                STREFA_PLOT("gpu_ms", s.metrics.gpu_ms);
                ++s.metrics.timing_samples;
                if (s.timings.size() < 10000)
                    s.timings.push_back(ms);
                else
                    ++s.metrics.timings_dropped;
                if (s.presentation_timings.size() < 10000)
                    s.presentation_timings.push_back(presentation_ms);
                else
                    ++s.metrics.presentation_timings_dropped;
            }
        });
    };
    scalar(s.timers, s.timer_head, false);
    scalar(s.render_timers, s.render_head, true);
    if (effective_timer_mode(s.profile_options) != TimerMode::off && s.profile_options.group_timers)
        collect(s.group_timers, s.group_head, [&](auto& timer) {
            std::array<GLuint64, 6> timestamps{};
            for (std::size_t i = 0; i < timestamps.size(); ++i)
                s.gl.GetQueryObjectui64v(timer.ids[i], GL_QUERY_RESULT, &timestamps[i]);
            if (timer.generation == s.epoch && s.group_profiles.size() < 10000) {
                GroupProfile result;
                result.tick = timer.tick;
                result.generation = timer.generation;
                for (std::size_t i = 0; i < result.milliseconds.size(); ++i)
                    result.milliseconds[i] = static_cast<double>(timestamps[i + 1] - timestamps[i]) * 1e-6;
                s.group_profiles.push_back(result);
            } else if (s.profile_options.collect)
                ++s.calls.timer_stale;
        });
    const auto timer_end = stamp();
    s.poll_timer_ms = static_cast<double>(timer_end - timer_start) / 1e6;
    if (!collect_hud) return;
    if (const auto packet = s.readbacks.poll(s.epoch, s.readback_revision, s.metrics.steps)) {
        const auto& values = packet->counters;
        s.metrics.grains = values[2];
        s.metrics.water = values[3];
        s.metrics.oil = values[4];
        s.metrics.max_compression = std::bit_cast<float>(values[5]);
        const auto& received = packet->mission;
        if (s.mission_enabled && received.status[1] == s.epoch && received.status[2] >= s.snapshot.status[2])
            s.snapshot = received;
        s.readback_age = s.readbacks.stats().last_latency_ticks;
    }
    const auto read_times = s.readbacks.timings();
    s.poll_fence_ms = read_times.fence_ms;
    s.poll_read_ms = read_times.read_ms;
    if (s.readback_refresh) s.stage_count(true);
}
void ParticleWorld::render(float alpha) {
    auto& s = *impl_;
    s.device.require_current();
    s.resources->vfx.poll();
    if (s.thermal) s.thermal->poll();
    auto& timer = s.render_timers[s.render_cursor];
    const bool timers_enabled = effective_timer_mode(s.profile_options) != TimerMode::off;
    const bool measure = timers_enabled && !timer.pending;
    if (s.profile_options.collect && timers_enabled && !measure) ++s.calls.timer_skips;
    const auto now = SDL_GetTicksNS();
    const auto& hp = s.gameplay.state().hp;
    for (unsigned i = 0; i < 3; ++i) {
        if (hp[i] < s.presented_hp[i]) s.hit_started[i] = now;
        s.presented_hp[i] = hp[i];
        s.renderer.targets.hp[i] = hp[i];
        s.renderer.targets.hit[i] =
                s.hit_started[i] ? std::max(0.F, 1.F - static_cast<float>(now - s.hit_started[i]) / 150000000.F) : 0.F;
    }

    STREFA_GPU_ZONE("render");
    const detail::ElapsedQueryGuard elapsed_guard{s.gl, s.elapsed_active};
    if (measure) s.begin_timer(timer);
    s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    s.renderer.lighting.impulse =
            s.mission_enabled ? std::clamp(s.snapshot.flash[3] / .3F, 0.F, 1.F) : s.explosions.flash(s.metrics.steps);
    s.renderer.render({s.buffers, s.current, s.fluids_enabled, s.mission_enabled}, alpha);
    s.invalidate_bindings();
    if (measure) {
        s.end_timer(timer);
        if (s.profile_options.collect) ++s.calls.timer_submissions;
        s.render_cursor = (s.render_cursor + 1) % static_cast<unsigned>(s.render_timers.size());
    }
}
std::shared_ptr<RenderDevice> ParticleWorld::render_device() const {
    return impl_->resources;
}
void ParticleWorld::set_thermal_options(ThermalOptions value) {
    if (value.pressure_iterations == 0 || value.pressure_iterations > 128)
        throw std::invalid_argument{"Iteracje ciśnienia termiki: 1–128."};
    auto& s = *impl_;
    if (value.enabled && !s.thermal) {
        s.thermal = std::make_unique<detail::ThermalSystem>(s.gl, s.device.state);
        s.thermal->initialize(s.shader_library, s.shader_folder, s.layout_defines());
        s.thermal->reset(s.buffers, s.current, s.epoch);
        s.renderer.thermal = s.thermal.get();
    }
    s.thermal_config = value;
    if (s.thermal) s.thermal->options = value;
    s.thermal_brush = {};
    s.invalidate_bindings();
}
ThermalOptions ParticleWorld::thermal_options() const {
    return impl_->thermal_config;
}
void ParticleWorld::set_thermal_view(ThermalView view) {
    if (impl_->thermal) impl_->thermal->view = view;
}
void ParticleWorld::set_thermal_brush(const ThermalBrush& brush) {
    if (!valid(brush)) throw std::invalid_argument{"Nieprawidłowy pędzel termiki."};
    impl_->thermal_brush = brush;
}
const ThermalTotals& ParticleWorld::thermal_totals() const {
    static const ThermalTotals empty;
    return impl_->thermal ? impl_->thermal->totals() : empty;
}
ThermalSnapshot test::read_thermal(ParticleWorld& world, ThermalRead what) {
    return WorldAccess::impl(world).thermal ? WorldAccess::impl(world).thermal->snapshot(what) : ThermalSnapshot{};
}
void ParticleWorld::request_thermal_inspection(int x, int y) {
    if (impl_->thermal && impl_->thermal_config.enabled) impl_->thermal->request_inspection(x, y);
}
const ThermalInspection& ParticleWorld::thermal_inspection() const {
    static const ThermalInspection empty;
    return impl_->thermal ? impl_->thermal->inspection() : empty;
}
std::uint64_t ParticleWorld::thermal_memory() const {
    return impl_->thermal ? impl_->thermal->memory_bytes() : 0;
}
void ParticleWorld::set_vfx_options(VfxOptions value) {
    impl_->resources->vfx.set_options(value);
}
VfxOptions ParticleWorld::vfx_options() const noexcept {
    return impl_->resources->vfx.options();
}
const VfxCounters& ParticleWorld::vfx_stats() const {
    return impl_->resources->vfx.stats();
}
void ParticleWorld::set_render_options(RenderOptions value) {
    impl_->renderer.set_options(value);
}
std::array<float, 2> ParticleWorld::camera_offset() const {
    const auto& r = impl_->renderer;
    const auto c = r.lighting.camera(r.get_options().lighting);
    return r.get_options().lighting.quality == LightingQuality::off
                 ? std::array<float, 2>{}
                 : std::array<float, 2>{c[0] / static_cast<float>(r.render_width()) * 320.F,
                                        c[1] / static_cast<float>(r.render_height()) * 192.F};
}
RenderOptions ParticleWorld::render_options() const noexcept {
    return impl_->renderer.get_options();
}
void ParticleWorld::set_background_texture(unsigned texture) {
    impl_->renderer.background_texture = texture;
}
void ParticleWorld::invalidate_gl_state() {
    impl_->invalidate_bindings();
}
std::vector<RenderSample> ParticleWorld::take_render_samples() {
    return impl_->renderer.take_samples();
}
unsigned ParticleWorld::texture() const noexcept {
    return impl_->renderer.image;
}
std::vector<double> ParticleWorld::take_render_timings() {
    return std::exchange(impl_->render_timings, {});
}
std::vector<double> ParticleWorld::take_cpu_timings() {
    return std::exchange(impl_->cpu_timings, {});
}
const Metrics& ParticleWorld::metrics() const noexcept {
    return impl_->metrics;
}
const MissionSnapshot& ParticleWorld::mission() const noexcept {
    auto& s = *impl_;
    if (s.mission_enabled)
        s.gameplay.present(s.snapshot.inventory, s.snapshot.timers, s.snapshot.targets, s.snapshot.status[3]);
    return s.snapshot;
}
void ParticleWorld::set_profile_options(const ProfileOptions& options) {
    auto& s = *impl_;
    const auto previous_mode = effective_timer_mode(s.profile_options), next_mode = effective_timer_mode(options);
    const bool previous_groups = previous_mode != TimerMode::off && s.profile_options.group_timers;
    const bool next_groups = next_mode != TimerMode::off && options.group_timers;
    if (previous_mode != next_mode) s.reset_timer_queries(next_mode);
    if (previous_mode != next_mode || previous_groups != next_groups) s.reset_group_queries(next_groups);
    s.profile_options = options;
    s.gl.observe(options.collect ? &s.calls : nullptr);
    s.readbacks.configure(options.readback_mode, s.debug_groups);
    if (options.readback_period == 0) s.readback_refresh = false;
    for (auto kernels : {std::span<Impl::Kernel>{s.granular}, std::span<Impl::Kernel>{s.liquids},
                         std::span<Impl::Kernel>{s.mission_kernels}, std::span<Impl::Kernel>{s.coupling_kernels}})
        for (const auto& k : kernels)
            if (k.id && k.coupling >= 0) {
                s.gl.UseProgram(k.id);
                s.gl.Uniform1i(k.coupling, options.body_coupling ? 0 : 1);
            }
    s.invalidate_bindings();
}
ProfileOptions ParticleWorld::profile_options() const noexcept {
    return impl_->profile_options;
}
const StepProfile& ParticleWorld::step_profile() const noexcept {
    return impl_->last_profile;
}
PollTimings ParticleWorld::poll_timings() const noexcept {
    return {impl_->poll_timer_ms, impl_->poll_fence_ms, impl_->poll_read_ms};
}
ReadbackStats ParticleWorld::readback_stats() const noexcept {
    return impl_->readbacks.stats();
}
UploadStats ParticleWorld::upload_stats() const noexcept {
    return impl_->uploads.stats();
}
ShaderCacheStats ParticleWorld::shader_cache_stats() const noexcept {
    return impl_->shader_library.stats();
}
ExecutionOptions ParticleWorld::execution_options() const noexcept {
    return impl_->execution;
}
std::array<bool, 5> test::pbf_restrict_programs(const ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    const detail::ComputeKernelSet set{s.granular, s.liquids, s.neighbors, s.mission_kernels, s.coupling_kernels};
    for (const auto kernels : {set.granular, set.mission, set.neighbors, set.coupling})
        for (const auto& kernel : kernels)
            if (kernel.particle_restrict) throw std::logic_error{"Kwalifikatory PBF wyszły poza dwa programy."};
    std::array<bool, 5> result{};
    for (unsigned i = 0; i < result.size(); ++i) result[i] = s.liquids[i].particle_restrict;
    return result;
}
std::array<std::string, 5> test::pbf_program_sources_for_test(const ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    std::array<std::string, 5> result;
    for (unsigned i = 0; i < result.size(); ++i) {
        if (!s.liquids[i].id) continue;
        GLuint shader{};
        GLsizei attached{};
        s.gl.GetAttachedShaders(s.liquids[i].id, 1, &attached, &shader);
        if (attached != 1) throw std::logic_error{"Program PBF musi mieć dokładnie jeden shader obliczeniowy."};
        GLint length{};
        s.gl.GetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &length);
        if (length <= 1) throw std::logic_error{"Program PBF nie udostępnia źródła shadera."};
        result[i].resize(static_cast<std::size_t>(length));
        GLsizei written{};
        s.gl.GetShaderSource(shader, length, &written, result[i].data());
        result[i].resize(static_cast<std::size_t>(written));
    }
    return result;
}
void test::probe_pbf_alias_for_test(ParticleWorld& world, unsigned operation, unsigned source, unsigned binding) {
    auto& s = WorldAccess::impl(world);
    if ((operation != 1 && operation != 4) || !s.liquids[operation].particle_restrict || source > 8 || binding >= 8
        || source == binding)
        throw std::invalid_argument{"Nieprawidłowa próba błędu wiązań PBF."};
    const auto before = s.calls;
    const auto current = s.current;
    s.pbf_fault_source = source;
    s.pbf_fault_binding = binding;
    try {
        s.liquid_dispatch(static_cast<int>(operation), true);
    } catch (const ParticleAliasError&) {
        s.pbf_fault_source = s.pbf_fault_binding = 8;
        if (s.calls.api_calls != before.api_calls || s.current != current)
            throw std::logic_error{"Odrzucenie aliasu PBF zmieniło stan OpenGL lub bieżący bank."};
        return;
    } catch (...) {
        s.pbf_fault_source = s.pbf_fault_binding = 8;
        throw;
    }
    s.pbf_fault_source = s.pbf_fault_binding = 8;
    throw std::logic_error{"Próba aliasu PBF nie została odrzucona."};
}
std::array<std::uint32_t, 37> test::read_dispatch_commands(const ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    std::array<std::uint32_t, 37> result{};
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[5]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, detail::command_offset(0), sizeof(result), result.data());
    return result;
}
CallCounters ParticleWorld::counters() const noexcept {
    return impl_->calls;
}
std::vector<StepProfile> ParticleWorld::take_step_profiles() {
    return std::exchange(impl_->profiles, {});
}
std::vector<GroupProfile> ParticleWorld::take_group_profiles() {
    return std::exchange(impl_->group_profiles, {});
}
MissionSnapshot ParticleWorld::read_mission() const {
    auto& s = *impl_;
    MissionSnapshot result{};
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[6]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 32, sizeof(result), &result);
    if (!s.hold_events) s.event_ring.poll(s.gameplay);
    if (s.mission_enabled) s.gameplay.present(result.inventory, result.timers, result.targets, result.status[3]);
    return result;
}
std::vector<std::uint32_t> ParticleWorld::read_terrain() const {
    auto& s = *impl_;
    std::vector<std::uint32_t> result(width * height * 4);
    detail::read_staged(s.gl, s.buffers[4], 0, std::span{result});
    return result;
}
void ParticleWorld::set_actor_textures(unsigned parts, unsigned weapons, int w, int h) {
    impl_->renderer.parts_texture = parts;
    impl_->renderer.weapons_texture = weapons;
    impl_->renderer.weapon_width = w;
    impl_->renderer.weapon_height = h;
}
void ParticleWorld::queue_explosion(const Explosion& e) {
    impl_->explosions.queue(e);
}
void ParticleWorld::configure_mission(const MissionConfig& config) {
    if (!std::isfinite(config.spawn_x) || !std::isfinite(config.spawn_y) || !std::isfinite(config.relative_density)
        || config.spawn_x < 4 || config.spawn_x > width - 4 || config.spawn_y < 18 || config.spawn_y > height
        || config.relative_density <= 0)
        throw std::invalid_argument{"Nieprawidłowa konfiguracja gracza GPU."};
    auto& s = *impl_;
    s.reset_body();
    MissionSnapshot g{};
    s.explosions.clear();
    s.next_generation();
    ++s.readback_revision;
    s.readback_refresh = false;
    g.actor = {config.spawn_x, config.spawn_y, 0, 0};
    g.previous = {config.spawn_x, config.spawn_y, config.spawn_x, config.spawn_y};
    g.aim = {1, 0, 1.5F, 0};
    g.status = {1, s.epoch, 0, 0};
    g.inventory = {game::rifle_magazine, game::initial_stock, 0, 0};
    g.control = {0, config.spawn_x + 40, config.spawn_y - 14, 0};
    g.exit = config.exit;
    g.targets = config.targets;
    s.gameplay.reset(s.epoch, {static_cast<unsigned>(config.targets[0][2]), static_cast<unsigned>(config.targets[1][2]),
                               static_cast<unsigned>(config.targets[2][2])});
    s.presented_hp = s.gameplay.state().hp;
    s.hit_started = {};
    constexpr float area = 80 + 16 * 3.14159265358979323846F;
    g.body = {2.65F / (area * config.relative_density), config.relative_density, area, 0};
    const std::vector<std::byte> empty(mission_buffer_size - 32);
    s.upload(6, 32, static_cast<GLsizeiptr>(empty.size()), empty.data());
    s.upload(6, 32, sizeof(g), &g);
    s.snapshot = g;
    s.controls = {0, g.control[1], g.control[2], 0};
    s.mission_enabled = true;
    s.grains_enabled = true;
}
void test::restore_coupling_checkpoint(ParticleWorld& world, const MissionSnapshot& checkpoint, std::uint64_t tick) {
    if (checkpoint.status[0] == 0 || checkpoint.events[0] != 0 || checkpoint.counts[2] != 0)
        throw std::invalid_argument{"Punkt kontrolny sprzężenia wymaga aktywnej sceny bez pocisków i zniszczeń."};
    for (float value : checkpoint.actor)
        if (!std::isfinite(value)) throw std::invalid_argument{"Nieskończona wartość w punkcie kontrolnym sprzężenia."};
    auto& s = WorldAccess::impl(world);
    s.reset_body();
    s.explosions.clear();
    s.next_generation();
    s.gameplay.reset(s.epoch);
    ++s.readback_revision;
    s.readback_refresh = false;
    auto state = checkpoint;
    state.status[1] = s.epoch;
    s.upload(6, 32, sizeof(state), &state);
    s.snapshot = state;
    s.gameplay.reset(s.epoch,
                     {static_cast<unsigned>(state.targets[0][2]), static_cast<unsigned>(state.targets[1][2]),
                      static_cast<unsigned>(state.targets[2][2])},
                     state.status[2]);
    s.presented_hp = s.gameplay.state().hp;
    s.hit_started = {};
    s.metrics.steps = tick;
    s.controls = {0, state.control[1], state.control[2], 0};
    s.mission_enabled = true;
    s.grains_enabled = true;
}
std::expected<void, std::string> ParticleWorld::validate_gameplay_protocol(const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "gameplay-tests.txt"};
        std::ofstream timing{output / "gameplay-timing.csv"};
        timing << "tick,cpu_submit_ms,cpu_logic_ms,pending_events,ack_tick\n";
        auto& s = *impl_;
        const auto require = [](bool ok, const char* text) {
            if (!ok) throw std::runtime_error{text};
        };
        const auto drain = [&]() {
            glFinish();
            poll();
        }; // diagnostic only
        const auto tick = [&](MissionInput input = MissionInput{}) {
            require(step(input), "Unexpected event backpressure in bounded fixture");
        };
        // Check actual driver std430 offsets of fields used by the kernels.
        const auto resource =
                reinterpret_cast<PFNGLGETPROGRAMRESOURCEINDEXPROC>(SDL_GL_GetProcAddress("glGetProgramResourceIndex"));
        const auto properties =
                reinterpret_cast<PFNGLGETPROGRAMRESOURCEIVPROC>(SDL_GL_GetProcAddress("glGetProgramResourceiv"));
        require(resource && properties, "GL 4.3 reflection unavailable");
        const auto program = s.mission_kernels[0].id;
        const std::array<std::pair<const char*, std::size_t>, 8> offsets{
                {{"commandBatch.header", command_offset},
                 {"commandBatch.commands[0].identity", command_offset + 64},
                 {"commandBatch.commands[0].detail", command_offset + 80},
                 {"commandBatch.commands[0].payload", command_offset + 96},
                 {"eventBatch.header", event_offset},
                 {"eventBatch.events[0].identity", event_offset + 16},
                 {"eventBatch.events[0].detail", event_offset + 32},
                 {"eventBatch.events[0].payload", event_offset + 48}}};
        for (const auto& [name, offset] : offsets) {
            const auto id = resource(program, GL_BUFFER_VARIABLE, name);
            require(id != GL_INVALID_INDEX, "Protocol reflection field missing");
            constexpr GLenum property = GL_OFFSET;
            GLint value{};
            properties(program, GL_BUFFER_VARIABLE, id, 1, &property, 1, nullptr, &value);
            report << name << "=" << value << '\n';
            require(value == static_cast<GLint>(offset), "C++/GLSL protocol offset mismatch");
        }
        report << "abi_reflection=PASS\n";
        report << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
               << "\nversion=" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << '\n';
        // The GL 4.3 fallback transports the same real payload without mapping.
        {
            const auto supported = s.gl.persistent_storage;
            s.gl.persistent_storage = false;
            detail::EventRing fallback{s.gl};
            fallback.initialize();
            s.gl.persistent_storage = supported;
            game::GameplaySession session;
            session.reset(42);
            const auto commands = session.prepare({0, 54, 177, fire});
            game::EventBatch batch;
            batch.header = {1, 1, 42, 0};
            batch.events[0] = {42, 1, 1, 1, commands.commands[0].id, 0, 0, 0, {}, {}};
            s.upload(6, event_offset, sizeof(batch), &batch);
            fallback.submit(s.buffers[6], event_offset, 42, 1);
            glFinish();
            fallback.poll(session);
            require(session.state().shots == 1 && session.pending() == 0 && fallback.stats().acknowledged == 1,
                    "GL 4.3 reliable fallback payload lost");
        }
        report << "event_gl43_fallback=PASS\n";
        // Retransmitted command IDs never create another physical projectile.
        reset(Scene::empty);
        configure_mission({});
        s.controls = {0, 54, 177, 0};
        auto first = s.gameplay.prepare({0, 54, 177, fire});
        s.upload(6, command_offset, sizeof(first), &first);
        s.mission_dispatch(0);
        s.event_ring.submit(s.buffers[6], event_offset, s.epoch, 1);
        auto duplicate = s.gameplay.prepare({});
        duplicate.header[2] = 1;
        duplicate.commands[0] = first.commands[0];
        duplicate.commands[0].tick = 2;
        s.upload(6, command_offset, sizeof(duplicate), &duplicate);
        s.mission_dispatch(0);
        s.event_ring.submit(s.buffers[6], event_offset, s.epoch, 2);
        drain();
        std::array<std::array<float, 8>, bullet_capacity> duplicate_bullets{};
        s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[6]);
        s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 32 + sizeof(MissionSnapshot), sizeof(duplicate_bullets),
                              duplicate_bullets.data());
        require(std::ranges::count_if(duplicate_bullets, [](const auto& b) { return b[4] > 0; }) == 1
                        && gameplay().state().shots == 1,
                "Duplicate command executed twice");
        report << "gpu_command_dedup=PASS\n";
        // All 128 bullets can report a hit in one tick, plus command acks.
        reset(Scene::empty);
        configure_mission({});
        std::array<std::array<float, 8>, bullet_capacity> simultaneous{};
        for (auto& bullet : simultaneous) {
            bullet[0] = 50;
            bullet[1] = 177;
            bullet[2] = 600;
            bullet[4] = 1;
        }
        s.upload(6, 32 + sizeof(MissionSnapshot), sizeof(simultaneous), simultaneous.data());
        tick({0, 54, 177, fire | grenade | jump});
        drain();
        require(gameplay().state().score == 1 && gameplay().state().ammunition == 30 && gameplay().pending() == 0,
                "Maximum hit batch lost events/reservations");
        report << "128_simultaneous_hits_and_acks=PASS\n";

        // Refuse admission with a full staging queue. Leave actual GPU fences
        // unsignalled to the consumer using a validation-only hold flag.
        reset(Scene::empty);
        configure_mission({});
        s.hold_events = true;
        for (unsigned i = 0; i < game::event_slots; ++i) tick({0, 54, 177, i == 0 ? fire | grenade : 0U});
        const auto before = metrics().steps;
        const auto ammo = gameplay().state().ammunition;
        require(!step({0, 54, 177, fire}) && metrics().steps == before && gameplay().state().ammunition == ammo,
                "Full ring consumed tick or ammo");
        s.hold_events = false;
        drain();
        require(gameplay().acknowledged_tick() == game::event_slots, "Batched events incomplete");
        require(gameplay().state().shots == 1 && gameplay().pending() == 0, "Command acknowledgement incomplete");
        report << "full_ring_admission_and_batched_ack=PASS\n";
        // Projectile pool rejection refunds the exact reservation.
        reset(Scene::empty);
        configure_mission({});
        std::array<std::array<float, 8>, bullet_capacity> full{};
        for (auto& p : full) {
            p[0] = 100;
            p[1] = 100;
            p[4] = 1;
        }
        s.upload(6, 32 + sizeof(MissionSnapshot), sizeof(full), full.data());
        tick({0, 54, 177, fire});
        drain();
        require(gameplay().state().ammunition == 30 && gameplay().state().shots == 0 && gameplay().pending() == 0,
                "Rejected rifle did not refund");
        report << "projectile_rejection_refund=PASS\n";
        // Pending old-generation commands cannot affect a new mission.
        s.hold_events = true;
        tick({0, 54, 177, grenade});
        configure_mission({});
        s.hold_events = false;
        drain();
        require(gameplay().state().grenades == 3 && gameplay().state().score == 0 && gameplay().pending() == 0,
                "Reset leaked reservation/event");
        report << "reset_in_flight=PASS\n";
        // Simultaneous fuses are serialized, every detonation reaches the CPU.
        std::array<std::array<float, 8>, grenade_capacity> grenades{};
        for (unsigned i = 0; i < grenades.size(); ++i) {
            grenades[i][0] = 100 + static_cast<float>(i);
            grenades[i][1] = 100;
            grenades[i][4] = .001F;
        }
        s.upload(6, 32 + sizeof(MissionSnapshot) + bullet_capacity * 32, sizeof(grenades), grenades.data());
        for (unsigned i = 0; i < grenade_capacity + 2; ++i) tick();
        drain();
        require(gameplay().state().explosions == grenade_capacity, "Simultaneous detonations lost/duplicated");
        report << "simultaneous_detonations=PASS\n";
        // Coyote jump on current GPU state; no HUD poll is used for admission.
        reset(Scene::empty);
        MissionConfig airborne;
        airborne.spawn_y = 100;
        configure_mission(airborne);
        float air_time = .05F;
        s.upload(6, 32 + offsetof(MissionSnapshot, timers) + 8, 4, &air_time);
        tick({0, 54, 100, jump});
        drain();
        require(read_mission().actor[3] < -60, "Coyote jump failed");
        tick({0, 54, 100, jump});
        drain();
        require(gameplay().pending() == 1, "Jump reused coyote allowance");
        cancel_mission_input();
        tick();
        drain();
        require(gameplay().pending() == 0, "Focus cancellation left buffered jump");
        report << "coyote_and_focus_cancel=PASS\n";
        // Press shortly before landing: contact from terrain/sand is consumed
        // once on the GPU, within the 12-tick input window.
        reset(Scene::empty);
        airborne.spawn_y = 186;
        configure_mission(airborne);
        air_time = 1;
        s.upload(6, 32 + offsetof(MissionSnapshot, timers) + 8, 4, &air_time);
        std::array<float, 4> falling{24, 186, 0, 50};
        s.upload(6, 32 + offsetof(MissionSnapshot, actor), 16, falling.data());
        tick({0, 54, 177, jump});
        for (unsigned i = 0; i < 10; ++i) tick();
        drain();
        require(read_mission().actor[3] < -30 && gameplay().pending() == 0, "Landing jump buffer failed");
        report << "landing_buffer=PASS\n";
        reset(Scene::empty);
        airborne.spawn_y = 100;
        configure_mission(airborne);
        air_time = 1;
        s.upload(6, 32 + offsetof(MissionSnapshot, timers) + 8, 4, &air_time);
        tick({0, 54, 177, jump});
        for (unsigned i = 0; i < 14; ++i) tick();
        drain();
        require(gameplay().pending() == 0 && read_mission().actor[3] > 0, "Expired jump was executed in the air");
        report << "airborne_buffer_expiry=PASS\n";

        // Three render frames of delayed consumption; same tick-indexed physical
        // script at 30/60/144 Hz. World trajectory must not authorize CPU rules.
        MissionSnapshot reference{};
        for (unsigned fps : {30U, 60U, 144U}) {
            reset(Scene::empty);
            configure_mission({});
            unsigned submitted = 0, frame = 0;
            s.hold_events = true;
            double accumulator = 0;
            while (submitted < 240) {
                accumulator += 120.0 / fps;
                unsigned count = static_cast<unsigned>(accumulator + 1e-9);
                accumulator -= count;
                for (unsigned j = 0; j < count && submitted < 240; ++j) {
                    tick({0, 54, 177, submitted < 60 ? fire : 0U});
                    ++submitted;
                    const auto stats = event_stats();
                    timing << submitted << ',' << metrics().cpu_submit_ms << ',' << metrics().gameplay_ms << ','
                           << stats[3] << ',' << gameplay().acknowledged_tick() << '\n';
                }
                if (++frame % 3 == 0) {
                    s.hold_events = false;
                    drain();
                    s.hold_events = true;
                }
            }
            s.hold_events = false;
            drain();
            const auto result = read_mission();
            require(result.inventory[2] == 1 && gameplay().pending() == 0, "Delayed frame route score/ack");
            if (fps == 30)
                reference = result;
            else
                require(result.inventory == reference.inventory
                                && std::hypot(result.actor[0] - reference.actor[0],
                                              result.actor[1] - reference.actor[1])
                                           < .01,
                        "Frame-dependent physical route");
            report << "delayed_" << fps << "_fps=PASS\n";
        }

        // Stair slopes and a moving granular support use live GPU contacts.
        std::vector<std::uint32_t> slope(width * height);
        for (int x = 0; x < width; ++x)
            for (int y = 187 - std::min(12, x / 8); y < height; ++y) slope[y * width + x] = 1;
        load({}, slope);
        MissionConfig on_slope;
        on_slope.spawn_x = 72;
        on_slope.spawn_y = 170;
        configure_mission(on_slope);
        for (unsigned i = 0; i < 80; ++i) {
            tick();
            if (i % 8 == 7) drain();
        }
        drain();
        require(read_mission().locomotion[2] > .5F, "Slope support missing");
        tick({0, 100, 150, jump});
        drain();
        require(read_mission().actor[3] < -60, "Slope jump failed");
        report << "slope_jump=PASS\n";
        std::vector<Grain> support;
        for (int y = 174; y < 187; ++y)
            for (int x = 15; x < 60; ++x) {
                Grain grain;
                grain.position = {x + .5F, y + .5F, x + .5F, y + .5F};
                grain.motion = {0, 0, 1, 1};
                support.push_back(grain);
            }
        std::vector<std::uint32_t> floor(width * height);
        for (int y = 187; y < height; ++y)
            for (int x = 0; x < width; ++x) floor[y * width + x] = 1;
        load(support, floor);
        MissionConfig on_sand;
        on_sand.spawn_x = 35;
        on_sand.spawn_y = 165;
        configure_mission(on_sand);
        for (unsigned i = 0; i < 160; ++i) {
            tick({i > 100 ? .3F : 0.F, 80, 150, 0});
            if (i % 8 == 7) drain();
        }
        drain();
        require(read_mission().locomotion[2] > .5F, "Moving sand support missing");
        tick({.3F, 80, 150, jump});
        drain();
        require(read_mission().actor[3] < -40, "Moving sand jump failed");
        report << "moving_sand_jump=PASS\n";
        // Completed previous GPU work is drained outside CPU timing. Each measured
        // step includes the previous packet's consumption, new commands and submit.
        std::ofstream contract_csv{output / "full-mission-contract.csv"};
        contract_csv << "trial,tick,cpu_contract_ms,cpu_logic_ms,gpu_ms,event_depth\n";
        for (unsigned trial = 1; trial <= 3; ++trial) {
            reset_mission();
            for (unsigned i = 0; i < 120; ++i) {
                glFinish();
                tick();
            }
            glFinish();
            poll(false);
            static_cast<void>(take_timings());
            for (unsigned i = 0; i < 240; ++i) {
                glFinish();
                tick();
                contract_csv << trial << ',' << i + 1 << ',' << metrics().cpu_submit_ms << ',' << metrics().gameplay_ms
                             << ',' << metrics().physics_ms << ',' << event_stats()[3] << '\n';
            }
            drain();
        }
        report << "full_mission_contract_trials=3\n";
        report.flush();
        return {};
    } catch (const std::exception& e) {
        impl_->hold_events = false;
        return std::unexpected{e.what()};
    }
}
void ParticleWorld::reset_mission() {
    std::vector<std::uint32_t> walls(width * height);
    std::vector<Grain> grains;
    auto box = [&](int x0, int y0, int x1, int y1, unsigned kind = 1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) walls[y * width + x] = kind;
    };
    box(0, 187, 319, 191);
    box(0, 0, 0, 191);
    box(319, 0, 319, 191);
    // Raised reservoir with a breakable outlet; accessible underpass to its right.
    box(81, 111, 82, 186);
    box(83, 158, 132, 160);
    box(132, 111, 133, 157, 2);
    // Suspended sand silo. The orange floor is the destructible support.
    box(210, 80, 211, 126);
    box(248, 80, 249, 126);
    box(212, 125, 247, 126, 2);
    auto fill = [&](int x0, int y0, int x1, int y1, int kind) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const auto [px, py] = seeded_position(x, y);
                Grain p;
                p.position = {px, py, px, py};
                p.motion = {0, 0, kind == 5 ? 2.65F : kind == 6 ? 3.3125F : 1.F, static_cast<float>(kind)};
                grains.push_back(p);
            }
    };
    fill(84, 123, 130, 156, 5);
    fill(84, 115, 130, 122, 6);
    fill(213, 91, 246, 123, 1);
    load(grains, walls);
    configure_mission({});
    render(1);
}
std::vector<Grain> ParticleWorld::readback() const {
    auto& s = *impl_;
    std::vector<Grain> result(capacity);
    detail::read_staged(s.gl, s.buffers[s.current], 0, std::span{result});
    if (s.execution.soa) {
        std::vector<std::array<float, 4>> packed(capacity * 4);
        std::memcpy(packed.data(), result.data(), capacity * sizeof(Grain));
        for (unsigned i = 0; i < capacity; ++i)
            result[i] = {packed[i], packed[capacity + i], packed[2 * capacity + i], packed[3 * capacity + i]};
    }
    return result;
}
NeighborSnapshot test::read_neighbors(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    if (s.execution.neighbors == NeighborMode::linked) throw std::logic_error{"Wymagany posortowany indeks."};
    s.invalidate_bindings();
    s.bins();
    NeighborSnapshot result;
    std::vector<GLuint> cells(16 + 3 * width * height + 480);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[2]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(cells.size() * 4), cells.data());
    result.displacement = std::bit_cast<float>(cells[0]);
    result.builds = cells[1];
    result.starts.resize(width * height + 1);
    result.counts.resize(width * height);
    for (unsigned i = 0; i < width * height; ++i) {
        result.counts[i] = cells[16 + i];
        result.starts[i] = cells[16 + width * height + i] + cells[16 + 3 * width * height + 240 + i / 256];
    }
    result.starts.back() = result.starts[width * height - 1] + result.counts.back();
    result.order.resize(capacity);
    result.indices.resize(capacity);
    result.anchors.resize(capacity);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[3]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, capacity * 4, result.order.data());
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, capacity * 4, capacity * 4, result.indices.data());
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, capacity * 8, capacity * 16, result.anchors.data());
    return result;
}
std::array<std::uint32_t, 240> test::read_phase_mask(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    if (s.execution.neighbors == NeighborMode::linked || !s.execution.phase_gating)
        throw std::logic_error{"Wymagany posortowany indeks z bramkowaniem faz."};
    // Keep the same anchor generation as the neighbor index. With reuse enabled
    // bins() preserves the cached index and mask after bounded particle motion.
    s.invalidate_bindings();
    s.bins();
    std::array<std::uint32_t, 240> result{};
    constexpr auto offset = static_cast<GLintptr>((16 + 3 * width * height + 480) * 4);
    detail::read_staged(s.gl, s.buffers[2], offset, std::span{result});
    return result;
}
void test::start_diagnostic_trace(ParticleWorld& world, const TraceConfig& config) {
    auto& s = WorldAccess::impl(world);
    if ((config.mode == CostMode::bounds || config.mode == CostMode::snapshots)
        && s.execution.neighbors != NeighborMode::reused)
        throw std::invalid_argument{"Ślad przestrzenny wymaga produkcyjnego indeksu."};
    s.trace.start(config, capacity * sizeof(Grain), s.cell_buffer_size(), capacity * 24);
}
TraceReport test::read_diagnostic_trace(ParticleWorld& world) {
    WorldAccess::impl(world).trace_step = false;
    return WorldAccess::impl(world).trace.read();
}
void test::capture_diagnostic_snapshot(ParticleWorld& world, std::string label) {
    auto& s = WorldAccess::impl(world);
    if (!s.bins_valid || s.execution.neighbors != NeighborMode::reused)
        throw std::logic_error{"Migawka wymaga istniejącego, poprawnego indeksu."};
    s.trace.capture(std::move(label), s.epoch, s.metrics.steps, 0, s.execution.soa, s.buffers[s.current], s.buffers[2],
                    s.buffers[3]);
}
HotAudit test::audit_hot_view(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    HotAudit result;
    if (s.read_mode() != NeighborReads::sorted) return result;
    result.indexed = s.bins_valid;
    if (!result.indexed) return result;
    struct alignas(16) HotEntry {
        std::array<float, 4> geometry, motion;
    };
    static_assert(sizeof(HotEntry) == 32);
    constexpr auto view_bytes = capacity * sizeof(HotEntry);
    std::vector<HotEntry> view(capacity);
    std::vector<std::uint32_t> indices(capacity);
    detail::read_staged(s.gl, s.buffers[3], static_cast<GLintptr>(capacity * 24 + s.current * view_bytes),
                        std::span{view});
    detail::read_staged(s.gl, s.buffers[3], capacity * 4, std::span{indices});
    const auto actual = world.readback();
    std::vector<bool> used(capacity);
    const auto count =
            static_cast<unsigned>(std::ranges::count_if(actual, [](const auto& p) { return p.motion[3] > .5F; }));
    for (unsigned id = 0; id < capacity; ++id)
        if (actual[id].motion[3] > .5F) {
            ++result.active;
            const auto entry = indices[id];
            if (entry >= count || used[entry]) {
                ++result.mismatches;
                continue;
            }
            used[entry] = true;
            const auto& p = actual[id];
            const std::array<float, 4> geometry{p.position[0], p.position[1], p.residual[0], p.residual[1]};
            if (std::bit_cast<std::array<std::uint32_t, 4>>(geometry)
                        != std::bit_cast<std::array<std::uint32_t, 4>>(view[entry].geometry)
                || std::bit_cast<std::array<std::uint32_t, 4>>(p.motion)
                           != std::bit_cast<std::array<std::uint32_t, 4>>(view[entry].motion))
                ++result.mismatches;
        }
    return result;
}
std::vector<std::array<float, 4>> test::audit_neighbors(ParticleWorld& world, bool pairs) {
    auto& s = WorldAccess::impl(world);
    if (s.execution.neighbors == NeighborMode::linked) throw std::logic_error{"Wymagany posortowany indeks."};
    s.invalidate_bindings();
    s.bins();
    std::vector<std::array<float, 4>> result(pairs ? 65536 : capacity);
    if (pairs) {
        s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        s.upload(7, 0, static_cast<GLsizeiptr>(result.size() * 16), result.data());
    }
    s.neighbor_dispatch(pairs ? 5U : 7U, pairs ? 1U : 128U);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[7]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(result.size() * 16), result.data());
    return result;
}
void test::displace_for_test(ParticleWorld& world, std::span<const std::array<float, 4>> deltas) {
    if (deltas.size() > capacity) throw std::invalid_argument{"Za dużo testowych przesunięć."};
    auto& s = WorldAccess::impl(world);
    if (s.execution.neighbors == NeighborMode::linked) throw std::logic_error{"Wymagany posortowany indeks."};
    s.invalidate_bindings();
    s.bins();
    std::vector<std::array<float, 4>> packed(capacity);
    std::ranges::copy(deltas, packed.begin());
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.upload(7, 0, capacity * 16, packed.data());
    s.neighbor_dispatch(6, 128);
    s.current = 1 - s.current;
}
BodySnapshot test::read_body(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    if (s.execution.coupling == CouplingMode::reference) throw std::logic_error{"Wymagane zwarte sprzężenie ciał."};
    BodySnapshot result;
    // Headers and command bytes through staging: validation must not migrate the simulation SSBO to host memory or
    // read consumed argument shadows.
    const auto bytes = std::as_writable_bytes(std::span{&result, 1});
    detail::read_staged(s.gl, s.buffers[3], static_cast<GLintptr>(s.body_offset()), bytes.first(64));
    detail::read_staged(s.gl, s.buffers[5], detail::command_offset(detail::body_reduce_command), bytes.subspan(64, 24));
    return result;
}
void test::inject_body_for_test(ParticleWorld& world, std::span<const std::array<float, 4>> reactions, bool apply) {
    auto& s = WorldAccess::impl(world);
    if (s.execution.coupling == CouplingMode::reference || reactions.size() > 2 * capacity)
        throw std::invalid_argument{"Nieprawidłowa testowa paczka ciał."};
    s.invalidate_bindings();
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    if (!reactions.empty())
        s.upload(7, capacity * 16, static_cast<GLsizeiptr>(reactions.size_bytes()), reactions.data());
    auto& k = s.prepare(s.coupling_kernels, 2, false, true);
    s.gl.Uniform1i(k.test_count, static_cast<GLint>(reactions.size()));
    s.gl.DispatchCompute(
            static_cast<GLuint>(std::max<std::size_t>(1, (reactions.size() + k.local_size - 1) / k.local_size)), 1, 1);
    s.gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
    if (apply) s.mission_dispatch(3);
}
#include "gpu/render_validation.inl"
#include "gpu/vfx_benchmark.inl"
#include "gpu/vfx_replay.inl"
#include "gpu/vfx_validation.inl"

void ParticleWorld::save_image(const std::filesystem::path& path) const {
    impl_->renderer.save_image(path);
}
std::vector<double> ParticleWorld::take_timings() {
    return std::exchange(impl_->timings, {});
}
std::vector<double> ParticleWorld::take_presentation_timings() {
    return std::exchange(impl_->presentation_timings, {});
}
std::vector<std::array<float, 4>> test::read_fluid_terms(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    s.invalidate_bindings();
    s.bins();
    s.liquid_dispatch(3);
    s.liquid_dispatch(2);
    std::vector<std::array<float, 4>> result(capacity);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[7]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, capacity * 16, result.data());
    return result;
}
std::vector<float> test::read_boundary_weights(const ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    std::vector<float> result(width * height, 1);
    if (s.execution.fluid.boundary == FluidBoundary::volume) {
        s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[4]);
        s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, width * height * 16,
                              static_cast<GLsizeiptr>(result.size() * sizeof(float)), result.data());
        s.invalidate_bindings();
    }
    return result;
}
std::vector<std::array<float, 4>> test::fluid_fields_for_test(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    if (!s.execution.fluid.fields()) throw std::logic_error{"Pola cieczy są wyłączone."};
    s.invalidate_bindings();
    s.bins();
    s.liquid_dispatch(5);
    s.liquid_dispatch(6);
    std::vector<std::array<float, 4>> result(capacity * 2);
    const auto offset = capacity * (s.execution.coupling == CouplingMode::reference ? 32U : 48U);
    s.gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    s.gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, s.buffers[7]);
    s.gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, static_cast<GLsizeiptr>(result.size() * sizeof(result[0])),
                          result.data());
    s.invalidate_bindings();
    return result;
}
void test::apply_fluid_velocities_for_test(ParticleWorld& world) {
    auto& s = WorldAccess::impl(world);
    s.invalidate_bindings();
    s.bins();
    if (s.execution.fluid.fields()) {
        s.liquid_dispatch(5);
        s.liquid_dispatch(6);
    }
    s.liquid_dispatch(4, true);
    if (s.mission_enabled && s.execution.fluid.velocity_reaction()) s.mission_dispatch(3);
}
void test::project_liquids_for_test(ParticleWorld& world, unsigned iterations) {
    auto& s = WorldAccess::impl(world);
    s.invalidate_bindings();
    for (unsigned i = 0; i < iterations; ++i) {
        s.bins();
        s.liquid_dispatch(0);
        s.liquid_dispatch(1, true);
        if (s.mission_enabled) s.mission_dispatch(3);
    }
}
} // namespace strefa::gpu

#include "gpu/lighting_validation.inl"
#include "gpu/thermal_scenes.inl"
#include "gpu/thermal_validation.inl"
