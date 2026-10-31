#version 430 core
#include "scene_common.glsl"
#include "render_terrain.glsl"
layout(binding=0) uniform sampler2D sceneColor;
layout(binding=1) uniform sampler2D field;
layout(binding=2) uniform sampler2D rawField;
layout(binding=4) uniform sampler2D thicknessMap;
layout(binding=5) uniform usampler2D receiverMap;layout(binding=6) uniform sampler2D lightMap;
uniform int hdrOutput;uniform int quality;uniform int hasFluid;uniform int stage;
layout(location=0) out vec4 color;
vec2 densityAt(ivec2 p){return texelFetch(field,clamp(p,ivec2(0),textureSize(field,0)-1),0).rg;}
float totalAt(ivec2 p){return dot(densityAt(p),vec2(1));}
vec3 normalAt(ivec2 p){vec2 g=vec2(totalAt(p+ivec2(1,0))-totalAt(p-ivec2(1,0)),totalAt(p+ivec2(0,1))-totalAt(p-ivec2(0,1)));return normalize(vec3(-g*2.2,1));}
#include "liquid_sampling.glsl"
void main(){ivec2 c=ivec2(gl_FragCoord.xy),size=textureSize(sceneColor,0);vec3 base=texelFetch(sceneColor,c,0).rgb;
    vec2 world=(vec2(c)+.5)/vec2(size)*vec2(320,192);vec2 raw=texelFetch(rawField,c,0).rg;float rd=raw.x+raw.y;
    if(hasFluid!=0&&stage!=1&&!solidAt(ivec2(world))&&rd>.10){
        vec4 f=texelFetch(field,c,0);float d=f.x+f.y,oil=f.y/max(d,1e-5);vec2 velocity=f.zw/max(d,1e-5);
        float coverage=smoothstep(.10,.35,rd);vec3 n=normalAt(c);
        float depth=quality==0?0.:texelFetch(thicknessMap,c,0).r;
        float optical=.65*clamp(d-.15,0.,2.5)+.075*depth;
        vec2 offset=clamp(-n.xy*min(optical,2.5)*.65,vec2(-1.5),vec2(1.5));
        ivec2 candidate=clamp(ivec2(round(vec2(c)+offset*vec2(size)/vec2(320,192))),ivec2(0),size-1);
        if(clearTerrain(world,(vec2(candidate)+.5)/vec2(size)*vec2(320,192))&&clearLiquid(c,candidate,raw.y>raw.x?1:0))base=texelFetch(sceneColor,candidate,0).rgb;
        vec3 sigma=mix(vec3(.42,.11,.045),vec3(.08,.19,.58),oil),transmission=exp(-sigma*optical);
        vec3 scatter=toLinear(mix(vec3(.09,.40,.64),vec3(.60,.39,.12),oil));
        vec3 illumination=hdrOutput!=0?texture(lightMap,world/vec2(320,192)).rgb:vec3(1);scatter*=illumination;vec3 body=base*transmission+scatter*(1.-transmission);
        if(quality>=1){float fresnel=.02+.98*pow(1.-n.z,5.);float spec=pow(max(dot(n,normalize(vec3(-.35,-.6,1.))),0.),32.);
            body=mix(body,toLinear(mix(vec3(.45,.85,.95),vec3(.93,.75,.34),oil)),min(.45,fresnel+.06*spec));
            body+=toLinear(vec3(.22,.36,.38))*(1.-n.z)*.08;
        }
        if(quality>=2&&stage==3){float above=totalAt(c-ivec2(0,2));float curvature=length(normalAt(c+ivec2(1,0)).xy-normalAt(c-ivec2(1,0)).xy);
            float foam=(1.-smoothstep(.15,.65,above))*smoothstep(.12,.55,curvature)*(1.-oil)*(.10+.12*min(length(velocity)/30.,1.));
            body=mix(body,toLinear(vec3(.65,.86,.86)),foam);
            uint receiver=texelFetch(receiverMap,candidate,0).r;
            bool floorNear=solidAt(ivec2(world)+ivec2(0,1));
            float convexity=max(0.,normalAt(c+ivec2(1,0)).x-normalAt(c-ivec2(1,0)).x);
            if(receiver==1u||floorNear)body+=toLinear(vec3(.55,.73,.65))*convexity*.13*exp(-.16*depth)*(1.-oil);
        }
        base=mix(texelFetch(sceneColor,c,0).rgb,body,coverage);
    }
    if(hdrOutput!=0){color=vec4(base,1);return;}
    vec3 display=toDisplay(base);const int bayer[16]=int[](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    float noise=(float(bayer[(c.y%4)*4+c.x%4])+.5)/16.-.5;
    color=vec4(clamp(floor(display*63.+noise+.5)/63.,0.,1.),1);
}
