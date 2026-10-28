void terrainBody(){
    ivec2 base=ivec2(floor(game.actor.xy));
    for(int y=-19;y<=1;++y)for(int x=-5;x<=5;++x){
        ivec2 b=base+ivec2(x,y);if(!solid(b))continue;
        vec2 axis=vec2(game.actor.x,clamp(float(b.y)+.5,game.actor.y-14,game.actor.y-4));
        vec2 low=vec2(b),d=axis-clamp(axis,low,low+1);float len=length(d);
        if(len>=4)continue;
        vec2 n;float depth;
        if(len>1e-5){n=d/len;depth=4-len;}
        else{vec4 e=vec4(axis-low,low+1-axis);float k=min(min(e.x,e.y),min(e.z,e.w));
            n=k==e.x?vec2(-1,0):(k==e.z?vec2(1,0):(k==e.y?vec2(0,-1):vec2(0,1)));depth=4+k;}
        game.actor.xy+=n*depth;if(n.y<-.55)game.locomotion.z=1;
    }
    game.actor.xy=clamp(game.actor.xy,vec2(4,18),vec2(W-4,H));
}
