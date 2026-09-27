#version 450
// =============================================================================
//  Aurea / shaders / effects / audio_spectrum.frag
//
//  Espectro de áudio: desenha as faixas do som deste quadro (u_tex1, uma
//  textura `faixas`×1: R = magnitude 0..1) ao longo de um caminho no plano
//  da camada — a reta do ponto inicial ao final, ou um círculo com o inicial
//  no centro e o final marcando o raio e onde a faixa 0 começa.
//
//  Cada pixel se descreve pelo caminho: `u` (0..1 do início ao fim), `v`
//  (distância com sinal, lado A positivo) e `along` (o comprimento do caminho
//  por unidade de u AQUI — a reta inteira, ou a circunferência neste raio),
//  para a largura de uma barra estar na mesma unidade da altura dela.
//
//  Toda borda tem um pixel de antialiasing, mais a suavidade pedida.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // o espectro (faixas × 1)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = ponto inicial, zw = ponto final (px da camada)
    vec4 p1;   // x = altura máxima (px), y = espessura (px), z = suavidade 0..1, w = faixas
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // x = exibição (0 barras, 1 linhas, 2 pontos), y = lado (0 A, 1 B, 2 os dois), z = em círculo, w = compor sobre o original
    vec4 color;   // cor de dentro (linear)
    vec4 q0;      // cor de fora (linear)
    vec4 q1;      // x = giro de matiz (voltas), y = 1 há espectro
    vec4 q2;
    vec4 q3;
} p;

/// Magnitude da faixa `i` de `n`, 0..1. Fora da linha (i < 0, i ≥ n) não há
/// faixa: silêncio.
float band_at(float i, float n) {
    if (p.q1.y < 0.5 || i < 0.0 || i >= n) return 0.0;
    return texture(u_tex1, vec2((i + 0.5) / n, 0.5)).r;
}

void main() {
    const vec4 base = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float n = max(floor(p.p1.w + 0.5), 1.0);
    const vec2 P0 = p.p0.xy;
    const vec2 P1 = p.p0.zw;

    float u, v, along;
    if (p.p3.z < 0.5) {
        const vec2 d = P1 - P0;
        const float L = max(length(d), 1e-4);
        const vec2 dir = d / L;
        const vec2 rel = point - P0;
        u = dot(rel, dir) / L;
        v = dot(rel, vec2(dir.y, -dir.x));
        along = L;
    } else {
        const vec2 rel = point - P0;
        const float r = length(rel);
        const float r0 = max(length(P1 - P0), 1e-4);
        const float a0 = atan(P1.y - P0.y, P1.x - P0.x);
        u = fract((atan(rel.y, rel.x) - a0) / AUREA_TAU);
        v = r - r0;
        along = AUREA_TAU * max(r, 1e-4);
    }
    const int side = int(p.p3.y + 0.5);
    const float vs = side == 0 ? v : (side == 1 ? -v : abs(v));
    const float maxH = max(p.p1.x, 1e-4);
    const float th = max(p.p1.y, 0.0);
    const float hw = 0.5 * th;
    // Um pixel da camada neste quadro (px por texel) mais a suavidade.
    const float px = 1.0 / max(p.texel.z, 1e-4);
    const float aa = px + p.p1.z * th;
    const int mode = int(p.p3.x + 0.5);
    const float fi = floor(u * n);
    const float seg = along / n;

    float cov = 0.0;
    if (mode == 0) {
        // Barras: uma por faixa, com a espessura, de pé sobre o caminho.
        const float uc = (fi + 0.5) / n;
        const float dAlong = abs(u - uc) * along;
        const float H = band_at(fi, n) * maxH;
        cov = aurea_band_coverage(dAlong, hw, aa) * aurea_band_coverage(vs - 0.5 * H, 0.5 * H, aa);
        if (u < 0.0 || u > 1.0) cov = 0.0;
    } else if (mode == 1) {
        // Linhas: a poligonal pelos topos das faixas, caindo ao caminho nas
        // pontas. Distância ao segmento (não só a diferença vertical): uma
        // subida íngreme fica tão fina quanto um trecho plano.
        const float x = u * n - 0.5;
        const float i0 = floor(x);
        const float f = x - i0;
        const float H0 = band_at(i0, n) * maxH;
        const float H1 = band_at(i0 + 1.0, n) * maxH;
        const float Hu = mix(H0, H1, f);
        const float slope = (H1 - H0) / max(seg, 1e-4);
        const float dist = abs(vs - Hu) / sqrt(1.0 + slope * slope);
        cov = aurea_band_coverage(dist, hw, aa) * step(0.0, u) * step(u, 1.0);
    } else {
        // Pontos: um disco no topo de cada faixa. Um ponto mais largo que a
        // vaga dele invade as vizinhas: as três mais próximas são testadas.
        for (int k = -1; k <= 1; ++k) {
            const float i = fi + float(k);
            if (i < 0.0 || i >= n) continue;
            const float uc = (i + 0.5) / n;
            const float Hi = band_at(i, n) * maxH;
            const vec2 dd = vec2((u - uc) * along, vs - Hi);
            cov = max(cov, aurea_band_coverage(length(dd), hw, aa));
        }
    }

    // Cor de dentro no caminho, de fora na altura máxima, e o matiz girando
    // no meio do caminho.
    const float fr = clamp(vs / maxH, 0.0, 1.0);
    vec3 col = mix(p.color.rgb, p.q0.rgb, fr);
    if (abs(p.q1.x) > 1e-5) col = aurea_hsv_shift(col, p.q1.x * fr, 1.0);
    const vec4 paint = vec4(clamp(col, 0.0, 1.0), 1.0) * cov;
    // Desligado, a camada some e só o espectro fica; ligado, ele é pintado
    // por cima dela.
    o_color = p.p3.w > 0.5 ? paint + base * (1.0 - cov) : paint;
}
