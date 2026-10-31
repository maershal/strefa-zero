#version 430 core
#define PARTICLE_RENDER 1
#include "particle_storage.glsl"
layout(std430,binding=4) readonly buffer Terrain {uint wall[];};
layout(std430,binding=5) readonly buffer Active {uint slots[];};
uniform float alpha;
uniform int terrainPass;
out vec3 color;uniform int sandStyle;layout(binding=6) uniform sampler2D moistureMap;layout(binding=7) uniform sampler2D sandField;
uint hash(uint x){x^=x>>16;x*=0x7feb352du;return x^(x>>15);}
void main(){
    vec2 corners[4]=vec2[](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1));
    vec2 pos;uint id=uint(gl_InstanceID);float shade=.88+float(hash(id)%100u)*.0018;
    if(terrainPass!=0){
        if(wall[id]==0u){gl_Position=vec4(2,2,0,1);return;}
        pos=vec2(id%320u,id/320u)+corners[gl_VertexID];color=(wall[id]>=2u?vec3(.65,.42,.22):vec3(.30,.36,.39))*shade;
    }else{
        id=slots[32768u+id];Grain g=readParticle(id);if(g.motion.w<.5||g.motion.w>=5){gl_Position=vec4(2,2,0,1);return;}
        pos=mix(g.contact.zw,g.position.xy,alpha)+(corners[gl_VertexID]-.5)*.92;
        color=(g.motion.w==1?vec3(.85,.70,.37):(g.motion.w==2?vec3(.56,.57,.55):(g.motion.w==4?vec3(.65,.42,.22):vec3(.46,.63,.72))))*shade;
    }
    if(sandStyle!=0&&terrainPass==0){Grain p=readParticle(id);if(p.motion.w==1.){const vec3 shades[4]=vec3[](vec3(.79,.64,.34),vec3(.85,.70,.37),vec3(.89,.74,.41),vec3(.82,.67,.35));float wet=texelFetch(moistureMap,ivec2(id%256u,id/256u),0).r;float density=texelFetch(sandField,clamp(ivec2(p.position.xy),ivec2(0),ivec2(319,191)),0).b;color=shades[hash(id)%4u]*mix(1.,.65,wet)*(1.-.18*clamp(density-.6,0.,1.));}}
    // FBO UV origin matches the UI texture orientation (top is v=0).
    gl_Position=vec4(pos/vec2(320,192)*2-1,0,1);
}
