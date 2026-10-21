#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_down.frag
//
//  Aprimorar: a imagem em MEIA resolução (média 2×2) no espaço do efeito. É a
//  base do redutor de ruído espacial (a pirâmide: o grosso em meia resolução,
//  o detalhe na cheia) e o "borrado" de referência do detalhe, dos halos e do
//  desborrar. Da entrada linear, cada texel é convertido ANTES da média (a raiz
//  da média não é a média das raízes: o detalhe na cheia sairia torto).
// =============================================================================
#include "../common/bindings.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da ENTRADA (cheia)
    vec4 flags;   // x = entrada linear (0/1)
} p;

void main() {
    if (p.flags.x > 0.5) {
        const vec2 d = 0.5 * p.texel.xy;
        o_color = 0.25 * (vec4(enh_from_linear(textureLod(u_src, v_uv + vec2(-d.x, -d.y), 0.0)), 1.0)
                        + vec4(enh_from_linear(textureLod(u_src, v_uv + vec2(d.x, -d.y), 0.0)), 1.0)
                        + vec4(enh_from_linear(textureLod(u_src, v_uv + vec2(-d.x, d.y), 0.0)), 1.0)
                        + vec4(enh_from_linear(textureLod(u_src, v_uv + vec2(d.x, d.y), 0.0)), 1.0));
    } else {
        o_color = textureLod(u_src, v_uv, 0.0);
    }
}
