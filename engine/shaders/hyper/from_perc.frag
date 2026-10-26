#version 450
// =============================================================================
//  Aurea / shaders / hyper / from_perc.frag
//
//  Perceptual reto → linear pré-multiplicado (a volta de to_perc.frag).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

void main() {
    o_color = hy_from_perc(texelFetch(u_src, ivec2(gl_FragCoord.xy), 0));
}
