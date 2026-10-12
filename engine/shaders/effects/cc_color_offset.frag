#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_color_offset.frag
//
//  Deslocar cor (o comportamento do CC Color Offset): cada canal anda pela sua
//  "fase" (uma volta = 360°) no valor sRGB da cor reta. O que passa do fim:
//    0 repetir   — volta pelo começo (o ciclo do disco de matiz);
//    1 solarizar — reflete (onda triangular: sobe e desce);
//    2 polarizar — repete e satura para os extremos (contraste forte).
//  Fase zero em repetir/solarizar devolve o valor exato (preto e branco
//  incluídos).
//
//  p0.rgb = fases (voltas), p0.a = modo; p1.x = mistura com o original.
// =============================================================================
#include "cc_common.glsl"

vec3 wrap01(vec3 x) {
    vec3 f = fract(x);
    // 1.0 continua 1.0 (o branco não vira preto com fase zero).
    return mix(f, x, step(0.0, x) * step(x, vec3(1.0)));
}

void main() {
    const vec4 src = cc_at(cc_px());
    const vec4 s = unpremultiply(src);
    const vec3 x = linear_to_srgb(s.rgb) + p.p0.rgb;
    const int mode = int(p.p0.a + 0.5);
    vec3 v;
    if (mode == 1) v = 1.0 - abs(mod(x, 2.0) - 1.0);
    else if (mode == 2) v = smoothstep(0.35, 0.65, wrap01(x));
    else v = wrap01(x);
    const vec4 shifted = vec4(srgb_to_linear(clamp(v, 0.0, 1.0)) * s.a, s.a);
    o_color = mix(shifted, src, clamp(p.p1.x, 0.0, 1.0));
}
