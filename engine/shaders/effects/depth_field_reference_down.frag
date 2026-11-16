#version 450
// =============================================================================
//  Aurea / shaders / effects / depth_field_reference_down.frag
//
//  One level of the Depth of Field reference pyramid
//  (aurea.ai.depth_field_reference): the previous level halved with a 2×2
//  box, then a [1 2 1]/4 blur at the new resolution (transparent border).
//  The native builds each halved world and blurs it once; this filter pair
//  reproduces the AE 2027 edge profile within 0.001 at radius 10
//  (batch-c/status.json). The code is Aurea's.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1 / source size, zw = 1 / destination size
} u;

void main() {
    vec4 sum = vec4(0.0);
    for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
        // A bilinear tap at the centre between four source texels = the 2×2 box.
        const vec2 at = v_uv + vec2(float(i), float(j)) * u.texel.zw;
        const float w = (i == 0 ? 2.0 : 1.0) * (j == 0 ? 2.0 : 1.0);
        sum += texture(u_src, at) * w;
    }
    o_color = sum / 16.0;
}
