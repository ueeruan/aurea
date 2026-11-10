#version 450
// =============================================================================
//  Aurea / shaders / video / flow_cut.frag
//
//  Decide, num único pixel, se o par de quadros é um corte de cena: leva uma
//  grade de pontos do quadro A ao B pelo fluxo já calculado e vê quantos não
//  chegaram na mesma imagem. Objeto rápido só erra no contorno e uma panorâmica
//  rápida casa depois do fluxo; num corte quase nada casa. Compara um bloco, e
//  não um texel: o fluxo sempre acha algum texel parecido, até entre cenas.
//
//  Saída: R = 1 para corte.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_forward;  // RG = A→B (px do nível base)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_level;    // R = A, G = B (nível base)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho do nível base
} p;

void main() {
    float missed = 0.0, sum = 0.0;
    for (int j = 0; j < 8; ++j) {
        for (int i = 0; i < 8; ++i) {
            const vec2 uv = (vec2(float(i), float(j)) + 0.5) / 8.0;
            const vec2 f = textureLod(u_forward, uv, 0.0).rg;
            float d = 0.0;
            for (int k = 0; k < 9; ++k) {
                const vec2 at = uv + vec2(float(k % 3 - 1), float(k / 3 - 1)) * 3.0 * p.texel.xy;
                d += abs(textureLod(u_level, at, 0.0).r - textureLod(u_level, at + f * p.texel.xy, 0.0).g);
            }
            d /= 9.0;
            sum += d;
            if (d > 0.05) missed += 1.0;
        }
    }
    o_color = vec4(missed >= 32.0 && sum / 64.0 > 0.05 ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
