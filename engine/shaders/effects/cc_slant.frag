#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_slant.frag
//
//  Inclinar (o comportamento do CC Slant): a camada é cisalhada a partir do
//  "Chão" — o que está acima dele anda para o lado na proporção da altura
//  (itálico) — e a altura é escalada em volta do chão. Com "Usar cor" a
//  camada vira a cor chapada (a sombra projetada clássica: inclinar, baixar a
//  altura, pintar de preto).
//
//  Ida: x' = x + (F − y)·tan, y' = F − (F − y)·h. Aqui a volta.
//  p0 = tan(inclinação), altura h, chão F (px), usar cor; color = cor pré-mult.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const float h = p.p0.y;
    if (h < 1e-4) { o_color = vec4(0.0); return; }
    const float F = p.p0.z;
    const float y = F - (F - px.y) / h;
    const float x = px.x - (F - y) * p.p0.x;
    vec4 col = cc_at(vec2(x, y));
    if (p.p0.w > 0.5) col = p.color * col.a;
    o_color = col;
}
