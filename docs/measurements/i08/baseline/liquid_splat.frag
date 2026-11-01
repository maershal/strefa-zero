#version 430 core
in vec2 local;flat in int kind;layout(location=0) out vec2 density;
void main(){float q=max(0,1-dot(local,local));density=kind==5?vec2(q*q,0):vec2(0,q*q);}
