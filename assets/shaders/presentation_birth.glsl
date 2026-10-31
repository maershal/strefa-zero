// An image binding does not consume an SSBO binding. Every allocation resets its slot.
layout(r32f,binding=7) uniform image2D presentationMoisture;
void presentationBirth(uint id){imageStore(presentationMoisture,ivec2(id%256u,id/256u),vec4(0));}
