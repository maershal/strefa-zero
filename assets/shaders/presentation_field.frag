#version 430 core
#include "render_terrain.glsl"
flat in float liquidFraction;flat in int kind;flat in vec2 center;flat in float speed;
layout(location=0) out vec4 density;
void main(){vec2 p=gl_FragCoord.xy;float q=max(0.,1.-dot((p-center)/1.35,(p-center)/1.35));if(q<=0||!clearTerrain(center,p))discard;
float w=q*q;density=vec4(kind==5?w*liquidFraction:0.,kind==6?w:0.,kind==1?w:0.,kind==1?w*min(speed,80.)/80.:0.);}
