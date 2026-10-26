#version 450
// =============================================================================
//  Aurea / shaders / hyper / deblock.frag
//
//  Hyper Clean (Turbo), desbloqueio: os degraus da grade 8x8 do codec (as
//  bordas de macrobloco 16x16 caem na mesma grade), um eixo por passe.
//  Código próprio.
//
//  Na borda entre p0 e q0 (p2 p1 p0 | q0 q1 q2, mais p3/q3 para a atividade):
//    degrau corrigido  D = (q0 − p0) − ½·[(p0 − p1) + (q1 − q0)]
//  (o degrau menos o que a inclinação de cada lado já explica: um degradê
//  real não é bloco). Só age quando o degrau é PEQUENO (|D| < alfa; borda de
//  verdade passa) e os dois lados são LISOS (atividade < beta): é onde o olho
//  vê o bloco. D vira uma rampa de 3 px de cada lado. A grade é a da FONTE
//  (px da camada); abaixo de 3/4 de densidade o passe é a identidade.
//  Entrada/saída: perceptual (flags.w = 1: a entrada é linear do motor).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho (texels)
    vec4 grid;    // x = texels por px da camada, y = origem da região (px), z = eixo (0 x, 1 y), w = entrada linear
    vec4 tune;    // x = força (0..1)
} p;

vec4 load(ivec2 at) {
    const vec4 v = texelFetch(u_src, hy_clampi(at, ivec2(p.size.xy)), 0);
    return p.grid.w > 0.5 ? hy_to_perc(v) : v;
}

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const vec4 c = load(at);
    const float s = p.grid.x;
    if (s < 0.75 || p.tune.x <= 0.0) { o_color = c; return; }
    const bool axisX = p.grid.z < 0.5;
    const float tex = axisX ? float(at.x) + 0.5 : float(at.y) + 0.5;
    const float lx = p.grid.y + tex / s;                 // px da camada
    const float bnd = floor(lx / 8.0 + 0.5) * 8.0;
    const float d = lx - bnd;                            // −4..4
    if (abs(d) >= 3.0) { o_color = c; return; }
    const ivec2 axis = axisX ? ivec2(1, 0) : ivec2(0, 1);
    // Texel logo antes da borda (p0) e depois (q0).
    const int b0 = int(floor((bnd - p.grid.y) * s - 0.5));
    const ivec2 base = axisX ? ivec2(b0, at.y) : ivec2(at.x, b0);
    vec3 v[8];
    for (int k = 0; k < 8; ++k) v[k] = load(base + axis * (k - 3)).rgb;   // p3 p2 p1 p0 q0 q1 q2 q3
    const vec3 p0 = v[3], p1 = v[2], q0 = v[4], q1 = v[5];
    const vec3 D = (q0 - p0) - 0.5 * ((p0 - p1) + (q1 - q0));
    const float lp = hy_luma(abs(v[3] - v[2])) + hy_luma(abs(v[2] - v[1])) + hy_luma(abs(v[1] - v[0]));
    const float lq = hy_luma(abs(v[4] - v[5])) + hy_luma(abs(v[5] - v[6])) + hy_luma(abs(v[6] - v[7]));
    const float st = p.tune.x;
    const float alpha = 0.015 + 0.075 * st;
    const float beta = 0.012 + 0.045 * st;
    const float g = (1.0 - smoothstep(0.6 * alpha, alpha, abs(hy_luma(D))))
                  * (1.0 - smoothstep(0.5 * beta, beta, max(lp, lq) / 3.0));
    // Rampa: 0,42 / 0,25 / 0,08 do degrau a 0,5 / 1,5 / 2,5 px da borda.
    const float f = 0.5 * (1.0 - abs(d) / 3.0);
    const float side = d < 0.0 ? 1.0 : -1.0;
    o_color = vec4(c.rgb + side * D * (f * g), c.a);
}
