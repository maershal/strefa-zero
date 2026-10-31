#version 430 core
#include "vfx_common.glsl"
#include "scene_common.glsl"
uniform vec4 renderExtent;uniform int linearOutput;
layout(binding=4) uniform sampler2D sourceImage;
in vec2 local;flat in vec2 center;flat in vec4 tint;flat in uint kind;flat in float age;
layout(location=0) out vec4 color;layout(location=1) out uint receiver;
void main(){float r=length(local);if(r>1)discard;vec2 p=gl_FragCoord.xy/renderExtent.xy*vec2(320,192);bool solid=vfxSolid(ivec2(floor(p)));
    if(kind==10u){if(!solid)discard;}else if(solid)discard;
    float a=tint.a*clamp((1-r)*4,0,1);vec3 rgb=tint.rgb;receiver=0u;
    if(kind==7u){float d=fluidAt(p).x;if(d<.15)discard;a*=smoothstep(.55,.85,r)*.7;}
    if(kind==6u&&fluidAt(p).x<.1)discard;
    if(kind==11u){float ring=exp(-pow((r-.86)*18,2));if(ring<.02||!vfxClear(center,p))discard;vec2 offset=normalize(p-center+vec2(.001))*ring*(1-age)*.7;
        if(!vfxClear(p,p+offset))offset=vec2(0);rgb=texture(sourceImage,(p+offset)/vec2(320,192)).rgb;a=ring*(1-age)*.7;}
    color=vec4(linearOutput!=0&&kind!=11u?toLinear(rgb)*((kind==1u||kind==3u||kind==4u||kind==8u)?3.:1.):rgb,a);
}
