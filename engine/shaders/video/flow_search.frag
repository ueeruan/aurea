#version 450
// =============================================================================
//  Aurea / shaders / video / flow_search.frag
//
//  Busca de vetor num nível grosso da pirâmide. O Lucas-Kanade só anda alguns
//  texels por nível: um objeto que corre mais que o próprio tamanho some nos
//  níveis de cima e o fundo arrasta o vetor dele. Aqui cada texel testa uma
//  grade de deslocamentos e o vetor que veio de cima, numa janela esparsa do
//  tamanho de um objeto pequeno. A grade é folgada de propósito: o Lucas-Kanade
//  dos níveis seguintes refina, a busca só precisa cair perto. Em textura que
//  se repete o vetor continua ambíguo; isso a busca não resolve.
//
//  Entrada: nível (R = atual, G = seguinte) e o fluxo do nível de cima.
//  Saída: RG = melhor vetor em px do nível de CIMA (o flow_lk multiplica por 2).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_level;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_coarse;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho do nível
    vec4 flags;   // x = sentido trocado (0/1)
} p;

const int kPatch = 13;

vec2 patch_offset(int k) {
    if (k == 0) return vec2(0.0);
    const float angle = float(k) * 0.5235987756;
    return vec2(cos(angle), sin(angle)) * ((k & 1) == 0 ? 4.0 : 2.0);
}

float src_luma(vec2 uv) { const vec2 c = textureLod(u_level, uv, 0.0).rg; return p.flags.x > 0.5 ? c.g : c.r; }
float dst_luma(vec2 uv) { const vec2 c = textureLod(u_level, uv, 0.0).rg; return p.flags.x > 0.5 ? c.r : c.g; }

void main() {
    float src[kPatch];
    for (int k = 0; k < kPatch; ++k) src[k] = src_luma(v_uv + patch_offset(k) * p.texel.xy);
    const vec2 fromAbove = textureLod(u_coarse, v_uv, 0.0).xy * 2.0;
    vec2 best = fromAbove;
    float bestCost = 1e9;
    for (int j = -5; j <= 5; ++j) {
        for (int i = -5; i <= 6; ++i) {
            // i == 6 é o vetor que veio de cima; o resto, a grade de ±10 texels.
            if (i == 6 && j != 0) continue;
            const vec2 v = i == 6 ? fromAbove : vec2(float(i), float(j)) * 2.0;
            float cost = 0.0;
            for (int k = 0; k < kPatch; ++k)
                cost += abs(dst_luma(v_uv + (patch_offset(k) + v) * p.texel.xy) - src[k]);
            // Empate: fica com o que a pirâmide já dizia, depois com o menor salto.
            cost = i == 6 ? cost * 0.85 - 0.02 : cost + length(v - fromAbove) * 0.002;
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
    }
    o_color = vec4(best * 0.5, 0.0, 1.0);
}
