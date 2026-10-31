#version 430 core
#include "scene_common.glsl"
uniform sampler2D partsTexture;uniform sampler2D weaponsTexture;
uniform int referencePass;uniform int overlay;
in vec2 uv;in vec4 tint;flat in int atlas;layout(location=0) out vec4 color;layout(location=1) out uint receiver;
void main(){if(atlas==3&&(length(uv)>.98||length(uv)<.87))discard;
    if(atlas==5&&max(abs(uv.x*2.-1.),abs(uv.y*2.-1.))<.86)discard;
    if(atlas==4){vec2 a=abs(uv*2.-1.);float r=length(uv*2.-1.);if(!(r>.63&&r<.85)&&!(min(a.x,a.y)<.12&&max(a.x,a.y)>.28))discard;}
    color=tint*(atlas==0?texture(partsTexture,uv):(atlas==1?texture(weaponsTexture,uv):vec4(1)));
    if(color.a<.01)discard;if(referencePass==0&&overlay==0)color.rgb=toLinear(color.rgb);receiver=3u;
}
