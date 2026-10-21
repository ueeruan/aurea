#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_reduce.frag
//
//  Aprimorar, modo Automático: redução 4×4 (média) de uma textura com N
//  LADRILHOS lado a lado, cada ladrilho reduzido só com os próprios texels —
//  as estatísticas de ruído e de nitidez descem juntas sem se misturar. Na
//  última redução cada ladrilho vira um texel. Laço de tamanho fixo (16).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;   // xy = tamanho de um ladrilho da ENTRADA, zw = tamanho de um ladrilho da SAÍDA
    vec4 count;  // x = número de ladrilhos
} p;

void main() {
    const ivec2 inTile = ivec2(p.size.xy);
    const ivec2 outTile = max(ivec2(p.size.zw), ivec2(1));
    const ivec2 o = ivec2(floor(v_uv * vec2(p.size.z * p.count.x, p.size.w)));
    const int tile = o.x / outTile.x;
    const ivec2 local = ivec2(o.x - tile * outTile.x, o.y);
    vec4 acc = vec4(0.0);
    float n = 0.0;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const ivec2 q = local * 4 + ivec2(i, j);
            if (q.x >= inTile.x || q.y >= inTile.y) continue;
            acc += texelFetch(u_src, ivec2(q.x + tile * inTile.x, q.y), 0);
            n += 1.0;
        }
    }
    o_color = acc / max(n, 1.0);
}
