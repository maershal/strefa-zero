#include "gpu/explosion_system.hpp"

#include "gpu/upload_ring.hpp"
#include "gpu/world.hpp"

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
void ExplosionSystem::upload(const PassContext& context, UploadRing& uploads) {
    if (queue_.empty()) return;
    const auto e = queue_.front();
    queue_.pop_front();
    const std::array<float, 4> payload{e.x, e.y, e.radius, e.impulse};
    const std::array<std::uint32_t, 4> id{e.id, 0, 0, 0};
    uploads.write(context.bindings[6], explosion_offset, 16, payload.data());
    uploads.write(context.bindings[6], explosion_offset + 16, 16, id.data());
}
void ExplosionSystem::apply(PassContext& context, ExplosionPasses& passes) const {
    passes.mission_pass(context, 7, capacity, true, true, false);
    passes.mission_pass(context, 5, width * height, false, false, false);
    // Pending cells keep the material until a free slot is available.
    passes.birth_pass(context);
    passes.mission_pass(context, 6, width * height, false, false, true);
    passes.rebuild_pass(context);
}
} // namespace strefa::gpu::detail
