#version 430 core
#include "render_terrain.glsl"
layout(binding=0) uniform sampler2D inputField;
layout(binding=2) uniform sampler2D rawField;
uniform vec4 directionExtent;uniform int bilateral;
layout(location=0) out vec4 filtered;
void main(){ivec2 c=ivec2(gl_FragCoord.xy),size=textureSize(inputField,0);vec2 world=(vec2(c)+.5)/vec2(size)*vec2(320,192);
    vec4 center=texelFetch(inputField,c,0),raw=texelFetch(rawField,c,0);float d=raw.x+raw.y;
    if(solidAt(ivec2(world))||d<.08){filtered=vec4(0);return;}
    vec4 sum=center;float weight=1.;float phase=raw.y/max(d,.0001);
    for(int i=-2;i<=2;++i){if(i==0)continue;ivec2 p=c+ivec2(directionExtent.xy)*i;if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,size)))continue;
        if(!clearTerrain(world,(vec2(p)+.5)/vec2(size)*vec2(320,192)))continue;
        vec4 v=texelFetch(inputField,p,0),r=texelFetch(rawField,p,0);float n=r.x+r.y;if(n<.08)continue;
        float w=exp(-float(i*i)*.5);
        if(bilateral!=0)w*=exp(-4.*abs(n-d)-12.*abs(r.y/max(n,.0001)-phase));
        sum+=v*w;weight+=w;
    }filtered=sum/weight;
    // Preserve the support of raw drops/streams rather than eroding their iso mask.
}
