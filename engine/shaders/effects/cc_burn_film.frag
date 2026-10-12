#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_burn_film.frag
//
//  Filme queimando (o comportamento do CC Burn Film): a camada queima a
//  partir do "Centro" numa frente irregular (distância + ruído fractal da
//  semente). Atrás da frente fica o furo; nela, uma brasa laranja-amarela;
//  logo depois, a película chamuscada escurece. 0% é a camada exata; 100%
//  queimou tudo.
//
//  p0 = centro (px), queimar (0..1), semente; p1.x = distância que cobre a
//  camada (px), p1.y = tamanho do ruído (px).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec4 src = cc_at(px);
    const float burn = clamp(p.p0.z, 0.0, 1.0);
    if (burn <= 0.0) { o_color = src; return; }
    if (burn >= 1.0) { o_color = vec4(0.0); return; }
    const float seed = p.p0.w;
    const vec2 q = px / max(p.p1.y, 1.0) + vec2(seed * 17.13, seed * 31.71);
    const float noise = aurea_fbm(q, 5, 0.5);
    // Campo em ~0..1: a distância normalizada manda, o ruído desenha a borda.
    const float field = length(px - p.p0.xy) / max(0.92 * p.p1.x, 1.0) * 0.75 + noise * 0.25;
    const float front = burn - 0.02;
    const float x = field - front;
    // Furo com meia largura de 1 texel no campo.
    const float fw = max(fwidth(field), 1e-4);
    const float keep = clamp(x / fw + 0.5, 0.0, 1.0);
    if (keep <= 0.0) { o_color = vec4(0.0); return; }
    const float ember = 1.0 - smoothstep(0.0, 0.05, x);           // brasa junto à frente
    const float scorch = 1.0 - smoothstep(0.0, 0.16, x);          // chamuscado
    vec3 rgb = src.rgb * mix(1.0, 0.18, scorch);
    rgb = mix(rgb, vec3(0.04, 0.02, 0.01) * src.a, ember * 0.6);
    const vec3 glow = mix(vec3(1.0, 0.25, 0.02), vec3(1.0, 0.8, 0.25), ember * ember);
    rgb += glow * (ember * ember * 2.2 * src.a);
    o_color = vec4(rgb, src.a) * keep;
}
