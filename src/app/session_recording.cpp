#include "app/session_recording.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>

namespace strefa::app {
namespace {
std::string configuration(const gpu::ExecutionOptions& e, const gpu::ProfileOptions& p) {
    std::ostringstream out;
    out << e.specialized << ' ' << e.cached_bindings << ' ' << e.fifo_queries << ' ' << e.conditional_work << ' '
        << e.local_size << ' ' << e.reduction_size << ' ' << static_cast<int>(e.neighbors) << ' ' << e.soa << ' '
        << static_cast<int>(e.coupling) << ' ' << e.phase_gating << ' ' << e.skip_zero_projection << ' '
        << static_cast<int>(e.storage) << ' ' << static_cast<int>(e.neighbor_reads) << ' ' << e.pbf_size << ' '
        << e.pbf_restrict << ' ' << p.body_coupling << ' ' << p.periodic_diagnostics;
    return out.str();
}
} // namespace
void SessionRecording::begin(gpu::ThermalOptions initial) {
    initial_ = initial;
    commands_.clear();
    commands_.reserve(capacity);
    active_ = true;
    overflow_ = false;
}
void SessionRecording::append(SessionCommand command) {
    if (!active_) return;
    if (commands_.size() == capacity) {
        active_ = false;
        overflow_ = true;
        return;
    }
    commands_.push_back(std::move(command));
}
std::expected<void, std::string> SessionRecording::save(const std::filesystem::path& path,
                                                        const gpu::ExecutionOptions& execution,
                                                        const gpu::ProfileOptions& profile) const {
    if (overflow_) return std::unexpected{"Nagranie przekroczyło 65536 poleceń. Rozpocznij krótszą sesję."};
    if (commands_.empty()) return std::unexpected{"Najpierw rozpocznij nagrywanie sceny."};
    std::ofstream out{path};
    if (!out) return std::unexpected{"Nie można zapisać scenariusza."};
    const bool thermal = initial_.enabled || std::ranges::any_of(commands_, [](const SessionCommand& c) {
                             return std::holds_alternative<gpu::ThermalOptions>(c)
                                 || std::holds_alternative<gpu::ThermalBrush>(c)
                                 || (std::holds_alternative<SceneConfiguration>(c)
                                     && std::get<SceneConfiguration>(c).thermal_scene != 0);
                         });
    out << (thermal ? "STREFA_SESSION 3\nI10.0 " : "STREFA_SESSION 2\n") << "I07.1 "
        << gpu::fluid_configuration(execution.fluid) << '\n'
        << configuration(execution, profile) << '\n'
        << std::setprecision(std::numeric_limits<float>::max_digits10);
    if (thermal)
        out << "H " << initial_.enabled << ' ' << initial_.transport << ' ' << initial_.reactions << ' '
            << initial_.pressure_iterations << ' ' << initial_.radiation << '\n';
    for (const auto& command : commands_) {
        if (const auto* scene = std::get_if<SceneConfiguration>(&command)) {
            out << "R " << scene->mission << ' ' << static_cast<int>(scene->laboratory);
            if (thermal) out << ' ' << scene->thermal_scene;
            out << '\n';
        } else if (const auto* stroke = std::get_if<gpu::Stroke>(&command))
            out << "P " << stroke->x0 << ' ' << stroke->y0 << ' ' << stroke->x1 << ' ' << stroke->y1 << ' '
                << stroke->radius << ' ' << static_cast<int>(stroke->material) << '\n';
        else if (const auto* c = std::get_if<gpu::ThermalOptions>(&command))
            out << "C " << c->enabled << ' ' << c->transport << ' ' << c->reactions << ' ' << c->pressure_iterations
                << ' ' << c->radiation << '\n';
        else if (const auto* b = std::get_if<gpu::ThermalBrush>(&command))
            out << "E " << b->segment[0] << ' ' << b->segment[1] << ' ' << b->segment[2] << ' ' << b->segment[3] << ' '
                << b->radius << ' ' << b->rate << ' ' << static_cast<int>(b->tool) << '\n';
        else {
            const auto& input = std::get<gpu::MissionInput>(command);
            out << "T " << input.move << ' ' << input.aim_x << ' ' << input.aim_y << ' ' << input.buttons << '\n';
        }
    }
    if (!out) return std::unexpected{"Niepełny zapis scenariusza."};
    return {};
}
std::expected<std::vector<SessionCommand>, std::string> SessionRecording::load(const std::filesystem::path& path,
                                                                               const gpu::ExecutionOptions& execution,
                                                                               const gpu::ProfileOptions& profile) {
    std::ifstream in{path};
    std::string line;
    if (!std::getline(in, line)) return std::unexpected{"Nieznany format scenariusza."};
    const bool thermal = line == "STREFA_SESSION 3";
    if (line == "STREFA_SESSION 2" || thermal) {
        if (!std::getline(in, line)
            || line != (thermal ? "I10.0 I07.1 " : "I07.1 ") + gpu::fluid_configuration(execution.fluid))
            return std::unexpected{"Nagranie wymaga tej samej rewizji i konfiguracji fizyki I07."};
    } else if (line == "STREFA_SESSION 1") {
        if (execution.fluid != gpu::FluidOptions{}) return std::unexpected{"Nagranie v1 wymaga profilu cieczy legacy."};
    } else
        return std::unexpected{"Nieznany format scenariusza."};
    if (!std::getline(in, line) || line != configuration(execution, profile))
        return std::unexpected{"Scenariusz wymaga tej samej konfiguracji solvera."};
    std::vector<SessionCommand> commands;
    const auto config = [](std::istringstream& row) -> std::optional<gpu::ThermalOptions> {
        int enabled{}, transport{}, reactions{}, iterations{}, radiation{};
        if (!(row >> enabled >> transport >> reactions >> iterations >> radiation) || enabled < 0 || enabled > 1
            || transport < 0 || transport > 1 || reactions < 0 || reactions > 1 || iterations < 1 || iterations > 128
            || radiation < 0 || radiation > 1)
            return {};
        return gpu::ThermalOptions{
                enabled != 0, transport != 0, reactions != 0, false, static_cast<unsigned>(iterations), radiation != 0};
    };
    if (thermal) {
        if (!std::getline(in, line)) return std::unexpected{"Brak konfiguracji I10."};
        std::istringstream row{line};
        char tag{};
        row >> tag;
        auto c = config(row);
        std::string extra;
        if (tag != 'H' || !c || (row >> extra)) return std::unexpected{"Nieprawidłowa konfiguracja I10."};
        commands.emplace_back(*c);
    }

    while (std::getline(in, line)) {
        if (commands.size() == capacity || line.size() > 512)
            return std::unexpected{"Scenariusz przekracza limit poleceń."};
        std::istringstream row{line};
        char kind{};
        row >> kind;
        if (kind == 'R') {
            int mission{}, scene{};
            if (!(row >> mission >> scene) || (mission != 0 && mission != 1) || scene < 0 || scene > 11)
                return std::unexpected{"Nieprawidłowa scena w nagraniu."};
            unsigned thermal_scene = 0;
            if (thermal && (!(row >> thermal_scene) || thermal_scene > 4 || (mission && thermal_scene)))
                return std::unexpected{"Nieprawidłowa scena I10."};
            commands.emplace_back(SceneConfiguration{mission != 0, static_cast<gpu::Scene>(scene), thermal_scene});
        } else if (kind == 'P') {
            gpu::Stroke stroke;
            int material{};
            if (!(row >> stroke.x0 >> stroke.y0 >> stroke.x1 >> stroke.y1 >> stroke.radius >> material) || material < 0
                || material > 6 || !std::isfinite(stroke.x0) || !std::isfinite(stroke.y0) || !std::isfinite(stroke.x1)
                || !std::isfinite(stroke.y1) || !std::isfinite(stroke.radius) || stroke.radius < 1
                || stroke.radius > 16)
                return std::unexpected{"Nieprawidłowy pędzel w nagraniu."};
            stroke.material = static_cast<gpu::Material>(material);
            commands.emplace_back(stroke);
        } else if (kind == 'C' && thermal) {
            auto c = config(row);
            if (!c) return std::unexpected{"Nieprawidłowa konfiguracja I10."};
            commands.emplace_back(*c);
        } else if (kind == 'E' && thermal) {
            gpu::ThermalBrush b;
            int tool{};
            if (!(row >> b.segment[0] >> b.segment[1] >> b.segment[2] >> b.segment[3] >> b.radius >> b.rate >> tool)
                || !std::ranges::all_of(b.segment, [](float v) { return std::isfinite(v); }) || !std::isfinite(b.radius)
                || b.radius < 1 || b.radius > 16 || !std::isfinite(b.rate) || b.rate < 0 || b.rate > 10000 || tool < 0
                || tool > 8)
                return std::unexpected{"Nieprawidłowe źródło energii I10."};
            b.tool = static_cast<gpu::ThermalTool>(tool);
            commands.emplace_back(b);
        } else if (kind == 'T') {
            gpu::MissionInput input;
            if (!(row >> input.move >> input.aim_x >> input.aim_y >> input.buttons) || !std::isfinite(input.move)
                || !std::isfinite(input.aim_x) || !std::isfinite(input.aim_y) || std::abs(input.move) > 1
                || input.buttons > 31)
                return std::unexpected{"Nieprawidłowy krok w nagraniu."};
            commands.emplace_back(input);
        } else
            return std::unexpected{"Nieznane polecenie scenariusza."};
        std::string extra;
        if (row >> extra) return std::unexpected{"Nadmiarowe dane scenariusza."};
    }
    if (in.bad() || commands.empty()
        || !(std::holds_alternative<SceneConfiguration>(commands.front())
             || (thermal && commands.size() > 1 && std::holds_alternative<SceneConfiguration>(commands[1]))))
        return std::unexpected{"Niepełny scenariusz."};
    return commands;
}
} // namespace strefa::app
