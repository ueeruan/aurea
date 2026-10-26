#version 450
// =============================================================================
//  Aurea / shaders / hyper / deint_reduce.frag
//
//  Redução 4x4 das médias de pente (RG), ponderada pelas amostras (B).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;   // xy = tamanho da ENTRADA
} p;

void main() {
    const ivec2 base = ivec2(gl_FragCoord.xy) * 4;
    const ivec2 sz = ivec2(p.size.xy);
    vec2 sum = vec2(0.0);
    float n = 0.0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const ivec2 at = base + ivec2(x, y);
            if (at.x >= sz.x || at.y >= sz.y) continue;
            const vec4 v = texelFetch(u_src, at, 0);
            sum += v.rg * v.b;
            n += v.b;
        }
    o_color = n > 0.0 ? vec4(sum / n, n, 1.0) : vec4(0.0);
}
