#version 430 core
layout(binding=0) uniform sampler2D sourceImage;
layout(binding=1) uniform sampler2D detailImage;
uniform vec4 renderExtent;uniform int mode;
layout(location=0) out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/renderExtent.xy,px=1./vec2(textureSize(sourceImage,0));vec3 c;
if(mode==0||mode==1){c=texture(sourceImage,uv).rgb*4.;c+=texture(sourceImage,uv+px*vec2(-1,-1)).rgb+texture(sourceImage,uv+px*vec2(1,-1)).rgb+texture(sourceImage,uv+px*vec2(-1,1)).rgb+texture(sourceImage,uv+px*vec2(1,1)).rgb;c*=.125;
if(mode==0){float peak=max(c.r,max(c.g,c.b));float knee=clamp(peak-.5,0.,1.);float gain=max(peak-1.,knee*knee*.25)/max(peak,1e-5);c*=gain;}}
else{c=(texture(sourceImage,uv+px*vec2(-1,0)).rgb+texture(sourceImage,uv+px*vec2(1,0)).rgb+texture(sourceImage,uv+px*vec2(0,-1)).rgb+texture(sourceImage,uv+px*vec2(0,1)).rgb)*.25;c+=texture(detailImage,uv).rgb;}
color=vec4(c,1);}
