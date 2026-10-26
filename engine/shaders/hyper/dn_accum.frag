#version 450
// =============================================================================
//  Aurea / shaders / hyper / dn_accum.frag
//
//  Hyper Clean (Turbo), redução de ruído TEMPORAL: soma UM vizinho no tempo,
//  alinhado pelos vetores de bloco (me_search: atual -> vizinho), à média
//  acumulada. Código próprio.
//
//  Para cada pixel: dos candidatos (o campo bilinear e os 4 blocos mais
//  perto) fica o vetor cujo remendo 3x3 do vizinho mais se parece com o do
//  atual. O peso é a SEMELHANÇA desse remendo (exp de −(d − h/2)²/h²): onde o
//  alinhamento falha — a parte que se move de outro jeito, o que aparece, o
//  que some — o peso vai a zero e o pixel fica com o próprio quadro (não
//  borra o que anda). Fundo parado ou bem alinhado soma tudo.
//
//  Entrada: o acumulado (perceptual reto, A = peso total) ou, no primeiro
//  vizinho, o próprio quadro (linear pré-multiplicado, peso 1).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_acc;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_nbr;     // vizinho (linear pré-multiplicado)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_field;   // RG = atual -> vizinho (px)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_luma;    // R = atual, G = vizinho

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho, zw = 1/xy
    vec4 field;   // x = bloco (px), yz = fw, fh
    vec4 tune;    // x = h (luma perceptual), y = entrada (0 acumulado, 1 linear, 2 perceptual), z = peso pela distância, w = 1 vizinho perceptual
} p;

float lc(vec2 pos) { return textureLod(u_luma, pos * p.size.zw, 0.0).r; }
float ln(vec2 pos) { return textureLod(u_luma, pos * p.size.zw, 0.0).g; }

float patch_distance(vec2 pos, vec2 v) {
    float d = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            const vec2 o = vec2(float(x), float(y));
            d += abs(lc(pos + o) - ln(pos + o + v));
        }
    return d / 9.0;
}

void main() {
    const vec2 pos = gl_FragCoord.xy;
    const ivec2 fsz = ivec2(p.field.yz);
    vec2 cand[5];
    cand[0] = textureLod(u_field, pos / (p.field.x * p.field.yz), 0.0).rg;
    const ivec2 c0 = ivec2(floor(pos / p.field.x - 0.5));
    cand[1] = texelFetch(u_field, clamp(c0, ivec2(0), fsz - 1), 0).rg;
    cand[2] = texelFetch(u_field, clamp(c0 + ivec2(1, 0), ivec2(0), fsz - 1), 0).rg;
    cand[3] = texelFetch(u_field, clamp(c0 + ivec2(0, 1), ivec2(0), fsz - 1), 0).rg;
    cand[4] = texelFetch(u_field, clamp(c0 + ivec2(1, 1), ivec2(0), fsz - 1), 0).rg;
    vec2 v = cand[0];
    float d = 1e9;
    for (int i = 0; i < 5; ++i) {
        const float di = patch_distance(pos, cand[i]);
        if (di < d) { d = di; v = cand[i]; }
    }
    const float h = max(p.tune.x, 1e-4);
    const float x = max(d - 0.5 * h, 0.0) / h;
    const float w = p.tune.z * exp(-x * x);
    vec3 cur;
    float W;
    if (p.tune.y > 1.5) {
        cur = texelFetch(u_acc, ivec2(pos), 0).rgb;
        W = 1.0;
    } else if (p.tune.y > 0.5) {
        cur = hy_to_perc(texelFetch(u_acc, ivec2(pos), 0)).rgb;
        W = 1.0;
    } else {
        const vec4 a = texelFetch(u_acc, ivec2(pos), 0);
        cur = a.rgb;
        W = a.a;
    }
    const vec4 nt = textureLod(u_nbr, (pos + v) * p.size.zw, 0.0);
    const vec3 n = p.tune.w > 0.5 ? nt.rgb : hy_to_perc(nt).rgb;
    o_color = vec4((cur * W + n * w) / (W + w), W + w);
}
