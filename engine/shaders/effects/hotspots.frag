#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
#include "light_energy.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec4 c=texture(u_tex0,v_uv*p.uvMap.xy+p.uvMap.zw);
    vec3 straight=unpremultiply(c).rgb;
    if (p.p1.x > 0.5) {
        vec3 source = linear_to_srgb(straight) * c.a;
        vec3 highlight = hue_preserving_highlights(source, p.p0.x, linear_to_srgb(p.color.rgb));
        float grey = dot(highlight, encodedLightLuma);
        highlight = max(mix(vec3(grey), highlight, p.p0.z) * p.p0.y, 0.0);
        vec3 mixed = mix(source, highlight, p.p0.w);
        vec3 encoded = clamp(mixed / max(c.a, 1e-4), 0.0, 16.0);
        o_color = vec4(srgb_to_linear(encoded) * c.a, c.a);
        return;
    }
    vec3 highlights=max(straight-vec3(p.p0.x)-p.color.rgb,vec3(0));
    float light=aurea_luma(highlights);
    highlights=max(mix(vec3(light),highlights,p.p0.z),vec3(0))*p.p0.y;
    o_color=vec4(mix(c.rgb,highlights*c.a,p.p0.w),c.a);
}
