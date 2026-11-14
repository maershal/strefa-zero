// --gpu-check thermal-bits: bit digests of the thermal state and of the world state it touches, after a fixed tick
// sequence per scene. The digests are compared between two builds on the same machine and driver (no reference in
// the repository). Every scene runs twice in one process; only scenes that repeat bit for bit are comparable across
// builds. Scenes with particles do not repeat today: the particle solver appends to the active list and the neighbor
// index with atomics, so their order, and with it the float summation order, changes from run to run.
#include "gpu/render_device.hpp"
#include "gpu/validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace strefa::gpu {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error{message};
}
// FNV-1a over 32-bit words: every bit of every word reaches the digest, independent of the field types.
std::uint64_t digest(std::span<const std::byte> bytes) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i + 4 <= bytes.size(); i += 4) {
        std::uint32_t word{};
        std::memcpy(&word, bytes.data() + i, 4);
        h ^= word;
        h *= 0x100000001b3ULL;
    }
    return h;
}
template<class T> std::uint64_t digest_of(const std::vector<T>& values) {
    return digest(std::as_bytes(std::span{values}));
}
// Digest of field `field` (a vec4 at offset 16*field) of every record of `values`.
template<class T> std::uint64_t field_digest(const std::vector<T>& values, unsigned field) {
    static_assert(sizeof(T) % 16 == 0);
    std::vector<std::byte> bytes(values.size() * 16);
    for (std::size_t i = 0; i < values.size(); ++i)
        std::memcpy(bytes.data() + i * 16, reinterpret_cast<const std::byte*>(&values[i]) + field * 16, 16);
    return digest(bytes);
}
// Changed words per vec4 field of the records.
template<class T> std::string changed_fields(const std::vector<T>& a, const std::vector<T>& b) {
    std::string out;
    for (unsigned f = 0; f < sizeof(T) / 16; ++f) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            n += std::memcmp(reinterpret_cast<const std::byte*>(&a[i]) + 16 * f,
                             reinterpret_cast<const std::byte*>(&b[i]) + 16 * f, 16)
              != 0;
        out += std::format("{}{}", f ? "," : "", n);
    }
    return out;
}
template<class T> std::size_t changed_words(const std::vector<T>& a, const std::vector<T>& b) {
    const auto x = std::as_bytes(std::span{a}), y = std::as_bytes(std::span{b});
    std::size_t n = 0;
    for (std::size_t i = 0; i + 4 <= x.size(); i += 4) n += std::memcmp(x.data() + i, y.data() + i, 4) != 0;
    return n;
}
// The state a thermal optimization must leave bit for bit: thermal cells and particles (current banks), world
// particles (current bank, all slots) and terrain. Every read goes through a staging copy.
struct State {
    std::vector<ThermalCell> cells;
    std::vector<ThermalParticle> heat;
    std::vector<Grain> particles;
    std::vector<std::uint32_t> terrain;
    ThermalTotals totals;
};
State capture(ParticleWorld& world) {
    glFinish();
    auto thermal = world.read_thermal(ThermalRead::all);
    State s{std::move(thermal.cells), std::move(thermal.particles), world.readback(), world.read_terrain(),
            thermal.totals};
    world.invalidate_gl_state();
    return s;
}
std::string line(const State& s) {
    return std::format("cells={:016x} heat={:016x} particles={:016x} terrain={:016x}", digest_of(s.cells),
                       digest_of(s.heat), digest_of(s.particles), digest_of(s.terrain));
}
std::string fields(const State& s) {
    std::string out = "cells";
    for (unsigned f = 0; f < sizeof(ThermalCell) / 16; ++f) out += std::format(" {:016x}", field_digest(s.cells, f));
    out += " heat";
    for (unsigned f = 0; f < 4; ++f) out += std::format(" {:016x}", field_digest(s.heat, f));
    out += " particles";
    for (unsigned f = 0; f < 4; ++f) out += std::format(" {:016x}", field_digest(s.particles, f));
    return out;
}
unsigned checkpoint_every() {
    const char* text = SDL_getenv("STREFA_THERMAL_BITS_EVERY");
    unsigned value = 60;
    if (text && *text) {
        const std::string_view view{text};
        const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
        if (error != std::errc{} || end != view.data() + view.size() || value == 0 || 60 % value != 0)
            throw std::runtime_error{"STREFA_THERMAL_BITS_EVERY: dzielnik 60."};
    }
    return value;
}
unsigned environment_ticks() {
    const char* text = SDL_getenv("STREFA_THERMAL_BITS_TICKS");
    if (!text || !*text) return 600;
    unsigned value{};
    const std::string_view view{text};
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (error != std::errc{} || end != view.data() + view.size() || value < 60 || value % 60 != 0)
        throw std::runtime_error{"STREFA_THERMAL_BITS_TICKS: wielokrotność 60, co najmniej 60."};
    return value;
}
// "control" is the cold scene with thermal off: it separates the particle solver from the thermal model. "idle" is
// the empty scene with thermal on, a world in equilibrium without particles.
constexpr std::array<std::string_view, 7> scene_names{"cold", "fire", "oil", "quench", "phases", "control", "idle"};
// Fixed brush stroke of the cold scene after its fixed-point probe: heat, then 120 ticks without input.
constexpr unsigned brush_ticks = 10, after_brush = 120;

