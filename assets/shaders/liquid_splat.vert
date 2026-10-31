#version 430 core
#define PARTICLE_RENDER 1
#include "particle_storage.glsl"
layout(std430,binding=5) readonly buffer Active {uint slots[];};
uniform float alpha;
out vec2 local;flat out int kind;flat out vec2 velocity;flat out vec2 center;
void main(){vec2 corners[4]=vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));
    Grain g=readParticle(slots[32768+gl_InstanceID]);kind=int(g.motion.w);local=corners[gl_VertexID];velocity=g.motion.xy;
    if(kind<5){gl_Position=vec4(2,2,0,1);return;}
    center=mix(g.contact.zw,g.position.xy,alpha);vec2 pos=center+local*1.35;
    gl_Position=vec4(pos/vec2(320,192)*2-1,0,1);
}
