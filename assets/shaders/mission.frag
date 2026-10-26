#version 430 core
uniform sampler2D partsTexture;
uniform sampler2D weaponsTexture;
in vec2 uv;in vec4 tint;flat in int atlas;out vec4 color;
void main(){
    if(atlas==3&&(length(uv)>.98||length(uv)<.87))discard;
    color=tint*(atlas==0?texture(partsTexture,uv):(atlas==1?texture(weaponsTexture,uv):vec4(1)));
    if(color.a<.01)discard;
}
