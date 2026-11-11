// =============================================================================
//  Aurea / shaders / effects / sea_common.glsl
//
//  Peças do pacote "Mar" (SeaEffects.cpp): Oceano, Embaixo d'água, Cáusticas
//  e Onda na praia. O bloco de uniforms é o dos efeitos CC (cc_common.glsl:
//  dst/src/box/info, p0..p5, color) com seis vec4 a mais (q0..q5), espelho de
//  `SeaUniforms` no C++. Tudo em px da CAMADA: a prévia em 1/4 e o export em
//  4K desenham o mesmo lugar. Cor linear pré-multiplicada.
//
//  Tudo aqui é função pura do ponto e do tempo do quadro (info.y): o mesmo
//  quadro sai igual no preview, no scrubbing, fora de ordem e no export.
//  Implementação própria (ruído de valor com derivada, células animadas).
// =============================================================================
#ifndef AUREA_SEA_COMMON_GLSL
#define AUREA_SEA_COMMON_GLSL

#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dst;    // região de saída (px da camada): x, y, largura, altura
    vec4 src;    // região da entrada
    vec4 box;    // caixa natural da camada
    vec4 info;   // x = px por texel de saída, y = tempo (s), z = texels por px, w = qualidade 0..1
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 p4;
    vec4 p5;
    vec4 color;
    vec4 q0;
    vec4 q1;
    vec4 q2;
    vec4 q3;
    vec4 q4;
    vec4 q5;
} p;

/// O ponto da camada (px) que este texel de saída representa.
vec2 sea_px() { return p.dst.xy + v_uv * p.dst.zw; }
/// px da camada → uv da entrada.
vec2 sea_uv(vec2 px) { return (px - p.src.xy) / max(p.src.zw, vec2(1e-6)); }
/// A entrada no ponto `px` da camada.
vec4 sea_at(vec2 px) { return texture(u_tex0, sea_uv(px)); }
/// Tamanho de um texel de saída em px da camada.
float sea_texel() { return max(p.info.x, 1e-4); }

/// Ruído de valor com a derivada: (valor 0..1, d/dx, d/dy). A derivada vem da
/// interpolação (sem amostras extras): é o relevo fino da água.
vec3 sea_noised(vec2 x, uint salt) {
    const vec2 i = floor(x);
    const vec2 f = x - i;
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const vec2 du = 6.0 * f * (1.0 - f);
    const float a = aurea_hash3(i, salt);
    const float b = aurea_hash3(i + vec2(1.0, 0.0), salt);
    const float c = aurea_hash3(i + vec2(0.0, 1.0), salt);
    const float d = aurea_hash3(i + vec2(1.0, 1.0), salt);
    const float k1 = b - a, k2 = c - a, k3 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k3 * u.x * u.y,
                du * vec2(k1 + k3 * u.y, k2 + k3 * u.x));
}

/// Ruído de valor simples (0..1) com sal.
float sea_noise(vec2 x, uint salt) { return sea_noised(x, salt).x; }

/// Células que dançam: devolve F2 − F1 (≈ distância até a borda entre duas
/// células). Cada centro gira num pequeno círculo com fase e ritmo próprios,
/// então a rede se deforma devagar sem saltos. 3×3 células: custo fixo.
float sea_cell_edge(vec2 x, float t, uint salt) {
    const vec2 i = floor(x);
    const vec2 f = x - i;
    float d1 = 8.0, d2 = 8.0;
    for (int j = -1; j <= 1; ++j) {
        for (int k = -1; k <= 1; ++k) {
            const vec2 g = vec2(float(k), float(j));
            const vec2 c = i + g;
            const float h1 = aurea_hash3(c, salt);
            const float h2 = aurea_hash3(c, salt + 7u);
            const vec2 o = 0.5 + 0.36 * vec2(sin(t * (0.55 + 0.5 * h1) + AUREA_TAU * h2),
                                             cos(t * (0.45 + 0.6 * h2) + AUREA_TAU * h1));
            const vec2 r = g + o - f;
            const float d = dot(r, r);
            if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
        }
    }
    return sqrt(d2) - sqrt(d1);
}

/// O desenho das cáusticas em `x` (unidades de célula): a rede clara de luz
/// focada no fundo d'água. Duas redes (escala e giro diferentes) somam e os
/// cruzamentos acendem mais; `sharp` 0..1 afina as linhas. `layers` 1 = só a
/// primeira rede (prévia leve). Devolve ~0..1,5.
float sea_caustics(vec2 x, float t, float sharp, uint salt, int layers) {
    // A rede respira: um ruído largo empurra o domínio devagar.
    const vec2 w = vec2(sea_noise(x * 0.31 + vec2(t * 0.09, 3.7), salt + 41u),
                        sea_noise(x * 0.31 + vec2(9.1, t * 0.11), salt + 43u)) - 0.5;
    const vec2 y = x + w * 0.7;
    const float width = mix(0.5, 0.07, clamp(sharp, 0.0, 1.0));
    float c = 1.0 - smoothstep(0.0, width, sea_cell_edge(y, t, salt));
    c *= c;
    if (layers > 1) {
        const vec2 z = mat2(0.8, -0.6, 0.6, 0.8) * y * 1.37 + vec2(5.3, 1.9);
        float c2 = 1.0 - smoothstep(0.0, width * 1.25, sea_cell_edge(z, t * 1.19, salt + 101u));
        c2 *= c2;
        c = 0.62 * c + 0.38 * c2 + 0.75 * c * c2;
    }
    return c;
}

#endif
