#include "particle_storage.glsl"
#if SORTED_NEIGHBORS
// Header, cell counts, exclusive local prefixes, scatter cursors, block sums
// and block offsets. The cell grid is never a fixed-capacity particle list.
layout(std430,binding=2) coherent buffer Cells {uint cells[];};
#if HOT_READS == 2
struct HotParticle {vec4 geometry;vec4 motion;};
#endif
layout(std430,binding=3) buffer Sorted {
    uint order[32768];uint sortedIndex[32768];vec4 anchors[32768];
#if HOT_READS == 2
    // Two streams mirror the authoritative particle ping-pong banks. The
    // reverse map is for producers; neighbor gathers use their existing entry.
    HotParticle hot[65536];
#endif
#if COUPLING_MODE != 0
    uvec4 bodyQueue,bodyStats;
    vec4 bodyLastSum,bodyTotalSum;
    uint bodyIds[65536];vec4 bodyPartial[1024];
#endif
};
const uint CELL_COUNT=61440u,HEADER=16u,COUNT_BASE=HEADER,START_BASE=HEADER+CELL_COUNT,
    CURSOR_BASE=HEADER+2u*CELL_COUNT,BLOCK_BASE=HEADER+3u*CELL_COUNT,
    BLOCK_OFFSET_BASE=BLOCK_BASE+240u,
    PHASE_MASK_BASE=BLOCK_OFFSET_BASE+240u,PHASE_TILE_COLUMNS=20u,PHASE_TILE_COUNT=240u;
#else
layout(std430,binding=2) buffer Heads {int head[];};
#if COUPLING_MODE != 0
layout(std430,binding=3) buffer Links {int link[32768];uvec4 bodyQueue,bodyStats;
    vec4 bodyLastSum,bodyTotalSum;
    uint bodyIds[65536];vec4 bodyPartial[1024];};
#else
layout(std430,binding=3) buffer Links {int link[];};
#endif
#endif
layout(std430,binding=4) buffer Terrain {uint wall[];};
layout(std430,binding=5) buffer Free {uint slots[];};
#include "mission_state.glsl"
// Floating-point solver data stays separate from atomic queue metadata.
// Channels: density, contact/drag reaction, and (combined only) PBF reaction.
layout(std430,binding=7) buffer FluidTerms {vec4 terms[];};
uniform int dirtyOnly;
#ifdef PASS_OPERATION
const int operation=PASS_OPERATION;
#else
uniform int operation;
#endif
const uint REBUILD_COMMAND=7u,SYNC_COMMAND=10u,TERRAIN_COMMAND=13u,
    BLAST_COMMAND=16u,BLAST_TERRAIN_COMMAND=19u,DEBRIS_COMMAND=22u,REDUCTION_COMMAND=25u,BODY_REDUCE_COMMAND=28u,BODY_FINAL_COMMAND=31u;
