#version 430 core
#define PARTICLE_RENDER 1
#include "particle_storage.glsl"
layout(std430,binding=5) readonly buffer Active {uint slots[];};
layout(std430,binding=2) readonly buffer ThermalPresentationParticles {vec4 thermalParticleData[];};uniform int heatEnabled;flat out float liquidFraction;flat out int kind;flat out vec2 center;flat out float speed;
void main(){uint id=slots[32768+gl_VertexID];Grain p=readParticle(id);liquidFraction=heatEnabled!=0?clamp(thermalParticleData[id*4u+3u].w,0.,1.):1.;kind=int(p.motion.w);center=p.position.xy;speed=length(p.motion.xy);gl_PointSize=3;
gl_Position=kind>0?vec4(center/vec2(320,192)*2-1,0,1):vec4(2,2,0,1);}
