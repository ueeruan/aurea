#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec2 point=p.p0.xy+v_uv*p.p0.zw, q=point-p.p1.zw;
    if(p.p2.x<.5) {
        float c=cos(p.p2.z),s=sin(p.p2.z),factor=exp(clamp(p.p2.y*.02,-1.9,1.9));
        vec2 a=vec2(c*q.x+s*q.y,-s*q.x+c*q.y);
        a*=vec2(factor,1.0/factor); q=vec2(c*a.x-s*a.y,s*a.x+c*a.y);
    } else {
        float halfSize=max(1.0,min(p.p1.x,p.p1.y)*.5);
        vec2 a=q/halfSize; float r=length(a), fov=radians(clamp(p.p2.y,0.0,160.0))*.5;
        if(r>1e-6 && fov>1e-6) {
            float sourceRadius=p.p2.z>.5 ? atan(r*tan(fov))/fov : tan(min(r*fov,1.55))/tan(fov);
            q=a*(sourceRadius/r)*halfSize;
        }
    }
    vec2 uv=(q+p.p1.zw-p.p0.xy)/p.p0.zw;
    o_color=all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1)))?texture(u_tex0,uv):vec4(0);
}
