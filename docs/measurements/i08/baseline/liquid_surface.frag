#version 430 core
layout(binding=0) uniform sampler2D field;
out vec4 color;
void main(){
    ivec2 c=ivec2(gl_FragCoord.xy);vec2 d=texelFetch(field,c,0).rg;
    if(d.x+d.y<.35)discard;
    bool oil=d.y>d.x;vec3 base=oil?vec3(.60,.39,.12):vec3(.09,.40,.64);
    float above=dot(texelFetch(field,max(c-ivec2(0,2),ivec2(0)),0).rg,vec2(1));
    float edge=1-smoothstep(.2,.8,above);
    color=vec4(mix(base,oil?vec3(.93,.75,.34):vec3(.45,.85,.95),edge*.7),1);
}
