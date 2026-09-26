#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec4 c=texture(u_tex0,v_uv*p.uvMap.xy+p.uvMap.zw);
    vec3 straight=unpremultiply(c).rgb;
    vec3 highlights=max(straight-vec3(p.p0.x)-p.color.rgb,vec3(0));
    float light=aurea_luma(highlights);
    highlights=max(mix(vec3(light),highlights,p.p0.z),vec3(0))*p.p0.y;
    o_color=vec4(mix(c.rgb,highlights*c.a,p.p0.w),c.a);
}
