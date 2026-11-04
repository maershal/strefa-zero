#include "gpu/explosion_system.hpp"

#include "gpu/dispatch_layout.hpp"
#include "gpu/upload_ring.hpp"
#include "gpu/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace strefa::gpu::detail {
void ExplosionSystem::queue(const Explosion& e) {
    if (e.id == 0 || !std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.radius) || !std::isfinite(e.impulse)
        || e.x < 0 || e.x >= width || e.y < 0 || e.y >= height || e.radius <= 0 || e.radius > 32 || e.impulse < 0
        || e.impulse > 80)
        throw std::invalid_argument{"Nieprawidłowy wybuch GPU."};
    if (queue_.size() >= 32) throw std::runtime_error{"Kolejka wybuchów GPU jest pełna."};
    queue_.push_back(e);
}
void ExplosionSystem::upload(const PassContext& context, UploadRing& uploads, const BlastTick& tick) {
    if (queue_.empty()) return;
    if (tick.mission && !tick.batched) {
        const auto e = queue_.front();
        queue_.pop_front();
        const std::array<float, 4> payload{e.x, e.y, e.radius, e.impulse};
        const std::array<std::uint32_t, 4> id{e.id, 0, 0, 0};
        uploads.write(context.bindings[6], explosion_offset, 16, payload.data());
        uploads.write(context.bindings[6], explosion_offset + 16, 16, id.data());
        return;
    }
    struct {
        std::array<std::array<float, 4>, blasts_per_tick> shapes{};
        std::array<std::array<std::uint32_t, 4>, blasts_per_tick> ids{};
        std::array<std::uint32_t, 4> header{};
    } list;
    static_assert(sizeof(list) == mission_buffer_size - blast_offset);
    game::EventBatch events;
    const auto count = static_cast<unsigned>(std::min<std::size_t>(queue_.size(), blasts_per_tick));
    for (unsigned i = 0; i < count; ++i) {
        const auto e = queue_.front();
        queue_.pop_front();
        list.shapes[i] = {e.x, e.y, e.radius, e.impulse};
        list.ids[i] = {e.id, 0, 0, 0};
        // As mission operation 0 emits a requested explosion: subject = explosion ID, value 1.
        auto& event = events.events[i];
        event.generation = tick.generation;
        event.tick = tick.tick;
        event.id = i + 1;
        event.kind = static_cast<std::uint32_t>(game::EventKind::explosion);
        event.subject = e.id;
        event.value = 1;
        event.payload = list.shapes[i];
    }
    list.header = {count, tick.tick, 1, 0};
    uploads.write(context.bindings[6], blast_offset, static_cast<GLsizeiptr>(sizeof(list)), &list);
    if (tick.mission) return;
    events.header = {count, tick.tick, tick.generation, 0};
    uploads.write(context.bindings[6], event_offset, static_cast<GLsizeiptr>(16 + count * sizeof(game::WorldEvent)),
                  &events);
    // Mission operation 0 writes these commands on the GPU; without it the host covers every slot and cell.
    const unsigned particles = (capacity + tick.local_size - 1) / tick.local_size,
                   cells = (width * height + tick.local_size - 1) / tick.local_size;
    const std::array<std::uint32_t, 9> commands{particles, 1, 1, cells, 1, 1, cells, 1, 1};
    static_assert(blast_terrain_command == blast_command + 3 && debris_command == blast_command + 6);
    uploads.write(context.bindings[5], command_offset(blast_command), static_cast<GLsizeiptr>(sizeof(commands)),
                  commands.data());
    lab_tick_ = tick.tick;
}
bool ExplosionSystem::apply(PassContext& context, ExplosionPasses& passes, const BlastTick& tick) const {
    if (!tick.mission && !lab_events(tick.tick)) return false;
    passes.mission_pass(context, 7, capacity, true, true, false);
    passes.mission_pass(context, 5, width * height, false, false, false);
    // Pending cells keep the material until a free slot is available.
    passes.birth_pass(context);
    passes.mission_pass(context, 6, width * height, false, false, true);
    passes.rebuild_pass(context);
    return true;
}
float ExplosionSystem::flash(std::uint64_t tick) const noexcept {
    if (lab_tick_ == 0 || tick < lab_tick_) return 0;
    // Mission operation 0 sets .3 s and decays it by one tick in the same tick.
    const auto seconds = .3F - static_cast<float>(tick - lab_tick_ + 1) / 120.F;
    return std::clamp(seconds / .3F, 0.F, 1.F);
}
} // namespace strefa::gpu::detail
