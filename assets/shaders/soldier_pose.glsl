// One set of presentation/IK equations compiled as C++ and GLSL. Physical muzzle
// uses the same pose at current GPU feet; interpolation is presentation only.
#ifdef __cplusplus
#define POSE_FN inline
#else
#define POSE_FN
#endif
struct PoseLimb {vec2 root,joint,end;};
struct SoldierPose {vec2 hip,shoulder,head,gun,muzzle,trigger_grip,support_grip;PoseLimb near_arm,far_arm,near_leg,far_leg;float facing;};
POSE_FN PoseLimb poseLimb(vec2 root,vec2 target,float upper,float lower,float bend){
    vec2 delta=target-root;
    float distance=clamp(length(delta),.001F,upper+lower-.001F);
    float angle=atan2(delta.y,delta.x);
    float cosine=clamp((upper*upper+distance*distance-lower*lower)/(2*upper*distance),-1.0F,1.0F);
    float elbow=angle+bend*acos(cosine);
    PoseLimb limb;limb.root=root;limb.joint=root+vec2(cos(elbow),sin(elbow))*upper;
    limb.end=root+vec2(cos(angle),sin(angle))*distance;return limb;
}
POSE_FN SoldierPose soldierPose(vec2 feet,vec2 velocity,vec2 aim,float stride,float recoil,float landing,float blend,bool grounded){
    SoldierPose p;p.facing=aim.x<0?-1.0F:1.0F;
    float gait=sin(stride),lift=cos(stride),compression=landing*1.5F;
    float bob=grounded?abs(lift)*blend*.4F:0;
    p.hip=feet+vec2(0,-7.5F+compression-bob);
    p.shoulder=p.hip+vec2(velocity.x*.009F,-6.2F)-aim*(recoil*.6F);
    p.head=p.shoulder+vec2(0,-3.3F);
    vec2 normal=vec2(-aim.y*p.facing,aim.x*p.facing);
    p.gun=p.shoulder+aim*(3-recoil*.7F);p.muzzle=p.gun+aim*6.5F;
    p.trigger_grip=p.gun-aim*1.2F+normal;p.support_grip=p.gun+aim*2.8F+normal*.7F;
    p.near_arm=poseLimb(p.shoulder,p.trigger_grip,3.8F,3.4F,p.facing);
    p.far_arm=poseLimb(p.shoulder+vec2(-p.facing,0),p.support_grip,3.8F,3.4F,p.facing);
    float step=gait*3*blend;
    vec2 near_foot=feet+vec2(step,-max(0.0F,lift)*2*blend),far_foot=feet+vec2(-step,-max(0.0F,-lift)*2*blend);
    if(!grounded){float fall=clamp(velocity.y/90,-1.0F,1.0F);near_foot=feet+vec2(p.facing*(2-fall),-2+fall);far_foot=feet+vec2(-p.facing*2,-1-fall);}
    p.near_leg=poseLimb(p.hip,near_foot,4.3F,4.2F,-p.facing);
    p.far_leg=poseLimb(p.hip+vec2(-p.facing*.6F,0),far_foot,4.3F,4.2F,-p.facing);return p;
}
#undef POSE_FN
