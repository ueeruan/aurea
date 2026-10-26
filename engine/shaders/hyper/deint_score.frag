#version 450
// =============================================================================
//  Aurea / shaders / hyper / deint_score.frag
//
//  Hyper Clean, detecção de entrelaçado (o modo Automático): a métrica de
//  "pente" por ladrilho de 8x8 texels — a MESMA de hyper::comb_score no C++:
//    R = média |Y − (Y↑ + Y↓)/2|    (vizinhas do campo OPOSTO)
//    G = média |Y − (Y↑↑ + Y↓↓)/2|  (vizinhas do MESMO campo)
//  em códigos de 0..255 da luma perceptual. Entrelaçado com movimento: R
//  bem maior que G. As reduções (deint_reduce.frag) levam a 1x1.
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;   // xy = tamanho da ENTRADA
} p;

float Y(ivec2 at) {
    return 255.0 * hy_luma(hy_to_perc(texelFetch(u_src, hy_clampi(at, ivec2(p.size.xy)), 0)).rgb);
}

void main() {
    const ivec2 tile = ivec2(gl_FragCoord.xy) * 8;
    const ivec2 sz = ivec2(p.size.xy);
    float inter = 0.0, intra = 0.0, n = 0.0;
    for (int y = 0; y < 8; ++y) {
        const int yy = tile.y + y;
        if (yy < 2 || yy + 2 >= sz.y) continue;
        for (int x = 0; x < 8; x += 2) {
            const int xx = tile.x + x;
            if (xx >= sz.x) continue;
            const float c = Y(ivec2(xx, yy));
            inter += abs(c - 0.5 * (Y(ivec2(xx, yy - 1)) + Y(ivec2(xx, yy + 1))));
            intra += abs(c - 0.5 * (Y(ivec2(xx, yy - 2)) + Y(ivec2(xx, yy + 2))));
            n += 1.0;
        }
    }
    // B = amostras (as reduções ponderam por elas).
    o_color = n > 0.0 ? vec4(inter / n, intra / n, n, 1.0) : vec4(0.0);
}
