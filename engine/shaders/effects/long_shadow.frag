#version 450
// =============================================================================
//  Aurea / shaders / effects / long_shadow.frag
//
//  Sombra longa (0.4.6, o "flat design"): a silhueta da camada arrastada numa
//  direção por um comprimento, atrás dela. Cada pixel de saída anda para TRÁS
//  na direção e pega o maior alfa da silhueta no caminho; com "esmaecer", a
//  sombra some com a distância. Passos em px da camada, convertidos para uv
//  pela densidade da entrada (mesmo comprimento na horizontal e na vertical).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // saída → entrada
    vec4 texel;
    vec4 p0;      // xy = direção (unitária, px), z = comprimento (px), w = passos
    vec4 p1;      // x = opacidade, y = esmaecer 0..1, z = só a sombra
    vec4 p2;
    vec4 p3;      // xy = tamanho da região da ENTRADA (px da camada)
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 stepUv = p.p0.xy / max(p.p3.xy, vec2(1e-4));     // 1 px da camada em uv da entrada
    const int steps = int(clamp(p.p0.w, 1.0, 256.0));
    const float len = max(p.p0.z, 0.0);
    float shadow = 0.0;
    for (int i = 1; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const vec2 uv = inUv - stepUv * (len * t);
        float a = texture(u_tex0, uv).a;
        a *= 1.0 - clamp(p.p1.y, 0.0, 1.0) * t;
        shadow = max(shadow, a);
    }
    shadow *= clamp(p.p1.x, 0.0, 1.0) * p.color.a;
    const vec4 sh = vec4(p.color.rgb * shadow, shadow);
    const vec4 layer = texture(u_tex0, inUv);
    o_color = p.p1.z > 0.5 ? sh * (1.0 - layer.a) : layer + sh * (1.0 - layer.a);
}
