#version 450
// =============================================================================
//  Aurea / shaders / video / flow_smooth.frag
//
//  Regulariza um nível do optical flow (0.1.8): média 5×5 do fluxo pesada pela
//  CONFIANÇA de cada vizinho (o menor autovalor do Lucas-Kanade: canto > borda
//  > liso) e pela SEMELHANÇA de luminância com o pixel do meio no quadro de
//  origem. Onde a imagem é lisa (sem informação) o fluxo vem dos vizinhos com
//  textura; na borda de um objeto, só do mesmo lado da borda — o fluxo do
//  fundo não vaza para o objeto que passa por cima (era o "tremido" das
//  bordas no Movimento suave).
//
//  Entrada: RG = fluxo (px do nível), B = confiança. Saída: o mesmo formato.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;    // RG = fluxo, B = confiança
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_level;   // R = atual, G = seguinte (luminância)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;    // xy = 1/tamanho do nível
    vec4 flags;    // x = quadro de origem é o G (fluxo para trás), y = σ da luminância
} p;

float source_luma(vec2 uv) {
    const vec2 c = textureLod(u_level, uv, 0.0).rg;
    return p.flags.x > 0.5 ? c.g : c.r;
}

void main() {
    const float lc = source_luma(v_uv);
    const float sigma = max(p.flags.y, 1e-3);
    const float inv2s2 = 0.5 / (sigma * sigma);
    vec2 acc = vec2(0.0);
    float conf = 0.0, wsum = 0.0;
    for (int k = 0; k < 25; ++k) {
        const vec2 o = vec2(float(k % 5 - 2), float(k / 5 - 2));
        const vec2 uv = v_uv + o * p.texel.xy;
        const vec3 f = textureLod(u_flow, uv, 0.0).rgb;
        const float dl = source_luma(uv) - lc;
        // Confiança: o autovalor é pequeno (gradientes em 0..1 ao quadrado);
        // satura em textura boa e não zera no liso (nunca divide por zero).
        const float c = clamp(f.b * 600.0, 0.03, 1.0);
        const float w = exp(-dot(o, o) * 0.18) * exp(-dl * dl * inv2s2) * c;
        acc += f.rg * w;
        conf += f.b * w;
        wsum += w;
    }
    o_color = vec4(acc / wsum, conf / wsum, 1.0);
}
