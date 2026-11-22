// Property tables are generated from sim/material.hpp by ThermalSystem; material IDs and phase transitions
// come from the registry (materials.glsl).
#include "materials.glsl"
struct ThermalParticle {vec4 matter;vec4 request;vec4 transfer;vec4 history;};
struct ThermalCell {vec4 matter;vec4 gas0;vec4 gas1;vec4 flow;vec4 demand;vec4 budget;vec4 extra;vec4 factors;};
const int TW=320,TH=192,TC=61440,TN=32768;
const float AMBIENT=293.15;
float enthalpy(int m,float t){return thermalProperties[m].y*t+thermalProperties[m].w;}
bool waterFamily(int m){return m==MATERIAL_WATER||m==MATERIAL_STEAM||m==MATERIAL_ICE;}
vec3 phase(int m,float h){
    if(waterFamily(m)){
        float a=enthalpy(MATERIAL_ICE,WATER_TRANSITION_TEMPERATURE),b=a+WATER_TRANSITION_LATENT;
        float c=enthalpy(MATERIAL_WATER,STEAM_TRANSITION_TEMPERATURE);
        if(h<a)return vec3(h/thermalProperties[MATERIAL_ICE].y,0,0);
        if(h<b)return vec3(WATER_TRANSITION_TEMPERATURE,(h-a)/WATER_TRANSITION_LATENT,0);
        if(h<c)return vec3((h-thermalProperties[MATERIAL_WATER].w)/thermalProperties[MATERIAL_WATER].y,1,0);
        if(h<c+STEAM_TRANSITION_LATENT)return vec3(STEAM_TRANSITION_TEMPERATURE,1,(h-c)/STEAM_TRANSITION_LATENT);
        return vec3((h-thermalProperties[MATERIAL_STEAM].w)/thermalProperties[MATERIAL_STEAM].y,1,1);
    }
    if(m==MATERIAL_STONE||m==MATERIAL_LAVA||m==MATERIAL_METAL||m==MATERIAL_MOLTEN_METAL){
        bool metal=m==MATERIAL_METAL||m==MATERIAL_MOLTEN_METAL;
        int a=metal?MATERIAL_METAL:MATERIAL_STONE,b=metal?MATERIAL_MOLTEN_METAL:MATERIAL_LAVA;
        float t=metal?MOLTEN_METAL_TRANSITION_TEMPERATURE:LAVA_TRANSITION_TEMPERATURE;
        float latent=metal?MOLTEN_METAL_TRANSITION_LATENT:LAVA_TRANSITION_LATENT,hs=enthalpy(a,t);
        if(h<hs)return vec3(h/thermalProperties[a].y,0,0);
        if(h<hs+latent)return vec3(t,(h-hs)/latent,0);
        return vec3((h-thermalProperties[b].w)/thermalProperties[b].y,1,0);
    }
    return vec3((h-thermalProperties[m].w)/thermalProperties[m].y,0,0);
}
// Two-stage wood combustion (registry [[pyrolysis]], thermal.comp): a wood record is part raw wood (m.z) and part
// char (m.x-m.z) until all of it has pyrolysed and the record becomes char.
float charMass(vec4 m){return int(m.w)==MATERIAL_WOOD?max(0.,m.x-m.z):0.;}
// Heat capacity of a matter record (kJ/K).
float matterCapacity(vec4 m){
    return (m.x-charMass(m))*thermalProperties[int(m.w)].y+charMass(m)*thermalProperties[WOOD_PYROLYSIS_CHAR].y;
}
float matterTemperature(vec4 m){
    if(m.x<=1e-12)return AMBIENT;
    return max(1.,int(m.w)==MATERIAL_WOOD?m.y/matterCapacity(m):phase(int(m.w),m.y/m.x).x);
}
float gasCapacity(ThermalCell c){return dot(c.gas0,vec4(1,1,2,1))+dot(c.gas1.xy,vec2(1.5,1));}
float gasOffset(ThermalCell c){return c.gas0.z*thermalProperties[MATERIAL_STEAM].w;}
float gasTemperature(ThermalCell c){return gasCapacity(c)>1e-12?max(1.,(c.gas1.z-gasOffset(c))/gasCapacity(c)):AMBIENT;}
float gasMass(ThermalCell c){return dot(c.gas0,vec4(1))+c.gas1.x+c.gas1.y;}
// Chemical energy that burning releases per unit mass of fuel k.
float fuelEnergy(int k){return (1.-combustionProperties[k].w)*combustionProperties[k].z;}
float chemical(vec4 m){
    int k=int(m.w);
    return m.z*(1.-combustionProperties[k].w)*combustionProperties[k].z+charMass(m)*fuelEnergy(WOOD_PYROLYSIS_CHAR);
}
// Enthalpy at 1 K of the coldest phase of the material: ice, stone for lava, metal for molten metal.
float minimumH(vec4 m){
    if(int(m.w)==MATERIAL_WOOD)return matterCapacity(m);
    return m.x*enthalpy(waterFamily(int(m.w))?MATERIAL_ICE
        :(int(m.w)==MATERIAL_LAVA?MATERIAL_STONE:(int(m.w)==MATERIAL_MOLTEN_METAL?MATERIAL_METAL:int(m.w))),1.);
}
bool validCell(ivec2 c){return all(greaterThanEqual(c,ivec2(0)))&&all(lessThan(c,ivec2(TW,TH)));}
int indexOf(ivec2 c){return c.y*TW+c.x;}
ivec2 coordinate(int i){return ivec2(i%TW,i/TW);}
const ivec2 directions[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
// Bitwise equality (distinguishes -0 from 0 and compares NaN payloads), for the exact gas-stage gate.
bool sameBits(vec4 a,vec4 b){return all(equal(floatBitsToUint(a),floatBitsToUint(b)));}
bool sameCell(ThermalCell a,ThermalCell b){
    return sameBits(a.matter,b.matter)&&sameBits(a.gas0,b.gas0)&&sameBits(a.gas1,b.gas1)&&sameBits(a.flow,b.flow)
        &&sameBits(a.demand,b.demand)&&sameBits(a.budget,b.budget)&&sameBits(a.extra,b.extra)
        &&sameBits(a.factors,b.factors);
}
