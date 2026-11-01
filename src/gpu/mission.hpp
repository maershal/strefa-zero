#pragma once
#include "game/world_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace strefa::gpu {
enum MissionButton : unsigned { jump = 1, jet = 2, fire = 4, reload = 8, grenade = 16 };
struct MissionInput {
    float move{}, aim_x{100}, aim_y{170};
    unsigned buttons{};
};
// Mirrors mission_state.glsl, std430. Snapshot is diagnostic/presentation only;
// Physical fields are never consumed for authoritative CPU collision. Rules
// below are overlaid from GameplaySession; reliable events use a separate ring.
struct MissionSnapshot {
    std::array<float, 4> actor{}, previous{}, aim{}, locomotion{}, timers{};
    std::array<std::uint32_t, 4> status{}, inventory{}, counts{}, events{};
    std::array<float, 4> control{}, exit{}, body{}, blast{}, flash{};
    std::array<std::array<float, 4>, 3> targets{};
};
static_assert(sizeof(MissionSnapshot) == 272);
static_assert(offsetof(MissionSnapshot, status) == 80);
struct MissionConfig {
    float spawn_x{24}, spawn_y{187}, relative_density{.9F};
    std::array<float, 4> exit{300, 164, 316, 187};
    std::array<std::array<float, 4>, 3> targets{{{54, 177, 3, 0}, {164, 177, 3, 0}, {276, 177, 3, 0}}};
};
struct Explosion {
    std::uint32_t id{};
    float x{}, y{}, radius{20}, impulse{70};
};
inline constexpr unsigned bullet_capacity = 128, grenade_capacity = 8;
inline constexpr std::size_t explosion_offset =
        32 + sizeof(MissionSnapshot) + (bullet_capacity + grenade_capacity) * 32;
inline constexpr std::size_t transport_offset = explosion_offset + 32;
inline constexpr std::size_t command_offset = transport_offset + 16;
inline constexpr std::size_t event_offset = command_offset + sizeof(game::CommandBatch);
inline constexpr std::size_t mission_buffer_size = event_offset + sizeof(game::EventBatch);
} // namespace strefa::gpu
