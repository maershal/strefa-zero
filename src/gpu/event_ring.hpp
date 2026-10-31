#pragma once
#include "gpu/gl_api.hpp"
#include "game/gameplay_session.hpp"

namespace strefa::gpu::detail {
struct EventStats {unsigned pending{},max_pending{};std::uint64_t submitted{},acknowledged{},stale{},backpressure{},max_age_ticks{};double logic_ms{};};
// One staging slot is reserved BEFORE a tick. It remains owned until its full
// packet is consumed. HUD drops and HUD sampling rate cannot affect this ring.
class EventRing {
public:
    explicit EventRing(Api& gl):gl_{gl}{}
    ~EventRing();
    EventRing(const EventRing&)=delete;
    EventRing& operator=(const EventRing&)=delete;
    void initialize();
    bool available(){if(stats_.pending<game::event_slots)return true;++stats_.backpressure;return false;}
    void submit(GLuint source,GLintptr offset,unsigned generation,unsigned tick);
    void poll(game::GameplaySession&);
    const EventStats& stats()const{return stats_;}
private:
    struct Slot {GLuint buffer{};GLsync fence{};const void* mapped{};unsigned generation{},tick{};};
    Api& gl_;
    std::array<Slot,game::event_slots> slots_{};
    unsigned head_{};
    EventStats stats_;
};
}
