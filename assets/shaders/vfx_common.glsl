const uint VFX_CAPACITY=16384u;
struct VfxParticle {vec4 motion;vec4 history;uvec4 appearance;};
struct VfxEvent {uvec4 identity;vec4 source;vec4 velocity;};
layout(std430,binding=0) buffer VfxParticles {VfxParticle particles[];};
layout(std430,binding=1) readonly buffer VfxAliveIn {uint aliveIn[];};
layout(std430,binding=2) buffer VfxAliveOut {uint aliveOut[];};
layout(std430,binding=3) buffer VfxFree {uint freeSlots[];};
layout(std430,binding=4) buffer VfxControl {uvec4 counts;uvec4 clockState;uvec4 layerCounts;uint emitted[12];uint rejected[12];uvec4 drawCommand;uvec4 copyCommand;uvec4 waveCommand;uvec4 preCommand;uvec4 postCommand;};
layout(std430,binding=5) readonly buffer VfxCpuEvents {uvec4 cpuHeader;VfxEvent cpuEvents[];};
layout(std430,binding=7) buffer VfxDrawSlots {uint drawSlots[];};
layout(binding=2) uniform sampler2D waterField;
layout(binding=3) uniform usampler2D terrainField;
uniform vec4 stepData; // generation, tick bits in x/y; seconds and reserved in z/w
uniform int quality;uniform float fieldNormalization;
uniform int sourceGeneration,sourceTick;
uint hashVfx(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float randomVfx(uint x){return float(hashVfx(x)&65535u)/65535.;}
bool vfxSolid(ivec2 p){return any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,ivec2(320,192)))||texelFetch(terrainField,p,0).r!=0u;}
vec4 fluidAt(vec2 p){ivec2 c=clamp(ivec2(floor(p)),ivec2(0),ivec2(319,191));vec4 f=texelFetch(waterField,c,0);float d=f.x+f.y;return vec4(clamp(f.x*fieldNormalization,0,1),clamp(f.y*fieldNormalization,0,1),d>1e-6?f.zw/d:vec2(0));}
// Full swept segment. Corner ties test both side cells. Exhaustion is blocked.
bool vfxClear(vec2 a,vec2 b){
    ivec2 c=ivec2(floor(a)),end=ivec2(floor(b)),s=ivec2(sign(b-a));vec2 d=b-a;
    vec2 delta=vec2(1e20),next=vec2(1e20);
    for(int k=0;k<2;++k)if(abs(d[k])>1e-8){delta[k]=abs(1/d[k]);next[k]=((s[k]>0?float(c[k]+1):float(c[k]))-a[k])/d[k];}
    for(int i=0;i<516;++i){if(vfxSolid(c))return false;if(all(equal(c,end)))return true;
        if(abs(next.x-next.y)<1e-6){if(vfxSolid(c+ivec2(s.x,0))||vfxSolid(c+ivec2(0,s.y)))return false;c+=s;next+=delta;}
        else if(next.x<next.y){c.x+=s.x;next.x+=delta.x;}else{c.y+=s.y;next.y+=delta.y;}
    }return false;
}
void appendVfxDraw(uint id,uint kind){
    // Stable secondary-particle LOD: low targets 8192 drawn water markers; every slot still updates.
    if(quality==1&&kind>=5u&&kind<=7u&&counts.x>8192u&&float(hashVfx(particles[id].appearance.w)&65535u)>=65536.*8192./float(counts.x))return;
uint layer=(kind==7u||kind==9u||kind==10u)?0u:kind==11u?2u:1u;uint index=atomicAdd(layerCounts[layer],1u);drawSlots[layer*VFX_CAPACITY+index]=id;}
void emitVfx(uint kind,vec2 p,vec2 v,float life,float size,uint color,uint seed,bool important){
    uint reserve=important?0u:2458u,available=counts.y;
    for(;;){if(available<=reserve){atomicAdd(rejected[kind],1u);return;}uint previous=atomicCompSwap(counts.y,available,available-1u);if(previous==available)break;available=previous;}
    uint slot=freeSlots[available-1u],index=atomicAdd(counts.z,1u);
    particles[slot]=VfxParticle(vec4(p,v),vec4(p,0,life),uvec4(kind,color,floatBitsToUint(size),seed));aliveOut[index]=slot;appendVfxDraw(slot,kind);atomicAdd(emitted[kind],1u);
}
