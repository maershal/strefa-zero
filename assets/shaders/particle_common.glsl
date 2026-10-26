struct Grain {vec4 position;vec4 motion;vec4 contact;vec4 residual;};
layout(std430,binding=0) readonly buffer Input {Grain src[];};
layout(std430,binding=1) buffer Output {Grain dst[];};
layout(std430,binding=2) buffer Heads {int head[];};
layout(std430,binding=3) buffer Links {int link[];};
layout(std430,binding=4) buffer Terrain {uint wall[];};
layout(std430,binding=5) buffer Free {uint slots[];};
#include "mission_state.glsl"
layout(std430,binding=7) buffer FluidTerms {vec4 terms[];};
uniform int dirtyOnly;
uniform int operation;
uniform int substep;
uniform vec4 stroke; // start xy, end xy
uniform float brushRadius;
uniform int material;
const int W=320,H=192,N=32768;
const float dt=1.0/240.0,R=.46;
bool liquid(Grain p){return p.motion.w>=5;}
float radius(Grain p){return liquid(p)?.32:R;}
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
