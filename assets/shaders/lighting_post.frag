#version 430 core
#include "scene_common.glsl"
layout(binding=0) uniform sampler2D hdrImage;
layout(binding=1) uniform sampler2D bloomImage;
layout(binding=2) uniform sampler2D paletteLut;
uniform vec4 renderExtent,effects,camera; // bloom,vignette,aberration,exposure; offset.xy
layout(location=0) out vec4 color;
vec3 acesFit(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);}
// A small toe offset keeps the existing dark pixel-art palette readable.
vec3 film(vec3 x){vec3 toe=acesFit(vec3(.03));return clamp((acesFit(x+.03)-toe)/(1.-toe),0.,1.);}
vec3 grade(vec3 c){vec3 p=clamp(c,0.,1.)*15.;float z=floor(p.b);vec2 uv0=(vec2(mod(z,4.)*16.+p.r,floor(z/4.)*16.+p.g)+.5)/64.;float next=min(z+1.,15.);vec2 uv1=(vec2(mod(next,4.)*16.+p.r,floor(next/4.)*16.+p.g)+.5)/64.;return mix(texture(paletteLut,uv0).rgb,texture(paletteLut,uv1).rgb,fract(p.b));}
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec2 uv=(gl_FragCoord.xy+camera.xy)/renderExtent.xy;vec2 shift=(uv-.5)*effects.z*.003;
vec3 base=texture(hdrImage,uv).rgb;if(effects.z>0.){base.r=texture(hdrImage,uv+shift).r;base.b=texture(hdrImage,uv-shift).b;}
base+=texture(bloomImage,uv).rgb*effects.x;float v=1.-effects.y*clamp(dot((uv-.5)*1.4,(uv-.5)*1.4),0.,1.);vec3 display=toDisplay(grade(film(base*effects.w*v)));
const int bayer[16]=int[](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);float n=(float(bayer[(pixel.y%4)*4+pixel.x%4])+.5)/16.-.5;
color=vec4(clamp(floor(display*63.+n+.5)/63.,0.,1.),1);}
