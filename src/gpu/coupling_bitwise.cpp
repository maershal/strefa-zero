#include "gpu/diagnostic_read.hpp"
#include "gpu/dispatch_layout.hpp"
#include "gpu/validation.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace strefa::gpu {
namespace {
using Vec4 = std::array<float, 4>;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
Vec4 add(Vec4 a, const Vec4& b) {
    for (unsigned k = 0; k < 4; ++k) a[k] += b[k];
    return a;
}
// The shared-memory tree of coupling.comp: strides L/2, ..., 1 over L lanes.
Vec4 tree(std::vector<Vec4> lanes) {
    for (std::size_t stride = lanes.size() / 2; stride > 0; stride /= 2)
        for (std::size_t lane = 0; lane < stride; ++lane) lanes[lane] = add(lanes[lane], lanes[lane + stride]);
    return lanes[0];
}
// Summation order of the two-dispatch reduction before Z05, in float: one tree per block of L queue entries; one block
// is the result itself; more blocks go to lane k as 0 + block k + block k+L + ..., followed by the same tree.
Vec4 two_pass_sum(std::span<const Vec4> entries, unsigned group) {
    const std::size_t blocks = (entries.size() + group - 1) / group;
    std::vector<Vec4> sums(blocks);
    for (std::size_t b = 0; b < blocks; ++b) {
        std::vector<Vec4> lanes(group);
        for (std::size_t lane = 0; lane < group && b * group + lane < entries.size(); ++lane)
            lanes[lane] = entries[b * group + lane];
        sums[b] = tree(std::move(lanes));
    }
    if (blocks <= 1) return blocks ? sums[0] : Vec4{};
    std::vector<Vec4> lanes(group);
    for (std::size_t lane = 0; lane < group; ++lane)
        for (std::size_t b = lane; b < blocks; b += group) lanes[lane] = add(lanes[lane], sums[b]);
    return tree(std::move(lanes));
}
bool same_bits(const Vec4& a, const Vec4& b) {
    for (unsigned k = 0; k < 4; ++k)
        if (std::bit_cast<std::uint32_t>(a[k]) != std::bit_cast<std::uint32_t>(b[k])) return false;
    return true;
}
std::string hex(const Vec4& v) {
    return std::format("{:08x},{:08x},{:08x},{:08x}", std::bit_cast<std::uint32_t>(v[0]),
                       std::bit_cast<std::uint32_t>(v[1]), std::bit_cast<std::uint32_t>(v[2]),
                       std::bit_cast<std::uint32_t>(v[3]));
}
// Reads what the coupling kernels see through their SSBO bindings: the queue (binding 3, body metadata at the end of
// the buffer) and the body reaction channels terms[N, 3N) (binding 7). Diagnostic reads go through staging copies.
struct BufferProbe {
    detail::Api gl;
    PFNGLGETINTEGERI_VPROC get_integer_i{};
    BufferProbe() {
        gl.load();
        get_integer_i = reinterpret_cast<PFNGLGETINTEGERI_VPROC>(SDL_GL_GetProcAddress("glGetIntegeri_v"));
        require(get_integer_i != nullptr, "Brak OpenGL: glGetIntegeri_v");
    }
    GLuint bound(unsigned binding) const {
        GLint name{};
        get_integer_i(GL_SHADER_STORAGE_BUFFER_BINDING, binding, &name);
        require(name != 0, std::format("SSBO binding {} is empty", binding));
        return static_cast<GLuint>(name);
    }
    std::vector<std::uint32_t> queue(unsigned count) const {
        const auto buffer = bound(3);
        gl.BindBuffer(GL_COPY_READ_BUFFER, buffer);
        GLint size{};
        gl.GetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &size);
        require(static_cast<std::size_t>(size) >= detail::body_metadata_size, "Index buffer has no body metadata");
        std::vector<std::uint32_t> ids(count);
        detail::read_staged(gl, buffer, static_cast<GLintptr>(size - detail::body_metadata_size + 64), std::span{ids});
        return ids;
    }
    std::vector<Vec4> reactions() const {
        std::vector<Vec4> values(2 * capacity);
        detail::read_staged(gl, bound(7), static_cast<GLintptr>(capacity * sizeof(Vec4)), std::span{values});
        return values;
    }
};
struct Outcome {
    Vec4 expected{}, actual{};
    bool sum{}, total{}, actor{};
};
// Queues `input` through the test producer, reads the queue order the GPU chose, applies the reduction and compares
// the result, the running total and the actor with the two-pass order, bit for bit.
Outcome reduce_and_compare(ParticleWorld& world, const BufferProbe& probe, std::span<const Vec4> input, unsigned group,
                           unsigned queued, bool actor_moves_freely) {
    const auto before = world.read_body();
    const auto actor_before = world.read_mission().actor;
    world.inject_body_for_test(input, false);
    const auto pending = world.read_body();
    require(pending.queue[0] == queued && pending.queue[2] == 0,
            std::format("Queue holds {} reactions, expected {}", pending.queue[0], queued));
    require(pending.commands[0] == (queued + group - 1) / group && pending.commands[3] == (queued > group ? 1U : 0U),
            "Producer wrote different indirect arguments");
    const auto ids = probe.queue(queued);
    const auto terms = probe.reactions();
    std::vector<Vec4> entries;
    entries.reserve(queued);
    for (auto id : ids) {
        require(id < terms.size(), "Queue holds an invalid tagged ID");
        entries.push_back(terms[id]);
    }
    Outcome result;
    result.expected = two_pass_sum(entries, group);
    world.inject_body_for_test({});
    const auto after = world.read_body();
    const auto actor_after = world.read_mission().actor;
    require(after.queue[0] == 0 && after.commands[0] == 0 && after.commands[3] == 0, "Reduction left pending work");
    require(after.stats[0] - before.stats[0] == queued && after.stats[1] - before.stats[1] == (queued ? 1U : 0U),
            "Reduction changed the reaction or batch counters");
    require(after.stats[2] - before.stats[2] == (queued + group - 1) / group
                    && after.stats[3] - before.stats[3] == (queued > group ? 1U : 0U),
            "Reduction changed the meaning of the group or finalization counters");
    if (!queued) {
        result.sum = result.total = result.actor = same_bits(after.last_sum, before.last_sum)
                                                && same_bits(after.total_sum, before.total_sum)
                                                && actor_after == actor_before;
        return result;
    }
    result.actual = after.last_sum;
    result.sum = same_bits(after.last_sum, result.expected);
    result.total = same_bits(after.total_sum, add(before.total_sum, result.expected));
    // terrainBody() runs after the move; in free air it leaves the position as moved.
    Vec4 moved = actor_before;
    const float xy = result.expected[0] * result.expected[0] + result.expected[1] * result.expected[1];
    if (xy > 1e-12F)
        for (unsigned k = 0; k < 2; ++k) moved[k] += result.expected[k];
    result.actor = !actor_moves_freely || (actor_after[0] == moved[0] && actor_after[1] == moved[1]);
    return result;
}
std::vector<std::uint32_t> floor_at(int y) {
    std::vector<std::uint32_t> walls(width * height);
    for (int j = y; j < height; ++j)
        for (int x = 0; x < width; ++x) walls[j * width + x] = 1;
    return walls;
}
// Normal floats with full mantissas and mixed signs and exponents, so every summation order rounds differently.
Vec4 reaction(std::uint32_t i) {
    const auto mix = [](std::uint32_t v) {
        v ^= v >> 16;
        v *= 0x7feb352dU;
        v ^= v >> 15;
        v *= 0x846ca68bU;
        return v ^ (v >> 16);
    };
    const auto value = [&](std::uint32_t salt, float scale) {
        const auto bits = mix(i * 4 + salt);
        const float magnitude = scale * (1.F + static_cast<float>(bits & 0xffffffU) / 16777216.F)
                              * std::ldexp(1.F, static_cast<int>((bits >> 24) % 6));
        return bits & 0x80000000U ? -magnitude : magnitude;
    };
    return {value(0, 1e-6F), value(1, 1e-6F), i % 11 == 0 ? 1.F : 0.F, value(3, 1e-3F)};
}
// Subnormal inputs could be flushed by the GPU and not by the CPU; mission data passes through unchanged otherwise.
Vec4 normal_or_zero(Vec4 v) {
    for (auto& x : v)
        if (std::fpclassify(x) == FP_SUBNORMAL) x = 0;
    return v;
}
} // namespace
std::expected<void, std::string> validate_coupling_bitwise(const std::filesystem::path& shaders,
                                                           const std::filesystem::path& output,
                                                           const ExecutionOptions& layout) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "coupling-bitwise.txt"};
        require(static_cast<bool>(report), "Cannot create coupling-bitwise.txt");
        const BufferProbe probe;
        unsigned compared = 0, order_sensitive = 0;
        for (auto mode : {CouplingMode::local, CouplingMode::combined})
            for (unsigned group : {64U, 128U, 256U}) {
                auto options = layout;
                options.coupling = mode;
                options.reduction_size = group;
                auto made = ParticleWorld::create(shaders, options);
                require(made.has_value(), made ? "" : made.error());
                auto& world = **made;
                world.load({}, floor_at(187));
                MissionConfig config;
                config.spawn_x = 160;
                config.spawn_y = 100;
                world.configure_mission(config);
                for (unsigned count :
                     {0U, 1U, group - 1, group, group + 1, 1000U, static_cast<unsigned>(2 * capacity)}) {
                    std::vector<Vec4> input(count);
                    for (unsigned i = 0; i < count; ++i) input[i] = reaction(i);
                    const auto r = reduce_and_compare(world, probe, input, group, count, true);
                    Vec4 sequential{};
                    for (const auto& v : input) sequential = add(sequential, v);
                    if (count && !same_bits(sequential, r.expected)) ++order_sensitive;
                    report << "batch mode=" << static_cast<int>(mode) << " group=" << group << " count=" << count
                           << " expected=" << hex(r.expected) << " actual=" << hex(r.actual) << " sum=" << r.sum
                           << " total=" << r.total << " actor=" << r.actor << std::endl;
                    require(r.sum && r.total && r.actor,
                            std::format("Single-group reduction differs from the two-pass order (mode {}, group {}, "
                                        "count {})",
                                        static_cast<int>(mode), group, count));
                    ++compared;
                }
            }
        require(order_sensitive >= 12, "Batches do not discriminate summation orders");
        // Scripted mission route: every 100 ticks the content of the reaction channels terms[N, 3N) (the last substep's
        // reactions and older values the channels still hold) goes through the reduction again, in the queue order the
        // GPU chooses, with the actor where the route put it. Mission-shaped values and queues of ~60k entries.
        unsigned route_cases = 0, route_entries = 0;
        for (auto mode : {CouplingMode::local, CouplingMode::combined}) {
            auto options = layout;
            options.coupling = mode;
            auto made = ParticleWorld::create(shaders, options);
            require(made.has_value(), made ? "" : made.error());
            auto& world = **made;
            world.reset_mission();
            for (unsigned tick = 0; tick < 1700;) {
                world.step(scripted_mission_input(tick++));
                if (tick % 100 != 0) continue;
                require(world.read_body().queue[0] == 0, "Mission tick left reactions pending");
                auto input = probe.reactions();
                unsigned queued = 0;
                for (auto& v : input) {
                    v = normal_or_zero(v);
                    if (v[0] != 0 || v[1] != 0 || v[2] != 0) ++queued;
                }
                const auto r = reduce_and_compare(world, probe, input, options.reduction_size, queued, false);
                report << "route mode=" << static_cast<int>(mode) << " tick=" << tick << " queued=" << queued
                       << " expected=" << hex(r.expected) << " actual=" << hex(r.actual) << " sum=" << r.sum
                       << " total=" << r.total << std::endl;
                require(r.sum && r.total, std::format("Mission route reduction differs from the two-pass order "
                                                      "(mode {}, tick {}, {} reactions)",
                                                      static_cast<int>(mode), tick, queued));
                ++route_cases;
                route_entries += queued;
            }
        }
        require(route_entries > 0, "Mission route produced no body reactions");
        report << "batches=" << compared << " order_sensitive=" << order_sensitive << " route_cases=" << route_cases
               << " route_reactions=" << route_entries << "\ncoupling_bitwise=passed\n";
        require(report.good(), "Cannot write coupling-bitwise.txt");
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "coupling-bitwise-error.txt"} << e.what();
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
