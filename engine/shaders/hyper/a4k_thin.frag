#version 450
// =============================================================================
//  Aurea / shaders / hyper / a4k_thin.frag
//
//  Hyper Upscale (Anime, Turbo), passo 1: AFINA AS LINHAS. Em cada uma das 4
//  orientações (vertical, horizontal e as 2 diagonais), se um lado da
//  vizinhança é inteiro mais claro que o centro e que o lado oposto, o centro
//  é uma linha escura sendo atravessada: mistura-se a média do lado claro
//  (força `strength`) e fica a opção mais clara. O traço que o upscale
//  engordou volta à largura do desenho. Perceptual -> perceptual.
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"
#include "a4k_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho
    vec4 flags;   // x = força (0..1)
} p;

void consider(inout vec4 best, inout float bestL, vec4 c, float s, vec4 a, vec4 b, vec4 d) {
    const vec4 cand = mix(c, (a + b + d) / 3.0, s);
    const float l = hy_luma(cand.rgb);
    if (l > bestL) { best = cand; bestL = l; }
}

void main() {
    const ivec2 sz = ivec2(p.size.xy);
    const A4kNb n = a4k_load(u_src, ivec2(gl_FragCoord.xy), sz);
    const float s = p.flags.x;
    const float tl = hy_luma(n.tl.rgb), t = hy_luma(n.t.rgb), tr = hy_luma(n.tr.rgb);
    const float l = hy_luma(n.l.rgb), c = hy_luma(n.c.rgb), r = hy_luma(n.r.rgb);
    const float bl = hy_luma(n.bl.rgb), b = hy_luma(n.b.rgb), br = hy_luma(n.br.rgb);
    vec4 best = n.c;
    float bestL = c;
    // Vertical: cima x baixo.
    float maxDark = a4k_max3(br, b, bl), minLight = a4k_min3(tl, t, tr);
    if (minLight > c && minLight > maxDark) consider(best, bestL, n.c, s, n.tl, n.t, n.tr);
    else {
        maxDark = a4k_max3(tl, t, tr); minLight = a4k_min3(br, b, bl);
        if (minLight > c && minLight > maxDark) consider(best, bestL, n.c, s, n.br, n.b, n.bl);
    }
    // Diagonal / (canto de cima à direita x canto de baixo à esquerda).
    maxDark = a4k_max3(c, l, b); minLight = a4k_min3(r, t, tr);
    if (minLight > maxDark) consider(best, bestL, n.c, s, n.r, n.t, n.tr);
    else {
        maxDark = a4k_max3(c, r, t); minLight = a4k_min3(bl, l, b);
        if (minLight > maxDark) consider(best, bestL, n.c, s, n.bl, n.l, n.b);
    }
    // Horizontal: esquerda x direita.
    maxDark = a4k_max3(l, tl, bl); minLight = a4k_min3(r, br, tr);
    if (minLight > c && minLight > maxDark) consider(best, bestL, n.c, s, n.r, n.br, n.tr);
    else {
        maxDark = a4k_max3(r, br, tr); minLight = a4k_min3(l, tl, bl);
        if (minLight > c && minLight > maxDark) consider(best, bestL, n.c, s, n.l, n.tl, n.bl);
    }
    // Diagonal \ .
    maxDark = a4k_max3(c, l, t); minLight = a4k_min3(r, br, b);
    if (minLight > maxDark) consider(best, bestL, n.c, s, n.r, n.br, n.b);
    else {
        maxDark = a4k_max3(c, r, b); minLight = a4k_min3(t, l, tl);
        if (minLight > maxDark) consider(best, bestL, n.c, s, n.t, n.l, n.tl);
    }
    o_color = vec4(best.rgb, n.c.a);
}
