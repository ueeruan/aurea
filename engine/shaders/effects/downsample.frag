#version 450
// =============================================================================
//  Aurea / shaders / effects / downsample.frag
//
//  Redução por 2 com filtro de caixa 4x4: quatro amostras bilineares nos
//  cantos do pixel de saída, cada uma já sendo a média de 2x2 texels.
//
//  Um único tap bilinear no centro daria só 2x2 — ao encadear reduções para um
//  blur grande, o que sobra de alta frequência vira cintilação quando a
//  câmera se move. A caixa 4x4 é a base dos blurs e do glow.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // uv de entrada = v_uv * xy + zw
    vec4 texel;    // xy = tamanho de um texel da ENTRADA em uv
} p;

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 d = p.texel.xy;
    o_color = 0.25 * (texture(u_tex0, uv + vec2(-d.x, -d.y)) + texture(u_tex0, uv + vec2(d.x, -d.y))
                    + texture(u_tex0, uv + vec2(-d.x, d.y)) + texture(u_tex0, uv + vec2(d.x, d.y)));
}
