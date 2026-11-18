#version 450
// =============================================================================
//  Aurea / shaders / video / flow_dis_dense.frag
//
//  DIS — densificação (1.0.3; Kroeger et al., ECCV 2016). Cada pixel do nível
//  é coberto por vários blocos sobrepostos (flow_dis_patch.frag); o vetor do
//  pixel é a média dos vetores desses blocos, cada um pesado por quão bem ELE
//  leva este pixel ao outro quadro: 1 / max(ε, |erro de cor|). Perto de um
//  contorno, o bloco que está do lado certo da borda ganha. O erro usa a
//  luminância e a crominância (a guia de cor): o lado certo da borda entre
//  pele e parede verde-água tem a mesma luminância do errado.
//
//  Saída: RG = vetor (px do nível), B = confiança (1 = o melhor bloco leva o
//  pixel ao outro quadro sem erro).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_patch;   // RG = vetor do bloco
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_level;   // R = atual, G = seguinte
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_guide;   // RG = CbCr do atual, BA = do seguinte

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho do nível, zw = tamanho
    vec4 blk;     // x = passo entre blocos (px), y = sentido trocado (0/1), z = peso da crominância, zw: -
    vec4 grid;    // xy = tamanho da grade de blocos
} p;

void main() {
    const vec2 pix = floor(v_uv * p.texel.zw);
    const float stride = p.blk.x;
    const bool back = p.blk.y > 0.5;
    const vec2 l0 = textureLod(u_level, v_uv, 0.0).rg;
    const vec4 g0 = textureLod(u_guide, v_uv, 0.0);
    const float s0 = back ? l0.g : l0.r;
    const vec2 c0 = back ? g0.ba : g0.rg;
    const ivec2 lo = ivec2(max(ceil((pix - 7.0) / stride), vec2(0.0)));
    const ivec2 hi = ivec2(min(floor(pix / stride), p.grid.xy - 1.0));
    vec2 acc = vec2(0.0);
    float wsum = 0.0, bestErr = 1e9;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const ivec2 q = lo + ivec2(i, j);
            if (q.x > hi.x || q.y > hi.y) continue;
            const vec2 u = texelFetch(u_patch, q, 0).rg;
            const vec2 at = v_uv + u * p.texel.xy;
            const vec2 l1 = textureLod(u_level, at, 0.0).rg;
            const vec4 g1 = textureLod(u_guide, at, 0.0);
            const float dl = (back ? l1.r : l1.g) - s0;
            const vec2 dc = (back ? g1.rg : g1.ba) - c0;
            const float err = sqrt(dl * dl + p.blk.z * dot(dc, dc));
            const float w = 1.0 / max(err, 0.01);
            acc += u * w;
            wsum += w;
            bestErr = min(bestErr, err);
        }
    }
    if (wsum <= 0.0) {
        o_color = vec4(texelFetch(u_patch, ivec2(clamp(pix / stride, vec2(0.0), p.grid.xy - 1.0)), 0).rg, 0.0, 1.0);
        return;
    }
    o_color = vec4(acc / wsum, exp(-bestErr * bestErr * 400.0), 1.0);
}
