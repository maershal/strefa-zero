#include "app/thermal_conformance.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const std::string& text) {
    if (!ok) throw std::runtime_error{text};
}
} // namespace

// Usage: strefa_thermal_conformance_tests <thresholds file> <output directory>
int main(int argc, char** argv) {
    try {
        using strefa::app::parse_conformance_thresholds;
        check(argc == 3, "usage: <thresholds file> <output directory>");
        const auto parsed =
                parse_conformance_thresholds("# comment\n\nrod t_at_10 5 0.05 # trailing\nrod t_at_30 0 0\n");
        check(parsed && parsed->size() == 2 && (*parsed)[0].scene == "rod" && (*parsed)[0].observable == "t_at_10"
                      && (*parsed)[0].absolute == 5 && (*parsed)[0].relative == 0.05,
              "Valid thresholds");
        for (const auto* bad :
             {"", "# only a comment\n", "rod t_at_10 5\n", "rod t_at_10 5 0.05 extra\n", "rod t_at_10 -1 0\n",
              "rod t_at_10 1 nan\n", "rod t_at_10 1 0.1x\n", "rod t_at_10 1 0\nrod t_at_10 2 0\n"})
            check(!parse_conformance_thresholds(bad), std::string{"Accepted invalid thresholds: "} + bad);

        const auto observables = strefa::app::thermal_conformance_observables();
        std::size_t scenes = 0;
        for (std::size_t i = 0; i < observables.size(); ++i)
            scenes += i == 0 || observables[i].scene != observables[i - 1].scene;
        check(scenes == 10, "10 conformance scenes");

        const std::filesystem::path output{argv[2]};
        std::filesystem::remove_all(output);
        const auto result = strefa::app::run_thermal_conformance(argv[1], output);
        check(bool(result), result ? std::string{} : result.error());
        std::ifstream csv{output / "thermal-conformance-cpu.csv"};
        std::size_t rows = 0;
        for (std::string line; std::getline(csv, line);) rows += !line.empty();
        check(rows == strefa::app::thermal_conformance_sample_count() + 1,
              "Every observable sampled at every tick of its scene (plus the header)");
        std::ifstream report{output / "thermal-conformance.txt"};
        const std::string text{std::istreambuf_iterator<char>{report}, {}};
        check(text.find("status=cpu_only") != std::string::npos
                      && text.find("gpu=not_implemented") != std::string::npos,
              "Report states the CPU-only skeleton");

        // A thresholds file that misses one observable must be rejected before anything runs.
        std::ifstream source{argv[1]};
        std::string all{std::istreambuf_iterator<char>{source}, {}};
        const auto first = all.find("\nrod ");
        check(first != std::string::npos, "Thresholds list the rod scene");
        all.erase(first + 1, all.find('\n', first + 1) - first);
        const auto partial = output / "partial-thresholds.txt";
        std::ofstream{partial} << all;
        check(!strefa::app::run_thermal_conformance(partial, output / "partial"), "Missing threshold accepted");
        const auto extra = output / "extra-thresholds.txt";
        std::ifstream complete{argv[1]};
        std::ofstream{extra} << std::string{std::istreambuf_iterator<char>{complete}, {}} << "rod not_recorded 1 0\n";
        const auto rejected = strefa::app::run_thermal_conformance(extra, output / "extra");
        check(!rejected && rejected.error().find("rod not_recorded") != std::string::npos,
              "A threshold without an observable must be rejected by name");
        std::cout << "Thermal conformance skeleton PASS (" << rows - 1 << " CPU samples)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
