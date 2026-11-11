// =============================================================================
//  Aurea / shaders / effects / ae_common.glsl
//
//  Peças dos efeitos do lote "AE 2026" (AeEffects2026*.cpp): o MESMO bloco de
//  uniforms em todos (`AeUniforms` no C++, 320 bytes std140) e as conversões
//  de cor que eles usam. Os efeitos foram especificados pelo comportamento
//  descrito na documentação pública do After Effects; o código é do Aurea.
//
//  Ao contrário de common.glsl, este arquivo DECLARA o bloco e a textura 0:
//  é o contrato do lote inteiro, e cada shader só acrescenta as texturas
//  extras que lê (u_tex1...).
//
//  Coordenadas: `ae_px()` é o ponto do fragmento em px da CAMADA (resolução
//  cheia); `ae_uv_of_px` leva um ponto da camada ao uv da entrada. Pontos
//  relativos dos parâmetros já chegam em px da camada (o C++ multiplica pelo
//  tamanho natural dela), então uma região de entrada maior que a camada
//  (sombra/brilho antes) não tira o centro do lugar.
// =============================================================================
#ifndef AUREA_AE_COMMON_GLSL
#define AUREA_AE_COMMON_GLSL

#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;      // uv da saída → uv da entrada: uv * xy + zw
    vec4 texel;      // x = 1/largura, y = 1/altura (texels da entrada), zw = texels por px da camada
    vec4 inRegion;   // região da entrada (px da camada)
    vec4 outRegion;  // região da saída (px da camada)
    vec4 layer;      // xy = tamanho natural da camada (px), z = tempo (s), w = quadros por segundo
    vec4 p[12];      // parâmetros do efeito (o C++ de cada um documenta o seu)
    vec4 c[3];       // cores (lineares, alfa reto)
} u;

vec2 ae_in_uv() { return v_uv * u.uvMap.xy + u.uvMap.zw; }
vec2 ae_px() { return u.outRegion.xy + v_uv * u.outRegion.zw; }
vec2 ae_uv_of_px(vec2 px) { return (px - u.inRegion.xy) / max(u.inRegion.zw, vec2(1e-4)); }
vec4 ae_at_px(vec2 px) { return texture(u_tex0, ae_uv_of_px(px)); }
/// Um texel da entrada em px da camada (o passo de amostragem natural).
float ae_texel_px() { return 1.0 / max(u.texel.z, 1e-6); }
vec2 ae_layer_size() { return max(u.layer.xy, vec2(1.0)); }

// --- Cor -----------------------------------------------------------------------
/// Pré-multiplicado linear → cor CODIFICADA (sRGB, alfa reto) e alfa.
vec4 ae_decode(vec4 premul) {
    const vec4 s = unpremultiply(premul);
    return vec4(clamp(aurea_linear_to_srgb(max(s.rgb, vec3(0.0))), 0.0, 1.0), clamp(s.a, 0.0, 1.0));
}
/// Cor codificada (alfa reto) → pré-multiplicado linear.
vec4 ae_encode(vec3 enc, float a) {
    return vec4(aurea_srgb_to_linear(clamp(enc, 0.0, 1.0)) * a, a);
}
float ae_luma(vec3 enc) { return dot(enc, vec3(0.2126, 0.7152, 0.0722)); }

vec3 ae_rgb2hsl(vec3 c) {
    const float mx = max(c.r, max(c.g, c.b));
    const float mn = min(c.r, min(c.g, c.b));
    const float l = 0.5 * (mx + mn);
    const float d = mx - mn;
    float h = 0.0, s = 0.0;
    if (d > 1e-6) {
        s = l > 0.5 ? d / max(2.0 - mx - mn, 1e-6) : d / max(mx + mn, 1e-6);
        if (mx == c.r)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
        else                h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, s, l);
}
float ae_hue2rgb(float p, float q, float t) {
    t = fract(t);
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}
vec3 ae_hsl2rgb(vec3 hsl) {
    const float s = clamp(hsl.y, 0.0, 1.0), l = clamp(hsl.z, 0.0, 1.0);
    if (s <= 1e-6) return vec3(l);
    const float q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const float p = 2.0 * l - q;
    return vec3(ae_hue2rgb(p, q, hsl.x + 1.0 / 3.0), ae_hue2rgb(p, q, hsl.x), ae_hue2rgb(p, q, hsl.x - 1.0 / 3.0));
}
/// Distância circular de matiz (voltas), 0..0,5.
float ae_hue_dist(float a, float b) {
    const float d = abs(fract(a) - fract(b));
    return min(d, 1.0 - d);
}

/// Mistura de uma camada de cima (codificada, alfa reto) com a de baixo pelo
/// modo do lote: 0 normal, 1 somar, 2 multiplicar, 3 tela, 4 sobrepor,
/// 5 diferença, 6 mais escuro, 7 mais claro, 8 subtrair.
vec3 ae_blend_mode(vec3 base, vec3 top, int mode) {
    if (mode == 1) return min(base + top, vec3(1.0));
    if (mode == 2) return base * top;
    if (mode == 3) return 1.0 - (1.0 - base) * (1.0 - top);
    if (mode == 4) return mix(2.0 * base * top, 1.0 - 2.0 * (1.0 - base) * (1.0 - top), step(0.5, base));
    if (mode == 5) return abs(base - top);
    if (mode == 6) return min(base, top);
    if (mode == 7) return max(base, top);
    if (mode == 8) return max(base - top, vec3(0.0));
    return top;
}

/// Hash determinístico de (célula, semente) em 0..1.
float ae_hash(vec2 cell, float seed) {
    const uint s = uint(int(floor(seed)));
    return aurea_hash(uvec2(ivec2(floor(cell))) ^ uvec2(s * 747796405u + 2891336453u, s * 2654435761u));
}

#endif
