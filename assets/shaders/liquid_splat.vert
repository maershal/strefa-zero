#version 430 core
struct Grain {vec4 position;vec4 motion;vec4 contact;vec4 residual;};
layout(std430,binding=0) readonly buffer Particles {Grain p[];};
layout(std430,binding=5) readonly buffer Active {uint slots[];};
uniform float alpha;
out vec2 local;flat out int kind;
void main(){
    vec2 corners[4]=vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));
    Grain g=p[slots[32768+gl_InstanceID]];kind=int(g.motion.w);local=corners[gl_VertexID];
    if(kind<5){gl_Position=vec4(2,2,0,1);return;}
    vec2 pos=mix(g.contact.zw,g.position.xy,alpha)+local*1.35;
    gl_Position=vec4(pos/vec2(320,192)*2-1,0,1);
}
