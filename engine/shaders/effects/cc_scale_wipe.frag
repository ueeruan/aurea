#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_scale_wipe.frag
//
//  Esticar em varredura (o comportamento do CC Scale Wipe): do lado da
//  "Direção", depois da linha que passa pelo "Centro", a imagem é esticada a
//  partir da linha. Em 100% todo esse lado mostra só a linha de pixels do
//  centro (os riscos de uma varredura por esticamento).
//
//  p0 = centro (px), direção (rad), esticar (0..1).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec2 dir = vec2(cos(p.p0.z), sin(p.p0.z));
    const float along = dot(px - p.p0.xy, dir);
    if (along <= 0.0) { o_color = cc_at(px); return; }
    o_color = cc_at(px - dir * (along * clamp(p.p0.w, 0.0, 1.0)));
}
