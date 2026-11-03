#version 450
// =============================================================================
//  Aurea / shaders / effects / magnify.frag
//
//  Lupa (0.4.6): um círculo que amplia o que está embaixo dele, com aro
//  opcional. Fora do círculo, a imagem como estava.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // xy = centro (uv), z = raio (fração da altura), w = ampliação (×)
    vec4 p1;      // x = aro (fração da altura), y = suavidade da borda (fração do raio)
    vec4 p2;
    vec4 p3;
    vec4 color;   // aro (linear, pré-multiplicado na conta)
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const float aspect = max(p.texel.y / max(p.texel.x, 1e-9), 1e-9);
    vec2 d = inUv - p.p0.xy;
    d.x *= aspect;
    const float r = length(d);
    const float R = max(p.p0.z, 1e-4);
    const float zoom = max(p.p0.w, 0.05);
    vec2 q = d / zoom;
    q.x /= aspect;
    const vec4 base = texture(u_tex0, inUv);
    const vec4 lens = texture(u_tex0, p.p0.xy + q);
    const float soft = max(p.p1.y * R, 1e-4);
    const float inside = 1.0 - smoothstep(R - soft, R, r);
    vec4 outc = mix(base, lens, inside);
    if (p.p1.x > 0.0) {
        const float ring = smoothstep(R - soft, R, r) * (1.0 - smoothstep(R + p.p1.x, R + p.p1.x + soft, r));
        const vec4 rim = vec4(p.color.rgb * p.color.a, p.color.a) * ring;
        outc = rim + outc * (1.0 - rim.a);
    }
    o_color = outc;
}
