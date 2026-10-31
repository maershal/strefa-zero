#version 430 core
#include "scene_common.glsl"
in vec3 color;layout(location=0) out vec4 result;layout(location=1) out uint receiver;
uniform int referencePass;
void main(){result=vec4(referencePass!=0?color:toLinear(color),1);receiver=1u;}
