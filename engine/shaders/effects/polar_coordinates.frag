#version 450
// =============================================================================
//  Aurea / shaders / effects / polar_coordinates.frag
//
//  Coordenadas polares (0.4.6): modo 0 = retangular → polar (a imagem vira um
//  disco: a borda de cima vai para o centro), modo 1 = polar → retangular (o
//  inverso: um disco aberto em faixa). Interpolação entre a imagem e o
//  resultado pela quantidade.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = modo (0 ret→polar, 1 polar→ret), y = quantidade 0..1, z = rotação (voltas)
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 uv;
    if (p.p0.x < 0.5) {
        // Saída polar: o pixel é (raio, ângulo) em volta do centro; lê a imagem retangular.
        const vec2 d = inUv - 0.5;
        const float r = length(d) * 2.0;                       // 0 no centro, 1 na borda do quadrado
        const float ang = atan(d.x, -d.y) / AUREA_TAU + 0.5 + p.p0.z;
        uv = vec2(fract(ang), r);
    } else {
        // Saída retangular: x = ângulo, y = raio; lê a imagem polar.
        const float ang = (inUv.x - 0.5 - p.p0.z) * AUREA_TAU;
        const float r = inUv.y * 0.5;
        uv = vec2(0.5 + sin(ang) * r, 0.5 - cos(ang) * r);
    }
    const vec4 polar = (uv.y > 1.0 || uv.y < 0.0) ? vec4(0.0) : texture(u_tex0, uv);
    o_color = mix(texture(u_tex0, inUv), polar, clamp(p.p0.y, 0.0, 1.0));
}
