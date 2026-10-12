#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_kernel.frag
//
//  Núcleo 3×3 (o comportamento do CC Kernel): convolução da camada com nove
//  pesos, divididos pelo "Divisor" (0 = a soma dos pesos; soma zero = 1). As
//  nove amostras ficam a "Distância" px uma da outra. A conta é na cor
//  pré-multiplicada (o alfa passa pelo mesmo núcleo); o negativo vira zero.
//
//  p0 = linha de cima, p1 = do meio, p2 = de baixo (xyz); p0.w = 1/divisor,
//  p1.w = distância (px), p2.w = mistura com o original.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const float d = max(p.p1.w, 0.0);
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 3; ++j) {
        const vec3 row = j == 0 ? p.p0.xyz : (j == 1 ? p.p1.xyz : p.p2.xyz);
        for (int i = 0; i < 3; ++i) {
            sum += cc_at(px + vec2(float(i - 1), float(j - 1)) * d) * row[i];
        }
    }
    sum *= p.p0.w;
    const vec4 filtered = vec4(max(sum.rgb, vec3(0.0)), clamp(sum.a, 0.0, 1.0));
    o_color = mix(filtered, cc_at(px), clamp(p.p2.w, 0.0, 1.0));
}
