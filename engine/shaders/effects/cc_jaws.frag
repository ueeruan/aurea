#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_jaws.frag
//
//  Mandíbulas (o comportamento do CC Jaws): a camada é cortada por uma linha
//  dentada que passa pelo "Centro" na "Direção", e as duas metades se afastam
//  na perpendicular até sumirem. A borda tem o perfil escolhido:
//    0 dentes (serra), 1 robô (dentes quadrados), 2 blocos (alturas
//    sorteadas por dente), 3 ondas (senoide).
//  Conclusão 0% é a camada exata (as duas coberturas somam 1 na emenda);
//  100% afasta as metades além da camada inteira.
//
//  p0 = centro (px), direção (rad), conclusão (0..1);
//  p1 = altura dos dentes (px), largura (px), forma, distância total (px);
//  p2.x = fator da inclinação da borda (antisserrilhado ao longo da normal).
// =============================================================================
#include "cc_common.glsl"

float profile(float u, int shape) {
    if (shape == 1) return fract(u) < 0.5 ? 0.5 : -0.5;
    if (shape == 2) return aurea_hash2(vec2(floor(u), 7.0)) - 0.5;
    if (shape == 3) return 0.5 * sin(AUREA_TAU * u);
    return 1.0 - 2.0 * abs(fract(u) - 0.5) - 0.5;
}

void main() {
    const vec2 px = cc_px();
    const vec2 c = p.p0.xy;
    const vec2 dir = vec2(cos(p.p0.z), sin(p.p0.z));
    const vec2 nrm = vec2(-dir.y, dir.x);
    const vec2 d = px - c;
    const float u = dot(d, dir);
    const float v = dot(d, nrm);
    const float e = p.p1.x * profile(u / max(p.p1.y, 1e-3), int(p.p1.z + 0.5));
    const float D = clamp(p.p0.w, 0.0, 1.0) * p.p1.w;
    const float aa = cc_texel() * max(p.p2.x, 1.0);
    const float covA = clamp((e - (v + D)) / aa + 0.5, 0.0, 1.0);
    const float covB = clamp(((v - D) - e) / aa + 0.5, 0.0, 1.0);
    vec4 col = vec4(0.0);
    if (covA > 0.0) col += cc_at(c + dir * u + nrm * (v + D)) * covA;
    if (covB > 0.0) col += cc_at(c + dir * u + nrm * (v - D)) * covB;
    o_color = col;
}
