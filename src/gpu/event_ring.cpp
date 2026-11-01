#include "gpu/event_ring.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace strefa::gpu::detail {
EventRing::~EventRing() {
    for (auto& s : slots_) {
        if (s.fence) gl_.DeleteSync(s.fence);
        if (s.mapped) {
            gl_.BindBuffer(GL_COPY_WRITE_BUFFER, s.buffer);
            gl_.UnmapBuffer(GL_COPY_WRITE_BUFFER);
        }
        if (s.buffer) gl_.DeleteBuffers(1, &s.buffer);
    }
}
void EventRing::initialize() {
    for (auto& s : slots_) {
        gl_.GenBuffers(1, &s.buffer);
        gl_.BindBuffer(GL_COPY_WRITE_BUFFER, s.buffer);
        if (gl_.persistent_storage) {
            constexpr auto flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            gl_.BufferStorage(GL_COPY_WRITE_BUFFER, sizeof(game::EventBatch), nullptr, flags | GL_CLIENT_STORAGE_BIT);
            s.mapped = gl_.MapBufferRange(GL_COPY_WRITE_BUFFER, 0, sizeof(game::EventBatch), flags);
            if (!s.mapped) throw std::runtime_error{"Cannot map reliable events"};
        } else
            gl_.BufferData(GL_COPY_WRITE_BUFFER, sizeof(game::EventBatch), nullptr, GL_STREAM_READ);
    }
}
void EventRing::submit(GLuint source, GLintptr offset, unsigned generation, unsigned tick) {
    if (stats_.pending == game::event_slots) throw std::logic_error{"Event slot not reserved before tick"};
    auto& s = slots_[(head_ + stats_.pending) % game::event_slots];
    gl_.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl_.BindBuffer(GL_COPY_READ_BUFFER, source);
    gl_.BindBuffer(GL_COPY_WRITE_BUFFER, s.buffer);
    gl_.CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, offset, 0, sizeof(game::EventBatch));
    gl_.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    s.fence = gl_.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!s.fence) throw std::runtime_error{"Cannot fence reliable events"};
    if (gl_.observation) ++gl_.observation->event_copies;
    s.generation = generation;
    s.tick = tick;
    ++stats_.pending;
    ++stats_.submitted;
    stats_.max_pending = std::max(stats_.max_pending, stats_.pending);
}
void EventRing::poll(game::GameplaySession& session) {
    while (stats_.pending) {
        auto& s = slots_[head_];
        const auto status = gl_.ClientWaitSync(s.fence, 0, 0);
        if (status == GL_WAIT_FAILED) throw std::runtime_error{"Reliable event fence failed"};
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) break;
        if (s.generation == session.generation()) {
            game::EventBatch batch;
            if (s.mapped)
                std::memcpy(&batch, s.mapped, sizeof(batch));
            else {
                gl_.BindBuffer(GL_COPY_WRITE_BUFFER, s.buffer);
                gl_.GetBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(batch), &batch);
            }
            if (batch.header[1] != s.tick || batch.header[2] != s.generation)
                throw std::runtime_error{"Reliable event publication mismatch"};
            const auto logic_started = SDL_GetTicksNS();
            session.consume(batch);
            stats_.logic_ms += static_cast<double>(SDL_GetTicksNS() - logic_started) / 1e6;
            ++stats_.acknowledged;
            if (gl_.observation) {
                ++gl_.observation->event_packets;
                if (s.mapped) gl_.observation->mapped_read_bytes += sizeof(batch);
            }
            stats_.max_age_ticks = std::max(stats_.max_age_ticks, static_cast<std::uint64_t>(session.tick() - s.tick));
        } else
            ++stats_.stale;
        gl_.DeleteSync(s.fence);
        s.fence = nullptr;
        --stats_.pending;
        head_ = (head_ + 1) % game::event_slots;
    }
}
} // namespace strefa::gpu::detail
