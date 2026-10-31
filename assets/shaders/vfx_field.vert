#version 430 core
#define PARTICLE_RENDER 1
#include "particle_storage.glsl"
layout(std430,binding=5) readonly buffer Active {uint slots[];};
out vec2 local;flat out int kind;flat out vec2 velocity;flat out vec2 center;
void main(){Grain p=readParticle(slots[32768+gl_VertexID]);kind=int(p.motion.w);velocity=p.motion.xy;center=p.position.xy;local=vec2(0);
    if(kind<5){gl_Position=vec4(2,2,0,1);return;}
    // At 320x192, a three-pixel point covers every sample of the 1.35-cell kernel.
    gl_PointSize=3;gl_Position=vec4(center/vec2(320,192)*2-1,0,1);
}
