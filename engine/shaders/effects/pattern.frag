#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;

float band(float phase, float aa) {
    float width = p.p2.z;
    if (width <= 0.0) return 0.0;
    if (width >= 1.0) return 1.0;
    // At sub-pixel frequencies use the coverage average, not aliased stripes.
    float d = abs(fract(phase + 0.5) - 0.5);
    float feather = max(0.00001, max(aa * 0.5, p.p2.w));
    float cov = 1.0 - smoothstep(width * 0.5 - feather, width * 0.5 + feather, d);
    return mix(cov, width, smoothstep(0.5, 1.0, aa));
}
void main() {
    vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    vec2 point = p.p0.xy + v_uv * p.p0.zw - p.p1.xy;
    float cs = cos(p.p2.x), sn = sin(p.p2.x);
    vec2 q = vec2(cs * point.x + sn * point.y, -sn * point.x + cs * point.y);
    q.y /= p.p3.w;
    float coverage;
    if (p.p3.x < 0.5) {
        float phase = q.x / p.p1.z + p.p2.y;
        coverage = band(phase, fwidth(phase));
    } else if (p.p3.x < 1.5) {
        float radius = length(q);
        float phase = atan(q.y, q.x) * p.p1.w / AUREA_TAU + p.p2.y;
        float aa = length(fwidth(q)) * p.p1.w / (AUREA_TAU * max(radius, 0.0001));
        coverage = band(phase, aa);
    } else {
        vec2 phase = q / p.p1.z + p.p2.y;
        vec2 aa = fwidth(phase);
        float x = band(phase.x, aa.x), y = band(phase.y, aa.y);
        coverage = x + y - x * y;
    }
    vec3 straight = src.a > 0.000001 ? src.rgb / src.a : vec3(0.0);
    vec3 tint = p.color.rgb;
    if (p.p3.z > 1.5) tint = vec3(1.0) - (vec3(1.0) - straight) * (vec3(1.0) - tint);
    else if (p.p3.z > 0.5) tint *= straight;
    // Preserve the input alpha exactly: transparent margins stay transparent.
    float mixAmount = clamp(coverage * p.p3.y * p.color.a, 0.0, 1.0);
    o_color = vec4(mix(src.rgb, tint * src.a, mixAmount), src.a);
}
