#version 450
// =============================================================================
//  Aurea / shaders / scene3d / motion / neighbor_max.frag
//
//  A maior velocidade entre o bloco e os 8 vizinhos. Um objeto que anda
//  espalha até meio rastro para FORA da própria silhueta — sobre pixels cujo
//  bloco não tem velocidade nenhuma. Com o rastro limitado a 2× o bloco, a
//  vizinhança 3×3 sempre alcança o pixel que recebe o borrão.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_tile;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tiles;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 cfg;   // xy = tamanho da grade de blocos
} p;

void main() {
    const ivec2 size = ivec2(p.cfg.xy + 0.5);
    const ivec2 here = ivec2(gl_FragCoord.xy);
    vec2 best = vec2(0.0);
    float bestLen = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 at = clamp(here + ivec2(x, y), ivec2(0), size - 1);
            vec2 v = texelFetch(u_tiles, at, 0).xy;
            float len = dot(v, v);
            if (len > bestLen) { bestLen = len; best = v; }
        }
    }
    o_tile = vec4(best, 0.0, 1.0);
}
