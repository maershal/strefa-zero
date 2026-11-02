#include "app/application.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using strefa::app::SmokeScenario;
using strefa::app::StartMode;

void check(bool ok, const std::string& text) {
    if (!ok) throw std::runtime_error{text};
}
auto parse(std::vector<const char*> args, strefa::app::OptionScope scope = strefa::app::OptionScope::all) {
    args.insert(args.begin(), "game");
    std::vector<char*> argv;
    for (const auto* text : args) argv.push_back(const_cast<char*>(text));
    return strefa::app::parse_options(static_cast<int>(argv.size()), argv.data(), scope);
}

// Expected values copied from the boolean lists that preceded the predicates.
struct ModeCase {
    const char* flag;
    bool window_smoke, test_pacing, gl43;
    SmokeScenario smoke;
};
constexpr ModeCase mode_cases[]{
        {"--smoke-test", true, false, false, SmokeScenario::menu},
        {"--lab-smoke-test", true, true, false, SmokeScenario::cpu_laboratory},
        {"--gpu-smoke-test", true, true, true, SmokeScenario::gpu_laboratory},
        {"--mission-smoke-test", true, true, true, SmokeScenario::mission},
        {"--session-smoke-test", true, true, true, SmokeScenario::session},
        {"--mission-ui-smoke-test", true, true, true, SmokeScenario::mission_quick},
        {"--mode-fault-test", true, true, false, SmokeScenario::mode_faults},
        {"--gpu-test", false, false, true, SmokeScenario::none},
        {"--gpu-liquid-test", false, false, true, SmokeScenario::none},
        {"--gpu-flow-test", false, false, true, SmokeScenario::none},
        {"--mission-test", false, true, true, SmokeScenario::none},
        {"--gameplay-check", false, true, true, SmokeScenario::none},
        {"--gpu-profile", false, true, true, SmokeScenario::none},
        {"--gpu-stabilization-profile", false, true, true, SmokeScenario::none},
        {"--gpu-profile-check", false, true, true, SmokeScenario::none},
        {"--gpu-readback-check", false, true, true, SmokeScenario::none},
        {"--gpu-readback-profile", false, true, true, SmokeScenario::none},
        {"--gpu-execution-check", false, true, true, SmokeScenario::none},
        {"--gpu-pbf-restrict-check", false, true, true, SmokeScenario::none},
        {"--gpu-execution-profile", false, true, true, SmokeScenario::none},
        {"--gpu-neighbor-check", false, true, true, SmokeScenario::none},
        {"--gpu-neighbor-profile", false, true, true, SmokeScenario::none},
        {"--gpu-coupling-check", false, true, true, SmokeScenario::none},
        {"--gpu-coupling-control", false, true, true, SmokeScenario::none},
        {"--gpu-coupling-legacy-check", false, true, true, SmokeScenario::none},
        {"--gpu-coupling-profile", false, true, true, SmokeScenario::none},
        {"--gpu-work-check", false, true, true, SmokeScenario::none},
        {"--gpu-transport-check", false, true, true, SmokeScenario::none},
        {"--gpu-hot-check", false, true, true, SmokeScenario::none},
        {"--gpu-cost-profile", false, true, true, SmokeScenario::none},
        {"--gpu-cost-check", false, true, true, SmokeScenario::none},
        {"--gpu-thermal-check", false, true, true, SmokeScenario::none},
        {"--gpu-thermal-benchmark", false, true, true, SmokeScenario::none},
        {"--gpu-vfx-check", false, true, true, SmokeScenario::none},
        {"--gpu-vfx-benchmark", false, true, true, SmokeScenario::none},
        {"--gpu-lighting-check", false, true, true, SmokeScenario::none},
        {"--gpu-lighting-benchmark", false, true, true, SmokeScenario::none},
        {"--gpu-render-check", false, true, true, SmokeScenario::none},
        {"--gpu-render-benchmark", false, true, true, SmokeScenario::none},
        {"--gpu-fluid-check", false, true, true, SmokeScenario::none},
        {"--gpu-fluid-benchmark", false, true, true, SmokeScenario::none},
        {"--shader-cache-check", false, true, true, SmokeScenario::none},
};
} // namespace

