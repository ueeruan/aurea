#version 450
// =============================================================================
//  Aurea / shaders / effects / ripple_dissolve.frag
//
//  Dissolver com ondulação (Fase 7.3 §40): a imagem some em círculos que
//  crescem do centro para fora, com a borda ondulando. Animar `progresso` de
//  0 a 1 é a transição; parado no meio, é o efeito.
//
//  O que "dissolve" aqui é o ALFA: a camada de baixo aparece pelo buraco. É o
//  que torna o efeito útil sozinho, e o que faz dele uma transição de verdade
//  quando a camada está sobre outra.
//
//  A distância é medida em pixels da LAYER e normalizada pela MEIA-DIAGONAL:
//  assim a onda é um círculo de verdade, e não uma elipse que estica junto com
//  a proporção da composição.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = progresso (0..1), y = amplitude da ondulação (px), z = comprimento da onda (px), w = suavidade da borda
    vec4 p1;   // x = centro X, y = centro Y, z = velocidade da onda, w = semente
    vec4 p2;   // x = 1 distorcer a imagem junto, y = 1 dissolver de fora para dentro
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;

    // Pixels da layer, medidos do centro; a meia-diagonal normaliza para 0..1.
    const vec2 relPx = (inUv - p.p1.xy) / uvPerLayer;
    const vec2 halfPx = vec2(0.5, 0.5) / uvPerLayer;
    const float diag = max(length(halfPx), 1e-6);
    const float rr = length(relPx) / diag;

    // A borda do círculo ondula: é o que separa isto de um dissolve comum.
    const float wavelength = max(p.p0.z, 1.0);
    const float wavePx = sin(rr * (diag / wavelength) * AUREA_TAU + p.p3.x * p.p1.z * 0.02 + p.p1.w * 6.28)
                       * p.p0.y;

    const float softness = max(p.p0.w, 0.001);
    float mask = smoothstep(p.p0.x - softness, p.p0.x + softness, rr + wavePx / diag);
    if (p.p2.y > 0.5) mask = 1.0 - mask;

    // A imagem ondula junto: um dissolve que só apaga parece um fade; com a
    // distorção, parece que a imagem está sendo sugada.
    vec2 uv = inUv;
    if (p.p2.x > 0.5) {
        uv += normalize(relPx + vec2(1e-6)) * wavePx * uvPerLayer * 9.0;
    }
    // Fora da imagem: transparente (esticar a borda inventava faixas).
    const bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
    const vec4 s = inside ? unpremultiply(texture(u_tex0, uv)) : vec4(0.0);
    o_color = premultiply(vec4(max(s.rgb, vec3(0.0)), s.a * clamp(mask, 0.0, 1.0)));
}
