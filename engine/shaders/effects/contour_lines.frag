#version 450
// =============================================================================
//  Aurea / shaders / effects / contour_lines.frag
//
//  Curvas de nível (0.4.6): linhas de mesma luminância, como num mapa
//  topográfico. A largura da linha é medida em pixels pela derivada da
//  luminância (linha uniforme em áreas planas ou íngremes).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = níveis, y = espessura (px), z = deslocamento das faixas 0..1, w = fundo (0 imagem, 1 transparente)
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = texture(u_tex0, inUv);
    const vec3 straight = src.a > 1e-4 ? src.rgb / src.a : vec3(0.0);
    const float l = aurea_linear_to_srgb(clamp(aurea_luma(straight), 0.0, 1.0));
    const float v = l * max(p.p0.x, 1.0) + p.p0.z;
    const float fw = max(fwidth(v), 1e-4);
    const float dist = abs(fract(v - 0.5) - 0.5) / fw;          // px até a linha mais perto
    const float line = (1.0 - smoothstep(p.p0.y * 0.5, p.p0.y * 0.5 + 1.0, dist)) * src.a * p.color.a;
    const vec4 ink = vec4(p.color.rgb * line, line);
    const vec4 base = p.p0.w > 0.5 ? vec4(0.0) : src;
    o_color = ink + base * (1.0 - ink.a);
}
