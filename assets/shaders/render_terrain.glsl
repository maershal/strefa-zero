layout(binding=3) uniform usampler2D terrainMap;
bool solidAt(ivec2 c){return any(lessThan(c,ivec2(0)))||any(greaterThanEqual(c,ivec2(320,192)))||texelFetch(terrainMap,c,0).r!=0u;}
// Supercover DDA: also visits both side cells at a corner. Bounded refraction
// needs at most 6 cells; exhausting the cap rejects, never accepts, a ray.
bool clearTerrain(vec2 a,vec2 b){
    ivec2 c=ivec2(floor(a)),end=ivec2(floor(b));if(solidAt(c)||solidAt(end))return false;
    vec2 d=b-a;ivec2 stepDir=ivec2(sign(d));vec2 delta=vec2(1e20),next=vec2(1e20);
    for(int k=0;k<2;++k)if(abs(d[k])>1e-7){delta[k]=abs(1/d[k]);next[k]=((d[k]>0?float(c[k]+1):float(c[k]))-a[k])/d[k];}
    for(int i=0;i<12;++i){
        if(all(equal(c,end)))return true;
        if(abs(next.x-next.y)<1e-6){if(solidAt(c+ivec2(stepDir.x,0))||solidAt(c+ivec2(0,stepDir.y)))return false;c+=stepDir;next+=delta;}
        else if(next.x<next.y){c.x+=stepDir.x;next.x+=delta.x;}else{c.y+=stepDir.y;next.y+=delta.y;}
        if(solidAt(c))return false;
    }return false;
}
