// Stable ID addresses are shared by physics, edits and rendering. Only the
// internal layout changes; Grain is the host/validation transfer format.
const int PARTICLE_CAPACITY=32768;
struct Grain {vec4 position;vec4 motion;vec4 contact;vec4 residual;};
#if PARTICLE_SOA
layout(std430,binding=0) readonly buffer Input {
    vec4 positions[PARTICLE_CAPACITY];vec4 motions[PARTICLE_CAPACITY];
    vec4 contacts[PARTICLE_CAPACITY];vec4 residuals[PARTICLE_CAPACITY];
};
#ifndef PARTICLE_RENDER
layout(std430,binding=1) buffer Output {
    vec4 outPositions[PARTICLE_CAPACITY];vec4 outMotions[PARTICLE_CAPACITY];
    vec4 outContacts[PARTICLE_CAPACITY];vec4 outResiduals[PARTICLE_CAPACITY];
};
#endif
Grain readParticle(uint id){return Grain(positions[id],motions[id],contacts[id],residuals[id]);}
#else
layout(std430,binding=0) readonly buffer Input {Grain src[];};
#ifndef PARTICLE_RENDER
layout(std430,binding=1) buffer Output {Grain dst[];};
#endif
Grain readParticle(uint id){return src[id];}
#endif
#ifndef PARTICLE_RENDER
void writeParticle(uint id,Grain p){
#if PARTICLE_SOA
    outPositions[id]=p.position;outMotions[id]=p.motion;outContacts[id]=p.contact;outResiduals[id]=p.residual;
#else
    dst[id]=p;
#endif
}
#endif
