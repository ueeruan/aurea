#version 450
// =============================================================================
//  Aurea / shaders / effects / grain.frag
//
//  Grão de filme (Fase 7.3 §46). Não é ruído branco por cima: grão de filme
//  tem TAMANHO (o cristal), tem grão de luma e grão de cor separados, e vive
//  nas sombras — nas altas luzes o cristal já saturou.
//
//  A semente troca por quadro (`p2.w` = quadro local), então o grão FERVE como
//  o de verdade. Com semente fixa, ele fica parado — útil para textura.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade (0..1), y = tamanho do grão (px), z = grão de cor (0..1), w = rugosidade
    vec4 p1;   // x = sombras (0..1), y = altas luzes, z = semente, w = animado (0/1)
    vec4 p2;   // x = monocromático (0/1), y = suavizar, zw livres
    vec4 p3;   // x = quadro local (para a semente andar)
    vec4 color;
} p;

/// Um cristal de grão: dois valores independentes por posição, um por luma e
/// um por croma, cada um com a própria semente.
vec3 grain_at(vec2 px, float seed, float sizePx) {
    const vec2 q = px / max(sizePx, 0.5);
    const uint s = uint(seed) * 2654435761u;
    const float a = aurea_hash(uvec2(ivec2(floor(q))) ^ uvec2(s, 0u)) * 2.0 - 1.0;
    const float b = aurea_hash(uvec2(ivec2(floor(q)) + ivec2(7919, 104729)) ^ uvec2(s, 0u)) * 2.0 - 1.0;
    const float c = aurea_hash(uvec2(ivec2(floor(q)) + ivec2(31337, 7)) ^ uvec2(s, 0u)) * 2.0 - 1.0;
    return vec3(a, b, c);
}

void main() {
    vec4 src = unpremultiply(texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw));
    vec3 c = src.rgb;
    float alpha = src.a;

    // Em pixels da LAYER: o cristal tem o mesmo tamanho no preview e no export.
    const vec2 px = v_uv / max(p.texel.xy, vec2(1e-6)) / max(p.texel.zw, vec2(1e-6));
    float seed = p.p1.z + (p.p1.w > 0.5 ? p.p3.x : 0.0);
    vec3 n = grain_at(px, seed, p.p0.y);

    // Grão de cor: cada canal com o próprio cristal; `p0.z` diz quanto do
    // croma entra. Monocromático é o mesmo cristal nos três canais.
    vec3 gn = vec3(n.x);
    if (p.p2.x < 0.5) gn = mix(vec3(n.x), vec3(n.x, n.y, n.z), clamp(p.p0.z, 0.0, 1.0));

    // Onde o grão aparece: cheio nas sombras, sumindo nas altas luzes (o
    // cristal já foi exposto). É o que faz o grão parecer filme e não estática.
    const float luma = clamp(aurea_luma(aurea_linear_to_srgb(max(c, vec3(0.0)))), 0.0, 1.0);
    const float lo = clamp(p.p1.x, 0.0, 1.0);
    const float hi = clamp(p.p1.y, 0.0, 1.0);
    float weight = smoothstep(0.0, 1.0, luma);
    weight = mix(1.0, weight, hi);            // atenuar nas luzes
    weight *= mix(1.0, luma * 2.0 + 0.15, lo); // e reforçar nas sombras

    // Rugosidade: >1 deixa o grão duro (cristal grande e salpicado), <1 macio.
    gn = sign(gn) * pow(abs(gn), vec3(max(p.p0.w, 0.05)));

    vec3 outC = c + gn * p.p0.x * weight;
    // O grão é aditivo em linear, mas o olho vê o resultado codificado: sem o
    // corte aqui, as sombras ganham um azul que não existe.
    outC = max(outC, vec3(0.0));
    o_color = premultiply(vec4(outC, alpha));
}
