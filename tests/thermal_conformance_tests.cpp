#include "app/thermal_conformance.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool ok, const std::string& text) {
    if (!ok) throw std::runtime_error{text};
}

// Parser, scene list and thresholds file only; no scene runs, so this stays fast under sanitizers.
// expected_scenes, when given, is the comma-separated list of the per-scene CTest entries: it must name every scene
// once, in code order.
void check_thresholds(const std::filesystem::path& thresholds, const std::filesystem::path& output,
                      std::string_view expected_scenes) {
    using strefa::app::parse_conformance_thresholds;
    const auto parsed = parse_conformance_thresholds("# comment\n\nrod t_at_10 5 0.05 # trailing\nrod t_at_30 0 0\n");
    check(parsed && parsed->size() == 2 && (*parsed)[0].scene == "rod" && (*parsed)[0].observable == "t_at_10"
                  && (*parsed)[0].absolute == 5 && (*parsed)[0].relative == 0.05,
          "Valid thresholds");
    for (const auto* bad :
         {"", "# only a comment\n", "rod t_at_10 5\n", "rod t_at_10 5 0.05 extra\n", "rod t_at_10 -1 0\n",
          "rod t_at_10 1 nan\n", "rod t_at_10 1 0.1x\n", "rod t_at_10 1 0\nrod t_at_10 2 0\n"})
        check(!parse_conformance_thresholds(bad), std::string{"Accepted invalid thresholds: "} + bad);

    const auto scenes = strefa::app::thermal_conformance_scenes();
    check(scenes.size() == 10, "10 conformance scenes");
    std::string listed;
    for (const auto scene : scenes) listed += (listed.empty() ? "" : ",") + std::string{scene};
    check(expected_scenes.empty() || listed == expected_scenes,
          "CTest scene entries (" + std::string{expected_scenes} + ") differ from the scenes in code (" + listed + ")");

    std::filesystem::remove_all(output);
    std::filesystem::create_directories(output);
    // Every check below must fail before a scene runs; the scene filter keeps a wrongly accepted file short.
    const auto unknown = strefa::app::run_thermal_conformance(thresholds, output / "unknown", "no_such_scene");
    check(!unknown && unknown.error().find("no_such_scene") != std::string::npos,
          "An unknown scene must be rejected by name");

    // A thresholds file that misses one observable must be rejected.
    std::ifstream source{thresholds};
    std::string all{std::istreambuf_iterator<char>{source}, {}};
    const auto first = all.find("\nrod ");
    check(first != std::string::npos, "Thresholds list the rod scene");
    all.erase(first + 1, all.find('\n', first + 1) - first);
    const auto partial = output / "partial-thresholds.txt";
    std::ofstream{partial} << all;
    check(!strefa::app::run_thermal_conformance(partial, output / "partial", "rod"), "Missing threshold accepted");
    const auto extra = output / "extra-thresholds.txt";
    std::ifstream complete{thresholds};
    std::ofstream{extra} << std::string{std::istreambuf_iterator<char>{complete}, {}} << "rod not_recorded 1 0\n";
    const auto rejected = strefa::app::run_thermal_conformance(extra, output / "extra", "rod");
    check(!rejected && rejected.error().find("rod not_recorded") != std::string::npos,
          "A threshold without an observable must be rejected by name");
    std::cout << "Thermal conformance thresholds PASS (" << scenes.size() << " scenes)\n";
}

void run_scene(const std::filesystem::path& thresholds, const std::filesystem::path& output, std::string_view scene) {
    std::filesystem::remove_all(output);
    const auto result = strefa::app::run_thermal_conformance(thresholds, output, scene);
    check(bool(result), result ? std::string{} : result.error());
    std::ifstream csv{output / "thermal-conformance-cpu.csv"};
    std::size_t rows = 0;
    std::string header;
    std::getline(csv, header);
    for (std::string line; std::getline(csv, line);) {
        if (line.empty()) continue;
        check(line.starts_with(std::string{scene} + ','), "Row of another scene: " + line);
        ++rows;
    }
    check(rows > 0 && rows == strefa::app::thermal_conformance_sample_count(scene),
          "Every observable sampled at every tick of the scene");
    std::ifstream report{output / "thermal-conformance.txt"};
    const std::string text{std::istreambuf_iterator<char>{report}, {}};
    check(text.find("status=cpu_only") != std::string::npos && text.find("gpu=not_implemented") != std::string::npos
                  && text.find("scenes=" + std::string{scene} + '\n') != std::string::npos,
          "Report states the CPU-only skeleton of the scene");
    std::cout << "Thermal conformance scene " << scene << " PASS (" << rows << " CPU samples)\n";
}
} // namespace

// Usage: strefa_thermal_conformance_tests <thresholds file> <output directory> [--scenes <id,...> | --scene <id>]
// Without --scene only the fast checks run (--scenes also compares the scene list); --scene runs one scene.
int main(int argc, char** argv) {
    try {
        check(argc == 3 || argc == 5, "usage: <thresholds file> <output directory> [--scenes <id,...> | --scene <id>]");
        const std::string_view mode{argc == 5 ? argv[3] : "--scenes"};
        if (mode == "--scenes")
            check_thresholds(argv[1], argv[2], argc == 5 ? argv[4] : "");
        else if (mode == "--scene")
            run_scene(argv[1], argv[2], argv[4]);
        else
            check(false, "unknown option " + std::string{mode});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
