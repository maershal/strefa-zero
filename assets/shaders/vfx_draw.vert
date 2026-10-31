#version 430 core
#include "vfx_common.glsl"
uniform int layer;uniform float alpha;
out vec2 local;flat out vec2 center;flat out vec4 tint;flat out uint kind;flat out float age;
void main(){VfxParticle p=particles[drawSlots[uint(layer)*VFX_CAPACITY+uint(gl_InstanceID)]];kind=p.appearance.x;
    int expected=(kind==7u||kind==9u||kind==10u)?0:kind==11u?2:1;
    if(expected!=layer){gl_Position=vec4(2,2,0,1);return;}
    vec2 corners[4]=vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));local=corners[gl_VertexID];center=mix(p.history.xy,p.motion.xy,alpha);age=clamp(p.history.z/max(p.history.w,.001),0,1);
    float size=uintBitsToFloat(p.appearance.z);vec2 extent=vec2(size);vec2 direction=length(p.motion.zw)>.01?normalize(p.motion.zw):vec2(1,0);
    if(kind==8u||kind==4u||(kind==9u&&(p.appearance.y>>24u)<200u)){extent.x+=min(3,length(p.motion.zw)*.02);extent.y*=.5;}
    if(kind==1u)extent*=1+age*2;if(kind==11u)extent*=.1+age;
    vec2 q=local*extent;if(kind==8u||kind==4u||(kind==9u&&(p.appearance.y>>24u)<200u))q=direction*q.x+vec2(-direction.y,direction.x)*q.y;
    tint=unpackUnorm4x8(p.appearance.y);tint.a*=1-age;gl_Position=vec4((center+q)/vec2(320,192)*2-1,0,1);
}
