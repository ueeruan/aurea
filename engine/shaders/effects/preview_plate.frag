#version 450
// =============================================================================
//  Aurea / shaders / effects / preview_plate.frag
//
//  A CARTELA DE DEMONSTRAÇÃO das prévias de efeito (Fase 7.3 §13, §15).
//
//  É a "cena" sobre a qual todo efeito é pré-visualizado no navegador. Ela não
//  é decorativa: cada faixa existe para revelar um comportamento.
//
//    - clarão branco no alto        → glow, raios, sweep, halos
//    - barras saturadas R G B C M Y → cor, croma, colorama, canal trocado
//    - escada de cinza              → exposição, níveis, curva, contraste
//    - xadrez fino                  → desfoque, nitidez, pixelização, JPEG
//    - arestas diagonais duras      → nitidez, contorno, meio-tom
//    - riscos de 1 px               → varredura de linha, moiré, sinal
//    - manchas de grão              → grão, dano de filme, ruído
//
//  Gerada no shader, sem textura de entrada: a prévia de um efeito nunca
//  depende de um arquivo, e duas execuções dão exatamente a mesma imagem.
//  O resultado sai no espaço de trabalho (linear, pré-multiplicado).
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;   // x = largura em texels, y = altura, zw livres
} p;

/// Ruído determinístico em [0,1): hash inteiro da coordenada do pixel.
float hash21(vec2 q0) {
    uvec2 q = uvec2(ivec2(floor(q0)));
    uint h = 0x811C9DC5u;
    h = (h ^ q.x) * 16777619u;
    h = (h ^ q.y) * 16777619u;
    h ^= h >> 15;
    return float(h & 0xFFFFu) / 65536.0;
}

/// Cinza da escada (0 a 1 em 8 degraus).
float stair(float x) { return floor(clamp(x, 0.0, 0.9999) * 8.0) / 7.0; }

void main() {
    vec2 uv = v_uv;
    vec2 px = p.size.xy;
    vec3 c;

    // Fundo: gradiente frio, escuro embaixo e à esquerda.
    c = mix(vec3(0.035, 0.055, 0.085), vec3(0.16, 0.22, 0.34), uv.x * 0.6 + (1.0 - uv.y) * 0.4);

    // Escada de cinza na faixa de baixo (0 → 1, oito degraus).
    if (uv.y < 0.20) c = vec3(stair(uv.x));

    // Barras saturadas no meio-baixo.
    if (uv.y >= 0.20 && uv.y < 0.36) {
        vec3 bars[6] = vec3[6](
            vec3(0.90, 0.12, 0.10), vec3(0.15, 0.85, 0.25), vec3(0.15, 0.35, 0.95),
            vec3(0.10, 0.85, 0.85), vec3(0.90, 0.20, 0.80), vec3(0.95, 0.85, 0.15));
        int k = int(clamp(floor(uv.x * 6.0), 0.0, 5.0));
        c = bars[k];
    }

    // Xadrez fino numa janela: 8 px por casa (o que um desfoque apaga).
    if (uv.x > 0.52 && uv.x < 0.78 && uv.y > 0.40 && uv.y < 0.66) {
        vec2 t = floor(uv * px / 8.0);
        c = mix(vec3(0.10), vec3(0.85), mod(t.x + t.y, 2.0));
    }

    // Arestas diagonais duras (o que a nitidez realça e o meio-tom quebra).
    if (uv.y >= 0.40 && uv.y < 0.78 && (uv.x < 0.26 || uv.x > 0.80)) {
        float d = uv.x * 0.9 + uv.y * 0.5;
        c = mod(floor(d * 26.0), 2.0) < 1.0 ? vec3(0.94) : vec3(0.06);
    }

    // Riscos verticais de 1 px: varredura, moiré, sinal.
    if (uv.y > 0.78 && uv.y < 0.86) {
        c = mix(vec3(0.06), vec3(0.92), step(0.5, fract(uv.x * px.x / 3.0)));
    }

    // Clarão: um ponto quente no alto, com halo suave.
    vec2 d = (uv - vec2(0.30, 0.86)) * vec2(1.0, 1.78);
    c += vec3(1.0, 0.97, 0.90) * exp(-dot(d, d) * 90.0) * 2.4;

    // Uma segunda luz, menor e mais fria, à direita.
    vec2 d2 = (uv - vec2(0.79, 0.90)) * vec2(1.0, 1.78);
    c += vec3(0.70, 0.86, 1.0) * exp(-dot(d2, d2) * 260.0) * 1.6;

    // Grão determinístico por cima de TUDO: o grão e o dano de filme precisam
    // de uma textura fina para agir sobre ela.
    c += vec3((hash21(uv * px) - 0.5) * 0.06);

    o_color = premultiply(vec4(srgb_to_linear(max(c, vec3(0.0))), 1.0));
}
