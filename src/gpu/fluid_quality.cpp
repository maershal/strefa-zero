#include "gpu/world.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error{message};
}
double kernel(double x, double y) {
    const double t = std::max(0.0, 4 - x * x - y * y);
    return t * t * t / (64 * std::numbers::pi);
}
Grain particle(float x, float y, int kind = 5, float vx = 0, float vy = 0) {
    Grain p;
    p.position = {x, y, x, y};
    p.motion = {vx, vy, kind == 6 ? 3.3125F : 2.65F, static_cast<float>(kind)};
    return p;
}
void tick(ParticleWorld& w, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        if (!w.step()) {
            glFinish();
            w.poll(false);
            --i;
        }
        if (i % 16 == 15) {
            glFinish();
            w.poll(false);
        }
    }
    glFinish();
    w.poll(false);
}
struct Totals {
    double px{}, py{}, energy{}, nearest{}, height_mean{}, surface_mean{}, surface_rms{};
    unsigned count{}, water{}, oil{};
};
Totals totals(const std::vector<Grain>& state, const std::vector<Grain>& initial) {
    Totals t;
    std::array<double, width> surface;
    surface.fill(height);
    unsigned columns = 0;
    double sum = 0;
    for (unsigned i = 0; i < state.size(); ++i) {
        const auto& p = state[i];
        require(p.motion[3] == initial[i].motion[3] && p.motion[2] == initial[i].motion[2],
                "Fluid quality changed stable material/mass");
        if (p.motion[3] < .5F) continue;
        ++t.count;
        t.water += p.motion[3] == 5;
        t.oil += p.motion[3] == 6;
        for (const auto* values : {&p.position, &p.motion, &p.contact, &p.residual})
            for (float x : *values) require(std::isfinite(x), "Fluid quality nonfinite state");
        const double mass = 1 / p.motion[2];
        t.px += mass * p.motion[0];
        t.py += mass * p.motion[1];
        t.energy += .5 * mass * (p.motion[0] * p.motion[0] + p.motion[1] * p.motion[1]);
        t.height_mean += p.position[1];
        if (p.motion[3] == 5 || p.motion[3] == 6) {
            const int x = std::clamp(static_cast<int>(p.position[0]), 0, width - 1);
            surface[x] = std::min(surface[x], double(p.position[1]));
        }
    }
    for (double y : surface)
        if (y < height) {
            sum += y;
            ++columns;
        }
    if (columns) {
        sum /= columns;
        t.surface_mean = sum;
        for (double y : surface)
            if (y < height) t.surface_rms += (y - sum) * (y - sum);
        t.surface_rms = std::sqrt(t.surface_rms / columns);
    }
    std::vector<int> head(width * height, -1), next(state.size(), -1);
    unsigned liquids = 0, clusters = 0;
    for (unsigned i = 0; i < state.size(); ++i)
        if (state[i].motion[3] == 5 || state[i].motion[3] == 6) {
            const int x = std::clamp(int(state[i].position[0]), 0, width - 1),
                      y = std::clamp(int(state[i].position[1]), 0, height - 1);
            next[i] = head[y * width + x];
            head[y * width + x] = int(i);
            ++liquids;
        }
    for (unsigned i = 0; i < state.size(); ++i)
        if (state[i].motion[3] == 5 || state[i].motion[3] == 6) {
            const int cx = std::clamp(int(state[i].position[0]), 0, width - 1),
                      cy = std::clamp(int(state[i].position[1]), 0, height - 1);
            bool close = false;
            for (int y = std::max(0, cy - 1); y <= std::min(height - 1, cy + 1); ++y)
                for (int x = std::max(0, cx - 1); x <= std::min(width - 1, cx + 1); ++x)
                    for (int j = head[y * width + x]; j >= 0; j = next[j])
                        if (unsigned(j) != i && state[j].motion[3] == state[i].motion[3]) {
                            const double dx = state[i].position[0] - state[j].position[0],
                                         dy = state[i].position[1] - state[j].position[1];
                            close = close || dx * dx + dy * dy < .25;
                        }
            clusters += close;
        }
    t.nearest = double(clusters) / std::max(1U, liquids);
    t.height_mean /= std::max(1U, t.count);
    return t;
}
void boundary_checks(ParticleWorld& w, std::ostream& checks) {
    std::vector<std::uint32_t> walls(width * height);
    for (int y = 30; y < 70; ++y)
        for (int x = 30; x < 70; ++x) walls[y * width + x] = 1;
    for (int x = 110; x < 140; ++x) walls[50 * width + x] = 1;
    walls[50 * width + 170] = 1;
    w.load({}, walls);
    auto weights = w.read_boundary_weights();
    const auto weighted = w.execution_options().fluid.boundary == FluidBoundary::volume;
    require(std::abs(weights[50 * width + 50] - 1) < 1e-5, "Interior boundary volume");
    require(std::abs(weights[50 * width + 120] - (weighted ? 1.7288135593 : 1)) < 1e-5, "Thin boundary volume");
    require(std::abs(weights[50 * width + 170] - (weighted ? 3.1875 : 1)) < 1e-5, "Isolated boundary volume");
    // Check every occupied sample, including concave/convex corners, against
    // an independent double sum over the authoritative material plane.
    if (weighted)
        for (int y = 3; y < height - 3; ++y)
            for (int x = 3; x < width - 3; ++x)
                if (walls[y * width + x]) {
                    double density = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx)
                            if (walls[(y + dy) * width + x + dx]) density += kernel(dx, dy);
                    require(std::abs(weights[y * width + x] - 1.014612762210833 / density) < 1e-5,
                            "Boundary volume CPU mismatch");
                }
    w.paint({169.5F, 50.5F, 169.5F, 50.5F, 1, Material::wall});
    const auto edited = w.read_boundary_weights();
    auto terrain = w.read_terrain();
    terrain.resize(width * height);
    w.load({}, terrain);
    const auto rebuilt = w.read_boundary_weights();
    require(edited == rebuilt, "Wall edit left stale boundary weights");
    checks << "boundary_unit_volume_CPU,thin_line,isolated,corner,edit=passed\n";
    walls.assign(width * height, 0);
    for (int y = 0; y < height; ++y) walls[y * width + 156] = 1;
    std::vector<Grain> one{particle(155.65F, 60)}, two{one[0], particle(157.35F, 60)};
    w.load(two, walls);
    const auto blocked = w.read_fluid_terms();
    w.load(one, walls);
    const auto alone = w.read_fluid_terms();
    require(std::abs(blocked[0][1] - alone[0][1]) < 1e-6, "Weighted density crossed thin wall");
    checks << "thin_wall_pressure_occlusion=passed\n";
    // Implicit world boundaries must not borrow volume from an empty cell.
    walls.assign(width * height, 0);
    std::vector<Grain> edgeProbe{particle(.5F, 60.5F)};
    w.load(edgeProbe, walls);
    double edgeDensity = kernel(0, 0);
    for (int y = 58; y <= 62; ++y)
        for (int x = -2; x < 0; ++x) {
            double volume = 1;
            if (weighted) {
                double sum = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (x + dx < 0) sum += kernel(dx, dy);
                volume = 1.014612762210833 / sum;
            }
            edgeDensity += volume * kernel(.5 - x - .5, 60.5 - y - .5);
        }
    require(std::abs(w.read_fluid_terms()[0][1] - edgeDensity / 1.014612762210833) < 1e-5,
            "Implicit world boundary density CPU mismatch");
    checks << "implicit_world_boundary_double_oracle=passed\n";
    walls.assign(width * height, 0);
    std::vector<Grain> probe{particle(104.4F, 72)};
    w.load(probe, walls);
    MissionConfig capsule;
    capsule.spawn_x = 100;
    capsule.spawn_y = 80;
    w.configure_mission(capsule);
    const auto capsuleTerms = w.read_fluid_terms();
    double reference = kernel(0, 0);
    const auto inside = [](int x, int y) {
        const double a = x + .5, b = y + .5 - std::clamp(y + .5, -14.0, -4.0);
        return a * a + b * b < 16;
    };
    for (int y = -18; y < 0; ++y)
        for (int x = -4; x < 4; ++x)
            if (inside(x, y)) {
                double volume = 1;
                if (weighted) {
                    double sum = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx)
                            if (inside(x + dx, y + dy)) sum += kernel(dx, dy);
                    volume = 1.014612762210833 / sum;
                }
                reference += volume
                           * kernel(double(probe[0].position[0]) - 100 - x - .5,
                                    double(probe[0].position[1]) - 80 - y - .5);
            }
    require(std::abs(capsuleTerms[0][1] - reference / 1.014612762210833) < 1e-5,
            "Capsule boundary density CPU mismatch");
    checks << "capsule_boundary_double_oracle=passed\n";
    walls.assign(width * height, 0);
    for (int y = 0; y < height; ++y) {
        walls[y * width + 154] = 1;
        walls[y * width + 158] = 1;
    }
    std::vector<Grain> channel;
    for (int y = 40; y < 70; ++y) channel.push_back(particle(156.5F, float(y)));
    w.load(channel, walls);
    tick(w, 120);
    const auto confined = w.readback();
    for (unsigned i = 0; i < channel.size(); ++i)
        require(confined[i].position[0] > 154.3F && confined[i].position[0] < 158.7F, "Narrow channel wall leak");
    checks << "narrow_channel_transport=passed\n";
    // Same geometry after GPU destruction and explicit reload must agree.
    walls.assign(width * height, 0);
    walls[50 * width + 170] = 2;
    w.load({}, walls);
    MissionConfig c;
    c.spawn_x = 240;
    w.configure_mission(c);
    w.queue_explosion({1, 170, 50, 5, 0});
    tick(w, 1);
    terrain = w.read_terrain();
    require(terrain[50 * width + 170] == 0, "Boundary fixture failed to destroy wall");
    const auto destroyed = w.read_boundary_weights();
    terrain.resize(width * height);
    w.load({}, terrain);
    require(destroyed == w.read_boundary_weights(), "Destruction left stale boundary weights");
    checks << "destruction_boundary_refresh=passed\n";
}
void fields_checks(const std::filesystem::path& shaders, const ExecutionOptions& source, std::ostream& checks) {
    ProfileOptions quiet;
    quiet.gpu_timers = false;
    quiet.readback_period = 0;
    const auto create = [&](ExecutionOptions e) {
        auto made = ParticleWorld::create(shaders, e, quiet);
        if (!made) throw std::runtime_error{made.error()};
        return std::move(*made);
    };
    auto e = source;
    e.fluid = {};
    e.fluid.surface = FluidSurface::akinci2d;
    auto surface = create(e);
    std::vector<std::uint32_t> walls(width * height);
    std::vector<Grain> pair{particle(80, 60), particle(81, 60)};
    pair[1].motion[2] = 3.3125F;
    surface->load(pair, walls);
    surface->apply_fluid_velocities_for_test();
    auto result = surface->readback();
    const double rho = kernel(0, 0) + kernel(1, 0), g = -54 / (64 * std::numbers::pi), normal = -2 * g / rho;
    const double coh = 35840 / (209 * std::numbers::pi * 256), force = 75.6 * coh + .5 * (2 * normal);
    const double expected = -pair[0].motion[2] * (1.014612762210833 / rho) * force / (120 * e.fluid.substeps);
    // For p0 at x=80, displacement to p1 is -1: both directed terms reverse.
    require(std::abs(result[0].motion[0] + expected) < 2e-5, "2D surface impulse CPU mismatch");
    require(std::abs(result[0].motion[0] / pair[0].motion[2] + result[1].motion[0] / pair[1].motion[2]) < 1e-6,
            "Pair surface momentum not conserved");
    checks << "surface_2D_double_oracle,unequal_mass_pair_momentum=passed\n";
    e.fluid = {};
    e.fluid.boundary = FluidBoundary::volume;
    e.fluid.adhesion = 1;
    auto adh = create(e);
    walls[60 * width + 100] = 1;
    std::vector<Grain> wallProbe{particle(102, 60.5F)};
    adh->load(wallProbe, walls);
    adh->apply_fluid_velocities_for_test();
    const double alpha = 8 * std::sqrt(2.0) * std::tgamma(1.75)
                       / (3 * std::pow(std::numbers::pi, 1.5) * std::tgamma(1.25)) * std::pow(2, -2.25);
    const double expectedAdhesion = -2.65 * 3.1875 * alpha * std::pow(.5, .25) / 240;
    require(std::abs(adh->readback()[0].motion[0] - expectedAdhesion) < 1e-6, "Wall adhesion CPU mismatch");
    walls.assign(width * height, 0);
    wallProbe = {particle(104.4F, 72)};
    adh->load(wallProbe, walls);
    MissionConfig actor;
    actor.spawn_x = 100;
    actor.spawn_y = 80;
    adh->configure_mission(actor);
    const auto state = adh->read_mission();
    adh->apply_fluid_velocities_for_test();
    const auto moved = adh->readback();
    const auto body = adh->read_body();
    const double expectedReaction = -moved[0].motion[0] * (state.body[0] / moved[0].motion[2]) / 240;
    require(std::abs(expectedReaction) > 1e-9 && std::abs(body.total_sum[0] - expectedReaction) < 1e-7,
            "Capsule adhesion lost opposite impulse");
    checks << "adhesion_2D_double_oracle,capsule_opposite_impulse=passed\n";

    e.fluid = {};
    e.fluid.vorticity = 1;
    auto vort = create(e);
    e.fluid.vorticity = 0;
    auto base = create(e);
    for (int mode = 0; mode < 5; ++mode) {
        std::vector<Grain> particles;
        for (int y = -5; y <= 5; ++y)
            for (int x = -5; x <= 5; ++x) {
                float vx = 0, vy = 0;
                if (mode == 1) {
                    vx = 4;
                    vy = 2;
                }
                if (mode == 2) {
                    vx = -float(y);
                    vy = float(x);
                }
                if (mode == 3) {
                    vx = float(y);
                    vy = -float(x);
                }
                if (mode == 4) vx = float(y);
                particles.push_back(particle(100.F + x, 70.F + y, 5, vx, vy));
            }
        vort->load(particles, walls);
        base->load(particles, walls);
        const auto fields = vort->fluid_fields_for_test();
        const auto center = capacity + 60;
        if (mode < 2) require(std::abs(fields[center][2]) < 1e-6, "Spurious omega at rest/translation");
        if (mode == 2) require(fields[center][2] > 0, "Wrong positive rotation curl");
        if (mode == 3) require(fields[center][2] < 0, "Wrong negative rotation curl");
        vort->apply_fluid_velocities_for_test();
        base->apply_fluid_velocities_for_test();
        const auto a = vort->readback(), b = base->readback();
        for (unsigned i = 0; i < particles.size(); ++i) {
            const double delta = std::hypot(a[i].motion[0] - b[i].motion[0], a[i].motion[1] - b[i].motion[1]);
            require(delta <= .25 * fields[capacity + i][3] + 2e-6, "Vorticity exceeded RMS cap");
            if (mode < 2) require(delta < 1e-6, "Vorticity excited rest/translation");
        }
    }
    checks << "vorticity_rest,translation,shear,both_signs,RMS_cap=passed\n";
}
void benchmark(ParticleWorld& w, const std::filesystem::path& out) {
    std::ofstream samples{out / "fluid-timings.csv"}, summary{out / "fluid-benchmark.csv"};
    samples << "scene,sample,gpu_ms,cpu_ms\n" << std::setprecision(12);
    summary << "scene,particles,gpu_median_ms,gpu_p95_ms,gpu_p99_ms,cpu_median_ms,cpu_p95_ms,cpu_p99_ms,dispatches,barriers,bins,device_bytes\n"
            << std::setprecision(12);
    const auto pct = [](std::vector<double> v, double q) {
        std::ranges::sort(v);
        return v[static_cast<std::size_t>(q * (v.size() - 1))];
    };
    for (unsigned count : {3096U, 16000U, 32768U}) {
        if (count == 3096)
            w.reset_mission();
        else {
            std::vector<Grain> grains(count);
            std::vector<std::uint32_t> walls(width * height);
            for (int y = 187; y < height; ++y)
                for (int x = 0; x < width; ++x) walls[y * width + x] = 1;
            for (unsigned i = 0; i < count; ++i) grains[i] = particle(1.5F + float(i % 300), 1.5F + float(i / 300), 1);
            w.load(grains, walls);
            w.configure_mission({});
        }
        tick(w, 120);
        static_cast<void>(w.take_timings());
        static_cast<void>(w.take_cpu_timings());
        const auto before = w.counters();
        for (unsigned i = 0; i < 240; ++i) {
            glFinish();
            w.poll(false);
            require(w.step(), "Unexpected benchmark admission failure");
        }
        glFinish();
        w.poll(false);
        const auto gpu = w.take_timings(), cpu = w.take_cpu_timings();
        const auto calls = w.counters() - before;
        require(gpu.size() == 240 && cpu.size() == 240, "Missing fluid benchmark samples");
        const char* name = count == 3096 ? "mission" : count == 16000 ? "stress_16k" : "stress_32k";
        for (unsigned i = 0; i < 240; ++i) samples << name << ',' << i << ',' << gpu[i] << ',' << cpu[i] << '\n';
        summary << name << ',' << count << ',' << pct(gpu, .5) << ',' << pct(gpu, .95) << ',' << pct(gpu, .99) << ','
                << pct(cpu, .5) << ',' << pct(cpu, .95) << ',' << pct(cpu, .99) << ',' << calls.dispatches / 240.0
                << ',' << calls.barriers / 240.0 << ',' << calls.bins / 240.0 << ',' << w.upload_stats().device_bytes
                << std::endl;
    }
    require(samples.good() && summary.good(), "Incomplete fluid benchmark output");
}
} // namespace
std::expected<void, std::string> validate_fluid_quality(const std::filesystem::path& shaders,
                                                        const std::filesystem::path& output,
                                                        const ExecutionOptions& execution, bool timed) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream checks{output / "fluid-checks.txt"}, env{output / "fluid-environment.txt"};
        checks << std::setprecision(12);
        env << "revision=I07.1\nfluid=" << fluid_configuration(execution.fluid) << '\n';
        for (const auto& [label, key] :
             {std::pair{"vendor", GL_VENDOR}, std::pair{"renderer", GL_RENDERER}, std::pair{"version", GL_VERSION}})
            env << label << '=' << reinterpret_cast<const char*>(glGetString(key)) << '\n';
        ProfileOptions options;
        options.collect = true;
        options.gpu_timers = timed;
        options.readback_period = 2;
        auto made = ParticleWorld::create(shaders, execution, options);
        if (!made) return std::unexpected{made.error()};
        auto& world = **made;
        if (timed) {
            benchmark(world, output);
            checks << "status=passed\n";
            return {};
        }
        boundary_checks(world, checks);
        fields_checks(shaders, execution, checks);
        std::ofstream quality{output / "fluid-quality.csv"};
        quality << "scene,tick,count,water,oil,compression,surface_spatial_rms,surface_mean,cluster_fraction,mean_y,px,py,kinetic_energy\n"
                << std::setprecision(12);
        for (const auto scene : {Scene::water_tank, Scene::connected_vessels, Scene::dam_break, Scene::oil_drop,
                                 Scene::oil_water, Scene::inverted_oil, Scene::sand_water}) {
            world.reset(scene);
            const auto initial = world.readback();
            for (unsigned step = 60; step <= 1200; step += 60) {
                tick(world, 60);
                const auto state = world.readback();
                const auto t = totals(state, initial);
                const auto terms = world.read_fluid_terms();
                double compression = 0;
                for (unsigned i = 0; i < state.size(); ++i)
                    if (state[i].motion[3] == 5 || state[i].motion[3] == 6)
                        compression = std::max(compression, double(terms[i][1]) - 1);
                quality << static_cast<int>(scene) << ',' << step << ',' << t.count << ',' << t.water << ',' << t.oil
                        << ',' << compression << ',' << t.surface_rms << ',' << t.surface_mean << ',' << t.nearest
                        << ',' << t.height_mean << ',' << t.px << ',' << t.py << ',' << t.energy << std::endl;
                if (step == 600 || step == 1200) {
                    world.render(1);
                    world.save_image(output
                                     / ("fluid-" + std::to_string(static_cast<int>(scene)) + "-" + std::to_string(step)
                                        + ".bmp"));
                }
                if (step == 1200)
                    require(compression < .05, "Fluid quality exceeds existing 5 percent compression limit");
            }
        }
        require(quality.good() && checks.good() && env.good(), "Incomplete fluid quality output");
        require(glGetError() == GL_NO_ERROR, "OpenGL error in fluid quality validation");
        checks << "status=passed\n";
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "fluid-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
