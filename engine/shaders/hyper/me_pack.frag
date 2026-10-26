#version 450
// =============================================================================
//  Aurea / shaders / hyper / me_pack.frag
//
//  Os dois sentidos num texel do cache: RG = A -> B, BA = B -> A (px cheios).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_fwd;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_bwd;

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    o_color = vec4(texelFetch(u_fwd, at, 0).rg, texelFetch(u_bwd, at, 0).rg);
}
