#include "gpu/world.hpp"

#include <fstream>

namespace strefa::gpu {
void write_profile_samples(ParticleWorld& world, const std::filesystem::path& output) {
    std::ofstream steps{output / "window-steps.csv"}, groups{output / "window-gpu-groups.csv"};
    steps << "generation,tick,total_ms,poll_ms,upload_ms,prepare_ms,solver_ms,diagnostics_ms,staging_ms,timer_poll_ms,fence_poll_ms,read_ms,readback_age_ticks";
#define WRITE_HEADER(name) steps << "," #name;
    STREFA_GPU_COUNTERS(WRITE_HEADER)
#undef WRITE_HEADER
    steps << ",timer_begin_ms,timer_end_ms,timer_mode\n";
    for (const auto& p : world.take_step_profiles()) {
        steps << p.generation << ',' << p.tick << ',' << p.total_ms << ',' << p.poll_ms << ',' << p.upload_ms << ','
              << p.prepare_ms << ',' << p.solver_ms << ',' << p.diagnostics_ms << ',' << p.staging_ms << ','
              << p.timer_poll_ms << ',' << p.fence_poll_ms << ',' << p.read_ms << ',' << p.readback_age_ticks;
#define WRITE_VALUE(name) steps << ',' << p.calls.name;
        STREFA_GPU_COUNTERS(WRITE_VALUE)
#undef WRITE_VALUE
        steps << ',' << p.timer_begin_ms << ',' << p.timer_end_ms << ',' << timer_mode_name(p.timer_mode) << '\n';
    }
    groups << "generation,tick";
    for (auto name : gpu_group_names) groups << ',' << name << "_ms";
    groups << '\n';
    for (const auto& p : world.take_group_profiles()) {
        groups << p.generation << ',' << p.tick;
        for (double ms : p.milliseconds) groups << ',' << ms;
        groups << '\n';
    }
    const auto calls = world.counters();
    std::ofstream totals{output / "window-counters.txt"};
#define WRITE_TOTAL(name) totals << #name "=" << calls.name << '\n';
    STREFA_GPU_COUNTERS(WRITE_TOTAL)
#undef WRITE_TOTAL
    const auto options = world.profile_options();
    totals << "gpu_timer_mode=" << timer_mode_name(effective_timer_mode(options))
           << "\ngpu_timers_requested=" << options.gpu_timers << "\ngroup_timers_requested=" << options.group_timers
           << "\nreadback_period=" << options.readback_period << '\n';
    const auto execution = world.execution_options();
    totals << "specialized=" << execution.specialized << "\ncached_bindings=" << execution.cached_bindings
           << "\nfifo_queries=" << execution.fifo_queries << "\ncoupling_mode=" << static_cast<int>(execution.coupling)
           << "\nneighbor_mode=" << static_cast<int>(execution.neighbors) << "\nsoa=" << execution.soa
           << "\nconditional_work=" << execution.conditional_work << "\nlocal_size=" << execution.local_size
           << "\nreduction_size=" << execution.reduction_size << "\npbf_size_requested=" << execution.pbf_size
           << "\npbf_size_effective=" << execution.effective_pbf_size()
           << "\npbf_restrict_requested=" << execution.pbf_restrict
           << "\npbf_restrict_effective=" << execution.effective_pbf_restrict() << '\n';
    const auto stats = world.readback_stats();
    totals << "readback_mode=" << readback_mode_name(stats.mode) << "\nreadback_capacity=" << stats.capacity
           << "\nreadback_pending=" << stats.pending << "\nreadback_max_pending=" << stats.max_pending
           << "\nreadback_submitted=" << stats.submitted << "\nreadback_completed=" << stats.completed
           << "\nreadback_dropped=" << stats.dropped << "\nreadback_stale=" << stats.stale
           << "\nreadback_latency_ticks=" << stats.last_latency_ticks << '\n';
    const auto upload = world.upload_stats();
    totals << "neighbor_reads_requested=" << neighbor_reads_name(execution.neighbor_reads)
           << "\nneighbor_reads_effective="
           << neighbor_reads_name(effective_neighbor_reads(execution.neighbor_reads, execution.neighbors))
           << "\nhot_cache_bytes="
           << (effective_neighbor_reads(execution.neighbor_reads, execution.neighbors) == NeighborReads::sorted
                       ? capacity * 64
                       : 0)
           << '\n';
    totals << "storage_requested=" << device_storage_name(execution.storage)
           << "\nstorage_effective=" << device_storage_name(upload.mode) << "\nupload_writes=" << upload.writes
           << "\nupload_payload_bytes=" << upload.bytes << "\nupload_copies=" << upload.copies
           << "\nupload_fence_polls=" << upload.fence_polls << "\nupload_busy_orphans=" << upload.busy_orphans
           << "\nupload_allocations=" << upload.allocations << "\nupload_staging_bytes=" << upload.staging_bytes
           << "\nupload_staging_peak_bytes=" << upload.staging_peak_bytes
           << "\ndevice_buffer_bytes=" << upload.device_bytes << '\n';
}
} // namespace strefa::gpu
