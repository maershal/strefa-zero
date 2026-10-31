#pragma once
#include <cstddef>

namespace strefa::gpu::detail {
// Word offsets after the two N-word ID lists. Mirrored in particle_common.glsl;
// execution validation reads the commands produced by the actual GPU programs.
inline constexpr unsigned active_command=0,draw_command=3,rebuild_command=7,
    sync_command=10,terrain_command=13,blast_command=16,blast_terrain_command=19,
    debris_command=22,reduction_command=25,body_reduce_command=28,body_final_command=31,pbf_command=34,command_words=37;
inline constexpr std::size_t body_metadata_size=64+65536*4+1024*16;
inline constexpr std::ptrdiff_t command_offset(unsigned command){return 32768*8+command*4;}
}
