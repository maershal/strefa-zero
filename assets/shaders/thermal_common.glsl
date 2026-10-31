// Property tables are generated from sim/material.hpp by ThermalSystem.
struct ThermalParticle {vec4 matter;vec4 request;vec4 transfer;vec4 history;};
struct ThermalCell {vec4 matter;vec4 gas0;vec4 gas1;vec4 flow;vec4 demand;vec4 budget;vec4 extra;vec4 factors;};
const int TW=320,TH=192,TC=61440,TN=32768;
const float AMBIENT=293.15;
float enthalpy(int m,float t){return thermalProperties[m].y*t+thermalProperties[m].w;}
bool waterFamily(int m){return m==3||m==8||m==17;}
vec3 phase(int m,float h){
    if(waterFamily(m)){
        float a=enthalpy(17,273.15),b=a+334.,c=enthalpy(3,373.15);
        if(h<a)return vec3(h/thermalProperties[17].y,0,0);
        if(h<b)return vec3(273.15,(h-a)/334.,0);
        if(h<c)return vec3((h-thermalProperties[3].w)/thermalProperties[3].y,1,0);
        if(h<c+2256.)return vec3(373.15,1,(h-c)/2256.);
        return vec3((h-thermalProperties[8].w)/thermalProperties[8].y,1,1);
    }
    if(m==1||m==10||m==16||m==18){bool metal=m==16||m==18;int a=metal?16:1,b=metal?18:10;float t=metal?1811.:1400.,latent=metal?270.:400.,hs=enthalpy(a,t);
        if(h<hs)return vec3(h/thermalProperties[a].y,0,0);
        if(h<hs+latent)return vec3(t,(h-hs)/latent,0);
        return vec3((h-thermalProperties[b].w)/thermalProperties[b].y,1,0);
    }
    return vec3((h-thermalProperties[m].w)/thermalProperties[m].y,0,0);
}
float matterTemperature(vec4 m){return m.x>1e-12?max(1.,phase(int(m.w),m.y/m.x).x):AMBIENT;}
float gasCapacity(ThermalCell c){return dot(c.gas0,vec4(1,1,2,1))+dot(c.gas1.xy,vec2(1.5,1));}
float gasOffset(ThermalCell c){return c.gas0.z*thermalProperties[8].w;}
float gasTemperature(ThermalCell c){return gasCapacity(c)>1e-12?max(1.,(c.gas1.z-gasOffset(c))/gasCapacity(c)):AMBIENT;}
float gasMass(ThermalCell c){return dot(c.gas0,vec4(1))+c.gas1.x+c.gas1.y;}
float chemical(vec4 m){int k=int(m.w);return m.z*(1.-combustionProperties[k].w)*combustionProperties[k].z;}
float minimumH(vec4 m){return m.x*enthalpy(waterFamily(int(m.w))?17:(int(m.w)==10?1:(int(m.w)==18?16:int(m.w))),1.);}
bool validCell(ivec2 c){return all(greaterThanEqual(c,ivec2(0)))&&all(lessThan(c,ivec2(TW,TH)));}
int indexOf(ivec2 c){return c.y*TW+c.x;}
ivec2 coordinate(int i){return ivec2(i%TW,i/TW);}
const ivec2 directions[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
