// =============================================================================
//  Aurea / shaders / hyper / a4k_common.glsl
//
//  Hyper Upscale (Anime, Turbo): as peças do realce de linhas. A ideia —
//  "empurrar" as cores claras sobre as linhas escuras para afiná-las e depois
//  empurrar pelas bordas do gradiente — é a do Anime4K 0.9 (bloc97,
//  licença MIT; aviso em engine/assets/licenses/THIRD_PARTY_HYPER.md). Este
//  GLSL é próprio do Aurea.
//
//  Vizinhança 3x3 por texelFetch (preso à borda):
//      tl t tr
//      l  c  r
//      bl b br
// =============================================================================
#ifndef AUREA_HYPER_A4K_GLSL
#define AUREA_HYPER_A4K_GLSL

struct A4kNb { vec4 tl, t, tr, l, c, r, bl, b, br; };

A4kNb a4k_load(sampler2D tex, ivec2 at, ivec2 size) {
    A4kNb n;
    n.tl = texelFetch(tex, hy_clampi(at + ivec2(-1, -1), size), 0);
    n.t  = texelFetch(tex, hy_clampi(at + ivec2(0, -1), size), 0);
    n.tr = texelFetch(tex, hy_clampi(at + ivec2(1, -1), size), 0);
    n.l  = texelFetch(tex, hy_clampi(at + ivec2(-1, 0), size), 0);
    n.c  = texelFetch(tex, at, 0);
    n.r  = texelFetch(tex, hy_clampi(at + ivec2(1, 0), size), 0);
    n.bl = texelFetch(tex, hy_clampi(at + ivec2(-1, 1), size), 0);
    n.b  = texelFetch(tex, hy_clampi(at + ivec2(0, 1), size), 0);
    n.br = texelFetch(tex, hy_clampi(at + ivec2(1, 1), size), 0);
    return n;
}

float a4k_min3(float a, float b, float c) { return min(min(a, b), c); }
float a4k_max3(float a, float b, float c) { return max(max(a, b), c); }

#endif
