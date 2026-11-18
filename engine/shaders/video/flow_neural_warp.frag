#version 450
// =============================================================================
//  Aurea / shaders / video / flow_neural_warp.frag
//
//  Flow Retime "Neural (IA)" (1.0.3): a rede (RIFE v4.6, MIT, a mesma do Hyper
//  Motion) devolve, numa grade reduzida, os dois fluxos do instante t para os
//  quadros de origem e a máscara de visibilidade. Aqui o quadro sai em
//  resolução CHEIA na GPU (antes era um warp na CPU):
//      out = A(p + f₀(p))·m(p) + B(p + f₁(p))·(1 − m(p))
//  — a composição da última etapa do RIFE v4, com o fluxo ampliado pelo
//  filtro bilinear da textura.
//
//  Entrada: A, B (resolução cheia), fluxo (RG = f₀, BA = f₁, em px da imagem
//  cheia) e máscara (R) na grade da rede. Saída: o quadro intermediário.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_flow;   // RG = t→A, BA = t→B (px da imagem)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_mask;   // R = peso de A

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 image;   // xy = escala do fluxo (0 = cópia de A), zw = uv da grade da rede por uv da imagem (parte válida)
} p;

void main() {
    // image.x = 0: quadros iguais ou corte de cena — o quadro de origem.
    if (p.image.x <= 0.0) {
        o_color = textureLod(u_a, v_uv, 0.0);
        return;
    }
    const vec2 at = v_uv * p.image.zw;
    const vec4 f = textureLod(u_flow, at, 0.0);
    const float m = clamp(textureLod(u_mask, at, 0.0).r, 0.0, 1.0);
    const vec2 ua = clamp(v_uv + f.rg * p.image.xy, vec2(0.0), vec2(1.0));
    const vec2 ub = clamp(v_uv + f.ba * p.image.xy, vec2(0.0), vec2(1.0));
    o_color = textureLod(u_a, ua, 0.0) * m + textureLod(u_b, ub, 0.0) * (1.0 - m);
}
