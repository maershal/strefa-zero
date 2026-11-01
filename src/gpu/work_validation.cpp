#include "gpu/validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
Grain particle(float x, float y, unsigned kind = 1) {
    Grain p;
    p.position = {x, y, x, y};
    p.motion = {0, 0, kind == 5 ? 2.65F : kind == 6 ? 3.3125F : 1.F, static_cast<float>(kind)};
    return p;
}
std::vector<std::uint32_t> floor_at(unsigned y) {
    std::vector<std::uint32_t> wall(width * height);
    for (unsigned row = y; row < height; ++row)
        for (unsigned x = 0; x < width; ++x) wall[row * width + x] = 1;
    return wall;
}
double compare(const std::vector<Grain>& reference, const std::vector<Grain>& candidate) {
    double error = 0;
    for (unsigned id = 0; id < capacity; ++id) {
        const auto& a = reference[id];
        const auto& b = candidate[id];
        require(a.motion[3] == b.motion[3], "Work gating changed active IDs/material membership");
        if (a.motion[3] < .5F) continue;
        for (const auto& pair : {std::pair{a.position, b.position}, std::pair{a.motion, b.motion},
                                 std::pair{a.contact, b.contact}, std::pair{a.residual, b.residual}})
            for (unsigned k = 0; k < 4; ++k) {
                require(std::isfinite(pair.second[k]), "Work gating produced a nonfinite state");
                error = std::max(error, std::abs(static_cast<double>(pair.first[k]) - pair.second[k]));
            }
    }
    require(error < .0001, "Work gating differs from full reference projection/contact state");
    return error;
}
void check_mask(ParticleWorld& world) {
    const auto state = world.readback();
    const auto index = world.read_neighbors();
    const auto mask = world.read_phase_mask();
    std::array<std::uint32_t, 240> expected{};
    std::vector<unsigned> active;
    for (unsigned id = 0; id < capacity; ++id)
        if (state[id].motion[3] > .5F) {
            active.push_back(id);
            const auto& anchor = index.anchors[id];
            const int x = std::clamp(static_cast<int>(std::floor(anchor[0])), 0, width - 1);
            const int y = std::clamp(static_cast<int>(std::floor(anchor[1])), 0, height - 1);
            expected[(y / 16) * 20 + x / 16] |= state[id].motion[3] >= 5 ? 1U : 2U;
        }
    require(mask == expected, "GPU phase mask does not match independent anchor/material membership");
    // Independent all-pairs close tests: every actual liquid neighbor must
    // remain represented in the conservative anchor-tile query after motion.
    if (active.size() > 256) return;
    for (const auto i : active)
        if (state[i].motion[3] < 5) {
            const auto& p = state[i];
            const double reach = 2 + index.displacement + std::hypot(p.residual[0], p.residual[1]) + .001;
            const int x0 = std::clamp(static_cast<int>(std::floor(p.position[0] - reach)), 0, width - 1) / 16;
            const int y0 = std::clamp(static_cast<int>(std::floor(p.position[1] - reach)), 0, height - 1) / 16;
            const int x1 = std::clamp(static_cast<int>(std::floor(p.position[0] + reach)), 0, width - 1) / 16;
            const int y1 = std::clamp(static_cast<int>(std::floor(p.position[1] + reach)), 0, height - 1) / 16;
            bool represented = false;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) represented = represented || (mask[y * 20 + x] & 1U) != 0;
            for (const auto j : active)
                if (state[j].motion[3] >= 5) {
                    const auto& q = state[j];
                    const double dx =
                            (static_cast<double>(p.position[0]) - q.position[0]) + p.residual[0] - q.residual[0];
                    const double dy =
                            (static_cast<double>(p.position[1]) - q.position[1]) + p.residual[1] - q.residual[1];
                    if (dx * dx + dy * dy < 4)
                        require(represented, "A real liquid neighbor was excluded by the phase mask");
                }
        }
}
} // namespace
std::expected<void, std::string> validate_work(const std::filesystem::path& output, const ExecutionOptions& layout) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "work-tests.txt"}, errors{output / "projection-errors.csv"};
        require(report && errors, "Cannot create work validation reports");
        errors << "layout,neighbors,phase,certificate,case,max_state_error\n" << std::setprecision(10);
        const auto shaders =
                std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())} / "assets/shaders";
        const auto create = [&](const ExecutionOptions& options) {
            auto made = ParticleWorld::create(shaders, options);
            if (!made) throw std::runtime_error{made.error()};
            return std::move(*made);
        };
        require(layout.neighbors != NeighborMode::linked,
                "Work validation requires sorted/reused; linked fallback is checked internally");
        const bool soa = layout.soa;
        const auto mode = layout.neighbors;
        {
            ExecutionOptions base;
            base.soa = soa;
            base.neighbors = mode;
            base.phase_gating = false;
            base.skip_zero_projection = false;
            report << "reference_world_begin layout=" << soa << " neighbors=" << static_cast<int>(mode) << std::endl;
            const auto reference_started = SDL_GetTicksNS();
            auto reference = create(base);
            report << "reference_world_create_ms=" << static_cast<double>(SDL_GetTicksNS() - reference_started) / 1e6
                   << std::endl;
            for (const auto& flags : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
                auto options = base;
                options.phase_gating = flags.first;
                options.skip_zero_projection = flags.second;
                report << "candidate_world_begin phase=" << flags.first << " certificate=" << flags.second << std::endl;
                const auto candidate_started = SDL_GetTicksNS();
                auto candidate = create(options);
                report << "candidate_world_create_ms="
                       << static_cast<double>(SDL_GetTicksNS() - candidate_started) / 1e6 << std::endl;
                for (unsigned fixture = 0; fixture < 6; ++fixture) {
                    auto walls = floor_at(180);
                    std::vector<Grain> state(256);
                    if (fixture == 0) {
                        state[0] = particle(20, 181.9F);
                        state[255] = particle(220.5F, 100.5F, 5);
                    }
                    if (fixture == 1) {
                        state[0] = particle(15.85F, 80.5F);
                        for (unsigned j = 0; j < 9; ++j)
                            state[200 + j] = particle(16.15F + (j % 3) * .36F, 80.1F + (j / 3) * .36F, 5);
                    }
                    if (fixture == 2) {
                        state[0] = particle(31.3F, 79.95F);
                        state[255] = particle(128.2F, 80.1F, 6);
                    }
                    if (fixture == 3) {
                        state[0] = particle(10, 179.54F);
                        state[0].residual = {.0000004F, -.0000002F, 0, 0};
                        state[255] = particle(200, 80, 5);
                    }
                    if (fixture == 4) {
                        state[0] = particle(.1F, 40.5F);
                        state[200] = particle(1.3F, 40.6F, 5);
                        walls[40 * width + 1] = 1;
                    }
                    if (fixture == 5) {
                        state[0] = particle(40.4F, 80.4F);
                        state[255] = particle(41.4F, 81.4F, 5);
                        walls[80 * width + 41] = 1;
                        walls[81 * width + 40] = 1;
                    }
                    reference->load(state, walls);
                    candidate->load(state, walls);
                    if (flags.first) check_mask(*candidate);
                    if (fixture == 2) {
                        std::vector<std::array<float, 4>> deltas(256);
                        deltas[255] = {-96.2F, 0, 0, 0};
                        reference->displace_for_test(deltas);
                        candidate->displace_for_test(deltas);
                        if (flags.first) check_mask(*candidate);
                        if (mode == NeighborMode::reused)
                            require(candidate->read_neighbors().displacement > 96,
                                    "Outlier failed to exercise a multi-tile displacement");
                    }
                    reference->project_liquids_for_test(1);
                    candidate->project_liquids_for_test(1);
                    glFinish();
                    const auto expected = reference->readback(), actual = candidate->readback();
                    const auto error = compare(expected, actual);
                    if (fixture == 0) {
                        require(expected[0].position[1] < 180 && actual[0].position[1] < 180,
                                "Zero correction incorrectly skipped unresolved terrain penetration");
                        report << "deep_penetration_y=" << actual[0].position[1]
                               << " contact_y=" << actual[0].contact[1] << '\n';
                    }
                    errors << (soa ? "soa" : "aos") << ',' << (mode == NeighborMode::reused ? "reused" : "sorted")
                           << ',' << flags.first << ',' << flags.second << ',' << fixture << ',' << error << '\n';
                    errors.flush();
                }
                if (flags.first) {
                    auto walls = floor_at(180);
                    candidate->load({}, walls);
                    candidate->paint({150, 120, 155, 120, 3, Material::water});
                    check_mask(*candidate);
                    candidate->paint({150, 120, 155, 120, 9, Material::erase});
                    check_mask(*candidate);
                    require(std::ranges::all_of(candidate->read_phase_mask(), [](auto bits) { return bits == 0; }),
                            "Erased liquid left a stale phase bit");
                    std::vector<Grain> dense(capacity, particle(80.25F, 70.25F));
                    dense.back().motion[3] = 6;
                    candidate->load(dense, walls);
                    check_mask(*candidate);
                    candidate->load({}, walls);
                    check_mask(*candidate);
                }
                require(glGetError() == GL_NO_ERROR, "OpenGL error during work validation");
                report << "layout=" << soa << " neighbors=" << static_cast<int>(mode) << " phase=" << flags.first
                       << " certificate=" << flags.second << "=passed\n";
                report.flush();
            }
        }
        // The old linked-list path must retain its complete scan with gating requested.
        ExecutionOptions fallback;
        fallback.neighbors = NeighborMode::linked;
        fallback.phase_gating = true;
        fallback.skip_zero_projection = true;
        auto linked = create(fallback);
        auto walls = floor_at(180);
        linked->load(std::array{particle(20, 181.9F), particle(200, 80, 5)}, walls);
        linked->project_liquids_for_test(1);
        glFinish();
        require(linked->readback()[0].position[1] < 180, "Linked-list fallback skipped required terrain projection");
        require(glGetError() == GL_NO_ERROR, "OpenGL error in linked-list fallback");
        report << "linked_fallback=passed\nstatus=passed\n";
        return {};
    } catch (const std::exception& error) {
        std::ofstream{output / "work-error.txt"} << error.what();
        return std::unexpected{error.what()};
    }
}
} // namespace strefa::gpu
