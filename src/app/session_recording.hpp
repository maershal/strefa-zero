#pragma once
#include "gpu/world.hpp"
#include <expected>
#include <variant>

namespace strefa::app {
struct SceneConfiguration {
    bool mission{};
    gpu::Scene laboratory{gpu::Scene::hourglass};
    unsigned thermal_scene{};
};
using SessionCommand=std::variant<SceneConfiguration,gpu::Stroke,gpu::MissionInput,gpu::ThermalOptions,gpu::ThermalBrush>;
// Optional input recording, independent of presentation frequency and GPU
// snapshots. A recording starts with an explicit scene reset.
class SessionRecording {
public:
    static constexpr std::size_t capacity=65536;
    void begin(gpu::ThermalOptions initial={});
    void append(SessionCommand);
    bool active() const noexcept{return active_;}
    bool overflowed() const noexcept{return overflow_;}
    std::expected<void,std::string> save(const std::filesystem::path&,const gpu::ExecutionOptions&,const gpu::ProfileOptions&) const;
    static std::expected<std::vector<SessionCommand>,std::string> load(const std::filesystem::path&,const gpu::ExecutionOptions&,const gpu::ProfileOptions&);
private:
    bool active_{},overflow_{};gpu::ThermalOptions initial_{};
    std::vector<SessionCommand> commands_;
};
}
