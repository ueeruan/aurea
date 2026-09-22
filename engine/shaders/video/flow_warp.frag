#version 450
// =============================================================================
//  Aurea / shaders / video / flow_warp.frag
//
//  Quadro intermediário por movimento de pixels: com o fluxo f (atual →
//  seguinte) no instante t entre os dois, o pixel p vem do atual em p − t·f e
//  do seguinte em p + (1 − t)·f; a mistura pesa pela proximidade no tempo.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_flow;   // RG = px do nível base

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 blend;   // x = t, yz = uv por pixel do nível base (converte o fluxo em uv)
} p;

void main() {
    float t = p.blend.x;
    // O fluxo mora na posição do quadro ATUAL: o pixel que chega em p no
    // instante t saiu de p − t·f. Ponto fixo (3 passos) para achar esse f.
    vec2 f = texture(u_flow, v_uv).xy * p.blend.yz;
    for (int i = 0; i < 3; ++i) f = texture(u_flow, v_uv - t * f).xy * p.blend.yz;
    vec4 a = texture(u_a, v_uv - t * f);
    vec4 b = texture(u_b, v_uv + (1.0 - t) * f);
    o_color = mix(a, b, t);
}
