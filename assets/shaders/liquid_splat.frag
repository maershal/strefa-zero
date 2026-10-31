#version 430 core
#include "render_terrain.glsl"
in vec2 local;flat in int kind;flat in vec2 velocity;flat in vec2 center;
uniform vec4 renderExtent;
layout(location=0) out vec4 density;
void main(){
#ifdef VFX_FIELD
    vec2 kernelLocal=(gl_FragCoord.xy/renderExtent.xy*vec2(320,192)-center)/1.35;
#else
    vec2 kernelLocal=local;
#endif
    float q=max(0,1-dot(kernelLocal,kernelLocal));if(q<=0||!clearTerrain(center,gl_FragCoord.xy/renderExtent.xy*vec2(320,192)))discard;
    float w=q*q;density=vec4(kind==5?w:0,kind==6?w:0,w*velocity);
}
