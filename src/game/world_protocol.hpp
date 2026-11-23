#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace strefa::game {
// std430; identity is (generation,tick,id). IDs are exact integers, never floats.
// Counters stop before UINT32_MAX; starting a new generation is explicit.
// c4_place throws a charge into the grenade pool; c4_detonate fires every charge already stuck to terrain (or falling
// after losing its anchor) and reports their number in the accepted event's value. A charge still in flight is skipped.
enum class CommandKind : std::uint32_t { rifle = 1, grenade = 2, jump = 3, shotgun = 4, c4_place = 5, c4_detonate = 6 };
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
// Accepted rifle and shotgun events use reserved=1, payload.xy=muzzle, extra.xy=launch velocity (centre pellet).
// Presentation fields do not authorize commands or change the 64-byte ABI.
struct alignas(16) WorldEvent {
    std::uint32_t generation{}, tick{}, id{}, kind{};
    std::uint32_t command{}, subject{}, value{}, reserved{};
    std::array<float, 4> payload{}, extra{};
};
inline constexpr unsigned commands_per_tick = 3, events_per_tick = 144, event_slots = 32, command_capacity = 128;
struct alignas(16) CommandBatch {
    std::array<std::uint32_t, 4> header{};    // generation, tick, count, cancel jump
    std::array<std::uint32_t, 4> inventory{}; // ammunition, Stock::pack(), score, shots
    std::array<float, 4> rules{};             // cooldown, reload, finished, unused
    std::array<std::uint32_t, 4> targets{};   // HP, final element unused
    std::array<WorldCommand, commands_per_tick> commands{};
};
struct alignas(16) EventBatch {
    std::array<std::uint32_t, 4> header{}; // count, tick, generation, overflow (fatal)
    std::array<WorldEvent, events_per_tick> events{};
};
inline constexpr unsigned rifle_magazine = 30, grenade_stock = 3, shotgun_tube = 6, c4_stock = 2;
// Weapon slots 1, 2 and 3 of the mission HUD.
enum class Weapon : std::uint32_t { rifle = 0, shotgun = 1, c4 = 2 };
// inventory[1] packs the secondary stock, so the 256-byte batch and the 272-byte snapshot keep their layout:
// grenades in bits 0-7, shotgun shells 8-15, C4 charges carried 16-23, charges placed 24-27, weapon 28-29.
struct Stock {
    unsigned grenades{}, shells{}, charges{}, placed{};
    Weapon weapon{Weapon::rifle};
    [[nodiscard]] constexpr std::uint32_t pack() const noexcept {
        return (grenades & 0xffU) | (shells & 0xffU) << 8U | (charges & 0xffU) << 16U | (placed & 0xfU) << 24U
             | (static_cast<std::uint32_t>(weapon) & 3U) << 28U;
    }
    [[nodiscard]] static constexpr Stock unpack(std::uint32_t v) noexcept {
        return {v & 0xffU, v >> 8U & 0xffU, v >> 16U & 0xffU, v >> 24U & 0xfU, static_cast<Weapon>(v >> 28U & 3U)};
    }
};
inline constexpr std::uint32_t initial_stock = Stock{grenade_stock, shotgun_tube, c4_stock}.pack();
static_assert(Stock::unpack(Stock{3, 6, 2, 1, Weapon::c4}.pack()).placed == 1);
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
