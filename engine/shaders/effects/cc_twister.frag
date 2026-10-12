#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_twister.frag
//
//  Torcer (o comportamento do CC Twister): a camada é uma fita que gira em
//  volta de um eixo. Ao longo do eixo (coordenada t), o ângulo da fita sobe
//  suavemente de 0 até "Torção" dentro de "Tamanho" em volta do centro; antes
//  ela está intacta, depois girada por inteiro (180° = espelhada).
//
//  Vista de frente, a fatia girada por θ encolhe para cos θ da largura: o
//  pixel a uma distância s do eixo lê a camada em s / cos θ. Com cos θ < 0
//  vemos as costas da fita (a imagem espelhada). O sombreado escurece a fita
//  quando ela fica de lado.
//
//  p0 = centro (px), ângulo do eixo (rad), torção (rad);
//  p1.x = meio tamanho (px), p1.y = sombreado (0..1).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 c = p.p0.xy;
    const vec2 dir = vec2(cos(p.p0.z), sin(p.p0.z));
    const vec2 nrm = vec2(-dir.y, dir.x);
    const vec2 d = cc_px() - c;
    const float t = dot(d, dir);
    const float s = dot(d, nrm);
    const float hs = max(p.p1.x, 1e-3);
    const float theta = p.p0.w * smoothstep(-hs, hs, t);
    const float ct = cos(theta);
    const float k = ct >= 0.0 ? max(ct, 1e-4) : min(ct, -1e-4);
    vec4 col = cc_at(c + dir * t + nrm * (s / k));
    col.rgb *= mix(1.0, abs(ct), clamp(p.p1.y, 0.0, 1.0));
    o_color = col;
}
