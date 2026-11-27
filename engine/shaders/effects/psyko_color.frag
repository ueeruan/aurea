#version 450

#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // amount, pattern frequency, color cycles, source detail
    vec4 p1;   // phase in cycles, saturation, brightness, stripes flag
    vec4 p2;   // origin and size in normalized layer coordinates
    vec4 p3;   // stripe direction or seeded offset, blob octaves
    vec4 color;
} p;

void main() {
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float alpha = max(src.a, 1e-6);
    const float image = aurea_linear_to_srgb(aurea_luma(max(src.rgb / alpha, vec3(0.0))));
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float pattern = p.p1.w > 0.5
        ? dot(point, p.p3.xy) * (p.p0.y / 3.0)
        : aurea_fbm(point * p.p0.y + p.p3.xy, int(p.p3.z), 0.48) - 0.5;
    const float cycle = (pattern + image * p.p0.w) * p.p0.z + p.p1.x;
    const vec3 phase = AUREA_TAU * (cycle + vec3(0.0, 0.667, 0.333));
    // Fade unresolved bands smoothly at low preview resolutions.
    const float contrast = 1.0 - smoothstep(0.15, 0.65, fwidth(cycle));
    const vec3 spectrum = vec3(0.5) + 0.5 * cos(phase) * contrast;
    const float luma = aurea_luma(spectrum);
    const vec3 rgb = clamp(mix(vec3(luma), spectrum, p.p1.y) * p.p1.z, 0.0, 1.0);
    o_color = vec4(mix(src.rgb, aurea_srgb_to_linear(rgb) * src.a, p.p0.x), src.a);
}
