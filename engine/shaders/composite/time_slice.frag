#version 450
// =============================================================================
//  Aurea / shaders / composite / time_slice.frag
//
//  Uma faixa do TimeSlice: a mesma amostragem de composite/layer.frag, mas só
//  dentro da faixa `slice.z` de `slice.w`. O renderer desenha a camada uma vez
//  por faixa (cada uma com a textura do SEU instante) somando no acumulador
//  temporal; com "Interpolar quadros", duas amostras por faixa com pesos que
//  somam 1.
//
//  O eixo das faixas chega já normalizado em uv da região (C++):
//      t = dot(v_uv − 0.5, slice.xy) + 0.5  ∈ [0, 1]
//  cobre a projeção inteira do retângulo da camada na direção escolhida, em
//  qualquer ângulo e proporção. Faixa = floor(t · N).
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = peso da amostra
    vec4 slice;    // xy = eixo em uv (normalizado), z = índice da faixa, w = número de faixas
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

void main() {
    const float count = max(pc.slice.w, 1.0);
    const float t = clamp(dot(v_uv - vec2(0.5), pc.slice.xy) + 0.5, 0.0, 0.999999);
    const float index = floor(t * count);
    if (abs(index - pc.slice.z) > 0.5) discard;
    o_color = texture(u_tex0, v_uv) * pc.params.x;
}
