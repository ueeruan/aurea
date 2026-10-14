#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_repeat_tile.frag
//
//  Ladrilho repetido (o comportamento do CC RepeTile): a camada se repete em
//  volta dela mesma até onde a região foi expandida. O ladrilho (0, 0) é a
//  camada intacta; os outros seguem o modo:
//    0 repetir, 1 desdobrar (espelho contínuo), 2/3 espelho horizontal/vertical
//    em xadrez, 4 giro de 180° em xadrez, 5 tijolos, 6 tijolos verticais.
//  "Misturar bordas" funde cada ladrilho com o desdobrado (que é contínuo na
//  emenda) numa faixa junto às bordas — a costura some.
//
//  p0.x = modo, p0.y = faixa da mistura (fração do ladrilho), p0.zw = deslocamento (px).
// =============================================================================
#include "cc_common.glsl"

vec2 tile_uv(vec2 rel, int mode) {
    const vec2 cell = floor(rel);
    vec2 f = rel - cell;
    const bool oddX = mod(cell.x, 2.0) >= 1.0;
    const bool oddY = mod(cell.y, 2.0) >= 1.0;
    const bool checker = mod(cell.x + cell.y, 2.0) >= 1.0;
    if (mode == 1) {
        if (oddX) f.x = 1.0 - f.x;
        if (oddY) f.y = 1.0 - f.y;
    } else if (mode == 2) {
        if (checker) f.x = 1.0 - f.x;
    } else if (mode == 3) {
        if (checker) f.y = 1.0 - f.y;
    } else if (mode == 4) {
        if (checker) f = 1.0 - f;
    } else if (mode == 5) {
        f.x = fract(rel.x + (oddY ? 0.5 : 0.0));
    } else if (mode == 6) {
        f.y = fract(rel.y + (oddX ? 0.5 : 0.0));
    }
    return f;
}

void main() {
    const vec2 size = max(p.box.zw, vec2(1.0));
    const vec2 rel = (cc_px() - p.box.xy - p.p0.zw) / size;
    const int mode = int(p.p0.x + 0.5);
    vec4 col = cc_at(p.box.xy + tile_uv(rel, mode) * size);
    const float band = p.p0.y;
    if (band > 1e-4 && mode != 1) {
        const vec2 f = fract(rel);
        const float edge = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
        const float w = 1.0 - smoothstep(0.0, band, edge);
        if (w > 0.0) col = mix(col, cc_at(p.box.xy + tile_uv(rel, 1) * size), w);
    }
    o_color = col;
}
