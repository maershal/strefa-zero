#pragma once
// Definition of ParticleWorld::Impl, kept out of world.cpp so that code outside the class (test access, diagnostics)
// can reach the world state without new members on ParticleWorld (Z09 step 2). Included by world.cpp only so far.
#include "gpu/compute_kernels.hpp"
#include "gpu/diagnostic_trace.hpp"
#include "gpu/dispatch_layout.hpp"
#include "gpu/event_ring.hpp"
#include "gpu/explosion_system.hpp"
#include "gpu/readback_ring.hpp"
#include "gpu/render_device.hpp"
#include "gpu/scene_renderer.hpp"
#include "gpu/segment_migrations.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/thermal_system.hpp"
#include "gpu/upload_ring.hpp"
#include "gpu/world.hpp"
#include "platform/gpu_zone.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace strefa::gpu {
namespace detail {
struct ParticleAliasError : std::logic_error {
    using std::logic_error::logic_error;
};
inline void require_disjoint_particle_bases(const std::array<GLuint, 8>& bases) {
    for (unsigned particle = 0; particle < 2; ++particle) {
        if (!bases[particle]) throw ParticleAliasError{"Restricted PBF has an unbound particle bank."};
        for (unsigned other = 0; other < bases.size(); ++other)
            if (other != particle && bases[particle] == bases[other])
                throw ParticleAliasError{"Restricted PBF particle bank aliases another SSBO binding."};
    }
}
inline std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
inline std::array<float, 2> seeded_position(int x, int y) {
    const auto id = static_cast<std::uint32_t>(y * width + x);
    return {x + .5F + (static_cast<float>(hash(2 * id) & 65535U) / 65535.F - .5F) * .12F,
            y + .5F + (static_cast<float>(hash(2 * id + 1) & 65535U) / 65535.F - .5F) * .12F};
}
} // namespace detail
struct ParticleWorld::Impl final : detail::ExplosionPasses {
    std::filesystem::path shader_folder;
    std::shared_ptr<RenderDevice> resources;
    explicit Impl(std::shared_ptr<RenderDevice> value) : resources{std::move(value)} {}
    detail::DeviceContext& device{resources->context};
    detail::Api& gl{device.gl};
    detail::ShaderLibrary& shader_library{resources->library};
    detail::SceneRenderer renderer{gl, device.state};
    std::unique_ptr<detail::ThermalSystem> thermal;
    ThermalOptions thermal_config;
    ThermalBrush thermal_brush;
    detail::UploadRing uploads{gl};
    detail::ReadbackRing readbacks{gl};
    detail::EventRing event_ring{gl};
    game::GameplaySession gameplay;
    detail::DiagnosticTrace trace{gl};
    bool trace_step{}, hold_events{}; // hold_events is validation-only fault injection
    std::uint64_t readback_revision{1};
    bool readback_refresh{};
    bool debug_groups{};
    ProfileOptions profile_options;
    CallCounters calls;
    StepProfile last_profile;
    std::vector<StepProfile> profiles;
    std::vector<GroupProfile> group_profiles;
    struct GroupTimer {
        std::array<GLuint, 6> ids{};
        bool pending{};
        std::uint64_t tick{};
        unsigned generation{};
    };
    std::array<GroupTimer, 32> group_timers{};
    unsigned group_cursor{};
    std::uint64_t readback_age{};
    double poll_timer_ms{}, poll_fence_ms{}, poll_read_ms{};
    ExecutionOptions execution;
    using Kernel = detail::ComputeKernel;
    std::array<Kernel, 21> granular;
    std::array<Kernel, 7> liquids;
    std::array<Kernel, 9> neighbors;
    bool bins_valid{};
    NeighborReads read_mode() const { return effective_neighbor_reads(execution.neighbor_reads, execution.neighbors); }
    std::string fluid_defines() const {
        const auto& f = execution.fluid;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::scientific << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "#define FLUID_BOUNDARY_VOLUME " << (f.boundary == FluidBoundary::volume ? 1 : 0) << '\n'
            << "#define FLUID_SURFACE " << (f.surface == FluidSurface::akinci2d ? 1 : 0) << '\n'
            << "#define FLUID_FIELDS " << (f.fields() ? 1 : 0) << '\n'
            << "#define FLUID_SUBSTEPS " << f.substeps << '\n'
            << "#define FLUID_SCORR " << f.s_corr << '\n'
            << "#define FLUID_TENSION " << f.tension_scale << '\n'
            << "#define FLUID_CURVATURE " << f.curvature_scale << '\n'
            << "#define FLUID_ADHESION " << f.adhesion << '\n'
            << "#define FLUID_VORTICITY " << f.vorticity << '\n';
        return out.str();
    }
    std::string layout_defines() const {
        return fluid_defines() + std::string{"#define PARTICLE_SOA "} + (execution.soa ? "1\n" : "0\n")
             + "#define SORTED_NEIGHBORS " + (execution.neighbors != NeighborMode::linked ? "1\n" : "0\n")
             + "#define REUSE_NEIGHBORS " + (execution.neighbors == NeighborMode::reused ? "1\n" : "0\n")
             + "#define COUPLING_MODE " + std::to_string(static_cast<int>(execution.coupling))
             + "\n#define REDUCTION_LOCAL_SIZE " + std::to_string(execution.reduction_size) + "u\n#define PHASE_GATING "
             + (execution.phase_gating ? "1\n" : "0\n") + "#define SKIP_ZERO_PROJECTION "
             + (execution.skip_zero_projection ? "1\n" : "0\n") + "#define HOT_READS "
             + std::to_string(static_cast<int>(read_mode())) + "\n#define BATCHED_BLASTS "
             + (execution.batched_blasts ? "1\n" : "0\n");
    }
    std::array<Kernel, 9> mission_kernels;
    std::array<Kernel, 3> coupling_kernels;
    GLuint& bound_program{device.state.program};
    GLuint& bound_indirect{device.state.indirect};
    std::array<GLuint, 8>& bound_bases{device.state.bases};
    std::array<unsigned, 3> presented_hp{3, 3, 3};
    std::array<std::uint64_t, 3> hit_started{};
    unsigned pbf_fault_source{8}, pbf_fault_binding{8}; // validation only; 8 means no fault
    Stroke brush;
    int half_step{};
    void invalidate_bindings() { device.state.invalidate(); }
    void compile_kernels(const std::filesystem::path& shaders) {
        detail::compile_kernels(shader_library, gl, execution, layout_defines(), shaders, debug_groups,
                                {granular, liquids, neighbors, mission_kernels, coupling_kernels});
    }
    void configure_hot(Kernel& k, bool in_place, unsigned op) {
        const auto set = [&](GLint location, int value, int& cached) {
            if (location >= 0 && value != cached) {
                gl.Uniform1i(location, value);
                cached = value;
            }
        };
        set(k.hot_read, static_cast<int>(current), k.hot_read_value);
        set(k.hot_write, static_cast<int>(in_place ? current : 1 - current), k.hot_write_value);
        // New/free IDs have no reverse map until the next index build. Never
        // publish them into an old entry, including in-place spawning.
        const bool edits = (k.family == 0 && (op == 5 || op == 8 || op == 19)) || (k.family == 1 && op == 6);
        set(k.hot_valid, bins_valid && !edits ? 1 : 0, k.hot_valid_value);
    }
    Kernel& prepare(auto& kernels, int op, bool in_place = false, bool separate = false) {
        auto& k = kernels[execution.specialized || separate ? static_cast<unsigned>(op) : 0];
        if (!k.id) throw std::logic_error{"Brak przebiegu obliczeniowego."};
        auto bases = buffers;
        bases[0] = buffers[current];
        bases[1] = buffers[in_place ? current : 1 - current];
        if (k.particle_restrict) {
            if (pbf_fault_binding < bases.size())
                bases[pbf_fault_binding] = pbf_fault_source < bases.size() ? bases[pbf_fault_source] : 0;
            detail::require_disjoint_particle_bases(bases);
        }
        // A regression in the guard must still never submit an invalid restrict
        // program to the GPU. The test accepts only ParticleAliasError above.
        if (pbf_fault_binding < bases.size())
            throw std::runtime_error{"Błąd wiązań PBF ominął kontrolę przed uruchomieniem."};
        if (!execution.cached_bindings) {
            gl.UseProgram(granular[0].id);
            if (k.id != granular[0].id) gl.UseProgram(k.id);
        } else if (bound_program != k.id) {
            gl.UseProgram(k.id);
            bound_program = k.id;
        }
        for (unsigned i = 0; i < buffers.size(); ++i) {
            const auto buffer = bases[i];
            if (!execution.cached_bindings || bound_bases[i] != buffer) {
                gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, i, buffer);
                bound_bases[i] = buffer;
            }
        }
        if (k.operation >= 0) gl.Uniform1i(k.operation, op);
        configure_hot(k, in_place, static_cast<unsigned>(op));
        return k;
    }
    void indirect(unsigned command) {
        if (!execution.cached_bindings || bound_indirect != buffers[5]) {
            gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, buffers[5]);
            bound_indirect = buffers[5];
        }
        gl.DispatchComputeIndirect(detail::command_offset(command));
    }
    std::array<GLuint, 8> buffers{}; // A/B, head, link, walls, free, counters, fluid terms
    bool mission_enabled{};
    unsigned epoch{};
    MissionSnapshot snapshot{};
    detail::ExplosionSystem explosions;
    MissionInput controls{};
    void next_generation() {
        if (epoch == std::numeric_limits<unsigned>::max())
            throw std::overflow_error{"Wyczerpano numerację generacji świata."};
        ++epoch;
        resources->vfx.reset(epoch);
        renderer.lighting.reset(epoch);
    }
    bool fluids_enabled{}, grains_enabled{};
    struct Timer {
        GLuint id{}, start{};
        GLuint presentation{}; // physics ticks only: second interval, from the physics end to the tick end
        bool pending{};
        unsigned generation{};
        TimerMode mode{TimerMode::elapsed};
    };
    std::array<Timer, 32> timers{}, render_timers{};
    unsigned render_cursor{};
    unsigned timer_cursor{}, timer_head{}, render_head{}, group_head{}, current{};
    bool elapsed_active{}; // a GL_TIME_ELAPSED query is open (begin_timer .. end_*_timer)
    Metrics metrics;
    std::vector<double> timings, presentation_timings, render_timings, cpu_timings;
    void reset_timer_queries(TimerMode mode) {
        // Query objects acquire an immutable target on first use. Recreate the
        // pools on mode changes, including pending results; deletion is deferred
        // by GL and needs no CPU/GPU wait. No active query spans this method.
        for (auto* queue : {&timers, &render_timers})
            for (auto& timer : *queue) {
                if (timer.id) gl.DeleteQueries(1, &timer.id);
                if (timer.start) gl.DeleteQueries(1, &timer.start);
                if (timer.presentation) gl.DeleteQueries(1, &timer.presentation);
                timer = {};
                timer.mode = mode;
                if (mode != TimerMode::off) gl.GenQueries(1, &timer.id);
                if (mode == TimerMode::timestamp) gl.GenQueries(1, &timer.start);
                if (mode != TimerMode::off && queue == &timers) gl.GenQueries(1, &timer.presentation);
            }
        timer_cursor = timer_head = render_cursor = render_head = 0;
        metrics.gpu_ms = metrics.physics_ms = metrics.presentation_ms = 0;
        metrics.render_ms = 0;
        metrics.timing_samples = 0;
        timings.clear();
        presentation_timings.clear();
        render_timings.clear();
    }
    void reset_group_queries(bool enabled) {
        for (auto& timer : group_timers) {
            if (timer.ids[0]) gl.DeleteQueries(6, timer.ids.data());
            timer = {};
            if (enabled) gl.GenQueries(6, timer.ids.data());
        }
        group_cursor = group_head = 0;
        group_profiles.clear();
    }
    void begin_timer(Timer& timer) {
        // GL allows one active GL_TIME_ELAPSED query; nesting is a GL_INVALID_OPERATION.
        assert(!elapsed_active && "begin_timer with a GL_TIME_ELAPSED query still active");
        if (timer.mode == TimerMode::timestamp)
            gl.QueryCounter(timer.start, GL_TIMESTAMP);
        else {
            gl.BeginQuery(GL_TIME_ELAPSED, timer.id);
            elapsed_active = true;
        }
    }
    void end_timer(Timer& timer) {
        if (timer.mode == TimerMode::timestamp)
            gl.QueryCounter(timer.id, GL_TIMESTAMP);
        else {
            gl.EndQuery(GL_TIME_ELAPSED);
            elapsed_active = false;
        }
        if (timer.presentation) {
            // The physics timestamp doubles as the start of the presentation interval.
            if (timer.mode == TimerMode::elapsed) {
                gl.BeginQuery(GL_TIME_ELAPSED, timer.presentation);
                elapsed_active = true;
            }
            return;
        }
        timer.pending = true;
        timer.generation = epoch;
    }
    void end_presentation_timer(Timer& timer) {
        if (timer.mode == TimerMode::timestamp)
            gl.QueryCounter(timer.presentation, GL_TIMESTAMP);
        else {
            gl.EndQuery(GL_TIME_ELAPSED);
            elapsed_active = false;
        }
        timer.pending = true;
        timer.generation = epoch;
    }
    ~Impl() {
        gl.observe(nullptr);
        for (const auto* queue : {&timers, &render_timers})
            for (const auto& timer : *queue) {
                if (timer.id) gl.DeleteQueries(1, &timer.id);
                if (timer.start) gl.DeleteQueries(1, &timer.start);
                if (timer.presentation) gl.DeleteQueries(1, &timer.presentation);
            }
        for (const auto& timer : group_timers)
            if (timer.ids[0]) gl.DeleteQueries(6, timer.ids.data());
        if (buffers[0]) gl.DeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
    }
    void dispatch(int op, unsigned count, bool swap = false, bool in_place = false, bool conditional = false) {
        if (count == 0) return;
        STREFA_GPU_ZONE(detail::pass_zone_name(PassFamily::granular, static_cast<unsigned>(op)));
        if (op == 8) {
            renderer.lighting.prepare_birth(buffers, current);
            invalidate_bindings();
        }
        auto& k = prepare(granular, op, in_place);
        if (k.dirty >= 0 && k.dirty_value != static_cast<int>(conditional)) {
            gl.Uniform1i(k.dirty, conditional ? 1 : 0);
            k.dirty_value = conditional ? 1 : 0;
        }
        if (op == 1 && k.substep >= 0) gl.Uniform1i(k.substep, half_step);
        if (op == 5 || op == 7 || op == 8) {
            if (k.stroke >= 0) gl.Uniform4f(k.stroke, brush.x0, brush.y0, brush.x1, brush.y1);
            if (k.radius >= 0) gl.Uniform1f(k.radius, std::clamp(brush.radius, 1.F, 16.F));
            if (k.material >= 0) gl.Uniform1i(k.material, static_cast<int>(brush.material));
        }
        if (op == 9)
            gl.BindImageTexture(0, renderer.terrain_image, 0, static_cast<GLboolean>(GL_FALSE), 0, GL_WRITE_ONLY,
                                GL_R16UI);
        if (op == 9) renderer.begin_terrain_update();
        const auto token = trace.begin(PassFamily::granular, static_cast<unsigned>(op), epoch,
                                       metrics.steps + (trace_step ? 1 : 0));
        if (op >= 1 && op <= 4)
            indirect(detail::active_command);
        else if (execution.conditional_work && conditional && (op == 11 || op == 19 || op == 9))
            indirect(op == 11 ? detail::rebuild_command : op == 19 ? detail::sync_command : detail::terrain_command);
        else
            gl.DispatchCompute((count + k.local_size - 1) / k.local_size, 1, 1);
        GLbitfield barriers = GL_SHADER_STORAGE_BARRIER_BIT;
        if (op == 9) barriers |= GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        if (op == 12 || (op == 10 && execution.conditional_work)
            || (op == 3 && execution.coupling != CouplingMode::reference))
            barriers |= GL_COMMAND_BARRIER_BIT;
        gl.MemoryBarrier(barriers);
        if (op == 9) renderer.end_terrain_update();
        if (swap) current = 1 - current;
        trace.end(token, bins_valid && op != 5 && op != 8 && op != 19, execution.soa, buffers[current], buffers[2],
                  buffers[3]);
    }
    void neighbor_dispatch(unsigned op, unsigned groups) {
        STREFA_GPU_ZONE(detail::pass_zone_name(PassFamily::neighbors, op));
        auto& k = neighbors[op];
        if (!execution.cached_bindings || bound_program != k.id) {
            gl.UseProgram(k.id);
            bound_program = k.id;
        }
        for (unsigned i = 0; i < buffers.size(); ++i) {
            const auto buffer = i < 2 ? buffers[i == 0 ? current : 1 - current] : buffers[i];
            if (!execution.cached_bindings || bound_bases[i] != buffer) {
                gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, i, buffer);
                bound_bases[i] = buffer;
            }
        }
        configure_hot(k, false, op);
        const auto token = trace.begin(PassFamily::neighbors, op, epoch, metrics.steps + (trace_step ? 1 : 0));
        gl.DispatchCompute(groups, 1, 1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        trace.end(token, bins_valid || op == 8, execution.soa, buffers[current], buffers[2], buffers[3]);
    }
    void bins() {
        if (execution.neighbors == NeighborMode::reused && bins_valid) return;
        if (profile_options.collect) ++calls.bins;
        if (execution.neighbors == NeighborMode::linked) {
            dispatch(0, width * height);
            dispatch(2, capacity);
        } else {
            neighbor_dispatch(0, 240);
            // Fixed capacity launch avoids a CPU read of current membership;
            // inactive lanes return before touching particle state.
            neighbor_dispatch(1, 128);
            neighbor_dispatch(2, 240);
            neighbor_dispatch(3, 1);
            neighbor_dispatch(4, 128);
            neighbor_dispatch(8, 128); // in-cell order by stable ID (#95)
        }
        bins_valid = true;
    }
    void liquid_dispatch(int op, bool swap = false) {
        STREFA_GPU_ZONE(detail::pass_zone_name(PassFamily::liquids, static_cast<unsigned>(op)));
        prepare(liquids, op);
        const auto token = trace.begin(PassFamily::liquids, static_cast<unsigned>(op), epoch,
                                       metrics.steps + (trace_step ? 1 : 0));
        if (op == 3)
            gl.DispatchCompute(1, 1, 1);
        else
            indirect(detail::pbf_command);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
                         | (((op == 1 || (op == 4 && execution.fluid.velocity_reaction()))
                             && execution.coupling != CouplingMode::reference)
                                    ? GL_COMMAND_BARRIER_BIT
                                    : 0));
        if (swap) current = 1 - current;
        trace.end(token, bins_valid, execution.soa, buffers[current], buffers[2], buffers[3]);
    }
    void rebuild_active(bool conditional = false) {
        bins_valid = false;
        if (profile_options.collect) ++calls.active_rebuilds;
        dispatch(10, 1, false, false, conditional);
        dispatch(11, capacity, false, false, conditional);
        dispatch(12, 1, false, false, conditional);
        // Membership edits must reach both ping-pong copies before active-only work.
        dispatch(19, capacity, false, false, conditional);
        if (conditional) dispatch(9, width * height, false, false, true);
        dispatch(20, 1, false, false, conditional);
    }
    void mission_pass(detail::PassContext& c, int op, unsigned count, bool swap, bool active, bool in_place) override {
        current = c.bank;
        mission_dispatch(op, count, swap, active, in_place);
        c.bank = current;
    }
    void birth_pass(detail::PassContext& context) override {
        renderer.lighting.prepare_birth(buffers, context.bank, context.conditional);
        invalidate_bindings();
    }
    void rebuild_pass(detail::PassContext& context) override {
        current = context.bank;
        rebuild_active(true);
        context.bank = current;
    }
    void mission_dispatch(int op, unsigned count = 1, bool swap = false, bool active = false, bool in_place = false) {
        if (!profile_options.body_coupling && (op == 3 || op == 8)) return;
        STREFA_GPU_ZONE(detail::pass_zone_name(PassFamily::mission, static_cast<unsigned>(op)));
        if (profile_options.collect && op == 3) ++calls.body_reductions;
        if (op == 3 && execution.coupling != CouplingMode::reference) {
            prepare(coupling_kernels, 0, false, true);
            const auto token = trace.begin(PassFamily::coupling, 0, epoch, metrics.steps + (trace_step ? 1 : 0));
            indirect(detail::body_reduce_command); // group 0 reduces the whole queue (Z05)
            gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
            trace.end(token, bins_valid, execution.soa, buffers[current], buffers[2], buffers[3]);
            return;
        }
        // Blast impulses touch only their own IDs. In-place work can be gated
        // without switching to a stale ping-pong copy when zero groups execute.
        if (execution.conditional_work && op == 7) {
            in_place = true;
            swap = false;
        }
        auto& k = prepare(mission_kernels, op, in_place);
        if (op == 4 && k.substep >= 0) gl.Uniform1i(k.substep, half_step);
        if (op == 0)
            gl.Uniform4f(k.input, std::clamp(controls.move, -1.F, 1.F), controls.aim_x, controls.aim_y,
                         static_cast<float>(controls.buttons & 31U));
        const auto token = trace.begin(PassFamily::mission, static_cast<unsigned>(op), epoch,
                                       metrics.steps + (trace_step ? 1 : 0));
        if (execution.conditional_work && (op == 3 || op == 5 || op == 6 || op == 7))
            indirect(op == 3   ? detail::reduction_command
                     : op == 5 ? detail::blast_terrain_command
                     : op == 6 ? detail::debris_command
                               : detail::blast_command);
        else if (active)
            indirect(detail::active_command);
        else
            gl.DispatchCompute((count + k.local_size - 1) / k.local_size, 1, 1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
                         | (((op == 0 && execution.conditional_work)
                             || (op == 8 && execution.coupling != CouplingMode::reference))
                                    ? GL_COMMAND_BARRIER_BIT
                                    : 0));
        if (swap) current = 1 - current;
        trace.end(token, bins_valid && op != 6, execution.soa, buffers[current], buffers[2], buffers[3]);
    }
    std::size_t body_offset() const {
        return execution.neighbors == NeighborMode::linked
                     ? capacity * 4
                     : capacity * 24 + (read_mode() == NeighborReads::sorted ? capacity * 64 : 0);
    }
    // Header, three words per cell, block sums and offsets, the phase mask and the unordered scatter (neighbors.comp).
    std::size_t cell_buffer_size() const {
        return static_cast<std::size_t>(execution.neighbors == NeighborMode::linked
                                                ? width * height
                                                : 16 + 3 * width * height + 480 + 240 + capacity)
             * 4;
    }
    std::size_t index_buffer_size() const {
        return body_offset() + (execution.coupling == CouplingMode::reference ? 0 : detail::body_metadata_size);
    }
    std::size_t terms_buffer_size() const {
        return capacity
             * ((execution.coupling == CouplingMode::reference ? 32U : 48U) + (execution.fluid.fields() ? 32U : 0U));
    }
    void reset_body() {
        const std::array<GLuint, 16> zero{};
        const std::array<GLuint, 6> commands{0, 1, 1, 0, 1, 1};
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        if (execution.coupling != CouplingMode::reference)
            upload(3, static_cast<GLintptr>(body_offset()), sizeof(zero), zero.data());
        upload(5, detail::command_offset(detail::body_reduce_command), sizeof(commands), commands.data());
    }
    void upload(unsigned index, GLintptr offset, GLsizeiptr bytes, const void* data) {
        uploads.write(buffers[index], offset, bytes, data);
    }
    void stage_count(bool force = false) {
        if (profile_options.collect) ++calls.stage_requests;
        if (profile_options.readback_period == 0) return;
        if (!force && metrics.steps % profile_options.readback_period != 0) return;
        readback_refresh = !readbacks.submit(buffers[6], epoch, readback_revision, metrics.steps);
    }
};
} // namespace strefa::gpu
