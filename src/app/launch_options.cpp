#include "app/application.hpp"
#include "core/charconv.hpp"
#include "gpu/world.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <utility>
namespace strefa::app {
namespace {
std::filesystem::path utf8_path(const char* text) {
    return std::filesystem::path{reinterpret_cast<const char8_t*>(text)};
}
struct PlayerOption {
    std::string_view name, value, description;
};
// The only options StrefaZero accepts; StrefaQA additionally accepts every validation, benchmark and tuning option.
constexpr std::array player_options{
        PlayerOption{"--profile", "<katalog>", "katalog ustawień i raportów (domyślnie katalog użytkownika)"},
        PlayerOption{"--mission", "", "start w misji GPU"},
        PlayerOption{"--gpu-laboratory", "", "start w laboratorium GPU"},
        PlayerOption{"--laboratory", "", "start w modelu referencyjnym (CPU)"},
        PlayerOption{"--scene", "<gpu|lab>:<id>",
                     "start w scenie laboratorium GPU lub CPU (zły identyfikator wypisuje listę)"},
        PlayerOption{"--opengl33", "", "tryb zgodności OpenGL 3.3 (bez trybów GPU)"},
        PlayerOption{"--gl-debug", "", "zapis komunikatów sterownika OpenGL do gl-debug.txt w katalogu profilu"},
        PlayerOption{"--smoke-test", "", "test dymny menu (wymaga --profile)"},
        PlayerOption{"--mission-smoke-test", "", "test dymny misji GPU (wymaga --profile)"},
        PlayerOption{"--help", "", "ta pomoc"},
};
} // namespace
std::string help_text(OptionScope scope) {
    std::string text = scope == OptionScope::player ? "Strefa Zero — opcje uruchomienia:\n"
                                                    : "StrefaQA — opcje gry (definicje: src/app/launch_options.cpp):\n";
    for (const auto& option : player_options) {
        std::string usage{option.name};
        if (!option.value.empty()) usage += " " + std::string{option.value};
        usage.resize(std::max<std::size_t>(usage.size() + 2, 24), ' ');
        text += "  " + usage + std::string{option.description} + "\n";
    }
    if (scope == OptionScope::all)
        text += "Walidacje, benchmarki, profile i pokrętła GPU: README.md, sekcja StrefaQA.\n";
    return text;
}
int LaunchOptions::test_count() const {
    return static_cast<int>(smoke_test) + laboratory_smoke_test + gpu_test + gpu_liquid_test + gpu_flow_test
         + gpu_brush_test + gpu_smoke_test + mission_test + mission_smoke + gpu_profile + stabilization_profile
         + gpu_profile_check + readback_test + readback_profile + execution_test + execution_profile + neighbor_test
         + neighbor_profile + coupling_test + coupling_profile + work_test + transport_test + hot_test + cost_profile
         + cost_check + thermal_check + thermal_benchmark + vfx_check + vfx_benchmark + lighting_check
         + lighting_benchmark + render_check + render_benchmark + fluid_check + fluid_benchmark + session_smoke
         + cache_check + mission_ui_smoke + fault_smoke + !benchmark_scenes.empty() + !thermal_conformance.empty()
         + !gpu_check.empty();
}
StartMode LaunchOptions::start_mode() const {
    if (start_in_laboratory) return StartMode::cpu_laboratory;
    if (start_in_gpu) return StartMode::gpu_laboratory;
    if (start_in_mission) return StartMode::mission;
    return StartMode::menu;
}
SmokeScenario LaunchOptions::smoke_scenario() const {
    if (smoke_test) return SmokeScenario::menu;
    if (laboratory_smoke_test) return SmokeScenario::cpu_laboratory;
    if (gpu_smoke_test) return SmokeScenario::gpu_laboratory;
    if (mission_smoke) return SmokeScenario::mission;
    if (session_smoke) return SmokeScenario::session;
    if (mission_ui_smoke) return SmokeScenario::mission_quick;
    if (fault_smoke) return SmokeScenario::mode_faults;
    if (!benchmark_scenes.empty()) return SmokeScenario::scene_benchmark;
    return SmokeScenario::none;
}
std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv, OptionScope scope) {
    LaunchOptions result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (scope == OptionScope::player
            && std::ranges::none_of(player_options, [&](const auto& option) { return option.name == argument; }))
            return std::unexpected{"Nieznany albo niepełny argument: " + std::string{argument}};
        if (argument == "--help") {
            result.help = true;
            return result;
        }
        if (argument == "--gpu-lighting-check")
            result.lighting_check = true;
        else if (argument == "--gpu-lighting-benchmark")
            result.lighting_benchmark = true;
        else if (argument == "--gpu-lighting-quality" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            result.lighting_quality_set = true;
            if (value == "off")
                result.gpu_render.lighting.quality = gpu::LightingQuality::off;
            else if (value == "low")
                result.gpu_render.lighting.quality = gpu::LightingQuality::low;
            else if (value == "high")
                result.gpu_render.lighting.quality = gpu::LightingQuality::high;
            else
                return std::unexpected{"Oświetlenie: off, low lub high."};
        } else if (argument == "--gpu-thermal-check")
            result.thermal_check = true;
        else if (argument == "--gpu-thermal-benchmark")
            result.thermal_benchmark = true;
        else if (argument == "--gpu-thermal-conformance" && i + 1 < argc)
            result.thermal_conformance = utf8_path(argv[++i]);
        else if (argument == "--gpu-check" && i + 1 < argc) {
            result.gpu_check = argv[++i];
            if (std::ranges::find(gpu_checks, result.gpu_check) == gpu_checks.end())
                return std::unexpected{"Nieznany test GPU: " + result.gpu_check + " (dostępne: lab-blast)."};
        } else if (argument == "--gpu-batched-blasts" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "on" && value != "off") return std::unexpected{"Wybuchy wsadowe w misji: on lub off."};
            result.gpu_execution.batched_blasts = value == "on";
        } else if (argument == "--gpu-thermal-scene" && i + 1 < argc) {
            const auto scene = find_scene_id(thermal_scene_ids, argv[++i], "termiki");
            if (!scene) return std::unexpected{scene.error()};
            result.thermal_scene = *scene + 1;
            result.gpu_thermal.enabled = true;
            result.start_in_gpu = true;
        } else if (argument == "--scene" && i + 1 < argc) {
            if (std::exchange(result.scene_set, true)) return std::unexpected{"Podaj --scene tylko raz."};
            const std::string_view value{argv[++i]};
            const auto colon = value.find(':');
            const auto group = value.substr(0, colon);
            const auto name = colon == std::string_view::npos ? std::string_view{} : value.substr(colon + 1);
            if (group == "gpu") {
                const auto scene = find_scene_id(gpu::scene_ids, name, "GPU");
                if (!scene) return std::unexpected{scene.error()};
                result.gpu_scene = *scene;
                result.start_in_gpu = true;
            } else if (group == "lab") {
                const auto scene = find_scene_id(sim::scene_ids, name, "modelu referencyjnego (CPU)");
                if (!scene) return std::unexpected{scene.error()};
                result.lab_scene = *scene;
                result.start_in_laboratory = true;
            } else
                return std::unexpected{
                        "Scena startowa: gpu:<id> (laboratorium GPU) albo lab:<id> (model referencyjny CPU)."};
        } else if (argument == "--scene-benchmark" && i + 1 < argc) {
            auto scenes = parse_benchmark_scenes(argv[++i]);
            if (!scenes) return std::unexpected{scenes.error()};
            result.benchmark_scenes = std::move(*scenes);
        } else if (argument == "--seconds" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.benchmark_seconds);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result.benchmark_seconds < 1
                || result.benchmark_seconds > 600)
                return std::unexpected{"Czas pomiaru sceny: 1–600 s."};
            result.benchmark_tuned = true;
        } else if ((argument == "--repeat" || argument == "--loop-minutes") && i + 1 < argc) {
            const bool repeat = argument == "--repeat";
            const std::string_view value{argv[++i]};
            unsigned& target = repeat ? result.benchmark_repeat : result.benchmark_loop_minutes;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), target);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || target < 1
                || target > (repeat ? 1000U : 240U))
                return std::unexpected{repeat ? "Liczba przebiegów (--repeat): 1–1000."
                                              : "Czas pętli (--loop-minutes): 1–240 min."};
            result.benchmark_tuned = true;
        } else if (argument == "--vsync" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "on" && value != "off") return std::unexpected{"VSync benchmarku: on lub off."};
            result.benchmark_vsync = value == "on";
            result.benchmark_tuned = true;
        } else if (argument == "--gpu-thermal" && i + 1 < argc) {
            const std::string_view mode{argv[++i]};
            if (mode != "on" && mode != "off") return std::unexpected{"Termika GPU: on lub off."};
            result.gpu_thermal.enabled = mode == "on";
        } else if (argument == "--gpu-vfx-check")
            result.vfx_check = true;
        else if (argument == "--gpu-vfx-benchmark")
            result.vfx_benchmark = true;
        else if (argument == "--gpu-vfx-quality" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            result.vfx_quality_set = true;
            if (value == "off")
                result.gpu_vfx.quality = gpu::VfxQuality::off;
            else if (value == "low")
                result.gpu_vfx.quality = gpu::VfxQuality::low;
            else if (value == "high")
                result.gpu_vfx.quality = gpu::VfxQuality::high;
            else
                return std::unexpected{"Jakość VFX: off, low lub high."};
        } else if (argument == "--gpu-render-check")
            result.render_check = true;
        else if (argument == "--gpu-render-benchmark")
            result.render_benchmark = true;
        else if (argument == "--gpu-render-quality" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            result.render_quality_set = true;
            if (value == "basic")
                result.gpu_render.quality = gpu::RenderQuality::basic;
            else if (value == "standard")
                result.gpu_render.quality = gpu::RenderQuality::standard;
            else if (value == "high")
                result.gpu_render.quality = gpu::RenderQuality::high;
            else
                return std::unexpected{"Jakość renderowania: basic, standard lub high."};
        } else if (argument == "--session-smoke-test")
            result.session_smoke = true;
        else if (argument == "--mission-ui-smoke-test")
            result.mission_ui_smoke = true;
        else if (argument == "--mode-fault-test")
            result.fault_smoke = true;
        else if (argument == "--shader-cache-check")
            result.cache_check = true;
        else if (argument == "--shader-cache" && i + 1 < argc) {
            const std::string_view mode{argv[++i]};
            if (mode != "on" && mode != "off") return std::unexpected{"Cache programów: on lub off."};
            result.shader_cache = mode == "on";
        } else if (argument == "--gpu-fluid-check")
            result.fluid_check = true;
        else if (argument == "--gpu-fluid-benchmark")
            result.fluid_benchmark = true;
        else if (argument == "--gpu-fluid-profile" && i + 1 < argc) {
            const std::string_view v{argv[++i]};
            if (v == "legacy")
                result.gpu_execution.fluid = {};
            else if (v == "balanced")
                result.gpu_execution.fluid = gpu::FluidOptions::balanced();
            else
                return std::unexpected{"Profil cieczy: legacy lub balanced (eksperymentalny)."};
        } else if ((argument == "--gpu-fluid-boundary" || argument == "--gpu-fluid-surface") && i + 1 < argc) {
            const std::string_view v{argv[++i]};
            if (argument == "--gpu-fluid-boundary") {
                if (v == "unit")
                    result.gpu_execution.fluid.boundary = gpu::FluidBoundary::unit;
                else if (v == "volume")
                    result.gpu_execution.fluid.boundary = gpu::FluidBoundary::volume;
                else
                    return std::unexpected{"Granice cieczy: unit lub volume."};
            } else {
                if (v == "legacy")
                    result.gpu_execution.fluid.surface = gpu::FluidSurface::legacy;
                else if (v == "akinci2d")
                    result.gpu_execution.fluid.surface = gpu::FluidSurface::akinci2d;
                else
                    return std::unexpected{"Napięcie cieczy: legacy lub akinci2d."};
            }
        } else if (argument == "--gpu-fluid-substeps" && i + 1 < argc) {
            const std::string_view v{argv[++i]};
            auto& f = result.gpu_execution.fluid;
            if (v == "2") {
                f.substeps = 2;
                f.pbf_iterations = 8;
                f.contact_iterations = 12;
            } else if (v == "4") {
                f.substeps = 4;
                f.pbf_iterations = 4;
                f.contact_iterations = 6;
            } else
                return std::unexpected{"Podkroki cieczy: 2 lub 4."};
        } else if ((argument == "--gpu-fluid-s-corr" || argument == "--gpu-fluid-tension-scale"
                    || argument == "--gpu-fluid-curvature-scale" || argument == "--gpu-fluid-adhesion"
                    || argument == "--gpu-fluid-vorticity")
                   && i + 1 < argc) {
            const std::string_view v{argv[++i]};
            float value{};
            const auto parsed = core::from_chars(v.data(), v.data() + v.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != v.data() + v.size() || !std::isfinite(value))
                return std::unexpected{"Nieprawidłowy współczynnik cieczy."};
            auto& f = result.gpu_execution.fluid;
            if (argument == "--gpu-fluid-s-corr")
                f.s_corr = value;
            else if (argument == "--gpu-fluid-tension-scale")
                f.tension_scale = value;
            else if (argument == "--gpu-fluid-curvature-scale")
                f.curvature_scale = value;
            else if (argument == "--gpu-fluid-adhesion")
                f.adhesion = value;
            else
                f.vorticity = value;
        } else if (argument == "--smoke-test")
            result.smoke_test = true;
        else if (argument == "--lab-smoke-test")
            result.laboratory_smoke_test = true;
        else if (argument == "--laboratory")
            result.start_in_laboratory = true;
        else if (argument == "--gpu-laboratory")
            result.start_in_gpu = true;
        else if (argument == "--mission")
            result.start_in_mission = true;
        else if (argument == "--gameplay-check") {
            result.mission_test = true;
            result.gameplay_test = true;
        } else if (argument == "--mission-test")
            result.mission_test = true;
        else if (argument == "--mission-smoke-test")
            result.mission_smoke = true;
        else if (argument == "--gpu-test")
            result.gpu_test = true;
        else if (argument == "--gpu-liquid-test")
            result.gpu_liquid_test = true;
        else if (argument == "--gpu-flow-test")
            result.gpu_flow_test = true;
        else if (argument == "--gpu-brush-check")
            result.gpu_brush_test = true;
        else if (argument == "--gpu-smoke-test")
            result.gpu_smoke_test = true;
        else if (argument == "--opengl33")
            result.legacy_graphics = true;
        else if (argument == "--gpu-work-check")
            result.work_test = true;
        else if (argument == "--gpu-transport-check")
            result.transport_test = true;
        else if (argument == "--gpu-hot-check")
            result.hot_test = true;
        else if (argument == "--gpu-cost-profile")
            result.cost_profile = true;
        else if (argument == "--gpu-cost-check")
            result.cost_check = true;
        else if (argument == "--gpu-cost-mode" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            bool found = false;
            for (const auto mode :
                 {gpu::CostMode::off, gpu::CostMode::passes, gpu::CostMode::bounds, gpu::CostMode::snapshots})
                if (value == gpu::cost_mode_name(mode)) {
                    result.cost_mode = mode;
                    found = true;
                }
            if (!found) return std::unexpected{"Diagnostyka kosztu GPU: off, passes, bounds lub snapshots."};
        } else if (argument == "--gpu-neighbor-reads" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value == "full")
                result.gpu_execution.neighbor_reads = gpu::NeighborReads::full;
            else if (value == "narrow")
                result.gpu_execution.neighbor_reads = gpu::NeighborReads::narrow;
            else if (value == "sorted")
                result.gpu_execution.neighbor_reads = gpu::NeighborReads::sorted;
            else
                return std::unexpected{"Odczyty sąsiadów: full, narrow lub sorted."};
        } else if (argument == "--gpu-storage" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value == "direct")
                result.gpu_execution.storage = gpu::DeviceStorageMode::direct;
            else if (value == "staged")
                result.gpu_execution.storage = gpu::DeviceStorageMode::staged;
            else if (value == "immutable")
                result.gpu_execution.storage = gpu::DeviceStorageMode::immutable;
            else if (value == "auto")
                result.gpu_execution.storage = gpu::DeviceStorageMode::automatic;
            else
                return std::unexpected{"Pamięć GPU: direct, staged, immutable lub auto."};
        } else if ((argument == "--gpu-phase-gating" || argument == "--gpu-zero-projection") && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "on" && value != "off") return std::unexpected{"Optymalizacja pracy GPU: on lub off."};
            if (argument == "--gpu-phase-gating")
                result.gpu_execution.phase_gating = value == "on";
            else
                result.gpu_execution.skip_zero_projection = value == "on";
        } else if (argument == "--gpu-coupling-check")
            result.coupling_test = true;
        else if (argument == "--gpu-coupling-control") {
            result.coupling_test = true;
            result.coupling_reference_control = true;
        } else if (argument == "--gpu-coupling-legacy-check") {
            result.coupling_test = true;
            result.coupling_legacy_check = true;
        } else if (argument == "--gpu-coupling-profile")
            result.coupling_profile = true;
        else if (argument == "--gpu-coupling" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value == "reference")
                result.gpu_execution.coupling = gpu::CouplingMode::reference;
            else if (value == "local")
                result.gpu_execution.coupling = gpu::CouplingMode::local;
            else if (value == "combined")
                result.gpu_execution.coupling = gpu::CouplingMode::combined;
            else
                return std::unexpected{"Sprzężenie GPU: reference, local lub combined."};
        } else if (argument == "--gpu-neighbor-check")
            result.neighbor_test = true;
        else if (argument == "--gpu-neighbor-profile")
            result.neighbor_profile = true;
        else if (argument == "--gpu-neighbors" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value == "linked")
                result.gpu_execution.neighbors = gpu::NeighborMode::linked;
            else if (value == "sorted")
                result.gpu_execution.neighbors = gpu::NeighborMode::sorted;
            else if (value == "reused")
                result.gpu_execution.neighbors = gpu::NeighborMode::reused;
            else
                return std::unexpected{"Sąsiedztwo GPU: linked, sorted lub reused."};
        } else if (argument == "--gpu-layout" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "aos" && value != "soa") return std::unexpected{"Układ cząstek GPU: aos lub soa."};
            result.gpu_execution.soa = value == "soa";
        } else if (argument == "--gpu-execution-check")
            result.execution_test = true;
        else if (argument == "--gpu-pbf-restrict-check") {
            result.execution_test = true;
            result.pbf_restrict_test = true;
        } else if (argument == "--gpu-execution-profile")
            result.execution_profile = true;
        else if (argument == "--gpu-execution" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value == "reference")
                result.gpu_execution = gpu::ExecutionOptions::reference();
            else if (value == "optimized")
                result.gpu_execution = {};
            else
                return std::unexpected{"Harmonogram GPU: reference lub optimized."};
        } else if (argument == "--gpu-pbf-restrict" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "on" && value != "off") return std::unexpected{"Brak aliasowania PBF: on lub off."};
            result.gpu_execution.pbf_restrict = value == "on";
        } else if ((argument == "--gpu-local-size" || argument == "--gpu-reduction-size"
                    || argument == "--gpu-pbf-size")
                   && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            unsigned size{};
            if (value == "64")
                size = 64;
            else if (value == "128")
                size = 128;
            else if (value == "256")
                size = 256;
            else
                return std::unexpected{"Rozmiar grupy GPU: 64, 128 lub 256."};
            if (argument == "--gpu-local-size")
                result.gpu_execution.local_size = size;
            else if (argument == "--gpu-pbf-size")
                result.gpu_execution.pbf_size = size;
            else
                result.gpu_execution.reduction_size = size;
        } else if (argument == "--gpu-readback-check")
            result.readback_test = true;
        else if (argument == "--gpu-readback-profile")
            result.readback_profile = true;
        else if (argument == "--gpu-readback-mode" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            bool found = false;
            for (const auto mode : {gpu::ReadbackMode::automatic, gpu::ReadbackMode::legacy, gpu::ReadbackMode::stream,
                                    gpu::ReadbackMode::staging, gpu::ReadbackMode::persistent})
                if (value == gpu::readback_mode_name(mode)) {
                    result.gpu_profiling.readback_mode = mode;
                    found = true;
                }
            if (!found) return std::unexpected{"Tryb odczytu GPU: auto, legacy, stream, staging lub persistent."};
        } else if (argument == "--gpu-profile")
            result.gpu_profile = true;
        else if (argument == "--gpu-stabilization-profile")
            result.stabilization_profile = true;
        else if (argument == "--gpu-profile-check")
            result.gpu_profile_check = true;
        else if (argument == "--gl-debug")
            result.gl_debug = true;
        else if (argument == "--show-test-window")
            result.show_test_window = true;
        else if (argument == "--gpu-counters")
            result.gpu_profiling.collect = true;
        else if (argument == "--gpu-timer-mode" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value == "elapsed")
                result.gpu_profiling.timer_mode = gpu::TimerMode::elapsed;
            else if (value == "timestamp")
                result.gpu_profiling.timer_mode = gpu::TimerMode::timestamp;
            else if (value == "off")
                result.gpu_profiling.timer_mode = gpu::TimerMode::off;
            else
                return std::unexpected{"Tryb timerów GPU: elapsed, timestamp lub off."};
            result.gpu_profiling.gpu_timers = value != "off";
        } else if (argument == "--test-unpaced")
            result.test_unpaced = true;
        else if (argument == "--test-window-size" && i + 2 < argc) {
            std::array<int, 2> size{};
            for (unsigned axis = 0; axis < 2; ++axis) {
                const std::string_view value{argv[++i]};
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), size[axis]);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                    return std::unexpected{"Nieprawidłowy rozmiar okna testowego."};
            }
            result.test_window_size = size;
        } else if (argument == "--test-vsync" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "on" && value != "off") return std::unexpected{"Test VSync: on lub off."};
            result.test_vsync = value == "on";
        } else if (argument == "--test-frame-limit" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            unsigned limit{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), limit);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || limit < 30 || limit > 240)
                return std::unexpected{"Testowy limit FPS musi wynosić 30–240."};
            result.test_frame_limit = limit;
        } else if (argument == "--gpu-group-timers") {
            result.gpu_profiling.collect = true;
            result.gpu_profiling.group_timers = true;
        } else if (argument == "--gpu-no-timers") {
            result.gpu_profiling.gpu_timers = false;
            result.gpu_profiling.timer_mode = gpu::TimerMode::off;
        } else if (argument == "--gpu-no-coupling")
            result.gpu_profiling.body_coupling = false;
        else if (argument == "--gpu-no-diagnostics") {
            result.gpu_profiling.gpu_timers = false;
            result.gpu_profiling.periodic_diagnostics = false;
        } else if (argument == "--gpu-readback-period" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value != "0" && value != "1" && value != "2" && value != "4")
                return std::unexpected{"Okres odczytu GPU musi wynosić 0, 1, 2 lub 4."};
            result.gpu_profiling.readback_period = static_cast<unsigned>(value.front() - '0');
        } else if (argument == "--physics-threads" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            if (value.size() != 1 || value.front() < '1' || value.front() > '4')
                return std::unexpected{"Liczba wątków fizyki musi wynosić 1–4."};
            result.physics_threads = static_cast<unsigned>(value.front() - '0');
        } else if (argument == "--test-seconds" && i + 1 < argc) {
            const std::string_view value{argv[++i]};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.test_seconds);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result.test_seconds < 1
                || result.test_seconds > 120)
                return std::unexpected{"Czas testu: 1–120 s."};
        } else if (argument == "--profile" && i + 1 < argc)
            result.profile = utf8_path(argv[++i]);
        else
            return std::unexpected{"Nieznany albo niepełny argument: " + std::string{argument}};
    }
    if (!result.gpu_execution.fluid.valid()) return std::unexpected{"Konfiguracja cieczy poza zakresem."};
    if (result.is_test() && result.profile.empty())
        return std::unexpected{"Test wymaga osobnego katalogu: --smoke-test --profile <katalog>."};
    if (static_cast<int>(result.start_in_laboratory) + result.start_in_gpu + result.start_in_mission > 1)
        return std::unexpected{"Wybierz jeden tryb startowy."};
    const bool window_measurement = result.mission_smoke || result.gpu_smoke_test;
    if ((result.test_seconds || result.test_unpaced) && !window_measurement)
        return std::unexpected{"Opcje pomiaru okna wymagają testu misji lub laboratorium GPU."};
    if (result.test_window_size.has_value() && !window_measurement && result.benchmark_scenes.empty())
        return std::unexpected{"Opcja --test-window-size wymaga testu misji, laboratorium GPU lub benchmarku scen."};
    if (result.coupling_reference_control && result.coupling_legacy_check)
        return std::unexpected{"Wybierz jedną kontrolę sprzężenia: reference control albo legacy."};
    if (result.cost_profile
        && (!result.gpu_profiling.gpu_timers || result.gpu_profiling.timer_mode != gpu::TimerMode::elapsed
            || result.gpu_profiling.group_timers || !result.gpu_profiling.periodic_diagnostics
            || !result.gpu_profiling.body_coupling || result.gpu_profiling.readback_period != 2
            || result.gpu_profiling.readback_mode != gpu::ReadbackMode::automatic))
        return std::unexpected{
                "Profil kosztu ma stały protokół: elapsed, diagnostyka/sprzężenie ON, HUD auto/2, bez timerów grupowych."};
    if (result.test_count() > 1) return std::unexpected{"Wybierz jeden rodzaj testu."};
    if (result.scene_set && result.thermal_scene != 0) return std::unexpected{"Wybierz jedną scenę startową."};
    if (result.benchmark_tuned && result.benchmark_scenes.empty())
        return std::unexpected{"Opcje --seconds, --vsync, --repeat i --loop-minutes wymagają --scene-benchmark."};
    if (result.benchmark_repeat > 1 && result.benchmark_loop_minutes)
        return std::unexpected{"Wybierz --repeat albo --loop-minutes."};
    if (!result.benchmark_scenes.empty() && result.start_mode() != StartMode::menu)
        return std::unexpected{"Benchmark scen sam wybiera sceny; usuń opcje startowe."};
    if ((result.test_vsync.has_value() || result.test_frame_limit.has_value()) && !result.is_window_smoke())
        return std::unexpected{"Opcje --test-vsync i --test-frame-limit wymagają testu okna."};
    return result;
}
unsigned gpu_timeout_scale(const char* text) {
    if (!text) return 1;
    const std::string_view value{text};
    unsigned scale{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), scale);
    return error == std::errc{} && end == value.data() + value.size() && scale > 0 && scale <= 100 ? scale : 1;
}

} // namespace strefa::app
