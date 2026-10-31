// Mirrors game/world_protocol.hpp. Kept in the existing binding 6 for GL 4.3.
struct WorldCommand {uvec4 identity;uvec4 detail;vec4 payload;vec4 extra;};
struct WorldEvent {uvec4 identity;uvec4 detail;vec4 payload;vec4 extra;};
struct CommandBatch {uvec4 header;uvec4 inventory;vec4 rules;uvec4 targets;WorldCommand commands[3];};
struct EventBatch {uvec4 header;WorldEvent events[144];};
