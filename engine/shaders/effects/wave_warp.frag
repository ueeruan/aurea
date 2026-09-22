#version 450
// =============================================================================
//  Aurea / shaders / effects / wave_warp.frag
//
//  Distorção em onda (Fase 7.3 §36): uma senoide desloca a imagem na direção
//  escolhida. A onda CORRE (`velocidade` × quadro) e pode ser travada nas
//  bordas — que é como se prende o efeito a um objeto em vez de deixá-lo
//  passar por cima de tudo.
//
//  Direção: horizontal, vertical, diagonal e as duas cruzadas. A onda
//  quadrada troca a senoide por um degrau, que dá o rasgo duro.
//
//  Tudo medido em pixels da LAYER: o mesmo número vale no preview e no export.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = altura da onda (px da layer), y = largura de onda (px), z = velocidade (px/quadro), w = fase (graus)
    vec4 p1;   // x = direção (0 horiz, 1 vert, 2 diag, 3 horiz+vert), y = 1 quadrada, z = borda, w = 1 travar nas bordas
    vec4 p2;   // x = centro X da onda (0..1), y = centro Y (0..1)
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

/// Uma senoide (ou degrau) de período `wavelength`, andando `travel`.
float wave_at(float along, float wavelength, float travel, float phase, bool square) {
    const float a = (along + travel) / wavelength * AUREA_TAU + phase;
    return square ? (sin(a) >= 0.0 ? 1.0 : -1.0) : sin(a);
}

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 layerPx = v_uv / uvPerLayer;
    const vec2 uvn = v_uv;

    const float wavelength = max(p.p0.y, 1.0);
    const float travel = p.p0.z * p.p3.x;
    const float phase = radians(p.p0.w);
    const bool square = p.p1.y > 0.5;

    const int dir = int(p.p1.x + 0.5);
    const float h = p.p0.x;
    vec2 d = vec2(0.0);
    if (dir == 0 || dir == 3) d.x += wave_at(layerPx.y, wavelength, travel, phase, square) * h;
    if (dir == 1 || dir == 3) d.y += wave_at(layerPx.x, wavelength, travel, phase, square) * h;
    if (dir == 2) {
        // Diagonal: a onda anda no eixo perpendicular à diagonal, e o
        // deslocamento se divide entre os dois eixos.
        const float along = (layerPx.x - layerPx.y) * 0.7071;
        const float w = wave_at(along, wavelength, travel, phase, square) * h * 0.7071;
        d += vec2(w, -w);
    }

    // Travar nas bordas: a onda some perto da caixa, como se a imagem
    // estivesse presa dentro dela.
    if (p.p1.w > 0.5) {
        const float edge = min(min(uvn.x, 1.0 - uvn.x), min(uvn.y, 1.0 - uvn.y));
        d *= smoothstep(0.0, 0.12, edge);
    }
    d *= uvPerLayer;

    const vec2 uv = uvn + d;
    vec4 outc;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        const int mode = int(p.p1.z + 0.5);
        outc = mode == 1 ? vec4(0.0)
             : mode == 2 ? unpremultiply(texture(u_tex0, clamp(uv, 0.0, 1.0)))
                         : unpremultiply(texture(u_tex0, fract(uv)));
    } else {
        outc = unpremultiply(texture(u_tex0, uv));
    }
    o_color = premultiply(vec4(max(outc.rgb, vec3(0.0)), outc.a));
}
