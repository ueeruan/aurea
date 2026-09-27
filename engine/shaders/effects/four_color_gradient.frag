#version 450
// =============================================================================
//  Aurea / shaders / effects / four_color_gradient.frag
//
//  Degradê de 4 cores: cada pixel é a média das quatro cores pesada pelo
//  inverso da distância a cada ponto (elevado à suavidade). Os pesos são
//  normalizados: o espaço entre os pontos fica tão claro quanto eles. O piso
//  na distância é o que impede um pixel exatamente sob um ponto de dividir
//  por zero e virar um ponto brilhante.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = ponto 1, zw = ponto 2 (px da camada)
    vec4 p1;   // xy = ponto 3, zw = ponto 4
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // x = expoente da distância, y = ruído 0..1, z = opacidade, w = modo de mistura
    vec4 color;   // cor 1 (linear)
    vec4 q0;      // cor 2
    vec4 q1;      // cor 3
    vec4 q2;      // cor 4
    vec4 q3;      // x = preencher a caixa toda
} p;

void main() {
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float cover = p.q3.x > 0.5 ? 1.0 : src.a;
    const float opacity = clamp(p.p3.z, 0.0, 1.0);
    if (cover <= 0.0 || opacity <= 0.0) {
        o_color = src;
        return;
    }

    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    // Distâncias na escala da camada (a maior dimensão = 1), para o expoente
    // valer o mesmo num quadro pequeno e num 4K.
    const float unit = max(max(p.p2.z, p.p2.w), 1.0);
    const float e = max(p.p3.x, 0.05);
    vec4 w = vec4(
        1.0 / pow(max(length(point - p.p0.xy) / unit, 1e-3), e),
        1.0 / pow(max(length(point - p.p0.zw) / unit, 1e-3), e),
        1.0 / pow(max(length(point - p.p1.xy) / unit, 1e-3), e),
        1.0 / pow(max(length(point - p.p1.zw) / unit, 1e-3), e));
    w /= max(w.x + w.y + w.z + w.w, 1e-6);

    vec3 gen = p.color.rgb * w.x + p.q0.rgb * w.y + p.q1.rgb * w.z + p.q2.rgb * w.w;
    // Ruído: grão por pixel (determinístico pela posição) que quebra a banda.
    const float jitter = clamp(p.p3.y, 0.0, 1.0);
    if (jitter > 0.0) {
        const float n = aurea_hash(uvec2(ivec2(floor(point)))) - 0.5;
        gen = clamp(gen + n * jitter * 0.25, 0.0, 1.0);
    }

    const vec3 base = src.a > 1e-6 ? src.rgb / src.a : vec3(0.0);
    const vec3 blended = clamp(aurea_blend_generated(base, clamp(gen, 0.0, 1.0), int(p.p3.w + 0.5)), 0.0, 1.0);
    const vec3 outc = mix(base, blended, opacity);
    o_color = vec4(outc * cover, cover);
}
