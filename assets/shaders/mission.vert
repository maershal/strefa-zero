#version 430 core
#include "mission_state.glsl"
uniform vec4 cameraOffset;uniform float alpha;uniform int vfxEnabled;
uniform int instanceOffset;uniform vec4 targetHP;uniform vec4 targetHit;
uniform vec4 weaponSize;
out vec2 uv;out vec4 tint;flat out int atlas;
const vec2 corners[4]=vec2[](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1));
const vec4 crops[16]=vec4[](vec4(0,0,32,48),vec4(11,16,10,15),vec4(13,20,6,8),vec4(12,20,7,8),
    vec4(13,20,6,8),vec4(10,21,11,6),vec4(13,20,5,8),vec4(10,21,11,6),
    vec4(13,19,6,10),vec4(13,18,6,12),vec4(13,18,6,11),vec4(12,18,7,12),
    vec4(9,16,14,16),vec4(12,20,8,8),vec4(11,18,10,12),vec4(13,21,5,6));
void main(){
    vec2 corner=corners[gl_VertexID],center=vec2(0),down=vec2(0,1);vec2 size=vec2(1);float facing=game.aim.x<0?-1:1;
    int instance=gl_InstanceID+instanceOffset,part=0;atlas=2;tint=vec4(1);uv=corner;
    if(instance<18){
        atlas=0;
        vec2 feet=mix(game.previous.xy,game.actor.xy,alpha),offset=feet-game.actor.xy;
        SoldierPose pose=currentPose(feet);
        vec2 hip=pose.hip,arm=pose.shoulder,head=pose.head,gun=pose.gun;
        vec2 a,b;bool segment=false;
        if(instance<4){
            bool armPart=instance>=2;vec2 root=armPart?arm+vec2(-facing,0):hip+vec2(-facing*.6,0);
            PoseLimb limb=armPart?pose.far_arm:pose.far_leg;vec2 end=limb.end,mid=limb.joint;
            a=(instance%2)==0?root:mid;b=(instance%2)==0?mid:end;part=armPart?6+instance%2:10+instance%2;size.x=armPart?1.9:2.3;
            tint=vec4(.67,.71,.65,1);segment=true;
        }else if(instance==4){part=14;center=arm+vec2(-facing*2.5,1.5);size=vec2(3.7,6);}
        else if(instance==5){part=2;center=hip;size=vec2(4,3);}
        else if(instance==6||instance==7){vec2 mid=pose.near_leg.joint;a=instance==6?hip:mid;b=instance==6?mid:pose.near_leg.end;part=instance+2;size.x=2.5;segment=true;}
        else if(instance==8||instance==9){part=instance==8?1:12;center=(hip+arm)*.5;size=instance==8?vec2(5,7):vec2(6,7.5);}
        else if(instance==10){part=3;center=head;size=vec2(3.5,4);}
        else if(instance==11){part=13;center=head+vec2(0,-1.2);size=vec2(4.5,3);}
        else if(instance==12){atlas=1;center=gun;size=vec2(13,4);down=vec2(-game.aim.y,game.aim.x);uv=(vec2(0,10)+corner*vec2(32,13))/weaponSize.xy;}
        else if(instance==13||instance==14){vec2 end=pose.near_arm.end,mid=pose.near_arm.joint;
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
    else if(instance==158){center=game.flash.xy;size=vec2(game.flash.z*2*(1-game.flash.w/.3));tint=vec4(1,.7,.2,game.flash.w*1.5);atlas=3;uv=corner*2-1;}
    else{int target=(instance-159)/6,kind=(instance-159)%6;vec4 t=game.targets[target];float hp=targetHP[target];center=t.xy;atlas=2;
        if(kind==0){center+=vec2(0,9);size=vec2(1.2,7);tint=vec4(.4,.43,.37,.9);}
        else if(kind==1){atlas=5;size=vec2(8,13);tint=vec4(.12,.15,.13,hp>0?.85:.25);}
        else if(kind==2){size=vec2(5.5,8);atlas=4;tint=vec4(.94,.85,.59,hp>0?1:.25);}
        else if(kind==3){center.y-=8;size=vec2(8,1.6);tint=vec4(.12,.14,.12,1);}
        else if(kind==4){center+=vec2(-4.+4.*hp/3.,-8);size=vec2(8.*hp/3.,1.1);tint=vec4(.64,.85,.35,hp>0?1:0);}
        else{size=vec2(8,13);tint=vec4(1,.96,.7,targetHit[target]*.7);}}
    if(vfxEnabled!=0&&(instance==15||instance==16||instance==158))tint.a=0;
    vec2 right=vec2(down.y,-down.x),pos=center+right*(corner.x-.5)*size.x+down*(corner.y-.5)*size.y;
    if(tint.a<=0){gl_Position=vec4(2,2,0,1);return;}
    gl_Position=vec4((pos-cameraOffset.xy)/vec2(320,192)*2-1,0,1);
}
