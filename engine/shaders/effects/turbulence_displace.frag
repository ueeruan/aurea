#version 450
// =============================================================================
//  Aurea / shaders / effects / turbulence_displace.frag
//
//  Deslocamento por turbulência (Fase 7.3 §27): um campo de ruído procedural
//  empurra cada pixel. Não é um padrão fixo: o campo EVOLUI com o tempo
//  (`evolution` × quadro) e a semente muda o desenho sem mudar o movimento.
//
//  O ruído é o mesmo `aurea_turbulence` que o Grão e o Dano de filme usam —
//  determinístico e igual em todo aparelho, então o preview e o export batem.
//
//  O deslocamento é medido em pixels da LAYER; `texel.zw` traz texels por
//  pixel de layer, então o mesmo número vale no preview reduzido e no 4K.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade (px da layer), y = tamanho do ruído (px), z = complexidade (oitavas), w = evolução (px/quadro)
    vec4 p1;   // x = deslocamento X (px), y = deslocamento Y (px), z = semente, w = 1 só na horizontal
    vec4 p2;   // x = borda (0 repetir, 1 recortar, 2 esticar), y = girar o vetor (graus)
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    // Unidades de uv por PIXEL DA LAYER: com isto, um deslocamento em pixels
    // da layer vira o mesmo número de uv no preview e no export.
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));

    const float sizePx = max(p.p0.y, 1.0);
    const float t = p.p0.w * p.p3.x;
    const vec2 layerPx = inUv / uvPerLayer;

    // Duas consultas do MESMO campo, deslocadas: sai um VETOR, não uma onda
    // numa direção só. A semente entra como deslocamento do domínio.
    const vec2 seed = vec2(p.p1.z * 13.7, p.p1.z * 7.3);
    const int octaves = int(clamp(p.p0.z, 1.0, 6.0));
    const float nx = aurea_turbulence((layerPx + vec2(0.0, t) + seed) / sizePx, octaves, 0.5) - 0.5;
    const float ny = aurea_turbulence((layerPx + vec2(31.7, 17.3) + seed + vec2(0.0, t)) / sizePx, octaves, 0.5) - 0.5;

    vec2 d = vec2(nx, ny) * (2.0 * p.p0.x) * uvPerLayer;
    d += vec2(p.p1.x, p.p1.y) * uvPerLayer;
    if (p.p1.w > 0.5) d.y = 0.0;
    if (abs(p.p2.y) > 1e-4) d = aurea_rot2(radians(p.p2.y)) * d;

    const vec2 uv = inUv + d;
    const int mode = int(p.p2.x + 0.5);
    // O tratamento de borda é sobre a COORDENADA DA ENTRADA: fora do [0,1] da
    // textura é que não há pixel para ler.
    vec2 sampleUv = uv;
    vec4 outc;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        if (mode == 1) {
            outc = vec4(0.0);
        } else if (mode == 2) {
            sampleUv = clamp(uv, 0.0, 1.0);
            outc = unpremultiply(texture(u_tex0, sampleUv));
        } else {
            outc = unpremultiply(texture(u_tex0, fract(uv)));
        }
    } else {
        outc = unpremultiply(texture(u_tex0, uv));
    }

    // A borda transparente do modo recortar vale para o alfa também: a camada
    // não pode "crescer" só na cor.
    o_color = premultiply(vec4(max(outc.rgb, vec3(0.0)), outc.a));
}
