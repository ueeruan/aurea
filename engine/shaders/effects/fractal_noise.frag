#version 450
// =============================================================================
//  Aurea / shaders / effects / fractal_noise.frag
//
//  Ruído fractal: oitavas de ruído (valor, Perlin ou simplex) somadas como
//  fBm, turbulência (|ruído|) ou cristas (1 − |ruído|)², em coordenadas do
//  plano da camada (px da resolução cheia). O terceiro eixo é a EVOLUÇÃO: uma
//  volta = uma célula inteira, o campo ferve no lugar em vez de deslizar.
//
//  Hash INTEIRO, sem sin(): a GPU do celular avalia sin() em precisão reduzida
//  e o ruído mostra uma grade. Semente inteira: keyframear a semente pula
//  entre campos, não desliza o reticulado.
//
//  O ruído é um NÍVEL DE CINZA como a pessoa o vê (codificado): vira linear
//  antes de misturar com a camada, que está linear e pré-multiplicada.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = tipo de ruído (0 blocos, 1 linear, 2 suave, 3 Perlin, 4 simplex), y = fractal (0 fBm, 1 turbulência, 2 cristas), z = contraste, w = brilho
    vec4 p1;   // x = célula (px), y = complexidade (oitavas, fracionária), z = evolução (células), w = semente
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // xy = translação depois do giro, z = rotação (rad), w = estouro (0 recorta, 1 suave, 2 envolve)
    vec4 color;
    vec4 q0;   // x = opacidade, y = modo de mistura, z = inverter, w = preencher a caixa toda
    vec4 q1;   // x = influência das oitavas (persistência)
    vec4 q2;
    vec4 q3;
} p;

float fn_hash(ivec3 c, uint seed) {
    uint h = 0x811C9DC5u ^ (seed * 2654435761u);
    h = (h ^ uint(c.x)) * 16777619u;
    h = (h ^ uint(c.y)) * 16777619u;
    h = (h ^ uint(c.z)) * 16777619u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return float(h & 0xFFFFFFu) / 16777216.0;
}

/// Gradiente pseudo-aleatório num ponto do reticulado (componentes em −1..1).
vec3 fn_gradient(ivec3 c, uint seed) {
    return vec3(fn_hash(c, seed), fn_hash(c, seed + 101u), fn_hash(c, seed + 202u)) * 2.0 - 1.0;
}

/// Ruído de valor 3D; `kind` escolhe a curva entre nós em x/y (blocos, linear,
/// suave). A evolução (z) é sempre suave: um campo de blocos ferve no lugar.
float fn_value(vec3 x, int kind, uint seed) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    if (kind == 0) f.xy = step(0.5, f.xy);
    else if (kind == 2) f.xy = f.xy * f.xy * (3.0 - 2.0 * f.xy);
    f.z = f.z * f.z * (3.0 - 2.0 * f.z);
    ivec3 c = ivec3(i);
    float v00 = mix(fn_hash(c, seed), fn_hash(c + ivec3(1, 0, 0), seed), f.x);
    float v10 = mix(fn_hash(c + ivec3(0, 1, 0), seed), fn_hash(c + ivec3(1, 1, 0), seed), f.x);
    float v01 = mix(fn_hash(c + ivec3(0, 0, 1), seed), fn_hash(c + ivec3(1, 0, 1), seed), f.x);
    float v11 = mix(fn_hash(c + ivec3(0, 1, 1), seed), fn_hash(c + ivec3(1, 1, 1), seed), f.x);
    return mix(mix(v00, v10, f.y), mix(v01, v11, f.y), f.z);
}

/// Ruído de gradiente (Perlin) 3D, curva quíntica, devolvido em 0..1.
float fn_perlin(vec3 x, uint seed) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    ivec3 c = ivec3(i);
    float n000 = dot(fn_gradient(c, seed), f);
    float n100 = dot(fn_gradient(c + ivec3(1, 0, 0), seed), f - vec3(1, 0, 0));
    float n010 = dot(fn_gradient(c + ivec3(0, 1, 0), seed), f - vec3(0, 1, 0));
    float n110 = dot(fn_gradient(c + ivec3(1, 1, 0), seed), f - vec3(1, 1, 0));
    float n001 = dot(fn_gradient(c + ivec3(0, 0, 1), seed), f - vec3(0, 0, 1));
    float n101 = dot(fn_gradient(c + ivec3(1, 0, 1), seed), f - vec3(1, 0, 1));
    float n011 = dot(fn_gradient(c + ivec3(0, 1, 1), seed), f - vec3(0, 1, 1));
    float n111 = dot(fn_gradient(c + ivec3(1, 1, 1), seed), f - vec3(1, 1, 1));
    float n = mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
                  mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
    // Gradientes em −1..1 por eixo: o produto chega a ~±0,87. 0,58 traz para ±0,5.
    return clamp(0.5 + n * 0.58, 0.0, 1.0);
}

