#version 450
// =============================================================================
//  Aurea / shaders / hyper / a4k_grad.frag
//
//  Hyper Upscale (Anime, Turbo), passo 2: o gradiente (Sobel) da luma
//  perceptual. Saída R = 1 - |gradiente| (1 = liso, 0 = borda forte): é o
//  campo que o passo 3 "empurra" para endurecer as bordas.
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"
#include "a4k_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;
} p;

void main() {
    const A4kNb n = a4k_load(u_src, ivec2(gl_FragCoord.xy), ivec2(p.size.xy));
    const float tl = hy_luma(n.tl.rgb), t = hy_luma(n.t.rgb), tr = hy_luma(n.tr.rgb);
    const float l = hy_luma(n.l.rgb), r = hy_luma(n.r.rgb);
    const float bl = hy_luma(n.bl.rgb), b = hy_luma(n.b.rgb), br = hy_luma(n.br.rgb);
    const float gx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
    const float gy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
    const float mag = clamp(sqrt(gx * gx + gy * gy), 0.0, 1.0);
    o_color = vec4(1.0 - mag, 0.0, 0.0, 1.0);
}
