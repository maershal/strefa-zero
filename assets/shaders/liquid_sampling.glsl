bool wetAt(ivec2 c,int phase){vec2 d=texelFetch(rawField,c,0).rg;return d.x+d.y>=.12&&(d.y>d.x?1:0)==phase;}
bool clearLiquid(ivec2 a,ivec2 b,int phase){
    // Supercover in render pixels as well as the exact terrain-grid DDA.
    ivec2 c=a,d=b-a,s=ivec2(sign(vec2(d)));vec2 stepT=vec2(1e20),next=vec2(1e20);
    for(int k=0;k<2;++k)if(d[k]!=0){stepT[k]=abs(1./float(d[k]));next[k]=.5*stepT[k];}
    for(int i=0;i<32;++i){if(!wetAt(c,phase))return false;if(all(equal(c,b)))return true;
        if(abs(next.x-next.y)<1e-6){if(!wetAt(c+ivec2(s.x,0),phase)||!wetAt(c+ivec2(0,s.y),phase))return false;c+=s;next+=stepT;}
        else if(next.x<next.y){c.x+=s.x;next.x+=stepT.x;}else{c.y+=s.y;next.y+=stepT.y;}
    }return false;
}
