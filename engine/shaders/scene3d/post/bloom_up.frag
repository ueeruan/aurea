#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / bloom_up.frag
//
//  Subida da cadeia do bloom: tenda 3×3 (9 amostras bilineares) do nível
//  menor, SOMADA ao nível de cima pelo blend aditivo do pipeline (o passe
//  carrega o conteúdo da descida). Cada nível acumula todos os de baixo: o
//  halo tem a cauda longa e macia de um espalhamento de lente, sem o "anel"
//  de um blur de raio único. A normalização (1/níveis) fica no tone map.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da ORIGEM (o nível menor), z = raio (em texels da origem)
} p;

void main() {
    const vec2 r = p.texel.xy * p.texel.z;
    vec3 s = textureLod(u_src, v_uv, 0.0).rgb * 4.0;
    s += (textureLod(u_src, v_uv + vec2(-r.x, 0.0), 0.0).rgb + textureLod(u_src, v_uv + vec2(r.x, 0.0), 0.0).rgb
        + textureLod(u_src, v_uv + vec2(0.0, -r.y), 0.0).rgb + textureLod(u_src, v_uv + vec2(0.0, r.y), 0.0).rgb) * 2.0;
    s += textureLod(u_src, v_uv + vec2(-r.x, -r.y), 0.0).rgb + textureLod(u_src, v_uv + vec2(r.x, -r.y), 0.0).rgb
       + textureLod(u_src, v_uv + vec2(-r.x, r.y), 0.0).rgb + textureLod(u_src, v_uv + vec2(r.x, r.y), 0.0).rgb;
    o_color = vec4(s * (1.0 / 16.0), 0.0);
}
