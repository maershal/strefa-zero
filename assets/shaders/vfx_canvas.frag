#version 430 core
layout(binding=4) uniform sampler2D sourceImage;
uniform vec4 renderExtent;
out vec4 color;
void main(){color=texture(sourceImage,gl_FragCoord.xy/renderExtent.xy);}
