#version 450
// =============================================================================
//  Aurea / shaders / effects / drop_shadow_combine.frag
//
//  Sombra projetada, 2º de 2 passes: fecha o borrão no eixo PERPENDICULAR (a
//  silhueta deslocada e já borrada chega em `u_tex0`, na região do efeito) e
//  compõe a sombra ATRÁS da camada, que chega intacta em `u_tex1`, na região
//  dela — daí o segundo mapa de uv, em `p2`, que só `u_tex1` usa.
//
//  O passo chega em pixels da camada e vira uv com `p3` (aqui, o tamanho da
//  própria região do efeito, que é o espaço de `u_tex0`).
//
//  A composição é a do "over" com a sombra embaixo, a mesma conta do renderer
//  em alfa pré-multiplicado: o que a camada cobre fica com a camada, e a
//  franja que ela não cobre recebe a sombra. Uma sombra por cima de um pixel
//  opaco seria um erro, e é a multiplicação por (1 - a) que o impede.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // silhueta borrada (alfa em .a)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada como o efeito a recebeu

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // região do efeito → região da silhueta (identidade: mesma região)
    vec4 texel;
    vec4 p0;      // x,y = passo por amostra, z = só a sombra, w = opacidade
    vec4 p1;
    vec4 p2;      // mapa de uv da camada: x,y = escala, z,w = deslocamento
    vec4 p3;      // x,y = px de layer por uv da silhueta (largura, altura da região)
    vec4 color;   // cor da sombra, linear
} p;

const int kTaps = 8;

void main() {
    const vec2 uvPerPx = vec2(1.0) / max(p.p3.xy, vec2(1e-6));
    const vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 axis = p.p0.xy * uvPerPx;
    float sum = 0.0, weight = 0.0;
    for (int i = -kTaps; i <= kTaps; ++i) {
        const float w = exp(-3.0 * float(i * i) / float(kTaps * kTaps));
        sum += texture(u_tex0, uv + axis * float(i)).a * w;
        weight += w;
    }
    const float shadowA = (sum / max(weight, 1e-6)) * clamp(p.p0.w, 0.0, 1.0) * clamp(p.color.a, 0.0, 1.0);
    const vec4 shadow = vec4(p.color.rgb * shadowA, shadowA);
    if (p.p0.z > 0.5) { o_color = shadow; return; }
    // The expanded region extends beyond the clip. Clamping its edge pixels
    // would repeat opaque video over the shadow and hide it completely.
    const vec2 layerUV = v_uv * p.p2.xy + p.p2.zw;
    const bool inside = all(greaterThanEqual(layerUV, vec2(0.0))) && all(lessThanEqual(layerUV, vec2(1.0)));
    const vec4 layer = inside ? texture(u_tex1, layerUV) : vec4(0.0);   // pré-multiplicado
    o_color = layer + shadow * (1.0 - layer.a);
}
