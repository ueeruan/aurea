#version 450
// =============================================================================
//  Aurea / shaders / hyper / to_perc.frag
//
//  Linear pré-multiplicado → perceptual reto (hyper_common.glsl), no MESMO
//  tamanho. É a entrada do FSR, do realce de linhas e da estimativa de
//  movimento (uma conversão por texel de entrada, não uma por amostra).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

void main() {
    o_color = hy_to_perc(texelFetch(u_src, ivec2(gl_FragCoord.xy), 0));
}
