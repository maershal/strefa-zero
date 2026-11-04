#pragma once
#include "gpu/mission.hpp"
#include "gpu/pass_context.hpp"

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
// Queued explosions of the mission: one per tick is uploaded into the mission buffer, then the blast impulse,
// terrain damage and debris passes run in a fixed order.
class ExplosionSystem {
public:
    void queue(const Explosion&); // validated; at most 32 pending
    void clear() { queue_.clear(); }
    bool pending() const noexcept { return !queue_.empty(); }
    // Uploads the oldest pending explosion (mission buffer, binding 6); no-op when none is pending.
    void upload(const PassContext&, UploadRing&);
    // Runs after mission operation 0 has consumed the upload.
    void apply(PassContext&, ExplosionPasses&) const;

private:
    std::deque<Explosion> queue_;
};
} // namespace strefa::gpu::detail
