// =============================================================================
//  Aurea / shaders / effects / saber_common.glsl
//
//  O bloco de uniforms do Saber (espelho de `SaberUniforms` em SaberEffect.cpp,
//  std140, só vec4) e a distorção animada, a MESMA nos dois passes (núcleo e
//  composição) — o brilho deforma junto com o núcleo.
//
//  Tudo em px da camada (resolução cheia): a prévia em 1/4 e o export em 4K
//  desenham o mesmo traço. Cor linear pré-multiplicada.
// =============================================================================
#ifndef AUREA_SABER_COMMON_GLSL
#define AUREA_SABER_COMMON_GLSL

#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dst;        // região de saída (px da camada)
    vec4 src;        // região da entrada
    vec4 box;        // caixa natural da camada
    vec4 info;       // x = px por texel de saída, y = tipo de núcleo, z = usar alfa, w = brilho global (cintilação)
    vec4 seg;        // núcleo único: início xy, fim xy (px)
    vec4 core;       // x = raio do núcleo (px), y = suavidade 0..1, z = tamanho inicial, w = tamanho final
    vec4 window;     // x = deslocamento inicial, y = final (0..1), z = brilho do deslocamento, w = outra camada (0/1)
    vec4 caps;       // x = arredondamento inicial, y = final (0..1), z, w livres
    vec4 dist;       // x = tipo, y = quantidade (px), z = escala (px), w = fase no tempo
    vec4 misc;       // x = semente, y, z, w livres
    vec4 affA;       // composição ← camada: cx = a.x*x + a.z*y + b.x; cy = a.y*x + a.w*y + b.y
    vec4 affB;       // xy = translação, zw livres
    vec4 mapRegion;  // região da outra camada (px da composição)
    vec4 glowColor;  // rgb linear da cor do brilho, w = ganho do brilho
    vec4 coreColor;  // rgb linear da cor do núcleo, w = saturação do núcleo 0..1
    vec4 look;       // x = composição (0 somar, 1 transparente, 2 sobre preto), y = brilho ligado, z = ganho do núcleo, w livre
} p;

/// O ponto da camada (px) que este texel de saída representa.
vec2 saber_px() { return p.dst.xy + v_uv * p.dst.zw; }

/// Deslocamento (px) da distorção no ponto `px`. `scaleMul` alarga o ruído
/// (o brilho usa um ruído mais largo que o núcleo). Função pura do ponto, da
/// fase e da semente: o mesmo quadro dá o mesmo traço na prévia e no export.
vec2 saber_warp(vec2 px, float scaleMul) {
    const int type = int(p.dist.x + 0.5);
    if (type <= 0 || p.dist.y <= 0.0) return vec2(0.0);
    const float s = max(p.dist.z * scaleMul, 1.0);
    const vec2 q = px / s + vec2(p.misc.x * 13.17, p.misc.x * 7.31);
    const float t = p.dist.w;
    vec2 d;
    if (type == 1) {
        // Suave / ondulada: ondas largas que respiram.
        d = vec2(sin(q.y * 2.1 + t * 1.7 + sin(q.x * 0.7 + t)), cos(q.x * 1.9 - t * 1.3 + sin(q.y * 0.6 - t * 0.7))) * 0.6;
        d += (vec2(aurea_fbm(q + vec2(t * 0.2), 2, 0.5), aurea_fbm(q + vec2(41.3, 17.9) - vec2(t * 0.2), 2, 0.5)) - 0.5) * 0.8;
    } else if (type == 2) {
        // Turbulenta: ruído fractal que anda.
        const vec2 o = vec2(t * 0.35, -t * 0.27);
        d = (vec2(aurea_fbm(q * 1.3 + o, 4, 0.55), aurea_fbm(q * 1.3 + vec2(19.7, 7.3) - o, 4, 0.55)) - 0.5) * 2.4;
    } else if (type == 3) {
        // Elétrica: cristas finas que SALTAM (12 sorteios por unidade de fase).
        const uint step_t = uint(int(floor(t * 12.0)));
        const vec2 o = vec2(aurea_hash(uvec2(step_t, 7u)), aurea_hash(uvec2(step_t, 11u))) * 50.0;
        d = vec2(aurea_turbulence(q * 2.0 + o, 5, 0.6), aurea_turbulence(q * 2.0 + o + vec2(31.7, 3.1), 5, 0.6));
        d = (d - 0.3) * 3.2;
    } else {
        // Fumaça: domínio torcido que sobe devagar.
        const vec2 o = vec2(0.0, t * 0.45);
        const vec2 w = vec2(aurea_fbm(q + o, 3, 0.5), aurea_fbm(q + o + vec2(5.2, 1.3), 3, 0.5));
        d = (vec2(aurea_fbm(q + 2.5 * w + o, 4, 0.5), aurea_fbm(q + 2.5 * w + o + vec2(9.1, 2.8), 4, 0.5)) - 0.5) * 2.6;
    }
    return d * p.dist.y;
}

/// Desvio do núcleo de CAMINHO (único e máscaras), em unidades de
/// "quantidade": função da posição ao longo dele (px / escala), da fase e da
/// semente. Deslocar a linha na normal (e não o plano inteiro) faz ela ondular
/// sem rasgar — o raio elétrico continua um traço só.
float saber_wave(float s) {
    const int type = int(p.dist.x + 0.5);
    const float t = p.dist.w;
    const float seed = p.misc.x;
    if (type == 1) return 0.65 * sin(s * 2.3 + t * 1.7 + seed) + 0.35 * sin(s * 5.3 - t * 2.6 + 1.3 + seed * 1.7);
    if (type == 2) return (aurea_fbm(vec2(s * 1.6 - t * 0.8, seed * 3.1 + t * 0.35), 4, 0.55) - 0.5) * 2.6;
    if (type == 3) {
        // Zigue-zague em oitavas (interpolação reta = quinas de raio), sorteado
        // de novo 12 vezes por unidade de fase.
        const uint salt = uint(int(floor(t * 12.0))) * 7919u + uint(int(seed)) * 104729u;
        float v = 0.0, amp = 0.7, f = 1.6;
        for (int o = 0; o < 4; ++o) {
            const float x = s * f;
            const float i = floor(x);
            const uint k = uint(int(i));
            const uint so = salt + uint(o) * 131u;
            const float a = aurea_hash(uvec2(k, so)) * 2.0 - 1.0;
            const float b = aurea_hash(uvec2(k + 1u, so)) * 2.0 - 1.0;
            v += amp * mix(a, b, x - i);
            amp *= 0.5;
            f *= 2.7;
        }
        return v;
    }
    return (aurea_fbm(vec2(s * 0.9 + t * 0.25, seed * 1.3 - t * 0.4), 3, 0.5) - 0.5) * 1.6;
}

#endif
