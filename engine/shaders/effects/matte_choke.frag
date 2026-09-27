#version 450
// =============================================================================
//  Aurea / shaders / effects / matte_choke.frag
//
//  Encolher/crescer a máscara: morfológico REDONDO pelo alfa — cada pixel vira
//  o vizinho (num disco de até 6 texels) de menor alfa (encolher) ou de maior
//  (crescer). O pixel vencedor vai INTEIRO, pré-multiplicado como está: a cor
//  da borda que sobra é a de um pixel real, não uma média.
//
//  Raio maior que 6 texels: o C++ reduz a imagem antes (um morfológico de
//  raio r na escala 1/k é um de raio r/k), o custo por pixel fica preso.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // xy = 1/tamanho desta saída
    vec4 p0;      // x = raio (texels desta escala, ≤ 6), y = 1 cresce / 0 encolhe
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 uv0 = v_uv * p.uvMap.xy + p.uvMap.zw;
    const float r = clamp(p.p0.x, 0.0, 6.0);
    const bool grow = p.p0.y > 0.5;
    vec4 best = texture(u_tex0, uv0);
    float bestA = best.a;
    for (int dy = -6; dy <= 6; ++dy) {
        for (int dx = -6; dx <= 6; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (float(dx * dx + dy * dy) > r * r + 0.25) continue;   // disco
            const vec4 s = texture(u_tex0, uv0 + vec2(float(dx), float(dy)) * p.texel.xy);
            if (grow ? (s.a > bestA) : (s.a < bestA)) {
                best = s;
                bestA = s.a;
            }
        }
    }
    o_color = best;
}
