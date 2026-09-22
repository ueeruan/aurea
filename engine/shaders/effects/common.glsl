// =============================================================================
//  Aurea / shaders / effects / common.glsl
//
//  Peças que os efeitos compartilham. Nada aqui é específico de um efeito.
//
//  REGRA: o shader de um efeito declara só o que lê. Este arquivo NÃO declara
//  bindings nem uniforms — quem inclui é que decide o layout.
//
//  Tudo trabalha no espaço de TRABALHO do motor: linear, pré-multiplicado.
//  Um efeito que precisa julgar cor (limiar, saturação, matiz) converte para
//  sRGB com `linear_to_srgb`, decide, e volta — é o que faz o resultado bater
//  com o que o olho vê e com o export.
// =============================================================================
#ifndef AUREA_EFFECTS_COMMON_GLSL
#define AUREA_EFFECTS_COMMON_GLSL

// As conversões de cor do motor (linear_to_srgb, premultiply, luminance709)
// vivem lá desde sempre: os efeitos usam AS MESMAS, não uma cópia.
#include "../common/color.glsl"

const float AUREA_TAU = 6.28318530718;

// --- Ruído -------------------------------------------------------------------
// Hash inteiro: determinístico, sem textura de ruído e sem estado. A mesma
// coordenada dá o mesmo valor em qualquer aparelho — o que faz o efeito ser
// reproduzível no preview e no export (Fase 7.3 §23, §77).
uint aurea_hash_u(uvec2 q) {
    uint h = 0x811C9DC5u;
    h = (h ^ q.x) * 16777619u;
    h = (h ^ q.y) * 16777619u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

float aurea_hash(uvec2 q) { return float(aurea_hash_u(q) & 0xFFFFFFu) / 16777216.0; }
float aurea_hash2(vec2 p) { return aurea_hash(uvec2(ivec2(floor(p)))); }
float aurea_hash3(vec2 p, uint salt) { return aurea_hash(uvec2(ivec2(floor(p))) ^ uvec2(salt, salt * 2654435761u)); }

/// Ruído de valor com interpolação suave: manchas, não pixels soltos.
float aurea_value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    const float a = aurea_hash(uvec2(ivec2(i)));
    const float b = aurea_hash(uvec2(ivec2(i) + ivec2(1, 0)));
    const float c = aurea_hash(uvec2(ivec2(i) + ivec2(0, 1)));
    const float d = aurea_hash(uvec2(ivec2(i) + ivec2(1, 1)));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/// FBM de `octaves` oitavas — a base de turbulência, nuvem e deslocamento.
float aurea_fbm(vec2 p, int octaves, float gain) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * aurea_value_noise(p);
        norm += amp;
        p *= 2.03;
        amp *= gain;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

/// Turbulência (|ruído| acumulado) — cristas em vez de manchas suaves.
float aurea_turbulence(vec2 p, int octaves, float gain) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * abs(aurea_value_noise(p) * 2.0 - 1.0);
        norm += amp;
        p *= 2.03;
        amp *= gain;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

// --- Cor ---------------------------------------------------------------------
float aurea_luma(vec3 lin) { return dot(lin, vec3(0.2126, 0.7152, 0.0722)); }

float aurea_linear_to_srgb(float c) {
    const float x = clamp(c, 0.0, 1.0);
    return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
float aurea_srgb_to_linear(float c) {
    const float x = clamp(c, 0.0, 1.0);
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}
vec3 aurea_linear_to_srgb(vec3 c) {
    return vec3(aurea_linear_to_srgb(c.r), aurea_linear_to_srgb(c.g), aurea_linear_to_srgb(c.b));
}
vec3 aurea_srgb_to_linear(vec3 c) {
    return vec3(aurea_srgb_to_linear(c.r), aurea_srgb_to_linear(c.g), aurea_srgb_to_linear(c.b));
}

/// Matiz e saturação no sentido de HSV, sobre cor linear.
vec3 aurea_hsv_shift(vec3 lin, float hueTurns, float satMul) {
    const vec3 c = clamp(aurea_linear_to_srgb(lin), 0.0, 1.0);
    const float mx = max(c.r, max(c.g, c.b));
    const float mn = min(c.r, min(c.g, c.b));
    const float d = mx - mn;
    float h = 0.0;
    if (d > 1e-6) {
        if (mx == c.r)      h = fract((c.g - c.b) / d / 6.0);
        else if (mx == c.g) h = ((c.b - c.r) / d + 2.0) / 6.0;
        else                h = ((c.r - c.g) / d + 4.0) / 6.0;
    }
    h = fract(h + hueTurns);
    const float s = clamp((mx > 1e-6 ? d / mx : 0.0) * satMul, 0.0, 1.0);
    const float k = (h * 6.0);
    const vec3 p = vec3(abs(fract(vec3(k, k + 4.0, k + 2.0) / 6.0) * 6.0 - 3.0) - 1.0);
    return aurea_srgb_to_linear(clamp(mx * mix(vec3(1.0), clamp(p, 0.0, 1.0), s), 0.0, 1.0));
}

// --- Geometria ---------------------------------------------------------------
mat2 aurea_rot2(float radians) {
    const float c = cos(radians), s = sin(radians);
    return mat2(c, -s, s, c);
}

/// Faz a coordenada uv voltar para dentro com o tratamento de borda pedido.
/// `mode`: 0 = repetir, 1 = recortar (transparente), 2 = esticar a borda.
vec2 aurea_edge_uv(vec2 uv, int mode) {
    if (mode == 0) return fract(uv);
    if (mode == 1) return uv;
    return clamp(uv, 0.0, 1.0);
}

#endif
