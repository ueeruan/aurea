#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_griddler.frag
//
//  Grade de ladrilhos (o comportamento do CC Griddler): a camada vira uma
//  grade de quadrados centrada nela; cada ladrilho mostra o pedaço da imagem
//  em volta do próprio centro, escalado (horizontal/vertical) e girado.
//  "Cortar ladrilhos" transforma cada ladrilho num cartão: só o pedaço da
//  célula aparece, e o resto vira transparente (borda com antisserrilhado).
//
//  p0 = lado do ladrilho (px), escala X, escala Y, rotação (rad); p1.x = cortar.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const float T = max(p.p0.x, 1.0);
    const vec2 scale = max(p.p0.yz, vec2(1e-3));
    const vec2 origin = cc_box_center();
    const vec2 center = origin + floor((px - origin) / T + 0.5) * T;
    const vec2 q = cc_rot(px - center, -p.p0.w) / scale;
    vec4 col = cc_at(center + q);
    if (p.p1.x > 0.5) {
        // 1 texel de saída = 1/escala px do conteúdo do cartão.
        const float inside = 0.5 * T - max(abs(q.x), abs(q.y));
        col *= clamp(inside * min(scale.x, scale.y) / cc_texel() + 0.5, 0.0, 1.0);
    }
    o_color = col;
}
