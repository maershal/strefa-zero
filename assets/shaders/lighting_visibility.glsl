layout(binding=3) uniform usampler2D terrainMap;
bool lightSolid(ivec2 c){return any(lessThan(c,ivec2(0)))||any(greaterThanEqual(c,ivec2(320,192)))||texelFetch(terrainMap,c,0).r!=0u;}
// Receiver cell may be a surface. Only that first cell is excluded; all intervening cells block.
bool lightVisible(vec2 a,vec2 b){ivec2 c=ivec2(floor(a)),end=ivec2(floor(b)),s=ivec2(sign(b-a));vec2 d=b-a;
if(lightSolid(end))return false;if(all(equal(c,end)))return true;
vec2 delta=vec2(1e20),next=vec2(1e20);for(int k=0;k<2;++k)if(abs(d[k])>1e-8){delta[k]=abs(1./d[k]);next[k]=((s[k]>0?float(c[k]+1):float(c[k]))-a[k])/d[k];}
for(int i=0;i<516;++i){if(all(equal(c,end)))return true;
if(abs(next.x-next.y)<1e-6){if(lightSolid(c+ivec2(s.x,0))||lightSolid(c+ivec2(0,s.y)))return false;c+=s;next+=delta;}
else if(next.x<next.y){c.x+=s.x;next.x+=delta.x;}else{c.y+=s.y;next.y+=delta.y;}
if(lightSolid(c))return false;}return false;}