int main() {
    try {
        const auto plain = parse({});
        check(plain && !plain->is_test() && plain->test_count() == 0 && !plain->is_window_smoke()
                      && !plain->forces_test_pacing() && !plain->requires_gl43()
                      && plain->start_mode() == StartMode::menu && plain->smoke_scenario() == SmokeScenario::none,
              "Default launch must start the menu without tests");
        for (const auto& mode : mode_cases) {
            const std::string flag{mode.flag};
            check(!parse({mode.flag}), flag + " must require --profile");
            const auto options = parse({mode.flag, "--profile", "fixture"});
            check(bool(options), flag + " rejected");
            check(options->is_test() && options->test_count() == 1, flag + " must count as one test");
            check(options->is_window_smoke() == mode.window_smoke, flag + " window smoke");
            check(options->forces_test_pacing() == mode.test_pacing, flag + " test pacing");
            check(options->requires_gl43() == mode.gl43, flag + " OpenGL 4.3 requirement");
            check(options->smoke_scenario() == mode.smoke, flag + " smoke scenario");
            check(options->start_mode() == StartMode::menu, flag + " start mode");
        }
        const std::pair<const char*, StartMode> starts[]{{"--laboratory", StartMode::cpu_laboratory},
                                                         {"--arena", StartMode::cpu_arena},
                                                         {"--gpu-laboratory", StartMode::gpu_laboratory},
                                                         {"--mission", StartMode::mission}};
        for (const auto& [flag, start] : starts) {
            const auto options = parse({flag});
            check(options && options->start_mode() == start && !options->is_test(), std::string{flag} + " start");
        }
        const auto thermal = parse({"--gpu-thermal-scene", "fire"});
        check(thermal && thermal->start_mode() == StartMode::gpu_laboratory, "Thermal scene starts the GPU laboratory");
        check(thermal->thermal_scene == 1 && !parse({"--gpu-thermal-scene", "ice"}), "Thermal scene ids");
        const auto gpu_scene = parse({"--gpu-scene", "dam-break"});
        check(gpu_scene && gpu_scene->start_mode() == StartMode::gpu_laboratory && gpu_scene->gpu_scene == 5u
                      && !gpu_scene->is_test(),
              "--gpu-scene starts the GPU laboratory in that scene");
        const auto lab_scene = parse({"--lab-scene", "wood-fire"});
        check(lab_scene && lab_scene->start_mode() == StartMode::cpu_laboratory && lab_scene->lab_scene == 9u,
              "--lab-scene starts the CPU laboratory in that scene");
        check(!parse({"--gpu-scene", "nope"}) && !parse({"--lab-scene", "dam"}), "Unknown scene ids accepted");
        check(!parse({"--gpu-scene", "rain", "--lab-scene", "demo"}), "Two start scenes accepted");
        check(!parse({"--gpu-scene", "rain", "--gpu-thermal-scene", "fire"}), "GPU and thermal start scenes accepted");
        // StrefaQA scene benchmark: one test with its own window pacing.
        check(!parse({"--scene-benchmark", "all"}), "--scene-benchmark must require --profile");
        const auto bench = parse({"--scene-benchmark", "all", "--profile", "fixture"});
        check(bench && bench->test_count() == 1 && bench->benchmark_scenes.size() == 32
                      && bench->benchmark_seconds == 60 && bench->benchmark_vsync && !bench->is_window_smoke()
                      && bench->forces_test_pacing() && bench->requires_gl43()
                      && bench->smoke_scenario() == SmokeScenario::scene_benchmark
                      && bench->start_mode() == StartMode::menu,
              "--scene-benchmark defaults");
        const auto tuned = parse(
                {"--scene-benchmark", "mission,lab:demo", "--seconds", "5", "--vsync", "off", "--profile", "fixture"});
        check(tuned && tuned->benchmark_scenes.size() == 2 && tuned->benchmark_seconds == 5 && !tuned->benchmark_vsync,
              "--seconds and --vsync");
        const std::vector<std::vector<const char*>> bad_benchmarks{
                {"--seconds", "5"},
                {"--vsync", "off"},
                {"--scene-benchmark", "gpu:nope", "--profile", "fixture"},
                {"--scene-benchmark", "all", "--seconds", "0", "--profile", "fixture"},
                {"--scene-benchmark", "all", "--seconds", "601", "--profile", "fixture"},
                {"--scene-benchmark", "all", "--vsync", "maybe", "--profile", "fixture"},
                {"--scene-benchmark", "all", "--mission", "--profile", "fixture"},
                {"--scene-benchmark", "all", "--gpu-smoke-test", "--profile", "fixture"}};
        for (const auto& bad : bad_benchmarks)
            check(!parse(bad), std::string{"Invalid benchmark options accepted: "} + bad.front());
        const auto combined = parse({"--arena", "--smoke-test", "--profile", "fixture"});
        check(combined && combined->start_mode() == StartMode::cpu_arena
                      && combined->smoke_scenario() == SmokeScenario::menu,
              "Start mode and smoke test are independent");
        check(!parse({"--arena", "--mission"}), "Two start modes accepted");
        check(!parse({"--smoke-test", "--mission-smoke-test", "--profile", "fixture"}), "Two tests accepted");
        // StrefaZero: only player options, each still parsed exactly as in StrefaQA.
        using strefa::app::OptionScope;
        const auto player = OptionScope::player;
        for (const auto* flag :
             {"--mission", "--gpu-laboratory", "--laboratory", "--arena", "--opengl33", "--gl-debug"})
            check(bool(parse({flag}, player)), std::string{flag} + " must stay a player option");
        check(bool(parse({"--gpu-scene", "oil-water"}, player)) && bool(parse({"--lab-scene", "wood-fire"}, player)),
              "--gpu-scene and --lab-scene are player options");
        check(!parse({"--scene-benchmark", "all", "--profile", "fixture"}, player)
                      && !parse({"--gpu-thermal-scene", "fire"}, player),
              "Benchmark and thermal scenes are StrefaQA only");
        for (const auto* flag : {"--smoke-test", "--mission-smoke-test"}) {
            const auto options = parse({flag, "--profile", "fixture"}, player);
            check(options && options->test_count() == 1, std::string{flag} + " must stay in StrefaZero");
            check(!parse({flag}, player), std::string{flag} + " must require --profile in StrefaZero");
        }
        for (const auto& mode : mode_cases)
            if (mode.smoke != SmokeScenario::menu && mode.smoke != SmokeScenario::mission)
                check(!parse({mode.flag, "--profile", "fixture"}, player),
                      std::string{mode.flag} + " is StrefaQA only");
        for (const auto* flag :
             {"--gpu-layout", "--physics-threads", "--gpu-thermal", "--test-seconds", "--gpu-counters"})
            check(!parse({flag}, player), std::string{flag} + " is StrefaQA only");
        const auto help = parse({"--help"}, player);
        check(help && help->help && parse({"--help"})->help, "--help in both executables");
        const auto text = strefa::app::help_text(player);
        const auto listed = std::count(text.begin(), text.end(), '\n') - 1;
        check(listed < 15, "StrefaZero --help must list fewer than 15 options");
        std::cout << "Launch options contract PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
