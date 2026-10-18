#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_smear.frag
//
//  Borrar com o dedo (o comportamento do CC Smear): o conteúdo em "Origem" é
//  arrastado até "Destino". Ao longo do caminho (parâmetro t de 0 a 1) o pixel
//  lê t·D para trás — com alcance 100% todo o caminho mostra a origem, como
//  tinta esticada — e depois do destino a mancha inteira aparece deslocada.
//  O peso cai com a distância ao caminho até "Raio".
//
//  p0 = origem (px), destino (px); p1.x = alcance (0..2), p1.y = raio (px).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec2 from = p.p0.xy;
    const vec2 D = p.p0.zw - from;
    const float L2 = max(dot(D, D), 1e-6);
    const float t = clamp(dot(px - from, D) / L2, 0.0, 1.0);
    const float dist = length(px - (from + t * D));
    const float fall = 1.0 - smoothstep(0.0, max(p.p1.y, 1e-3), dist);
    o_color = cc_at(px - D * (t * fall * p.p1.x));
}
