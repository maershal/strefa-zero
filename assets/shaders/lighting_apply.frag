#version 430 core
layout(binding=0) uniform sampler2D sceneImage;
layout(binding=1) uniform sampler2D lightImage;
uniform vec4 renderExtent;
layout(location=0) out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/renderExtent.xy;color=vec4(texelFetch(sceneImage,ivec2(gl_FragCoord.xy),0).rgb*texture(lightImage,uv).rgb,1);}
