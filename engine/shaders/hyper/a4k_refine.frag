#version 450
// =============================================================================
//  Aurea / shaders / hyper / a4k_refine.frag
//
//  Hyper Upscale (Anime, Turbo), passo 3: ENDURECE AS BORDAS. Os mesmos 4
//  testes de orientação do afinamento, agora sobre o campo liso/borda do
//  passo 2: o pixel que está NA rampa de uma borda (o lado "liso" de um lado,
//  a borda do outro) recebe a média do lado liso — a rampa borrada pelo
//  upscale vira um degrau. A primeira orientação que casa decide. Saída
//  perceptual ou linear pré-multiplicada (flags.y).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"
#include "a4k_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_grad;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;
    vec4 flags;   // x = força (0..1), y = 1 saída linear
} p;

vec4 finish(vec4 c) { return p.flags.y > 0.5 ? hy_from_perc(c) : c; }
vec4 avg(vec4 c, vec4 a, vec4 b, vec4 d) { return vec4(mix(c.rgb, (a.rgb + b.rgb + d.rgb) / 3.0, p.flags.x), c.a); }

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const ivec2 sz = ivec2(p.size.xy);
    const A4kNb n = a4k_load(u_src, at, sz);
    const A4kNb g = a4k_load(u_grad, at, sz);
    const float tl = g.tl.r, t = g.t.r, tr = g.tr.r, l = g.l.r, c = g.c.r, r = g.r.r;
    const float bl = g.bl.r, b = g.b.r, br = g.br.r;
    // Vertical.
    float maxDark = a4k_max3(br, b, bl), minLight = a4k_min3(tl, t, tr);
    if (minLight > c && minLight > maxDark) { o_color = finish(avg(n.c, n.tl, n.t, n.tr)); return; }
    maxDark = a4k_max3(tl, t, tr); minLight = a4k_min3(br, b, bl);
    if (minLight > c && minLight > maxDark) { o_color = finish(avg(n.c, n.br, n.b, n.bl)); return; }
    // Diagonal /.
    maxDark = a4k_max3(c, l, b); minLight = a4k_min3(r, t, tr);
    if (minLight > maxDark) { o_color = finish(avg(n.c, n.r, n.t, n.tr)); return; }
    maxDark = a4k_max3(c, r, t); minLight = a4k_min3(bl, l, b);
    if (minLight > maxDark) { o_color = finish(avg(n.c, n.bl, n.l, n.b)); return; }
    // Horizontal.
    maxDark = a4k_max3(l, tl, bl); minLight = a4k_min3(r, br, tr);
    if (minLight > c && minLight > maxDark) { o_color = finish(avg(n.c, n.r, n.br, n.tr)); return; }
    maxDark = a4k_max3(r, br, tr); minLight = a4k_min3(l, tl, bl);
    if (minLight > c && minLight > maxDark) { o_color = finish(avg(n.c, n.l, n.tl, n.bl)); return; }
    // Diagonal \.
    maxDark = a4k_max3(c, l, t); minLight = a4k_min3(r, br, b);
    if (minLight > maxDark) { o_color = finish(avg(n.c, n.r, n.br, n.b)); return; }
    maxDark = a4k_max3(c, r, b); minLight = a4k_min3(t, l, tl);
    if (minLight > maxDark) { o_color = finish(avg(n.c, n.t, n.l, n.tl)); return; }
    o_color = finish(n.c);
}
