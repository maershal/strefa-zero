#version 430 core
#include "scene_common.glsl"
#include "render_terrain.glsl"
layout(std430,binding=4) readonly buffer Terrain {uint wall[];};
uniform vec4 renderExtent;
layout(location=0) out vec4 color;layout(location=1) out uint receiver;
uint hash(uint x){x^=x>>16;x*=0x7feb352du;return x^(x>>15);}
void main(){ivec2 c=ivec2(gl_FragCoord.xy/renderExtent.xy*vec2(320,192));uint material=texelFetch(terrainMap,c,0).r;if(material==0u)discard;
    uint id=uint(c.y*320+c.x);float shade=.88+float(hash(id)%100u)*.0018;
    float hp=material>=2u?float(wall[3*320*192+id])/3.:1.;
    vec3 base=(material>=2u?vec3(.65,.42,.22):vec3(.30,.36,.39))*shade;
    if(material>=2u&&hp<1.&&((c.x+2*c.y)%5==0))base*=mix(.5,1.,hp);
    color=vec4(toLinear(base),1);receiver=2u;
}
