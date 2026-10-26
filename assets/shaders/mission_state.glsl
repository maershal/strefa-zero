struct MissionState {
    vec4 actor,previous,aim,locomotion,timers;
    uvec4 status,inventory,counts,events;
    vec4 control,exitZone,body,blast,flash;
    vec4 targets[3];
};
struct Projectile { vec4 motion; vec4 info; };
layout(std430,binding=6) buffer Counters {
    uint freeCount,requested,aliveCount,waterCount,oilCount,maxCompression,reserved0,reserved1;
    MissionState game;
    Projectile bullets[128];
    Projectile grenades[8];
    vec4 requestedBlast;uvec4 requestedBlastId;
};
float capsuleDistance(vec2 p,vec2 feet){
    vec2 q=p-feet;return length(q-vec2(0,clamp(q.y,-14,-4)))-4;
}
bool bodySample(vec2 p){return game.status.x!=0u&&capsuleDistance(p,game.actor.xy)<0;}
vec2 bodySamplePosition(ivec2 local){return game.actor.xy+vec2(local)+.5;}
vec2 shoulder(){
    float compression=game.locomotion.y*1.5;
    float bob=game.locomotion.z>.5?abs(cos(game.locomotion.x))*game.locomotion.w*.4:0;
    return game.actor.xy+vec2(game.actor.z*.009,-13.7+compression-bob)-game.aim.xy*(game.aim.w*.6);
}
vec2 gunPosition(){return shoulder()+game.aim.xy*(3-game.aim.w*.7);}
vec2 muzzlePosition(){return gunPosition()+game.aim.xy*6.5;}
