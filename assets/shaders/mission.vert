#version 430 core
#include "mission_state.glsl"
uniform float alpha;
uniform vec4 weaponSize;
out vec2 uv;out vec4 tint;flat out int atlas;
const vec2 corners[4]=vec2[](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1));
const vec4 crops[16]=vec4[](vec4(0,0,32,48),vec4(11,16,10,15),vec4(13,20,6,8),vec4(12,20,7,8),
    vec4(13,20,6,8),vec4(10,21,11,6),vec4(13,20,5,8),vec4(10,21,11,6),
    vec4(13,19,6,10),vec4(13,18,6,12),vec4(13,18,6,11),vec4(12,18,7,12),
    vec4(9,16,14,16),vec4(12,20,8,8),vec4(11,18,10,12),vec4(13,21,5,6));
vec2 joint(vec2 root,vec2 target,float upper,float lower,float bend){
    vec2 d=target-root;float len=clamp(length(d),.001,upper+lower-.001);
    float a=atan(d.y,d.x)+bend*acos(clamp((upper*upper+len*len-lower*lower)/(2*upper*len),-1,1));
    return root+vec2(cos(a),sin(a))*upper;
}
void main(){
    vec2 corner=corners[gl_VertexID],center=vec2(0),down=vec2(0,1);vec2 size=vec2(1);float facing=game.aim.x<0?-1:1;
    int instance=gl_InstanceID,part=0;atlas=2;tint=vec4(1);uv=corner;
    if(instance<18){
        atlas=0;
        vec2 feet=mix(game.previous.xy,game.actor.xy,alpha),offset=feet-game.actor.xy;
        float gait=sin(game.locomotion.x),lift=cos(game.locomotion.x),blend=game.locomotion.w;
        float bob=game.locomotion.z>.5?abs(lift)*blend*.4:0;
        vec2 hip=feet+vec2(0,-7.5+game.locomotion.y*1.5-bob),arm=shoulder()+offset,head=arm+vec2(0,-3.3);
        vec2 gun=gunPosition()+offset,normal=vec2(-game.aim.y,game.aim.x)*facing;
        vec2 nf=feet+vec2(gait*3*blend,-max(0,lift)*2*blend),ff=feet+vec2(-gait*3*blend,-max(0,-lift)*2*blend);
        if(game.locomotion.z<.5){float fall=clamp(game.actor.w/90,-1,1);nf=feet+vec2(facing*(2-fall),-2+fall);ff=feet+vec2(-facing*2,-1-fall);}
        vec2 a,b;bool segment=false;
        if(instance<4){
            bool armPart=instance>=2;vec2 root=armPart?arm+vec2(-facing,0):hip+vec2(-facing*.6,0);
            vec2 end=armPart?gun+game.aim.xy*2.8+normal*.7:ff;
            vec2 mid=joint(root,end,armPart?3.8:4.3,armPart?3.4:4.2,armPart?facing:-facing);
            a=(instance%2)==0?root:mid;b=(instance%2)==0?mid:end;part=armPart?6+instance%2:10+instance%2;size.x=armPart?1.9:2.3;
            tint=vec4(.67,.71,.65,1);segment=true;
        }else if(instance==4){part=14;center=arm+vec2(-facing*2.5,1.5);size=vec2(3.7,6);}
        else if(instance==5){part=2;center=hip;size=vec2(4,3);}
        else if(instance==6||instance==7){vec2 mid=joint(hip,nf,4.3,4.2,-facing);a=instance==6?hip:mid;b=instance==6?mid:nf;part=instance+2;size.x=2.5;segment=true;}
        else if(instance==8||instance==9){part=instance==8?1:12;center=(hip+arm)*.5;size=instance==8?vec2(5,7):vec2(6,7.5);}
        else if(instance==10){part=3;center=head;size=vec2(3.5,4);}
        else if(instance==11){part=13;center=head+vec2(0,-1.2);size=vec2(4.5,3);}
        else if(instance==12){atlas=1;center=gun;size=vec2(13,4);down=vec2(-game.aim.y,game.aim.x);uv=(vec2(0,10)+corner*vec2(32,13))/weaponSize.xy;}
        else if(instance==13||instance==14){vec2 end=gun-game.aim.xy*1.2+normal,mid=joint(arm,end,3.8,3.4,facing);
            a=instance==13?arm:mid;b=instance==13?mid:end;part=instance-9;size.x=2;segment=true;}
        else if(instance==15){atlas=2;center=muzzlePosition()+offset;size=vec2(3);tint=vec4(1,.9,.4,game.aim.w>.65?1:0);}
        else if(instance==16){atlas=2;center=feet+vec2(-facing*3,-5);size=vec2(1.8,6+float(game.status.z%4u));tint=vec4(1,.6,.15,game.timers.w);}
        else{atlas=2;center=feet+vec2(0,2);size=vec2(7,.4);tint=vec4(.6,.8,.4,.7);}
        if(segment){center=(a+b)*.5;float len=max(.001,length(b-a));size.y=len+1;down=(b-a)/len;if(instance%2==1)size.x+=.4;}
        if(atlas==0){vec4 crop=crops[part];vec2 c=corner;if(facing<0)c.x=1-c.x;
            if(part==5||part==7)c=vec2(c.y,1-c.x);
            uv=(vec2(part%4*32,part/4*48)+crop.xy+c*crop.zw)/vec2(128,288);}
    }else if(instance<146){Projectile p=bullets[instance-18];center=mix(p.info.yz,p.motion.xy,alpha);size=vec2(2,.55);down=vec2(-normalize(p.motion.zw).y,normalize(p.motion.zw).x);tint=vec4(1,.85,.4,p.info.x>0?1:0);}
    else if(instance<154){Projectile p=grenades[instance-146];center=mix(p.info.yz,p.motion.xy,alpha);size=vec2(1.7);tint=vec4(.6,.7,.2,p.info.x>0?1:0);}
    else if(instance<157){vec4 target=game.targets[instance-154];center=target.xy;size=vec2(7,12);tint=vec4(.8,.35,.22,target.z>0?1:.2);}
    else if(instance==157){center=(game.exitZone.xy+game.exitZone.zw)*.5;size=game.exitZone.zw-game.exitZone.xy;tint=vec4(.45,.9,.35,game.inventory.z==3u?.6:.15);}
    else{center=game.flash.xy;size=vec2(game.flash.z*2*(1-game.flash.w/.3));tint=vec4(1,.7,.2,game.flash.w*1.5);atlas=3;uv=corner*2-1;}
    vec2 right=vec2(down.y,-down.x),pos=center+right*(corner.x-.5)*size.x+down*(corner.y-.5)*size.y;
    if(tint.a<=0){gl_Position=vec4(2,2,0,1);return;}
    gl_Position=vec4(pos/vec2(320,192)*2-1,0,1);
}
