#version 450
// =============================================================================
//  Aurea / shaders / video / flow_refine.frag
//
//  Sobe o optical flow do nível base para uma grade mais fina sem levar o
//  contorno junto. O Lucas-Kanade mede com janela, então perto de um objeto
//  rápido os vetores do objeto e do fundo se misturam numa faixa; esticar esse
//  campo é o que dobrava as bordas no Twixtor e fazia o RSMB invadir o fundo.
//  Aqui cada pixel escolhe, entre o vetor interpolado e os vizinhos a alguns
//  texels de distância, o que melhor leva o seu bloco ao outro quadro.
//  Onde os candidatos concordam (a maior parte da imagem) não há busca.
//
//  Entrada: fluxo base (RG = A→B, BA = B→A) e luminância fina (R = A, G = B).
//  Saída: o mesmo formato, ainda em pixels do nível BASE.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_luma;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 base;    // xy = 1/tamanho do nível base
    vec4 fine;    // xy = 1/tamanho desta grade
} p;

const float kReach = 3.0;       // texels do nível base: além da faixa misturada
const float kAgree = 0.3;       // px do nível base: abaixo disto, não há o que escolher

vec2 pick(int side) {
    const vec2 center = side == 0 ? textureLod(u_flow, v_uv, 0.0).rg : textureLod(u_flow, v_uv, 0.0).ba;
    vec2 cands[8];
    float spread = 0.0;
    for (int c = 0; c < 8; ++c) {
        const float angle = float(c) * 0.7853981634;
        const vec2 at = v_uv + vec2(cos(angle), sin(angle)) * kReach * p.base.xy;
        const vec4 f = textureLod(u_flow, at, 0.0);
        cands[c] = side == 0 ? f.rg : f.ba;
        spread = max(spread, length(cands[c] - center));
    }
    if (spread < kAgree) return center;
    // Bloco do quadro de origem, lido uma vez só.
    float src[9];
    for (int k = 0; k < 9; ++k) {
        const vec2 o = vec2(float(k % 3 - 1), float(k / 3 - 1)) * 2.0 * p.fine.xy;
        const vec2 l = textureLod(u_luma, v_uv + o, 0.0).rg;
        src[k] = side == 0 ? l.r : l.g;
    }
    vec2 best = center;
    float bestCost = 1e9;
    for (int c = 0; c < 9; ++c) {
        const vec2 v = c == 8 ? center : cands[c];
        float cost = 0.0;
        for (int k = 0; k < 9; ++k) {
            const vec2 o = vec2(float(k % 3 - 1), float(k / 3 - 1)) * 2.0 * p.fine.xy;
            const vec2 l = textureLod(u_luma, v_uv + o + v * p.base.xy, 0.0).rg;
            cost += abs((side == 0 ? l.g : l.r) - src[k]);
        }
        // O vetor interpolado ganha empates: região lisa continua suave.
        if (c == 8) cost = cost * 0.85 - 0.02;
        if (cost < bestCost) { bestCost = cost; best = v; }
    }
    return best;
}

void main() {
    o_color = vec4(pick(0), pick(1));
}
