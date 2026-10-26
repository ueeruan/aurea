#version 450
// =============================================================================
//  Aurea / shaders / scene3d / motion / tile_max.frag
//
//  Maior velocidade de cada bloco K×K do buffer de velocidade, em duas
//  passadas separáveis (K texels por fragmento em vez de K²):
//    eixo 0: (W/K × H)   ← K texels na horizontal do buffer cheio
//    eixo 1: (W/K × H/K) ← K texels na vertical da passada anterior
//  "Maior" = o vetor mais longo (a direção dominante do bloco).
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_tile;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 cfg;   // x = K (px), y = eixo (0 horizontal, 1 vertical), zw = tamanho da fonte (texels)
} p;

void main() {
    const int k = int(p.cfg.x + 0.5);
    const ivec2 size = ivec2(p.cfg.zw + 0.5);
    const ivec2 here = ivec2(gl_FragCoord.xy);
    const bool vertical = p.cfg.y > 0.5;
    vec2 best = vec2(0.0);
    float bestLen = 0.0;
    for (int i = 0; i < k; ++i) {
        ivec2 at = vertical ? ivec2(here.x, here.y * k + i) : ivec2(here.x * k + i, here.y);
        if (at.x >= size.x || at.y >= size.y) break;
        vec2 v = texelFetch(u_src, at, 0).xy;
        float len = dot(v, v);
        if (len > bestLen) { bestLen = len; best = v; }
    }
    o_tile = vec4(best, 0.0, 1.0);
}
