#version 450
// =============================================================================
//  Aurea / shaders / effects / voronoi_cells.frag
//
//  Células (0.4.6, Voronoi): pontos sorteados numa grade (um por célula,
//  determinístico); cada pixel olha os 9 vizinhos e acha o ponto mais perto
//  (F1) e o segundo (F2). Modos: 0 = bordas (F2 − F1 fino = linha), 1 = cor
//  da imagem pela célula (mosaico de vitral), 2 = distância (bolhas).
//  `evolução` move os pontos em círculos (animação contínua).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = tamanho da célula (px), y = modo, z = evolução, w = espessura da borda (px)
    vec4 p1;      // x = mistura, y = aleatoriedade 0..1
    vec4 p2;
    vec4 p3;      // xy = tamanho da região (px)
    vec4 color;   // cor da borda
} p;

vec2 cell_point(vec2 cell) {
    const float h1 = aurea_hash3(cell, 11u), h2 = aurea_hash3(cell, 23u), h3 = aurea_hash3(cell, 37u);
    const float a = AUREA_TAU * (h3 + p.p0.z * 0.15);
    return 0.5 + (vec2(h1, h2) - 0.5) * clamp(p.p1.y, 0.0, 1.0) + vec2(cos(a), sin(a)) * 0.15 * clamp(p.p1.y, 0.0, 1.0);
}

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 base = texture(u_tex0, inUv);
    const float size = max(p.p0.x, 2.0);
    const vec2 pos = inUv * p.p3.xy / size;
    const vec2 cell = floor(pos);
    float f1 = 1e9, f2 = 1e9;
    vec2 nearest = cell;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2 c = cell + vec2(x, y);
            const float d = length(c + cell_point(c) - pos);
            if (d < f1) { f2 = f1; f1 = d; nearest = c; } else if (d < f2) { f2 = d; }
        }
    }
    vec4 result;
    const int mode = int(p.p0.y + 0.5);
    if (mode == 0) {
        const float w = max(p.p0.w, 0.5) / size;
        const float edge = 1.0 - smoothstep(w * 0.5, w * 0.5 + 1.0 / size, f2 - f1);
        const float a = edge * p.color.a;
        result = vec4(p.color.rgb * a, a) + base * (1.0 - a);
    } else if (mode == 1) {
        const vec2 centerUv = (nearest + cell_point(nearest)) * size / p.p3.xy;
        result = texture(u_tex0, centerUv);
    } else {
        result = base * clamp(1.0 - f1, 0.0, 1.0);
    }
    o_color = mix(base, result, clamp(p.p1.x, 0.0, 1.0));
}
