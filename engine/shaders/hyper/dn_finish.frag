#version 450
// =============================================================================
//  Aurea / shaders / hyper / dn_finish.frag
//
//  Hyper Clean (Turbo), fim da redução de ruído: um bilateral 5x5 leve sobre
//  o acumulado temporal (o resíduo cai com a raiz do peso: onde o tempo já
//  limpou, o espacial quase não age; onde o pixel ficou sozinho — a parte
//  que anda — ele limpa mais, sem passar das bordas), a mistura com o
//  original e a volta para o espaço de trabalho com o ALFA do original.
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_acc;    // perceptual, A = peso (ou linear com flags.y)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_orig;   // a entrada do efeito (linear pré-mult.)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho
    vec4 tune;    // x = h espacial (luma perceptual), y = entrada (0 acumulado, 1 linear, 2 perceptual), z = mistura (0..1)
} p;

vec4 load(ivec2 at) {
    const vec4 v = texelFetch(u_acc, at, 0);
    if (p.tune.y > 1.5) return vec4(v.rgb, 1.0);
    return p.tune.y > 0.5 ? vec4(hy_to_perc(v).rgb, 1.0) : v;
}

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const ivec2 sz = ivec2(p.size.xy);
    const vec4 c = load(at);
    const float W = max(c.a, 1.0);
    const float h = max(p.tune.x / sqrt(W), 1e-4);
    vec3 sum = vec3(0.0);
    float ws = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            const vec3 s = load(hy_clampi(at + ivec2(x, y), sz)).rgb;
            const float dr = hy_luma(s - c.rgb) / h;
            const float w = exp(-0.5 * dr * dr - 0.18 * float(x * x + y * y));
            sum += s * w;
            ws += w;
        }
    const vec3 clean = sum / ws;
    const vec4 orig = texelFetch(u_orig, at, 0);
    const vec4 origP = hy_to_perc(orig);
    o_color = hy_from_perc(vec4(mix(origP.rgb, clean, clamp(p.tune.z, 0.0, 1.0)), orig.a));
}
