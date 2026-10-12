#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_hex_tile.frag
//
//  Ladrilhos hexagonais (o comportamento do CC HexTile): o hexágono de raio
//  "Raio" em volta do "Centro de entrada" é repetido numa colmeia em volta do
//  "Centro da grade", girada por "Rotação". "Espelhar bordas" alterna as
//  colunas da colmeia em espelho, para as emendas casarem.
//
//  O centro mais próximo da colmeia (rede de 60°) está sempre entre os quatro
//  cantos do losango da rede que contém o ponto.
//
//  p0 = centro de entrada (px), centro da grade (px);
//  p1 = raio (px), rotação (rad), espelhar, mistura com o original.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const float R = max(p.p1.x, 1.0);
    const vec2 a1 = vec2(1.7320508 * R, 0.0);
    const vec2 a2 = vec2(0.8660254 * R, 1.5 * R);
    const vec2 d = cc_rot(px - p.p0.zw, -p.p1.y);
    const float fj = d.y / a2.y;
    const vec2 base = floor(vec2((d.x - fj * a2.x) / a1.x, fj));
    vec2 best = d;
    vec2 cell = base;
    float bestD = 1e30;
    for (int u = 0; u < 2; ++u) {
        for (int v = 0; v < 2; ++v) {
            const vec2 k = base + vec2(float(u), float(v));
            const vec2 off = d - (k.x * a1 + k.y * a2);
            const float dd = dot(off, off);
            if (dd < bestD) { bestD = dd; best = off; cell = k; }
        }
    }
    if (p.p1.z > 0.5 && mod(cell.x, 2.0) >= 1.0) best.x = -best.x;
    const vec4 tiled = cc_at(p.p0.xy + cc_rot(best, p.p1.y));
    o_color = mix(tiled, cc_at(px), clamp(p.p1.w, 0.0, 1.0));
}
