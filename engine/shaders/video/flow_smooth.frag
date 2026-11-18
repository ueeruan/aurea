#version 450
// =============================================================================
//  Aurea / shaders / video / flow_smooth.frag
//
//  Regulariza um nível do optical flow (0.1.8): média 5×5 do fluxo pesada pela
//  CONFIANÇA de cada vizinho (o menor autovalor do Lucas-Kanade: canto > borda
//  > liso) e pela SEMELHANÇA de cor com o pixel do meio no quadro de origem.
//  Onde a imagem é lisa (sem informação) o fluxo vem dos vizinhos com textura;
//  na borda de um objeto, só do mesmo lado da borda — o fluxo do fundo não
//  vaza para o objeto que passa por cima.
//
//  1.0.3 (filmagem real): a semelhança usa também a CROMINÂNCIA (guia de cor,
//  flow_luma.frag com w = 1). Pele contra parede verde-água tem quase a mesma
//  luminância; só a cor separa os dois lados. flags.z = 0 mantém a conta só
//  na luminância (Aprimorar, que não monta a guia).
//
//  Entrada: RG = fluxo (px do nível), B = confiança. Saída: o mesmo formato.
// =============================================================================
#include "../common/bindings.glsl"
#include "flow_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;    // RG = fluxo, B = confiança
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_level;   // R = atual, G = seguinte (luminância)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_guide;   // RG = CbCr do atual, BA = do seguinte

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;    // xy = 1/tamanho do nível
    vec4 flags;    // x = origem é o G (fluxo para trás), y = σ da cor, z = usa a guia de cor (0/1), w = raio (0 = 2: 5×5)
} p;

float source_luma(vec2 uv) {
    const vec2 c = textureLod(u_level, uv, 0.0).rg;
    return p.flags.x > 0.5 ? c.g : c.r;
}
vec2 source_chroma(vec2 uv) {
    const vec4 c = textureLod(u_guide, uv, 0.0);
    return p.flags.x > 0.5 ? c.ba : c.rg;
}

void main() {
    const float lc = source_luma(v_uv);
    const bool guided = p.flags.z > 0.5;
    const vec2 cc = guided ? source_chroma(v_uv) : vec2(0.0);
    const float sigma = max(p.flags.y, 1e-3);
    const float inv2s2 = 0.5 / (sigma * sigma);
    const int radius = p.flags.w > 0.5 ? int(p.flags.w) : 2;
    vec2 acc = vec2(0.0);
    float conf = 0.0, wsum = 0.0;
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            if (abs(i) > radius || abs(j) > radius) continue;
            const vec2 o = vec2(float(i), float(j));
            const vec2 uv = v_uv + o * p.texel.xy;
            const vec3 f = textureLod(u_flow, uv, 0.0).rgb;
            const float dl = source_luma(uv) - lc;
            const vec2 dc = guided ? source_chroma(uv) - cc : vec2(0.0);
            // Confiança: o autovalor é pequeno (gradientes em 0..1 ao quadrado);
            // satura em textura boa e não zera no liso (nunca divide por zero).
            const float c = clamp(f.b * 600.0, 0.03, 1.0);
            const float w = exp(-dot(o, o) * 0.18) * exp(-flow_color_dist2(dl, dc, 1.0) * inv2s2) * c;
            acc += f.rg * w;
            conf += f.b * w;
            wsum += w;
        }
    }
    o_color = vec4(acc / wsum, conf / wsum, 1.0);
}
