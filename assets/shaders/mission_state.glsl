#include "world_protocol.glsl"
struct MissionState {
    vec4 actor,previous,aim,locomotion,timers;
    uvec4 status,inventory,counts,events;
    vec4 control,exitZone,body,blast,flash;
    vec4 targets[3];
};
uniform int disableBodyCoupling;
struct Projectile { vec4 motion; vec4 info; };
#ifdef MISSION_READ_ONLY
layout(std430,binding=6) readonly buffer Counters {
#else
layout(std430,binding=6) buffer Counters {
#endif
    uint freeCount,requested,aliveCount,waterCount,oilCount,maxCompression,reserved0,reserved1;
    MissionState game;
    Projectile bullets[128];
    Projectile grenades[8];
    vec4 requestedBlast;uvec4 requestedBlastId;
    uvec4 transportState; // last command ID, unused, unused, terrain baseline
    CommandBatch commandBatch;EventBatch eventBatch;
};
bool bodyCouplingEnabled(){return game.status.x!=0u&&disableBodyCoupling==0;}
float capsuleDistance(vec2 p,vec2 feet){
    vec2 q=p-feet;return length(q-vec2(0,clamp(q.y,-14,-4)))-4;
}
bool bodySample(vec2 p){return bodyCouplingEnabled()&&capsuleDistance(p,game.actor.xy)<0;}
vec2 bodySamplePosition(ivec2 local){return game.actor.xy+vec2(local)+.5;}
#define atan2 atan
#include "soldier_pose.glsl"
#undef atan2
SoldierPose currentPose(vec2 feet){return soldierPose(feet,game.actor.zw,game.aim.xy,game.locomotion.x,game.aim.w,game.locomotion.y,game.locomotion.w,game.locomotion.z>.5);}
vec2 shoulder(){return currentPose(game.actor.xy).shoulder;}
vec2 gunPosition(){return currentPose(game.actor.xy).gun;}
vec2 muzzlePosition(){return currentPose(game.actor.xy).muzzle;}
