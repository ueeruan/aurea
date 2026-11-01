#version 450
// =============================================================================
//  Aurea / shaders / effects / tile_shift.frag
//
//  Tile Shift, Hexagon Tile Shift e Hexagon Tile Rotate (TileShiftEffects.cpp):
//  a camada vira uma grade (quadrada ou hexagonal) e o conteúdo de CADA célula
//  anda ou gira sozinho. Uma amostra por pixel.
//
//  Cada célula sorteia um valor pela semente e pelo índice dela. A evolução
//  passa de um sorteio para o seguinte suavemente (360° = um sorteio novo).
//  "Repetir dentro da célula": o que sai da célula volta pelo outro lado (a
//  conta é o resto na própria grade — a colmeia também ladrilha o plano), e a
//  célula continua mostrando só o próprio conteúdo. "Imagem contínua": o que
//  passa da borda mostra a imagem do vizinho.
//
//  p0 = centro da grade (px), tamanho da célula (px; raio do hexágono), grade (0 quadrada, 1 hexagonal)
//  p1 = quantidade (px no deslocamento, rad no giro), aleatoriedade (0..1), evolução (voltas), semente
//  p2 = modo (0 deslocar, 1 girar), padrão (0 aleatório, 1 fileiras, 2 colunas), bordas, mistura
// =============================================================================
#include "cc_common.glsl"

float ts_hash(vec2 cell, float salt) {
    vec3 q = fract(vec3(cell.xyx + vec3(17.0, 59.0, 113.0) * salt) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

/// O sorteio da célula para a evolução atual (dois sorteios misturados suavemente).
float ts_random(vec2 cell, float channel) {
    const float evo = p.p1.z;
    const float i0 = floor(evo);
    const float f = evo - i0;
    const float w = f * f * (3.0 - 2.0 * f);
    const float s = p.p1.w * 7.0 + channel * 131.0;
    return mix(ts_hash(cell, s + i0 * 3.0), ts_hash(cell, s + (i0 + 1.0) * 3.0), w);
}

/// A célula do ponto `d` (px, relativo ao centro da grade): índice e o ponto
/// relativo ao centro dela.
void ts_cell(vec2 d, out vec2 cell, out vec2 local) {
    const float size = max(p.p0.z, 1e-3);
    if (p.p0.w < 0.5) {
        cell = floor(d / size + 0.5);
        local = d - cell * size;
        return;
    }
    // Colmeia de pontas para cima (rede de 60°): o centro mais próximo está
    // num dos quatro cantos do losango da rede que contém o ponto.
    const vec2 a1 = vec2(1.7320508 * size, 0.0);
    const vec2 a2 = vec2(0.8660254 * size, 1.5 * size);
    const float fj = d.y / a2.y;
    const vec2 base = floor(vec2((d.x - fj * a2.x) / a1.x, fj));
    float best = 1e30;
    cell = base;
    local = d;
    for (int u = 0; u < 2; ++u) {
        for (int v = 0; v < 2; ++v) {
            const vec2 k = base + vec2(float(u), float(v));
            const vec2 off = d - (k.x * a1 + k.y * a2);
            const float dd = dot(off, off);
            if (dd < best) { best = dd; cell = k; local = off; }
        }
    }
}

/// +1 / −1 alternando pela fileira (ou coluna) da célula.
float ts_alternate(float index) { return mod(index, 2.0) >= 1.0 ? -1.0 : 1.0; }

void main() {
    const vec2 px = cc_px();
    const vec2 d = px - p.p0.xy;
    vec2 cell, local;
    ts_cell(d, cell, local);
    const vec2 center = d - local;

    const float randomness = clamp(p.p1.y, 0.0, 1.0);
    const int pattern = int(p.p2.y + 0.5);
    // 0% de aleatoriedade = a quantidade cheia em todas; 100% = sorteada.
    const float r0 = ts_random(cell, 0.0);
    const float r1 = ts_random(cell, 1.0);
    vec2 src;
    if (p.p2.x < 0.5) {
        vec2 move;
        const float mag = p.p1.x * (1.0 - randomness * r1);
        if (pattern == 1) move = vec2(ts_alternate(cell.y) * mag, 0.0);
        else if (pattern == 2) move = vec2(0.0, ts_alternate(cell.x) * mag);
        else {
            const float a = r0 * 2.0 * CC_PI;
            move = vec2(cos(a), sin(a)) * mag;
        }
        // O conteúdo anda `move`: o pixel mostra o que estava atrás dele.
        src = local - move;
    } else {
        float angle;
        if (pattern == 1) angle = ts_alternate(cell.y) * p.p1.x * (1.0 - randomness * r1);
        else if (pattern == 2) angle = ts_alternate(cell.x) * p.p1.x * (1.0 - randomness * r1);
        else angle = p.p1.x * mix(1.0, r0 * 2.0 - 1.0, randomness);
        src = cc_rot(local, -angle);
    }
    if (p.p2.z < 0.5) {
        // Resto na grade: o ponto volta para dentro da PRÓPRIA célula.
        vec2 wrapCell, wrapped;
        ts_cell(src, wrapCell, wrapped);
        src = wrapped;
    }
    const vec4 shifted = cc_at(p.p0.xy + center + src);
    o_color = mix(shifted, cc_at(px), clamp(p.p2.w, 0.0, 1.0));
}
