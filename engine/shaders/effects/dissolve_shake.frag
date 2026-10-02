#version 450
// =============================================================================
//  Aurea / shaders / effects / dissolve_shake.frag
//
//  Tremor dissolvente: a camada treme e se quebra em fragmentos de ruído.
//
//  A saída é dividida em células (o "fragmento") numa grade irregular: cada
//  fileira anda um pedaço sorteado, então os blocos não formam um tabuleiro.
//  Cada célula sorteia, por (célula, semente, época), se SE SOLTOU (chance =
//  dissolução), a direção e a distância do voo, um tremor próprio
//  (aleatoriedade) e quanto fica transparente. As soltas que caem abaixo de
//  dissolução² somem; dentro delas um grão fino come pedaços (areia).
//
//  O pixel lê a entrada em (px − deslocamento da célula): é o fragmento
//  deslocado que aparece ali. Tudo em px da CAMADA (`texel.zw`), o mesmo
//  desenho no preview reduzido e no export.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 outRegion;   // região da saída em px da camada
    vec4 inRegion;    // região da entrada em px da camada
    vec4 a;           // xy = tremor global (px), z = tamanho do fragmento (px), w = dispersão (px)
    vec4 b;           // x = dissolução 0..1, y = aleatoriedade 0..1, z = transparência 0..1, w = época
    vec4 c;           // x = semente, y = eixos (0 os dois, 1 horizontal, 2 vertical), z = mistura, w = amplitude (px)
} p;

vec4 sample_at(vec2 layerPx) {
    const vec2 uv = (layerPx - p.inRegion.xy) / p.inRegion.zw;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return vec4(0.0);
    return texture(u_tex0, uv);
}

void main() {
    const vec2 px = p.outRegion.xy + v_uv * p.outRegion.zw;
    const vec4 original = sample_at(px);

    const float size = max(p.a.z, 1.0);
    const uint seed = uint(max(p.c.x, 0.0));
    const uint epoch = uint(max(p.b.w, 0.0));
    const uint salt = seed * 977u + epoch * 7919u + 1u;

    // Grade irregular: cada fileira desliza um pedaço sorteado.
    const float row = floor(px.y / size);
    const float shift = aurea_hash(uvec2(ivec2(int(row), 0)) ^ uvec2(salt, salt * 31u)) * size;
    const vec2 cell = vec2(floor((px.x + shift) / size), row);
    const uvec2 ci = uvec2(ivec2(cell));
    float h[7];
    for (int i = 0; i < 7; ++i) h[i] = aurea_hash(ci ^ uvec2(salt * uint(2 * i + 3), salt + uint(i) * 0x9E3779B9u));

    const float dissolve = clamp(p.b.x, 0.0, 1.0);
    const float rnd = clamp(p.b.y, 0.0, 1.0);
    const int axes = int(p.c.y + 0.5);

    // Tremor: o global, com escala e um empurrão próprios da célula.
    vec2 d = p.a.xy * mix(1.0, 0.4 + 1.2 * h[4], rnd);
    d += (vec2(h[2], h[3]) * 2.0 - 1.0) * rnd * p.c.w * 0.5;

    float alpha = 1.0;
    const bool broken = h[0] < dissolve;
    if (broken) {
        const float ang = AUREA_TAU * h[1];
        d += vec2(cos(ang), sin(ang)) * p.a.w * (0.35 + 0.65 * h[5]);
        alpha = 1.0 - clamp(p.b.z, 0.0, 1.0) * (0.3 + 0.7 * h[6]);
        if (h[0] < dissolve * dissolve * 0.6) alpha = 0.0;
        // Grão fino dentro do fragmento solto: a borda vira areia.
        const float g = aurea_hash3(floor(px / max(size * 0.25, 1.0)), salt + 5u);
        if (g < dissolve * 0.5) alpha = 0.0;
    }
    if (axes == 1) d.y = 0.0;
    if (axes == 2) d.x = 0.0;

    const vec4 moved = sample_at(px - d) * alpha;
    o_color = mix(original, moved, clamp(p.c.z, 0.0, 1.0));
}
