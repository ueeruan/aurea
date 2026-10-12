#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_light_burst.frag
//
//  Explosão de luz (o comportamento do CC Light Burst 2.5): raios de luz saem
//  do "Centro" levando a própria imagem. Cada pixel colhe amostras no caminho
//  até o centro (até "Comprimento dos raios" dele) e SOMA a média à imagem:
//    0 reta     — pesos iguais;
//    1 esmaecer — o perto pesa mais: o raio some com a distância;
//    2 centro   — o perto do centro pesa mais: a luz nasce no centro.
//  O começo das amostras é sorteado por pixel (determinístico): a escada das
//  poucas amostras vira um grão fino em vez de faixas.
//
//  p0 = centro (px), comprimento (0..0.95), intensidade;
//  p1 = modo, amostras, usar cor; color = cor dos raios (linear).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec2 c = p.p0.xy;
    const vec2 d = px - c;
    const float L = clamp(p.p0.z, 0.0, 0.95);
    const int mode = int(p.p1.x + 0.5);
    const int n = clamp(int(p.p1.y + 0.5), 4, 48);
    const float jitter = aurea_hash(uvec2(ivec2(gl_FragCoord.xy)));
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 48; ++i) {
        if (i >= n) break;
        const float t = (float(i) + jitter) / float(n);
        const float w = mode == 1 ? 1.0 - t : (mode == 2 ? 0.15 + t : 1.0);
        acc += cc_at(c + d * (1.0 - t * L)) * w;
        wsum += w;
    }
    vec4 rays = acc / max(wsum, 1e-4);
    if (p.p1.z > 0.5) rays.rgb = p.color.rgb * aurea_luma(rays.rgb);
    const vec4 src = cc_at(px);
    const float I = max(p.p0.w, 0.0);
    o_color = vec4(src.rgb + rays.rgb * I, clamp(src.a + rays.a * I * (1.0 - src.a), 0.0, 1.0));
}
