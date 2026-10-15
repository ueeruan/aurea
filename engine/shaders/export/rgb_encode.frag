#version 450
// =============================================================================
//  Aurea / shaders / export / rgb_encode.frag
//
//  Composição (linear, pré-multiplicada) → R'G'B' de 8 bits para o degrau
//  surface-direct do export (o encoder do aparelho converte para Y'CbCr com o
//  padrão de cor do MediaFormat). A curva é a MESMA do yuv_encode.frag
//  (`linear_to_srgb` sobre o texel, texelFetch sem filtro): o arquivo sai com
//  os mesmos códigos do caminho NV12, dentro do arredondamento do encoder.
//
//  Dither de 1/2 LSB por canal antes de quantizar (cfg.y), como no NV12.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 cfg;      // y = dither (0/1)
    vec4 matrix;   // não usado (mesmo bloco do yuv_encode)
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

float interleaved_gradient_noise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    ivec2 size = textureSize(u_tex0, 0);
    vec4 c = texelFetch(u_tex0, clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1), 0);
    vec3 rgb = linear_to_srgb(clamp(c.rgb, 0.0, 1.0));
    if (p.cfg.y > 0.5) rgb += (interleaved_gradient_noise(gl_FragCoord.xy) - 0.5) / 255.0;
    o_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
