#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_tex1;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;
} p;
void main(){
    vec4 base=texture(u_tex0,v_uv*p.uvMap.xy+p.uvMap.zw);
    if(p.p1.y<.5){o_color=base;return;}
    vec2 point=p.p2.xy+v_uv*p.p2.zw;
    vec2 uv=point/max(p.p3.xy,vec2(1));
    float raw=texture(u_tex1,clamp(uv,vec2(0),vec2(1))).r;
    float a=smoothstep(p.p0.x-p.p0.y,p.p0.x+p.p0.y,raw);
    if(p.p0.z>.5)a=1-a;
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))a=0;
    vec4 cut=base*a;
    if(p.p1.x>.5)cut=vec4(vec3(a)*base.a,base.a);
    o_color=mix(base,cut,clamp(p.p0.w,0,1));
}
