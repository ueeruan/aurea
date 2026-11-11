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
    vec4 p1;   // x = direção (0 horiz, 1 vert, 2 diag, 3 horiz+vert, 4 ângulo), y = quadrada, z = borda, w = travar
    vec4 p2;   // xy = direção angular unitária; zw = origem da entrada em px da camada
    vec4 p3;   // x = quadro local, y = tipo de onda AE 2026 (0 = seno/quadrada de p1.y; 2 triângulo,
               // 3 dente de serra, 4 círculo, 5 semicírculo, 6 semicírculo invertido, 7 ruído, 8 ruído suave)
    vec4 color;
} p;

/// Uma senoide (ou degrau) de período `wavelength`, andando `travel`.
float wave_at(float along, float wavelength, float travel, float phase, bool square) {
    const float a = (along + travel) / wavelength * AUREA_TAU + phase;
    const int type = int(p.p3.y + 0.5);
    if (type < 2) return square ? (sin(a) >= 0.0 ? 1.0 : -1.0) : sin(a);
    // AE 2026: as outras formas, todas com o mesmo período e a mesma fase do
    // seno (0 em x = 0, subindo), amplitude −1..1.
    const float x = fract(a / AUREA_TAU);
    if (type == 2) return 4.0 * abs(fract(x + 0.75) - 0.5) - 1.0;                 // triângulo
    if (type == 3) return 2.0 * fract(x + 0.5) - 1.0;                             // dente de serra
    if (type == 4) {                                                              // círculo
        const float h = x < 0.5 ? 4.0 * x - 1.0 : 4.0 * x - 3.0;
        const float y = sqrt(max(1.0 - h * h, 0.0));
        return x < 0.5 ? y : -y;
    }
    if (type == 5) { const float h = 2.0 * x - 1.0; return 2.0 * sqrt(max(1.0 - h * h, 0.0)) - 1.0; }   // semicírculo
    if (type == 6) { const float h = 2.0 * x - 1.0; return 1.0 - 2.0 * sqrt(max(1.0 - h * h, 0.0)); }   // invertido
    const float cell = a / AUREA_TAU * 2.0;                                       // meia onda por sorteio
    if (type == 7) return aurea_hash2(vec2(floor(cell), 17.0)) * 2.0 - 1.0;       // ruído
    const float i = floor(cell), f = fract(cell);
    const float s = f * f * (3.0 - 2.0 * f);
    return mix(aurea_hash2(vec2(i, 17.0)), aurea_hash2(vec2(i + 1.0, 17.0)), s) * 2.0 - 1.0;   // ruído suave
}

void main() {
    // A saída (região espalhada pela altura da onda) e a entrada NÃO cobrem a
    // mesma área: o ponto de saída vai à entrada pelo uvMap. Ler a entrada no
    // uv da SAÍDA esticava a camada até a região espalhada e, depois de um
    // Motion Tile (entrada maior que o quadro), deslocava a parede e abria
    // faixa preta na borda.
    const vec2 inSize = 1.0 / max(p.texel.xy * p.uvMap.xy, vec2(1e-9));    // px da camada da entrada
    const vec2 uvPerLayer = 1.0 / inSize;
    const vec2 uvIn = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 layerPx = p.p2.zw + uvIn * inSize;                          // px ABSOLUTOS da camada
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
    if (dir == 4) {
        // A rotated basis: the displacement follows the chosen direction,
        // and phase varies along its perpendicular. Zero degrees retains
        // the horizontal contract; existing modes above remain unchanged.
        const vec2 axis = p.p2.xy;
        const float along = dot(layerPx, vec2(-axis.y, axis.x));
        d = axis * wave_at(along, wavelength, travel, phase, square) * h;
    }

    // Travar nas bordas: a onda some perto da caixa, como se a imagem
    // estivesse presa dentro dela.
    if (p.p1.w > 0.5) {
        const float edge = min(min(uvn.x, 1.0 - uvn.x), min(uvn.y, 1.0 - uvn.y));
        d *= smoothstep(0.0, 0.12, edge);
    }
    d *= uvPerLayer;

    const vec2 uv = uvIn + d;
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
