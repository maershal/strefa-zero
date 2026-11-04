#pragma once
#include "gpu/mission.hpp"
#include "gpu/pass_context.hpp"

#include <cstdint>
#include <deque>

namespace strefa::gpu::detail {
class UploadRing;
// Pass helpers the explosion sequence runs; ParticleWorld::Impl implements them with its own dispatch code, so
// buffers, barriers, trace and counters stay those of the world.
class ExplosionPasses {
public:
    virtual void mission_pass(PassContext&, int op, unsigned count, bool swap, bool active, bool in_place) = 0;
    virtual void birth_pass(PassContext&) = 0;   // presentation birth buffer, then binding invalidation
    virtual void rebuild_pass(PassContext&) = 0; // active-list rebuild, gated by the dirty flag

protected:
    ~ExplosionPasses() = default;
};
// The tick an upload or apply belongs to.
struct BlastTick {
    bool mission{};                     // mission scene: operation 0 runs every tick and emits the explosion events
    bool batched{};                     // ExecutionOptions::batched_blasts; the laboratory always batches
    std::uint32_t generation{}, tick{}; // tick: the mission's gameplay tick, without a mission step + 1
    unsigned local_size{};              // particle work group size, for the laboratory's indirect dispatch commands
};
// Queued explosions. The mission (by default) uploads one per tick into its request slot. The laboratory, and the
// mission with batched_blasts, upload up to blasts_per_tick per tick into the batched list; the rest wait in the
// queue for the next tick. One blast impulse, one terrain damage and one debris pass handle every explosion of the
// tick, in a fixed order.
class ExplosionSystem {
public:
    void queue(const Explosion&); // validated; at most 32 pending
    void clear() {
        queue_.clear();
        lab_tick_ = 0;
    }
    bool pending() const noexcept { return !queue_.empty(); }
    // Uploads the oldest pending explosions (mission buffer, binding 6). Without a mission it also writes their
    // WorldEvents (kind 4, read by VFX, lighting and thermal) and the indirect commands of the three passes.
    void upload(const PassContext&, UploadRing&, const BlastTick&);
    // Runs after mission operation 0; without a mission only on a tick that uploaded explosions. Returns whether the
    // event batch in binding 6 belongs to this tick (always with a mission).
    bool apply(PassContext&, ExplosionPasses&, const BlastTick&) const;
    // Laboratory: whether the event batch belongs to this tick, and the flash (lighting and camera shake, 0-1)
    // that decays like MissionSnapshot::flash over the .3 s after the last explosion.
    bool lab_events(std::uint64_t tick) const noexcept { return lab_tick_ != 0 && lab_tick_ == tick; }
    float flash(std::uint64_t tick) const noexcept;

private:
    std::deque<Explosion> queue_;
    std::uint64_t lab_tick_{};
};
} // namespace strefa::gpu::detail