uniform int substep;
#if HOT_READS == 2 && SORTED_NEIGHBORS
uniform int hotReadBank,hotWriteBank,hotValid;
#endif
uniform vec4 stroke; // start xy, end xy
uniform float brushRadius;
uniform int material;
const int W=320,H=192,N=32768;
void dispatchCommand(uint offset,uint groups){slots[uint(2*N)+offset]=groups;slots[uint(2*N)+offset+1u]=1u;slots[uint(2*N)+offset+2u]=1u;}
const float dt=1.0/240.0,R=.46;
void appendBodyEntry(uint taggedId,vec4 reaction){
#if COUPLING_MODE != 0
    if(!bodyCouplingEnabled()||all(equal(reaction.xyz,vec3(0))))return;
    uint ticket=atomicAdd(bodyQueue.x,1u);atomicMax(bodyQueue.y,ticket+1u);
    if(ticket>=uint(2*N)){atomicAdd(bodyQueue.z,1u);return;}
    bodyIds[ticket]=taggedId;atomicAdd(bodyStats.x,1u);
    atomicMax(slots[2*N+BODY_REDUCE_COMMAND],(ticket+REDUCTION_LOCAL_SIZE)/REDUCTION_LOCAL_SIZE);
    if(ticket>=REDUCTION_LOCAL_SIZE)atomicOr(slots[2*N+BODY_FINAL_COMMAND],1u);
#endif
}
void writeBodyReaction(uint id,vec4 reaction,bool fluid){
#if COUPLING_MODE == 0
    terms[N+id]=reaction;
#else
    uint tagged=id;
#if COUPLING_MODE == 2
    if(fluid)tagged+=uint(N);
#endif
    terms[uint(N)+tagged]=reaction;appendBodyEntry(tagged,reaction);
#endif
}
bool bodyFluidCandidate(vec2 p){
    if(!bodyCouplingEnabled())return false;
#if COUPLING_MODE != 0
    // Exact current-pose broad phase: all interior capsule samples with h=2
    // lie in this box. No cached list can become stale after a body correction.
    vec2 q=p-game.actor.xy;return abs(q.x)<=6.01&&q.y>=-20.01&&q.y<=2.01;
#else
    return true;
#endif
}
#if SORTED_NEIGHBORS
uint cellStart(uint c){return c==CELL_COUNT?aliveCount:cells[START_BASE+c]+cells[BLOCK_OFFSET_BASE+c/256u];}
#endif
void storeParticle(uint id,Grain p){
    writeParticle(id,p);
#if HOT_READS == 2 && SORTED_NEIGHBORS
    if(hotValid!=0&&p.motion.w>.5){
        uint entry=sortedIndex[id];HotParticle value=HotParticle(vec4(p.position.xy,p.residual.xy),p.motion);
        hot[uint(hotWriteBank)*uint(N)+entry]=value;
    }
#endif
#if REUSE_NEIGHBORS
    // The source of the next dispatch is bounded relative to the build,
    // including compensated motion, terrain and capsule corrections.
    vec4 a=anchors[id];float moved=length((p.position.xy-a.xy)+p.residual.xy);
    // Round upwards to reduce atomic contention. An older concurrent read can
    // only cause a redundant atomicMax, never lower the conservative bound.
    uint bound=floatBitsToUint(ceil((moved+.0001)*16)/16);
    if(bound>cells[0])atomicMax(cells[0],bound);
#endif
}
// Each row is one contiguous range in the globally sorted cell ordering.
// Query around CURRENT p, expanded by the maximum displacement of the OTHER
// endpoint since building. Thus |q_anchor-p| <= R + Dmax for every pair in R.
// Unlike a capped Verlet list this remains complete even when 2*Dmax > skin.
ivec4 neighborWindow(Grain p,float reach){
#if SORTED_NEIGHBORS
    reach+=uintBitsToFloat(cells[0])+length(p.residual.xy)+.001;
#endif
    return ivec4(clamp(ivec2(floor(p.position.xy-reach)),ivec2(0),ivec2(W-1,H-1)),
                 clamp(ivec2(floor(p.position.xy+reach)),ivec2(0),ivec2(W-1,H-1)));
}
bool liquidInNeighborWindow(Grain p){
#if PHASE_GATING && SORTED_NEIGHBORS
    // Histogram phases refer to the same anchor positions as order[]. Query
    // every tile intersecting the CURRENT R+Dmax+residual window, including
    // arbitrarily large corrections. No fixed halo or displacement cap.
    ivec4 tiles=neighborWindow(p,2.0)/16;
    for(int y=tiles.y;y<=tiles.w;++y)for(int x=tiles.x;x<=tiles.z;++x)
        if((cells[PHASE_MASK_BASE+uint(y)*PHASE_TILE_COLUMNS+uint(x)]&1u)!=0u)return true;
    return false;
#else
    // Linked lists have no anchor-phase index; their complete old scan is used.
    return true;
#endif
}
#if SORTED_NEIGHBORS
#define FOR_NEIGHBOR(p,reach,j) {ivec4 box=neighborWindow(p,reach);for(int row=box.y;row<=box.w;++row){uint end=cellStart(uint(row*W+box.z+1));for(uint entry=cellStart(uint(row*W+box.x));entry<end;++entry){int j=int(order[entry]);
#define END_NEIGHBOR }}}
#else
#define FOR_NEIGHBOR(p,reach,j) {ivec4 box=neighborWindow(p,reach);for(int row=box.y;row<=box.w;++row)for(int column=box.x;column<=box.z;++column){int j=head[row*W+column];while(j>=0){
#define END_NEIGHBOR j=link[j];}}}
#endif
bool liquid(Grain p){return p.motion.w>=5;}
float radius(Grain p){return liquid(p)?.32:R;}
#if HOT_READS > 0
bool liquid(Interaction p){return p.motion.w>=5;}
float radius(Interaction p){return liquid(p)?.32:R;}
vec2 separation(Grain a,Interaction b){return (a.position.xy-b.position.xy)+(a.residual.xy-b.residual.xy);}
#if SORTED_NEIGHBORS
Interaction readInteraction(uint id,uint entry){
#if HOT_READS == 2
    if(hotValid!=0){
        HotParticle value=hot[uint(hotReadBank)*uint(N)+entry];
        return Interaction(vec4(value.geometry.xy,0,0),value.motion,vec4(value.geometry.zw,0,0));
    }
#endif
    return readInteractionRaw(id);
}
#define READ_INTERACTION(j) readInteraction(uint(j),entry)
#else
#define READ_INTERACTION(j) readInteractionRaw(uint(j))
#endif
#define NEIGHBOR_DATA Interaction
#else
#define NEIGHBOR_DATA Grain
#define READ_INTERACTION(j) readParticle(uint(j))
#endif
// Sand mass is the historical unit: rho_sand/rho_water = 2.65,
// rho_oil/rho_water = .8. Equal-volume carriers have unequal masses.
float inverseMass(int kind){return kind==1?1.0:(kind==2?4.0:(kind==3?.35:(kind==5?2.65:(kind==6?3.3125:1.0))));}
uint hash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
bool solid(ivec2 p){return p.x<0||p.y<0||p.x>=W||p.y>=H||wall[p.y*W+p.x]!=0u;}
int cell(vec2 p){ivec2 c=clamp(ivec2(floor(p)),ivec2(0),ivec2(W-1,H-1));return c.y*W+c.x;}
bool insideStroke(vec2 p){vec2 d=stroke.zw-stroke.xy;float t=clamp(dot(p-stroke.xy,d)/max(dot(d,d),.00001),0,1);return distance(p,stroke.xy+t*d)<=brushRadius;}
void move(inout Grain p,vec2 delta){
    precise vec2 increment=delta+p.residual.xy;
    precise vec2 sum=p.position.xy+increment;
    precise vec2 roundoff=increment-(sum-p.position.xy);
    p.residual.xy=roundoff;p.position.xy=sum;
}
vec2 separation(Grain a,Grain b){return (a.position.xy-b.position.xy)+(a.residual.xy-b.residual.xy);}
vec2 velocity(Grain p){return ((p.position.xy-p.position.zw)+(p.residual.xy-p.residual.zw))/dt;}
void clampWorld(inout Grain p,float r){
    vec2 bounded=clamp(p.position.xy,vec2(r),vec2(W,H)-r);
    if(bounded.x!=p.position.x)p.residual.x=0;if(bounded.y!=p.position.y)p.residual.y=0;p.position.xy=bounded;
}
void terrainBox(inout Grain p){
    // Circle/AABB constraints; out-of-domain cells are solid. Resolve on every
    // microstep and contact sweep, so a one-cell wall is not skipped.
    float r=radius(p);ivec2 c=ivec2(floor(p.position.xy));
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x){
        ivec2 b=c+ivec2(x,y);if(!solid(b))continue;
        vec2 low=vec2(b),high=low+1;
        vec2 d=(p.position.xy-clamp(p.position.xy,low,high))+p.residual.xy;
        float len=length(d);vec2 normal;float penetration;
        if(len>=r)continue;
        if(len>.00001){normal=d/len;penetration=r-len;}
        else {
            vec4 edges=vec4(p.position.xy-low,high-p.position.xy);
            float nearest=min(min(edges.x,edges.y),min(edges.z,edges.w));
            if(nearest==edges.x)normal=vec2(-1,0);else if(nearest==edges.y)normal=vec2(0,-1);
            else if(nearest==edges.z)normal=vec2(1,0);else normal=vec2(0,1);
            penetration=r+nearest;
        }
        move(p,normal*penetration);p.contact.xy+=normal;
    }
    clampWorld(p,r);
}
float terrainDistance(ivec2 c){
    if(c.x<0||c.y<0||c.x>=W||c.y>=H)return -.5;
    return uintBitsToFloat(wall[2*W*H+c.y*W+c.x]);
}
vec3 terrainField(vec2 p){
    vec2 q=p-.5;ivec2 c=ivec2(floor(q));vec2 t=fract(q),t2=t*t,t3=t2*t;
    // Tensor cubic B-spline. Linear fields (flat walls) are reproduced, but
    // the normal is continuous at grid lines instead of snagging on a knot.
    vec4 wx=vec4(pow(1-t.x,3),3*t3.x-6*t2.x+4,-3*t3.x+3*t2.x+3*t.x+1,t3.x)/6;
    vec4 wy=vec4(pow(1-t.y,3),3*t3.y-6*t2.y+4,-3*t3.y+3*t2.y+3*t.y+1,t3.y)/6;
    vec4 dx=vec4(-.5*(1-t.x)*(1-t.x),1.5*t2.x-2*t.x,-1.5*t2.x+t.x+.5,.5*t2.x);
    vec4 dy=vec4(-.5*(1-t.y)*(1-t.y),1.5*t2.y-2*t.y,-1.5*t2.y+t.y+.5,.5*t2.y);
    vec3 field=vec3(0);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        float d=terrainDistance(c+ivec2(x-1,y-1));field+=d*vec3(wx[x]*wy[y],dx[x]*wy[y],wx[x]*dy[y]);
    }
    return field;
}
void terrain(inout Grain p){
    // Keep the exact pixel channel clearance for liquids; smoothing is for
    // granular rolling contacts, where stair-shaped normals cause pinning.
    if(liquid(p)){terrainBox(p);return;}
    float r=radius(p);
    if(wall[W*H+cell(p.position.xy)]!=0u){
        // The signed field rounds subpixel stair corners while keeping
        // a one-cell wall negative at its centre. Newton projection uses the
        // derivative of that same field, never a random "unstick" direction.
        for(int i=0;i<3;++i){
            vec3 field=terrainField(p.position.xy);float depth=r-field.x;
            if(depth<=0)break;float norm2=dot(field.yz,field.yz);
            if(norm2<.00001){terrainBox(p);break;}
            vec2 correction=field.yz*(depth/norm2);float len=length(correction);
            move(p,correction*min(1,.4/max(len,.00001)));p.contact.xy+=normalize(field.yz);
        }
    }
    clampWorld(p,r);
}
#if SKIP_ZERO_PROJECTION
// A local certificate, never persisted across a dispatch or terrain edit.
// Only explicit nonpenetration plus an unchanged clamp certifies that another
// terrain call on the SAME position/residual is a no-op. Exhausting Newton's
// three iterations, the box fallback and all liquids remain uncertified.
bool terrainCertified(inout Grain p){
    if(liquid(p)){terrainBox(p);return false;}
    bool clear=false;float r=radius(p);
    if(wall[W*H+cell(p.position.xy)]!=0u){
        for(int i=0;i<3;++i){
            vec3 field=terrainField(p.position.xy);float depth=r-field.x;
            if(depth<=0){clear=true;break;}
            float norm2=dot(field.yz,field.yz);
            if(norm2<.00001){terrainBox(p);break;}
            vec2 correction=field.yz*(depth/norm2);float len=length(correction);
            move(p,correction*min(1,.4/max(len,.00001)));p.contact.xy+=normalize(field.yz);
        }
    }else clear=true;
    uvec4 before=uvec4(floatBitsToUint(p.position.xy),floatBitsToUint(p.residual.xy));
    clampWorld(p,r);
    return clear&&all(equal(before,uvec4(floatBitsToUint(p.position.xy),floatBitsToUint(p.residual.xy))));
}
#endif
// Exact grid traversal for kernel support (length <= 2). A wall blocks
// pressure and viscous interactions, as well as the actual particle path.
bool visible(vec2 a,vec2 b){
    // Second half of the terrain SSBO caches a radius-two wall neighbourhood.
    // Rebuilt only on terrain edits. Most fluid neighbours need no grid walk.
    if(wall[W*H+cell(a)]==0u)return true;
    ivec2 c=ivec2(floor(a)),goal=ivec2(floor(b));vec2 d=b-a;
    ivec2 direction=ivec2(sign(d));vec2 delta=1/max(abs(d),vec2(.0000001));
    vec2 boundary=vec2(c)+vec2(d.x>=0?1:0,d.y>=0?1:0);
    vec2 crossing=abs(boundary-a)*delta;
    if(direction.x==0)crossing.x=1e20;if(direction.y==0)crossing.y=1e20;
    for(int i=0;i<8;++i){
        if(solid(c))return false;if(all(equal(c,goal)))return true;
        if(abs(crossing.x-crossing.y)<.000001){
            if(solid(c+ivec2(direction.x,0))||solid(c+ivec2(0,direction.y)))return false;
            c+=direction;crossing+=delta;
        }else if(crossing.x<crossing.y){c.x+=direction.x;crossing.x+=delta.x;}
        else {c.y+=direction.y;crossing.y+=delta.y;}
    }
    return false;
}
