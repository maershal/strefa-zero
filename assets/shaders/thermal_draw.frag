#version 430 core
#include "thermal_common.glsl"
#include "scene_common.glsl"
uniform int linearOutput;
layout(std430,binding=4) readonly buffer Terrain {uint walls[];};
layout(std430,binding=5) readonly buffer ThermalCells {ThermalCell cells[];};
uniform int viewMode;
uniform vec4 renderExtent;
layout(location=0) out vec4 color;
vec3 heatColor(float t){float x=clamp(log(max(1.,t)/200.)/log(12.),0,1);return clamp(vec3(2*x,2.-abs(x-.5)*4,2.-x*3),0,1);}
void main(){ivec2 c=clamp(ivec2(gl_FragCoord.xy/renderExtent.xy*vec2(TW,TH)),ivec2(0),ivec2(TW-1,TH-1));int i=indexOf(c);ThermalCell a=cells[i];float t=a.matter.x>0?matterTemperature(a.matter):gasTemperature(a);if(a.extra.z>0)t=a.extra.z;vec3 rgb=vec3(0);float alpha=0;
 if(viewMode==1){rgb=heatColor(t);alpha=.9;}
 else if(viewMode==2){float x=clamp(a.flow.z*.02,-1,1);rgb=x>0?vec3(x,.1,.1):vec3(.1,.1,-x);alpha=.85;}
 else if(viewMode==3){rgb=vec3(clamp(vec2(.5)+a.flow.xy*.025,0,1),.2);alpha=.85;}
 else if(viewMode==4){rgb=vec3(.03,clamp(a.gas0.y/.005,0,1),.1);alpha=.9;}
 else if(viewMode==5){rgb=vec3(clamp((a.gas1.x+a.matter.z)*8.,0,1),.15,.4);alpha=.9;}
 else{
  if(a.matter.x>0){int m=int(a.matter.w);if(m==5){rgb=vec3(.43,.24,.11);alpha=.85;}else if(m==19){rgb=vec3(.35);alpha=.6;}else if(waterFamily(m)){rgb=vec3(.65,.85,.95);alpha=.4;}else if(m==10){rgb=vec3(1,.22,.02);alpha=.8;}}
  if(a.extra.z>0){int m=int(a.extra.w);if((m==17&&a.extra.z<=273.2)||(m==10&&a.extra.z>=1400.)||(m==18&&a.extra.z>=1811.)){rgb=m==17?vec3(.65,.85,.95):heatColor(a.extra.z);alpha=max(alpha,.5);}}
  if(walls[i]==0u){float opacity=1.-exp(-(2000.*a.gas0.w+80.*a.gas0.z+80.*a.gas1.y));vec3 gasRgb=a.gas0.z>a.gas0.w?vec3(.7,.82,.86):vec3(.23,.25,.27);float reactionGlow=a.extra.x*8.;for(int distance=1;distance<=2;++distance){ivec2 below=c+ivec2(0,distance);if(!validCell(below)||walls[indexOf(below)]!=0u)break;reactionGlow+=cells[indexOf(below)].extra.x*(distance==1?3.2:1.28);}float hotGas=clamp((gasTemperature(a)-700.)/650.,0,1)*clamp(a.gas1.y/max(gasMass(a),1e-9)*6.,0,1);float fire=max(clamp(reactionGlow,0,1),hotGas*.25);gasRgb=mix(gasRgb,vec3(1,.32,.035),fire);float gasAlpha=max(opacity,fire*.8);rgb=mix(rgb,gasRgb,gasAlpha);alpha=max(alpha,gasAlpha);}
 }
 color=vec4(linearOutput!=0?toLinear(rgb)*(viewMode==0?max(1.,clamp(a.extra.x*16.,0.,6.)):1.):rgb,alpha);
}
