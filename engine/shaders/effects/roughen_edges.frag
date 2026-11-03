#version 450
// =============================================================================
//  Aurea / shaders / effects / roughen_edges.frag
//
//  Bordas ásperas (0.4.6): a borda da silhueta é corroída por ruído fractal —
//  o alfa vem de um ponto deslocado pelo ruído (borda irregular) e um limiar
//  pelo ruído come pedaços perto da borda. `evolução` anima o ruído.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = aspereza (px), y = escala (px por célula), z = evolução, w = nitidez 0..1
    vec4 p1;
    vec4 p2;
    vec4 p3;      // xy = tamanho da região da entrada (px)
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 pxPos = inUv * p.p3.xy;
    const float scale = max(p.p0.y, 1.0);
    const vec2 q = pxPos / scale + vec2(p.p0.z * 0.37, p.p0.z * 0.21);
    const vec2 n = vec2(aurea_fbm(q, 4, 0.5), aurea_fbm(q + vec2(17.3, 5.1), 4, 0.5)) * 2.0 - 1.0;
    const vec2 offset = n * p.p0.x / max(p.p3.xy, vec2(1e-4));
    const vec4 moved = texture(u_tex0, inUv + offset);
    const vec4 src = texture(u_tex0, inUv);
    // Come a borda: o alfa deslocado passa por um limiar pelo ruído.
    const float noise = aurea_fbm(q * 2.3 + 9.0, 3, 0.5);
    const float sharp = clamp(p.p0.w, 0.0, 0.98);
    const float keep = smoothstep(noise * 0.6 - (1.0 - sharp) * 0.25, noise * 0.6 + (1.0 - sharp) * 0.25 + 1e-3, moved.a);
    // A cor é a da camada no ponto (sem borrar), recortada pelo novo alfa.
    const vec3 color = src.a > 1e-4 ? src.rgb / src.a : (moved.a > 1e-4 ? moved.rgb / moved.a : vec3(0.0));
    o_color = vec4(color * keep, keep);
}
