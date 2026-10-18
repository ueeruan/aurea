#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_split.frag
//
//  Rasgar (o comportamento do CC Split 2): a camada abre ao longo do segmento
//  A→B como um corte numa folha elástica. A abertura tem perfil de lente
//  (sen πt: fechada nas pontas) e cada lado abre o seu tanto (Abertura 1 de
//  um lado, Abertura 2 do outro). O vão fica transparente; o conteúdo é
//  empurrado e se acomoda num joelho de 2,5× a abertura — o mapa é monótono,
//  então nada dobra sobre si.
//
//  p0 = ponto A (px), ponto B (px); p1.x/y = abertura máxima de cada lado (px).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec2 A = p.p0.xy;
    const vec2 AB = p.p0.zw - A;
    const float len = length(AB);
    if (len < 1e-3) { o_color = cc_at(px); return; }
    const vec2 dir = AB / len;
    const vec2 nrm = vec2(-dir.y, dir.x);
    const vec2 d = px - A;
    const float t = dot(d, dir) / len;
    const float s = dot(d, nrm);
    const float h = (t > 0.0 && t < 1.0) ? sin(CC_PI * t) : 0.0;
    const float open = (s >= 0.0 ? p.p1.x : p.p1.y) * h;
    if (open <= 1e-4) { o_color = cc_at(px); return; }
    const float as = abs(s);
    const float k = 1.0 - smoothstep(open, open + 2.5 * open + 1.0, as);
    const float srcS = max(as - open * k, 0.0);
    const float cov = clamp((as - open) / cc_texel() + 0.5, 0.0, 1.0);
    o_color = cc_at(A + dir * (t * len) + nrm * (s >= 0.0 ? srcS : -srcS)) * cov;
}
