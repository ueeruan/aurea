#version 450
// =============================================================================
//  Aurea / shaders / effects / ballpoint.frag
//
//  Caneta esferográfica (Transição, 0.4.5): uma caneta rabisca a camada em
//  linhas, indo e voltando (como quem pinta uma área com caneta), e o rabisco
//  revela — ou apaga — a imagem. Logo atrás da caneta a área termina de
//  encher num hachurado que fecha, até ficar inteira.
//
//  O caminho: a camada é dividida em `linhas` faixas perpendiculares à
//  direção. A caneta percorre a faixa 0, depois a 1 no sentido contrário, e
//  assim por diante; em cada faixa ela faz um zigue-zague (os vértices sobem
//  e descem até a borda da faixa, com o tremor da mão sorteado por vértice).
//  O tempo da caneta é T = progresso × (linhas + atraso): em 100% toda faixa
//  foi percorrida e terminou de encher, EXATO.
//
//  p0 = progresso (0..1), modo (0 revelar, 1 remover), linhas, atraso (faixas)
//  p1 = direção (x, y), mínimo e extensão ao longo da direção (px)
//  p2 = mínimo e extensão na perpendicular (px), espessura (px), passo do zigue-zague (px)
//  p3 = tremor (0..1), tinta (0..1), semente, —
//  color = cor da tinta (linear, pré-multiplicada)
// =============================================================================
#include "cc_common.glsl"

float vertex_jitter(int row, int j, float salt) {
    return aurea_hash(uvec2(uint(row * 977 + 13), uint(j + 7919)) ^ uvec2(uint(p.p3.z * 131.0 + salt), 0x9E37u)) * 2.0 - 1.0;
}

/// Posição (ao longo, através; px no referencial da faixa) do vértice j da faixa `row`.
vec2 zig_vertex(int row, int j, float rowH, float zstep) {
    const float side = (j % 2 == 0) ? -1.0 : 1.0;
    const float jit = p.p3.x;
    const float along = float(j) * zstep + vertex_jitter(row, j, 1.0) * 0.35 * zstep * jit;
    const float across = (0.5 + side * (0.5 + 0.12 * vertex_jitter(row, j, 2.0) * jit)) * rowH;
    return vec2(along, across);
}

float seg_dist(vec2 q, vec2 a, vec2 b) {
    const vec2 ab = b - a;
    const float h = clamp(dot(q - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(q - a - ab * h);
}

/// Tinta da faixa `row` no ponto (along, across) — `across` relativo ao topo dela.
/// `pen` = quanto da faixa a caneta já percorreu (0..1); devolve a cobertura.
float row_ink(int row, vec2 q, float pen, float lenU, float rowH, float zstep, float halfW) {
    if (pen <= 0.0) return 0.0;
    // Faixas ímpares vão no sentido contrário: o "ao longo" é espelhado.
    const bool back = (row % 2) == 1;
    const float a = back ? lenU - q.x : q.x;
    const float penPx = clamp(pen, 0.0, 1.0) * lenU;
    if (a - halfW > penPx) return 0.0;
    const vec2 local = vec2(a, q.y);
    const int j = int(floor(a / max(zstep, 1e-3)));
    float d = 1e9;
    for (int k = -1; k <= 1; ++k) {
        const int i = max(j + k, 0);
        vec2 v0 = zig_vertex(row, i, rowH, zstep);
        vec2 v1 = zig_vertex(row, i + 1, rowH, zstep);
        // O segmento que a caneta ainda está traçando termina na ponta dela.
        if (v0.x > penPx) continue;
        if (v1.x > penPx) v1 = mix(v0, v1, clamp((penPx - v0.x) / max(v1.x - v0.x, 1e-3), 0.0, 1.0));
        d = min(d, seg_dist(local, v0, v1));
    }
    const float aa = cc_texel();
    return 1.0 - smoothstep(halfW - aa, halfW + aa, d);
}

void main() {
    const vec2 px = cc_px();
    const vec4 src = cc_at(px);
    const float progress = clamp(p.p0.x, 0.0, 1.0);
    const bool reveal = p.p0.y < 0.5;
    if (progress <= 0.0) { o_color = reveal ? vec4(0.0) : src; return; }
    if (progress >= 1.0) { o_color = reveal ? src : vec4(0.0); return; }

    const vec2 dir = p.p1.xy;
    const vec2 side = vec2(-dir.y, dir.x);
    const float lenU = max(p.p1.w, 1.0), lenV = max(p.p2.y, 1.0);
    const vec2 q = vec2(dot(px, dir) - p.p1.z, dot(px, side) - p.p2.x);   // 0..lenU, 0..lenV

    const float rows = max(p.p0.z, 1.0);
    const float lag = max(p.p0.w, 0.0);
    const float rowH = lenV / rows;
    const float T = progress * (rows + lag);
    const float halfW = 0.5 * max(p.p2.z, 0.5);
    const float zstep = max(p.p2.w, 1.0);

    const float vr = q.y / rowH;
    const int row = int(clamp(floor(vr), 0.0, rows - 1.0));
    // A faixa do pixel e a vizinha mais perto (o zigue-zague passa um pouco da borda).
    const int other = clamp(fract(vr) < 0.5 ? row - 1 : row + 1, 0, int(rows) - 1);
    float ink = 0.0;
    for (int n = 0; n < 2; ++n) {
        const int r = n == 0 ? row : other;
        if (n == 1 && r == row) break;
        const vec2 rq = vec2(q.x, q.y - float(r) * rowH);
        ink = max(ink, row_ink(r, rq, T - float(r), lenU, rowH, zstep, halfW));
    }

    // Enchimento: há quanto tempo (em faixas) a caneta passou por este ponto.
    const bool back = (row % 2) == 1;
    const float a = clamp((back ? lenU - q.x : q.x) / lenU, 0.0, 1.0);
    const float since = T - float(row) - a;
    float fill = lag > 1e-4 ? clamp(since / lag, 0.0, 1.0) : step(0.0, since);
    // O hachurado fecha: linhas diagonais engrossam até cobrir tudo.
    const float hatch = 0.5 + 0.5 * sin((q.x + q.y) * 3.14159265 / max(2.0 * halfW, 1.0));
    fill = clamp(fill * 1.7 - hatch * 0.7, 0.0, 1.0);
    if (since >= lag) fill = 1.0;

    const float shown = max(ink, fill);
    const float inkAmt = ink * clamp(p.p3.y, 0.0, 1.0) * (1.0 - fill);
    if (reveal) {
        const vec3 rgb = mix(src.rgb, p.color.rgb * src.a, inkAmt);
        o_color = vec4(rgb, src.a) * shown;
    } else {
        // Remover: a tinta passa por cima e a área some logo atrás.
        const vec3 rgb = mix(src.rgb, p.color.rgb * src.a, ink * clamp(p.p3.y, 0.0, 1.0));
        o_color = vec4(rgb, src.a) * (1.0 - fill);
    }
}
