#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    if (src.a <= 0.000001) { o_color = vec4(0); return; }
    float luma = clamp(aurea_luma(aurea_linear_to_srgb(src.rgb / src.a)), 0.0, 1.0);
    float middle = clamp(p.p0.x, 0.01, 0.99);
    vec4 mapped = luma < middle ? mix(p.p1, p.p2, luma / middle)
                               : mix(p.p2, p.color, (luma - middle) / (1.0 - middle));
    vec4 result = vec4(aurea_srgb_to_linear(mapped.rgb) * src.a * mapped.a, src.a * mapped.a);
    o_color = mix(src, result, clamp(p.p0.y, 0.0, 1.0));
}
