#version 430 core
layout(binding=4) uniform sampler2D sourceImage;
layout(binding=1) uniform sampler2D backgroundImage;
uniform int copyMode;uniform vec4 renderExtent;
out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/renderExtent.xy;vec4 c=texture(sourceImage,uv);if(copyMode!=0){vec3 bg=texture(backgroundImage,uv).rgb*.36;color=vec4(mix(bg,c.rgb,c.a),1);}else color=c;}
