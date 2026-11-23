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

float photographic_grain_knot(uvec2 cell, uint seed) {
    float a = aurea_hash(cell ^ uvec2(seed, seed * 2654435761u));
    float b = aurea_hash(cell ^ uvec2(seed ^ 0x85ebca6bu, seed * 2246822519u + 0xc2b2ae35u));
    return (a + b - 1.0) * 0.974;
}

float photographic_grain_value(vec2 q, uint seed) {
    uvec2 cell = uvec2(ivec2(floor(q)));
    vec2 f = fract(q);
    vec2 t = f * f * (3.0 - 2.0 * f);
    float a = photographic_grain_knot(cell, seed);
    float b = photographic_grain_knot(cell + uvec2(1u, 0u), seed);
    float c = photographic_grain_knot(cell + uvec2(0u, 1u), seed);
    float d = photographic_grain_knot(cell + uvec2(1u, 1u), seed);
    return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

float photographic_grain_band(vec2 q, uint seed, int octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    for (int i = 0; i < octaves; ++i) {
        value += photographic_grain_value(q, seed + uint(i) * 0x9e3779b9u) * amplitude;
        q *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

vec4 photographic_grain(vec4 src) {
    // Frequency is layer-width relative. The lattice basis and gain fit the
    // measured spatial correlation and variance, with original seeded samples.
    const float latticeScale = 2.814;
    vec2 q = (v_uv - vec2(0.5)) * vec2(1.0, p.p0.w) * latticeScale;
    uint seed = uint(p.p3.x) | (uint(p.p3.y) << 16);
    vec3 perturbation = vec3(0.0);
    if (p.p0.x > 0.0) {
        int octaves = int(p.p0.z);
        perturbation = p.p0.x * vec3(
            photographic_grain_band(q * (p.p0.y * p.p2.x), seed ^ 0x243f6a88u, octaves),
            photographic_grain_band(q * (p.p0.y * p.p2.y), seed ^ 0xb7e15162u, octaves),
            photographic_grain_band(q * (p.p0.y * p.p2.z), seed ^ 0x9e3779b9u, octaves));
        perturbation *= aurea_linear_to_srgb(max(p.color.rgb, vec3(0.0)));
    }
    if (p.p1.x > 0.0) {
        perturbation += p.p1.x * photographic_grain_band(q * p.p1.y, seed ^ 0x3745a11du, int(p.p1.z));
    }
    vec3 encoded = aurea_linear_to_srgb(max(src.rgb, vec3(0.0)));
    return premultiply(vec4(aurea_srgb_to_linear(max(encoded + perturbation, vec3(0.0))), src.a));
}

void main() {
    vec4 src = unpremultiply(texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw));
    if (p.p2.w > 0.5) {
        o_color = photographic_grain(src);
        return;
    }
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
