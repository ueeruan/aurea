#version 450
// =============================================================================
//  Aurea / shaders / hyper / me_luma.frag
//
//  Base da estimativa de movimento do Hyper (Motion e Clean): a luma
//  PERCEPTUAL dos dois quadros no mesmo texel — R = quadro A (o atual), G =
//  quadro B (o seguinte / o vizinho). Mesmo tamanho da entrada (o nível 0 da
//  pirâmide); os níveis de cima saem por effects/downsample.frag.
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 flags;   // x = 1: A já perceptual; y = 1: B já perceptual
} p;

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const vec4 ta = texelFetch(u_a, at, 0), tb = texelFetch(u_b, at, 0);
    const float a = hy_luma(p.flags.x > 0.5 ? ta.rgb : hy_to_perc(ta).rgb);
    const float b = hy_luma(p.flags.y > 0.5 ? tb.rgb : hy_to_perc(tb).rgb);
    o_color = vec4(a, b, 0.0, 1.0);
}
