// =============================================================================
//  Aurea / shaders / hyper / hyper_common.glsl
//
//  Peças comuns do AureaHyper (Turbo). O motor trabalha em RGB LINEAR com alfa
//  PRÉ-MULTIPLICADO; os algoritmos do Hyper trabalham no espaço PERCEPTUAL
//  (a curva sRGB, a mesma dos códigos do vídeo) e com alfa reto — é onde o
//  codec quantizou, onde o FSR e o realce de linhas foram pensados e onde a
//  diferença entre dois quadros corresponde ao que o olho vê.
//
//  Imagens "perceptuais" intermediárias: RGB = sRGB codificado 0..1 (reto),
//  A = alfa. Luma perceptual: Rec.709 sobre os códigos.
// =============================================================================
#ifndef AUREA_HYPER_COMMON_GLSL
#define AUREA_HYPER_COMMON_GLSL

float hy_srgb_enc1(float c) {
    c = clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
float hy_srgb_dec1(float c) {
    c = clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
vec3 hy_srgb_enc(vec3 c) { return vec3(hy_srgb_enc1(c.r), hy_srgb_enc1(c.g), hy_srgb_enc1(c.b)); }
vec3 hy_srgb_dec(vec3 c) { return vec3(hy_srgb_dec1(c.r), hy_srgb_dec1(c.g), hy_srgb_dec1(c.b)); }

/// Linear pré-multiplicado → perceptual reto (A = alfa).
vec4 hy_to_perc(vec4 premul) {
    const vec3 c = premul.a > 1e-5 ? premul.rgb / premul.a : vec3(0.0);
    return vec4(hy_srgb_enc(c), clamp(premul.a, 0.0, 1.0));
}
/// Perceptual reto → linear pré-multiplicado.
vec4 hy_from_perc(vec4 perc) {
    const float a = clamp(perc.a, 0.0, 1.0);
    return vec4(hy_srgb_dec(perc.rgb) * a, a);
}
float hy_luma(vec3 perc) { return dot(perc, vec3(0.2126, 0.7152, 0.0722)); }

/// Texel inteiro preso à borda.
ivec2 hy_clampi(ivec2 p, ivec2 size) { return clamp(p, ivec2(0), size - ivec2(1)); }

#endif
