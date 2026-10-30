#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace strefa::gpu {
enum class NeighborMode { linked, sorted, reused };
enum class NeighborReads { full, narrow, sorted };
inline constexpr std::string_view neighbor_reads_name(NeighborReads mode){
    switch(mode){case NeighborReads::full:return "full";case NeighborReads::narrow:return "narrow";case NeighborReads::sorted:return "sorted";}
    return "unknown";
}
inline constexpr NeighborReads effective_neighbor_reads(NeighborReads mode,NeighborMode index){
    return mode==NeighborReads::sorted&&index==NeighborMode::linked?NeighborReads::narrow:mode;
}
enum class CouplingMode { reference, local, combined };
enum class DeviceStorageMode { direct, staged, immutable, automatic };
inline constexpr std::string_view device_storage_name(DeviceStorageMode mode){
    switch(mode){
    case DeviceStorageMode::direct:return "direct";
    case DeviceStorageMode::staged:return "staged";
    case DeviceStorageMode::immutable:return "immutable";
    case DeviceStorageMode::automatic:return "auto";
    }
    return "unknown";
}
inline constexpr DeviceStorageMode resolve_device_storage(DeviceStorageMode mode,bool storage_supported){
    return mode==DeviceStorageMode::automatic?(storage_supported?DeviceStorageMode::immutable:DeviceStorageMode::staged):mode;
}
struct UploadStats {
    DeviceStorageMode mode{DeviceStorageMode::direct};
    std::uint64_t writes{},bytes{},copies{},fence_polls{},busy_orphans{},allocations{},staging_bytes{},staging_peak_bytes{},device_bytes{};
};
struct ExecutionOptions {
    bool specialized{true},cached_bindings{true},fifo_queries{true},conditional_work{true};
    unsigned local_size{256},reduction_size{256};
    NeighborMode neighbors{NeighborMode::reused};
    bool soa{true};
    CouplingMode coupling{CouplingMode::combined};
    // I04.5-C ablations remain opt-in until correctness and cost are measured.
    bool phase_gating{false},skip_zero_projection{false};
    DeviceStorageMode storage{DeviceStorageMode::direct};
    NeighborReads neighbor_reads{NeighborReads::full};
    static ExecutionOptions reference(){return {false,false,false,false,256,256,NeighborMode::linked,false,CouplingMode::reference};}
};
enum class ReadbackMode { automatic, legacy, stream, staging, persistent };
inline constexpr std::string_view readback_mode_name(ReadbackMode mode){
    switch(mode){
    case ReadbackMode::automatic:return "auto";
    case ReadbackMode::legacy:return "legacy";
    case ReadbackMode::stream:return "stream";
    case ReadbackMode::staging:return "staging";
    case ReadbackMode::persistent:return "persistent";
    }
    return "unknown";
}
struct ReadbackStats {
    ReadbackMode mode{ReadbackMode::staging};
    unsigned capacity{},pending{},max_pending{};
    std::uint64_t submitted{},completed{},dropped{},stale{},busy_polls{},last_latency_ticks{};
};
enum class TimerMode { elapsed, timestamp, off };
inline constexpr std::string_view timer_mode_name(TimerMode mode){
    switch(mode){
    case TimerMode::elapsed:return "elapsed";
    case TimerMode::timestamp:return "timestamp";
    case TimerMode::off:return "off";
    }
    return "unknown";
}
// Ablations are diagnostic experiments, never alternative production physics.
struct ProfileOptions {
    bool collect{};
    bool gpu_timers{true};
    bool group_timers{};
    bool periodic_diagnostics{true};
    bool body_coupling{true};
    unsigned readback_period{2}; // zero disables HUD/counter staging, not validation
    ReadbackMode readback_mode{ReadbackMode::automatic};
    TimerMode timer_mode{TimerMode::elapsed};
};
inline constexpr TimerMode effective_timer_mode(const ProfileOptions& options){
    return options.gpu_timers?options.timer_mode:TimerMode::off;
}

#define STREFA_GPU_COUNTERS(X) \
    X(api_calls) X(dispatches) X(barriers) X(bins) X(active_rebuilds) \
    X(body_reductions) X(buffer_binds) X(base_binds) X(program_uses) \
    X(query_polls) X(query_results) X(fence_polls) X(upload_bytes) \
    X(copy_bytes) X(read_bytes) X(read_calls) X(stage_requests) X(stage_copies) \
    X(render_state_queries) X(stage_drops) X(stale_readbacks) X(mapped_read_bytes) \
    X(indirect_dispatches) X(uniform_updates) X(timer_submissions) X(timer_skips) X(timer_stale)

struct CallCounters {
#define STREFA_DECLARE_COUNTER(name) std::uint64_t name{};
    STREFA_GPU_COUNTERS(STREFA_DECLARE_COUNTER)
#undef STREFA_DECLARE_COUNTER
};
inline CallCounters operator-(const CallCounters& a,const CallCounters& b){
    CallCounters result;
#define STREFA_SUBTRACT_COUNTER(name) result.name=a.name-b.name;
    STREFA_GPU_COUNTERS(STREFA_SUBTRACT_COUNTER)
#undef STREFA_SUBTRACT_COUNTER
    return result;
}

struct StepProfile {
    std::uint64_t tick{};
    unsigned generation{};
    CallCounters calls;
    double total_ms{},poll_ms{},upload_ms{},prepare_ms{},solver_ms{},diagnostics_ms{},staging_ms{};
    double timer_poll_ms{},fence_poll_ms{},read_ms{};
    double timer_begin_ms{},timer_end_ms{};
    TimerMode timer_mode{TimerMode::elapsed};
    std::uint64_t readback_age_ticks{};
};
inline constexpr std::array<std::string_view,5> gpu_group_names{
    "prepare","substep_0","substep_1","diagnostics","staging"};
struct GroupProfile {
    std::uint64_t tick{};
    unsigned generation{};
    std::array<double,5> milliseconds{};
};
struct PollTimings { double timers_ms{},fence_ms{},read_ms{}; };
}
