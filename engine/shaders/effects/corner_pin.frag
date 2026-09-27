#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec3 point=vec3(p.uvMap.xy+v_uv*p.uvMap.zw,1.0);
    float z=dot(p.p2.xyz,point);
    if(p.p2.w<0.5 || abs(z)<0.000001){o_color=vec4(0);return;}
    vec2 unit=vec2(dot(p.p0.xyz,point),dot(p.p1.xyz,point))/z;
    vec2 uv=(unit*p.color.xy-p.p3.xy)/p.p3.zw;
    if(any(lessThan(unit,vec2(0)))||any(greaterThan(unit,vec2(1)))||
       any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1)))){o_color=vec4(0);return;}
    o_color=texture(u_tex0,uv);
}
