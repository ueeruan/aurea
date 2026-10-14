#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_tiler.frag
//
//  Mosaico de cópias (o comportamento do CC Tiler): a camada inteira, reduzida
//  para "Escala", repetida em grade em volta do "Centro". Em 100% com o
//  centro no meio é a própria camada.
//
//  p0.xy = centro (px), p0.z = escala, p0.w = mistura com o original.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec2 size = max(p.box.zw, vec2(1.0));
    const vec2 rel = (px - p.p0.xy) / (size * max(p.p0.z, 1e-3)) + 0.5;
    const vec4 tiled = cc_at(p.box.xy + fract(rel) * size);
    o_color = mix(tiled, cc_at(px), clamp(p.p0.w, 0.0, 1.0));
}
