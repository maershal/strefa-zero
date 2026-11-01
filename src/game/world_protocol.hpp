#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace strefa::game {
// std430; identity is (generation,tick,id). IDs are exact integers, never floats.
// Counters stop before UINT32_MAX; starting a new generation is explicit.
enum class CommandKind : std::uint32_t { rifle = 1, grenade = 2, jump = 3 };
enum class EventKind : std::uint32_t {
    accepted = 1,
    rejected = 2,
    hit = 3,
    explosion = 4,
    terrain = 5,
    exit = 6,
    impact = 7
};
struct alignas(16) WorldCommand {
    std::uint32_t generation{}, tick{}, id{}, kind{};
    std::uint32_t subject{}, flags{}, reserved0{}, reserved1{};
    std::array<float, 4> payload{}, extra{};
};
// Accepted rifle events use reserved=1, payload.xy=muzzle, extra.xy=launch velocity.
// Presentation fields do not authorize commands or change the 64-byte ABI.
struct alignas(16) WorldEvent {
    std::uint32_t generation{}, tick{}, id{}, kind{};
    std::uint32_t command{}, subject{}, value{}, reserved{};
    std::array<float, 4> payload{}, extra{};
};
inline constexpr unsigned commands_per_tick = 3, events_per_tick = 144, event_slots = 32, command_capacity = 128;
struct alignas(16) CommandBatch {
    std::array<std::uint32_t, 4> header{}; // generation, tick, count, cancel jump
    std::array<std::uint32_t, 4> inventory{};
    std::array<float, 4> rules{};           // cooldown, reload, finished, unused
    std::array<std::uint32_t, 4> targets{}; // HP, final element unused
    std::array<WorldCommand, commands_per_tick> commands{};
};
struct alignas(16) EventBatch {
    std::array<std::uint32_t, 4> header{}; // count, tick, generation, overflow (fatal)
    std::array<WorldEvent, events_per_tick> events{};
};
static_assert(sizeof(WorldCommand) == 64 && alignof(WorldCommand) == 16);
static_assert(sizeof(WorldEvent) == 64 && alignof(WorldEvent) == 16);
static_assert(offsetof(WorldCommand, subject) == 16 && offsetof(WorldCommand, payload) == 32
              && offsetof(WorldCommand, extra) == 48);
static_assert(offsetof(WorldEvent, command) == 16 && offsetof(WorldEvent, payload) == 32
              && offsetof(WorldEvent, extra) == 48);
static_assert(offsetof(CommandBatch, commands) == 64 && sizeof(CommandBatch) == 256);
static_assert(offsetof(EventBatch, events) == 16 && sizeof(EventBatch) == 9232);
static_assert(std::is_trivially_copyable_v<EventBatch>);
} // namespace strefa::game
