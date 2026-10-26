#version 450
// =============================================================================
//  Aurea / shaders / hyper / me_median.frag
//
//  Mediana vetorial 3x3 do campo de blocos: fica o vetor (dos 9) com a menor
//  soma de distâncias aos outros. Tira o bloco "perdido" (textura repetida,
//  área lisa) sem borrar a fronteira entre dois movimentos.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_field;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;   // xy = fw, fh
} p;

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const ivec2 sz = ivec2(p.size.xy);
    vec4 v[9];
    int k = 0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) v[k++] = texelFetch(u_field, clamp(at + ivec2(x, y), ivec2(0), sz - 1), 0);
    float best = 1e9;
    vec4 pick = v[4];
    for (int i = 0; i < 9; ++i) {
        float d = 0.0;
        for (int j = 0; j < 9; ++j) d += abs(v[i].x - v[j].x) + abs(v[i].y - v[j].y);
        // Empate: o próprio bloco.
        if (i == 4) d -= 1e-4;
        if (d < best) { best = d; pick = v[i]; }
    }
    o_color = pick;
}
