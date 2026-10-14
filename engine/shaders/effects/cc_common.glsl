// =============================================================================
//  Aurea / shaders / effects / cc_common.glsl
//
//  O layout dos efeitos do pacote CC (CCEffects.cpp e irmãos): o MESMO bloco
//  de uniforms em todos (espelho de `cc::Uniforms`, std140, só vec4) e as
//  conversões texel de saída → px da camada → uv da entrada.
//
//  Tudo é medido em px da camada (resolução cheia): a prévia em 1/4 e o
//  export em 4K desenham o mesmo lugar. A cor é linear pré-multiplicada.
// =============================================================================
#ifndef AUREA_CC_COMMON_GLSL
#define AUREA_CC_COMMON_GLSL

#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dst;    // região de saída (px da camada): x, y, largura, altura
    vec4 src;    // região da entrada
    vec4 box;    // caixa natural da camada
    vec4 info;   // x = px por texel de saída, y = tempo (s), z = texels por px
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 p4;
    vec4 p5;
    vec4 color;
} p;

const float CC_PI = 3.14159265359;

/// O ponto da camada (px) que este texel de saída representa.
vec2 cc_px() { return p.dst.xy + v_uv * p.dst.zw; }
/// px da camada → uv da entrada.
vec2 cc_uv(vec2 px) { return (px - p.src.xy) / max(p.src.zw, vec2(1e-6)); }
/// A entrada no ponto `px` da camada.
vec4 cc_at(vec2 px) { return texture(u_tex0, cc_uv(px)); }
/// Tamanho de um texel de saída em px da camada (antisserrilhado das bordas).
float cc_texel() { return max(p.info.x, 1e-4); }
/// Gira `v` por `a` radianos (de x para y: horário na tela, que tem y para baixo).
vec2 cc_rot(vec2 v, float a) {
    const float c = cos(a), s = sin(a);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}
/// Centro da caixa da camada.
vec2 cc_box_center() { return p.box.xy + 0.5 * p.box.zw; }

#endif
