#version 430 core
#include "scene_common.glsl"
layout(binding=0) uniform sampler2D backgroundMap;
uniform int hasBackground;uniform vec4 renderExtent;
layout(location=0) out vec4 color;layout(location=1) out uint receiver;
void main(){vec2 uv=gl_FragCoord.xy/renderExtent.xy;
    vec3 base=hasBackground!=0?mix(texture(backgroundMap,uv).rgb,vec3(7,14,17)/255.,165./255.):vec3(.035,.055,.065);
    color=vec4(toLinear(base),1);receiver=0u;
}
