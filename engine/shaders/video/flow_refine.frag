#version 450
// =============================================================================
//  Aurea / shaders / video / flow_refine.frag
//
//  Sobe o optical flow do nível base para uma grade mais fina sem levar o
//  contorno junto. O fluxo é medido com janela, então perto de um objeto
//  rápido os vetores do objeto e do fundo se misturam numa faixa; esticar esse
//  campo é o que dobrava as bordas no Twixtor e fazia o RSMB invadir o fundo.
//
//  1.0.3 (filmagem real):
//   * a subida é GUIADA pela cor (joint bilateral upsampling, Kopf et al.
//     2007): cada pixel fino faz a média dos texels base em volta pesada pela
//     semelhança de cor (luminância + crominância) entre ele e o texel — o
//     vetor segue o contorno da imagem fina, não a grade grossa;
//   * onde os vizinhos discordam, cada pixel ainda escolhe, entre o vetor
//     guiado e os vizinhos a alguns texels, o que melhor leva o seu bloco ao
//     outro quadro — agora comparando cor, e com escolha SUAVE (softmin), sem
//     o degrau de uma escolha dura entre dois pixels vizinhos.
//
//  Entrada: fluxo base (RG = A→B, BA = B→A), luminância fina (R = A, G = B),
//  crominância fina (RG = A, BA = B), luminância e crominância do nível base.
//  Saída: o mesmo formato do fluxo, ainda em pixels do nível BASE.
// =============================================================================
#include "../common/bindings.glsl"
#include "flow_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_luma;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_chroma;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_baseLuma;
layout(set = 0, binding = AUREA_TEX4) uniform sampler2D u_baseChroma;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 base;    // xy = 1/tamanho do nível base, zw = tamanho
    vec4 fine;    // xy = 1/tamanho desta grade
    vec4 flags;   // x = peso da crominância, y = σ da cor na subida guiada (0 = sem guia), z = temperatura da escolha
} p;

const float kReach = 3.0;       // texels do nível base: além da faixa misturada
const float kAgree = 0.3;       // px do nível base: abaixo disto, não há o que escolher

vec3 fine_color(vec2 uv, int side) {
    const vec2 l = textureLod(u_luma, uv, 0.0).rg;
    const vec4 c = textureLod(u_chroma, uv, 0.0);
    return side == 0 ? vec3(l.r, c.rg) : vec3(l.g, c.ba);
}
vec3 fine_color_other(vec2 uv, int side) { return fine_color(uv, 1 - side); }

// Subida guiada: média 3×3 dos texels base pesada pela distância e pela cor.
vec2 guided(int side, vec3 here) {
    const vec2 b = v_uv * p.base.zw - 0.5;
    const vec2 c0 = floor(b + 0.5);
    const float sigma = max(p.flags.y, 1e-3);
    const float inv2s2 = 0.5 / (sigma * sigma);
    vec2 acc = vec2(0.0);
    float wsum = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const vec2 q = c0 + vec2(float(i), float(j));
            const vec2 uv = (q + 0.5) * p.base.xy;
            const vec4 f = textureLod(u_flow, uv, 0.0);
            const vec2 bl = textureLod(u_baseLuma, uv, 0.0).rg;
            const vec4 bc = textureLod(u_baseChroma, uv, 0.0);
            const vec3 col = side == 0 ? vec3(bl.r, bc.rg) : vec3(bl.g, bc.ba);
            const vec2 d = b - q;
            const vec3 dc = col - here;
            const float w = exp(-dot(d, d) * 0.9) * exp(-flow_color_dist2(dc.x, dc.yz, p.flags.x) * inv2s2) + 1e-6;
            acc += (side == 0 ? f.rg : f.ba) * w;
            wsum += w;
        }
    }
    return acc / wsum;
}

vec2 pick(int side) {
    const vec3 here = fine_color(v_uv, side);
    const vec2 bilinear = side == 0 ? textureLod(u_flow, v_uv, 0.0).rg : textureLod(u_flow, v_uv, 0.0).ba;
    const vec2 center = p.flags.y > 0.0 ? guided(side, here) : bilinear;
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
    vec3 src[9];
    for (int k = 0; k < 9; ++k) {
        const vec2 o = vec2(float(k % 3 - 1), float(k / 3 - 1)) * 2.0 * p.fine.xy;
        src[k] = fine_color(v_uv + o, side);
    }
    float costs[9];
    float bestCost = 1e9;
    for (int c = 0; c < 9; ++c) {
        const vec2 v = c == 8 ? center : cands[c];
        float cost = 0.0;
        for (int k = 0; k < 9; ++k) {
            const vec2 o = vec2(float(k % 3 - 1), float(k / 3 - 1)) * 2.0 * p.fine.xy;
            const vec3 d = fine_color_other(v_uv + o + v * p.base.xy, side) - src[k];
            cost += abs(d.x) + 0.5 * p.flags.x * (abs(d.y) + abs(d.z));
        }
        // O vetor guiado ganha empates: região lisa continua suave.
        if (c == 8) cost = cost * 0.85 - 0.02;
        costs[c] = cost;
        bestCost = min(bestCost, cost);
    }
    // Softmin: o melhor domina; dois quase iguais dividem (sem degrau).
    const float tau = max(p.flags.z, 1e-3);
    vec2 acc = vec2(0.0);
    float wsum = 0.0;
    for (int c = 0; c < 9; ++c) {
        const float w = exp(-(costs[c] - bestCost) / tau);
        acc += (c == 8 ? center : cands[c]) * w;
        wsum += w;
    }
    return acc / wsum;
}

void main() {
    o_color = vec4(pick(0), pick(1));
}