// One scene from a fresh world: checkpoint digests every 60 ticks, per-field digests at the end, the fixed-point
// probe (words a single further tick changes) and, in the cold scene, a heat brush and its aftermath.
struct SceneRun {
    std::vector<std::string> rows; // digests, compared between runs and builds
    ThermalTotals gate;            // gas-stage gate counters at the end, reported apart from the digests
};
// audit: the gas stage runs every tick and the gate only counts the skips it would have made and the cells such a
// stage changed (ThermalOptions::gate_audit).
SceneRun run_scene(const std::filesystem::path& shaders, const ExecutionOptions& layout,
                   std::shared_ptr<RenderDevice>& device, unsigned scene, unsigned ticks, unsigned every,
                   bool audit = false) {
    auto made = ParticleWorld::create(shaders, layout, {}, {}, device);
    require(made.has_value(), made ? "" : made.error());
    auto& world = **made;
    if (!device) device = world.render_device();
    world.set_vfx_options({VfxQuality::off, false});
    ThermalOptions initial;
    initial.gate_audit = audit;
    world.set_thermal_options(initial); // reset_thermal_scene keeps these options and enables thermal
    if (scene == 0 || scene >= 5) {
        ThermalOptions options = initial;
        options.enabled = scene != 5;
        world.set_thermal_options(options);
        world.reset(scene == 6 ? Scene::empty : Scene::sand_water);
    } else {
        world.reset_thermal_scene(scene);
    }
    std::vector<std::string> rows;
    const auto name = scene_names[scene];
    const auto advance = [&] {
        while (!world.step()) glFinish();
    };
    rows.push_back(std::format("{} tick=0 {}", name, line(capture(world))));
    for (unsigned tick = 1; tick <= ticks; ++tick) {
        advance();
        if (tick % every == 0) rows.push_back(std::format("{} tick={} {}", name, tick, line(capture(world))));
    }
    const auto before = capture(world);
    rows.push_back(std::format("{} tick={} fields {}", name, ticks, fields(before)));
    advance();
    const auto after = capture(world);
    rows.push_back(std::format("{} fixed_point tick={} cells_changed={} heat_changed={} particles_changed={} "
                               "terrain_changed={} cell_fields={} heat_fields={}",
                               name, ticks + 1, changed_words(before.cells, after.cells),
                               changed_words(before.heat, after.heat), changed_words(before.particles, after.particles),
                               changed_words(before.terrain, after.terrain), changed_fields(before.cells, after.cells),
                               changed_fields(before.heat, after.heat)));
    if (scene == 0) {
        ThermalBrush brush;
        brush.tool = ThermalTool::heat;
        brush.radius = 8;
        brush.rate = 4000;
        for (unsigned n = 0; n < brush_ticks; ++n) {
            const float x = 120.F + 8.F * static_cast<float>(n);
            brush.segment = {x, 150.F, x + 8.F, 150.F};
            world.set_thermal_brush(brush);
            advance();
        }
        world.set_thermal_brush({});
        for (unsigned n = 0; n < after_brush; ++n) advance();
        const auto brushed = capture(world);
        const auto at = ticks + 1 + brush_ticks + after_brush;
        rows.push_back(std::format("{} brush tick={} {}", name, at, line(brushed)));
        rows.push_back(std::format("{} brush tick={} fields {}", name, at, fields(brushed)));
    }
    require(glGetError() == GL_NO_ERROR, std::format("Błąd OpenGL w scenie {}.", name));
    return {std::move(rows), capture(world).totals};
}
} // namespace

