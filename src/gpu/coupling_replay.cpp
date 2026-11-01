#include "gpu/world.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}
// Fixed strict floors from the former max(floor, 2*single_control) rule.
// They constrain every replay and also the accumulated common-input defect.
constexpr double mean_limit = .05, center_limit = .02, actor_limit = .15;
struct Error {
    double mean{}, dx{}, dy{}, actor{}, ax{}, ay{};
};
Error compare(const std::vector<Grain>& actual, const std::vector<Grain>& expected, const MissionSnapshot& actor,
              const MissionSnapshot& reference) {
    Error error;
    unsigned count = 0;
    for (unsigned id = 0; id < capacity; ++id) {
        require(actual[id].motion[3] == expected[id].motion[3], "Coupling replay changed stable membership/materials");
        if (actual[id].motion[3] < .5F) continue;
        require(actual[id].motion[2] == expected[id].motion[2], "Coupling replay changed per-ID mass");
        ++count;
        for (const auto& values : {actual[id].position, actual[id].motion, actual[id].contact, actual[id].residual})
            for (float value : values) require(std::isfinite(value), "Coupling replay produced a nonfinite particle");
        const double dx = (static_cast<double>(actual[id].position[0]) - expected[id].position[0])
                        + (static_cast<double>(actual[id].residual[0]) - expected[id].residual[0]);
        const double dy = (static_cast<double>(actual[id].position[1]) - expected[id].position[1])
                        + (static_cast<double>(actual[id].residual[1]) - expected[id].residual[1]);
        error.mean += std::hypot(dx, dy);
        error.dx += dx;
        error.dy += dy;
    }
    require(count == 1443, "Coupling replay did not preserve the complete mixed fixture");
    error.mean /= count;
    error.dx /= count;
    error.dy /= count;
    for (float value : actor.actor) require(std::isfinite(value), "Coupling replay produced a nonfinite actor");
    error.ax = static_cast<double>(actor.actor[0]) - reference.actor[0];
    error.ay = static_cast<double>(actor.actor[1]) - reference.actor[1];
    error.actor = std::hypot(error.ax, error.ay);
    return error;
}
struct Ledger {
    double mean_max{}, center_max{}, actor_max{}, mean_sum{}, dx{}, dy{}, ax{}, ay{};
    void add(const Error& e) {
        mean_max = std::max(mean_max, e.mean);
        center_max = std::max(center_max, std::hypot(e.dx, e.dy));
        actor_max = std::max(actor_max, e.actor);
        mean_sum += e.mean;
        dx += e.dx;
        dy += e.dy;
        ax += e.ax;
        ay += e.ay;
    }
    bool accepted() const {
        return mean_max < mean_limit && center_max < center_limit && actor_max < actor_limit && mean_sum < mean_limit
            && std::hypot(dx, dy) < center_limit && std::hypot(ax, ay) < actor_limit;
    }
};
Grain particle(float x, float y, unsigned kind) {
    Grain p;
    p.position = {x, y, x, y};
    p.motion = {0, 0, kind == 5 ? 2.65F : 1.F, static_cast<float>(kind)};
    return p;
}
} // namespace
void validate_coupling_replay(const std::filesystem::path& output, std::ostream& checks,
                              const ExecutionOptions& layout) {
    std::ofstream cases{output / "coupling-replay.csv"}, summary{output / "coupling-replay-summary.csv"};
    require(cases && summary, "Cannot create coupling replay reports");
    cases << "tick,role,mean,center_dx,center_dy,center,actor_dx,actor_dy,actor,mean_sum,center_bias,actor_bias,reactions,queue_pending,queue_overflow\n"
          << std::setprecision(12);
    summary << "role,cases,mean_max,center_max,actor_max,mean_sum,center_bias,actor_bias,reactions,accepted,scope\n"
            << std::setprecision(12);
    const auto shaders = std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())} / "assets/shaders";
    const auto create = [&](CouplingMode mode, bool coupling = true, bool baseline = false) {
        auto execution = layout;
        execution.coupling = mode;
        // A fixed 256-lane source also checks the selected PBF size against
        // the pre-G arithmetic, rather than comparing only matching variants.
        if (baseline) {
            execution.pbf_size = 256;
            execution.pbf_restrict = false;
        }
        ProfileOptions quiet;
        quiet.gpu_timers = false;
        quiet.readback_period = 0;
        quiet.body_coupling = coupling;
        auto made = ParticleWorld::create(shaders, execution, quiet);
        require(made.has_value(), made ? "" : made.error().c_str());
        return std::move(*made);
    };
    auto source = create(CouplingMode::reference, true, true);
    checks << "paired_source_pbf_size=256 replica_pbf_size=" << layout.effective_pbf_size() << std::endl;
    checks << "paired_source_pbf_restrict=0 replica_pbf_restrict=" << layout.effective_pbf_restrict() << std::endl;
    std::array replicas{create(CouplingMode::reference), create(CouplingMode::local), create(CouplingMode::combined),
                        create(CouplingMode::local, false)};
    constexpr std::array<std::string_view, 4> roles{"reference_repeat", "local", "combined", "disabled_fault"};
    std::array<Ledger, 4> ledgers;
    std::array<std::uint64_t, 4> reaction_counts{};
    std::vector<std::uint32_t> walls(width * height);
    for (int y = 180; y < height; ++y)
        for (int x = 0; x < width; ++x) walls[y * width + x] = 1;
    std::vector<Grain> initial;
    for (int y = 140; y < 179; ++y)
        for (int x = 112; x < 149; ++x) initial.push_back(particle(x + .53F, y + .47F, y < 160 ? 5U : 1U));
    source->load(initial, walls);
    MissionConfig config;
    config.spawn_x = 130;
    config.spawn_y = 145;
    source->configure_mission(config);
    for (unsigned tick = 0; tick < 90; ++tick) {
        if (tick == 45) {
            config.spawn_x = 139;
            config.spawn_y = 151;
            source->configure_mission(config);
        }
        const auto before = source->readback();
        const auto checkpoint = source->read_mission();
        const auto engine_tick = source->metrics().steps;
        const MissionInput input{tick < 45 ? .4F : -.3F, 170, 140, 0};
        source->step(input);
        const auto expected = source->readback();
        const auto reference = source->read_mission();
        // The carrier is a real continuous 90-tick reference route, including
        // pose relocation. Replicas start from each of its authoritative states.
        static_cast<void>(compare(expected, expected, reference, reference));
        for (unsigned index = 0; index < replicas.size(); ++index) {
            auto& world = *replicas[index];
            world.load(before, walls);
            world.restore_coupling_checkpoint(checkpoint, engine_tick);
            world.step(input);
            const auto actual = world.readback();
            const auto actor = world.read_mission();
            const auto error = compare(actual, expected, actor, reference);
            ledgers[index].add(error);
            BodySnapshot body;
            if (index > 0) {
                body = world.read_body();
                require(body.queue[0] == 0 && body.queue[2] == 0,
                        "Common-state replay left pending/overflowed reactions");
                reaction_counts[index] += body.stats[0];
            }
            const auto& total = ledgers[index];
            cases << tick << ',' << roles[index] << ',' << error.mean << ',' << error.dx << ',' << error.dy << ','
                  << std::hypot(error.dx, error.dy) << ',' << error.ax << ',' << error.ay << ',' << error.actor << ','
                  << total.mean_sum << ',' << std::hypot(total.dx, total.dy) << ',' << std::hypot(total.ax, total.ay)
                  << ',' << body.stats[0] << ',' << body.queue[0] << ',' << body.queue[2] << '\n';
        }
        cases.flush();
    }
    for (unsigned i = 0; i < ledgers.size(); ++i) {
        const auto& total = ledgers[i];
        const char* scope = i == 3 ? "must_be_rejected" : "acceptance_gate";
        summary << roles[i] << ",90," << total.mean_max << ',' << total.center_max << ',' << total.actor_max << ','
                << total.mean_sum << ',' << std::hypot(total.dx, total.dy) << ',' << std::hypot(total.ax, total.ay)
                << ',' << reaction_counts[i] << ',' << total.accepted() << ',' << scope << '\n';
        checks << "paired role=" << roles[i] << " mean_sum=" << total.mean_sum
               << " center_bias=" << std::hypot(total.dx, total.dy) << " actor_bias=" << std::hypot(total.ax, total.ay)
               << " accepted=" << total.accepted() << std::endl;
    }
    summary.flush();
    require(cases.good() && summary.good(), "Cannot finish coupling replay reports");
    require(ledgers[0].accepted(), "Paired reference repeat exceeds fixed common-state error budgets");
    require(ledgers[1].accepted(), "Local coupling exceeds fixed common-state error budgets");
    require(ledgers[2].accepted(), "Combined coupling exceeds fixed common-state error budgets");
    require(reaction_counts[1] > 0 && reaction_counts[2] > 0, "Paired geometry exercised no actual compact reactions");
    require(!ledgers[3].accepted() && reaction_counts[3] == 0,
            "Common-state oracle failed to reject disabled body coupling");
    checks << "paired_limits mean=.05 center=.02 actor=.15 (per-step and accumulated common-state defect)\n"
           << "paired_state_replay=passed (90 checkpoints, relocation, stable ID/mass, finite state, queue drain)\n"
           << "disabled_coupling_negative_control=rejected\n";
}
} // namespace strefa::gpu