/// Simplex 2D numa fatia inteira da evolução, em 0..1.
float fn_simplex2(vec2 x, int slice, uint seed) {
    const float F2 = 0.36602540378;   // (sqrt(3) − 1) / 2
    const float G2 = 0.21132486540;   // (3 − sqrt(3)) / 6
    float s = (x.x + x.y) * F2;
    vec2 i = floor(x + s);
    float t = (i.x + i.y) * G2;
    vec2 d0 = x - (i - t);
    vec2 o = d0.x > d0.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 d1 = d0 - o + G2;
    vec2 d2 = d0 - 1.0 + 2.0 * G2;
    ivec3 c0 = ivec3(int(i.x), int(i.y), slice);
    ivec3 c1 = c0 + ivec3(int(o.x), int(o.y), 0);
    ivec3 c2 = c0 + ivec3(1, 1, 0);
    vec3 w = max(0.5 - vec3(dot(d0, d0), dot(d1, d1), dot(d2, d2)), 0.0);
    w = w * w * w * w;
    float n = w.x * dot(fn_gradient(c0, seed).xy, d0)
            + w.y * dot(fn_gradient(c1, seed).xy, d1)
            + w.z * dot(fn_gradient(c2, seed).xy, d2);
    return clamp(0.5 + n * 45.0, 0.0, 1.0);
}

/// Simplex com evolução: duas fatias inteiras misturadas — o campo evolui
/// suavemente sem um simplex 3D inteiro.
float fn_simplex(vec3 x, uint seed) {
    float z = floor(x.z);
    float f = x.z - z;
    f = f * f * (3.0 - 2.0 * f);
    return mix(fn_simplex2(x.xy, int(z), seed), fn_simplex2(x.xy, int(z) + 1, seed), f);
}

float fn_noise(vec3 x, int kind, uint seed) {
    if (kind == 3) return fn_perlin(x, seed);
    if (kind == 4) return fn_simplex(x, seed);
    return fn_value(x, kind, seed);
}

/// Estouro suave: identidade no meio, ombro racional no último quarto de
/// cada ponta, que só chega a 0 e 1 no infinito — o que o contraste empurra
/// para fora é comprimido de volta, não achatado num platô.
float fn_soft_clamp(float x) {
    const float k = 0.25;
    float hi = x - (1.0 - k);
    if (hi > 0.0) x = (1.0 - k) + k * hi / (k + hi);
    float lo = k - x;
    if (lo > 0.0) x = k - k * lo / (k + lo);
    return x;
}

void main() {
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float fillBox = p.q0.w;
    const float cover = fillBox > 0.5 ? 1.0 : src.a;
    const float opacity = clamp(p.q0.x, 0.0, 1.0);
    if (cover <= 0.0 || opacity <= 0.0) {
        o_color = src;
        return;
    }

    const int noiseKind = int(p.p0.x + 0.5);
    const int fractal = int(p.p0.y + 0.5);
    const int overflow = int(p.p3.w + 0.5);
    const uint seed = uint(max(p.p1.w, 0.0) + 0.5);

    // Ponto no plano da camada, girado em volta do centro e deslocado.
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float cs = cos(p.p3.z), sn = sin(p.p3.z);
    vec2 pos = vec2(cs * point.x - sn * point.y, sn * point.x + cs * point.y) + p.p3.xy;
    float cell = max(p.p1.x, 1e-3);
    float z = p.p1.z;
    const float persistence = clamp(p.q1.x, 0.0, 1.0);

    float sum = 0.0;
    float norm = 0.0;
    float amp = 1.0;
    for (int i = 0; i < 8; ++i) {
        // Peso fracionário: a oitava entra em fade quando a complexidade é
        // keyframeada, em vez de aparecer de repente.
        const float w = clamp(p.p1.y - float(i), 0.0, 1.0) * amp;
        if (w <= 0.0) break;
        const vec3 q = vec3(pos / cell, z) + vec3(float(i) * 17.0, float(i) * 29.0, float(i) * 7.0);
        const float v = fn_noise(q, noiseKind, seed + uint(i) * 977u);
        float t = v;
        if (fractal == 1) t = abs(2.0 * v - 1.0);
        else if (fractal == 2) {
            t = 1.0 - abs(2.0 * v - 1.0);
            t *= t;
        }
        sum += t * w;
        norm += w;
        amp *= persistence;
        cell *= 0.5;
        z *= 2.0;
    }
    float n = sum / max(norm, 1e-4);

    // Contraste em volta do cinza médio; brilho soma.
    n = (n - 0.5) * p.p0.z + 0.5 + p.p0.w;
    if (overflow == 1) n = fn_soft_clamp(n);
    else if (overflow == 2) n = 1.0 - abs(mod(n, 2.0) - 1.0);
    n = clamp(n, 0.0, 1.0);
    if (p.q0.z > 0.5) n = 1.0 - n;

    // Mistura com a camada, em linear e alfa reto; a saída volta pré-multiplicada.
    const vec3 base = src.a > 1e-6 ? src.rgb / src.a : vec3(0.0);
    const vec3 gen = vec3(aurea_srgb_to_linear(n));
    const vec3 blended = clamp(aurea_blend_generated(base, gen, int(p.q0.y + 0.5)), 0.0, 1.0);
    const vec3 outc = mix(base, blended, opacity);
    o_color = vec4(outc * cover, cover);
}