std::expected<void, std::string> validate_thermal_bits(const std::filesystem::path& shaders,
                                                       const std::filesystem::path& output,
                                                       const ExecutionOptions& layout) {
    try {
        std::filesystem::create_directories(output);
        const auto ticks = environment_ticks();
        const auto every = checkpoint_every();
        std::shared_ptr<RenderDevice> device;
        std::array<std::vector<std::vector<std::string>>, 2> runs;
        std::vector<ThermalTotals> gates;
        for (auto& run : runs)
            for (unsigned scene = 0; scene < scene_names.size(); ++scene) {
                auto result = run_scene(shaders, layout, device, scene, ticks, every);
                run.push_back(std::move(result.rows));
                if (&run == &runs[0]) gates.push_back(result.gate);
            }
        std::vector<ThermalTotals> audits;
        for (unsigned scene = 0; scene < scene_names.size(); ++scene)
            audits.push_back(run_scene(shaders, layout, device, scene, ticks, every, true).gate);
        // Header separate from the digests: digests are diffed between builds, the header names the machine.
        std::ofstream info{output / "thermal-bits-info.txt"};
        const auto text = [](GLenum name) {
            const auto* value = reinterpret_cast<const char*>(glGetString(name));
            return std::string{value ? value : "?"};
        };
        info << "renderer=" << text(GL_RENDERER) << "\nversion=" << text(GL_VERSION) << "\nvendor=" << text(GL_VENDOR)
             << "\nticks=" << ticks << "\nevery=" << every << '\n';
        // Scenes that repeat within this run go to thermal-bits.txt, the file diffed between builds. A scene whose two
        // runs differ (the particle solver orders atomics differently from run to run) goes to
        // thermal-bits-unstable.txt: its digests prove nothing across builds.
        std::ofstream report{output / "thermal-bits.txt"}, unstable{output / "thermal-bits-unstable.txt"};
        require(report && unstable && info, "Nie można zapisać thermal-bits.txt.");
        std::vector<std::string> failures;
        for (unsigned scene = 0; scene < scene_names.size(); ++scene) {
            const auto& first = runs[0][scene];
            const auto& second = runs[1][scene];
            std::size_t diverged = first.size();
            for (std::size_t i = 0; i < first.size() && diverged == first.size(); ++i)
                if (i >= second.size() || second[i] != first[i]) diverged = i;
            const bool repeats = diverged == first.size() && first.size() == second.size();
            info << "repeat " << scene_names[scene] << '=' << (repeats ? "yes" : "no");
            if (!repeats && diverged < first.size()) info << " first_difference=\"" << first[diverged] << '"';
            info << '\n';
            for (const auto& row : first) (repeats ? report : unstable) << row << '\n';
            // Scenes without particles never run the particle solver and must repeat.
            if (!repeats && (scene_names[scene] == "fire" || scene_names[scene] == "idle"))
                failures.push_back(std::format("scena {} nie powtarza się w jednym procesie", scene_names[scene]));
        }
        // The empty world with thermal on is in equilibrium: one more tick must not change a single word.
        // Gas-stage gate: skipped ticks of the first run, and the audit run in which every would-be skip is checked
        // against the stage it replaced.
        for (unsigned scene = 0; scene < scene_names.size(); ++scene) {
            info << "gate " << scene_names[scene] << " ticks=" << gates[scene].gate_ticks
                 << " skipped=" << gates[scene].gate_skips << " audit_ticks=" << audits[scene].gate_ticks
                 << " audit_skippable=" << audits[scene].gate_skips
                 << " audit_violations=" << audits[scene].gate_violations << '\n';
            if (audits[scene].gate_violations != 0)
                failures.push_back(
                        std::format("bramka gazu w scenie {} pominęłaby etap, który zmienia stan", scene_names[scene]));
        }
        const auto& idle = runs[0].back();
        const auto fixed = std::ranges::find_if(idle, [](const std::string& r) { return r.contains("fixed_point"); });
        if (fixed == idle.end() || !fixed->contains("cells_changed=0 heat_changed=0 particles_changed=0"))
            failures.push_back("tick termiki w pustym świecie nie jest punktem stałym");
        for (const auto& f : failures) info << "failure " << f << '\n';
        require(report.good() && unstable.good() && info.good(), "Niepełny zapis thermal-bits.txt.");
        if (!failures.empty())
            throw std::runtime_error{
                    std::format("thermal-bits: {} (szczegóły w thermal-bits-info.txt).", failures.front())};
        info << "thermal_bits=passed\n";
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "thermal-bits-error.txt"} << e.what() << '\n';
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
