#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_blobby.frag
//
//  Bolhas (o comportamento do CC Blobbylize): o mapa de bolhas é a própria
//  camada (luminância ou alfa) já suavizada (u_tex1, um gaussiano). O que
//  fica abaixo do "Corte" some; a borda do corte tem um texel de
//  antisserrilhado medido pelo gradiente do mapa. A superfície ganha relevo
//  arredondado (sobe junto à borda e achata no alto) e a luz da cena — a
//  imagem parece líquido grosso.
//
//  p0 = propriedade (0 luminância, 1 alfa), corte (0..1), escala do relevo
//  (px), —; p3 = região do mapa suavizado (px da camada).
// =============================================================================
#include "cc_common.glsl"
#include "cc_light.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // mapa suavizado

float blob(vec2 px) {
    const vec4 m = texture(u_tex1, (px - p.p3.xy) / max(p.p3.zw, vec2(1e-6)));
    return p.p0.x > 0.5 ? m.a : aurea_luma(m.rgb);
}

/// Perfil do relevo: sobe do corte até 0,35 acima dele.
float relief(float h) { return smoothstep(p.p0.y, min(p.p0.y + 0.35, 1.0) + 1e-4, h); }

void main() {
    const vec2 px = cc_px();
    const float e = cc_texel();
    const float h = blob(px);
    const float hl = blob(px - vec2(e, 0.0)), hr = blob(px + vec2(e, 0.0));
    const float hu = blob(px - vec2(0.0, e)), hd = blob(px + vec2(0.0, e));
    // Variação do mapa por texel de saída: a meia-largura do corte.
    const float w = max(0.5 * length(vec2(hr - hl, hd - hu)), 1e-4);
    const float cut = p.p0.y;
    const float alpha = cut <= 0.0 ? 1.0 : clamp((h - cut) / w + 0.5, 0.0, 1.0);
    if (alpha <= 0.0) { o_color = vec4(0.0); return; }
    const vec2 slope = vec2(relief(hr) - relief(hl), relief(hd) - relief(hu)) / (2.0 * e);
    const vec3 n = normalize(vec3(-slope * p.p0.z, 1.0));
    const vec4 src = cc_at(px);
    o_color = vec4(cc_shade(src.rgb, src.a, n), src.a) * alpha;
}
