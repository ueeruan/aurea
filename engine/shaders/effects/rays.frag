#version 450
// =============================================================================
//  Aurea / shaders / effects / rays.frag
//
//  Raios de luz (Fase 7.3 §35): a luz das partes claras se espalha em linha
//  reta a partir de um ponto. É a conta clássica de espalhamento radial —
//  marcha em direção à fonte, colhendo o que passa do limiar, decaindo.
//
//  Um passe só: o limiar é aplicado em CADA amostra, então não existe a
//  textura intermediária "só o brilho" — que seria um passe a mais por nada,
//  já que a marcha lê a imagem original de qualquer jeito.
//
//  A saída pode ser MAIOR que a entrada (Rays::rays_region): o uv da saída
//  vai ao da entrada por `uvMap`, e fora dela o amostrador é transparente.
//  Os raios carregam alfa próprio — num texto (glifos sobre transparente) a
//  luz aparece em volta das letras, como o brilho; antes o alfa da saída era
//  o da fonte e todo raio fora dos glifos sumia.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade, y = comprimento (0..1 do caminho), z = limiar (0..1), w = decaimento
    vec4 p1;   // x = centro X, y = centro Y (uv da SAÍDA), z = amostras (8..64), w = joelho do limiar
    vec4 p2;   // x = 1 guardar a cor da fonte, y = girar a cor (graus)
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));

    const vec2 delta = (p.p1.xy - v_uv) * clamp(p.p0.y, 0.0, 1.0) / max(p.p1.z, 1.0);
    const float knee = max(p.p1.w, 1e-4);
    const float threshold = clamp(p.p0.z, 0.0, 1.0);
    const float decay = clamp(p.p0.w, 0.0, 1.0);

    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    vec2 uv = v_uv;
    float weight = 1.0;
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= p.p1.z) break;
        uv += delta;
        const vec4 sampled = unpremultiply(texture(u_tex0, uv * p.uvMap.xy + p.uvMap.zw));
        const vec3 s = max(sampled.rgb, vec3(0.0));
        // Limiar com joelho suave: sem ele apareceria um degrau no meio do raio.
        const float luma = aurea_luma(aurea_linear_to_srgb(s));
        const float gate = smoothstep(threshold - knee, threshold + knee, luma);
        // Pesado pela cobertura: pixel transparente não emite (nem o RGB
        // escondido atrás de alfa zero), a borda do glifo emite em parte.
        acc += s * (gate * sampled.a * weight);
        wsum += weight;
        weight *= 1.0 - decay * 0.35;
    }
    acc *= p.p0.x / max(wsum, 1e-4);

    // Guardar a cor da fonte: os raios têm a cor do que os emitiu, tingida
    // pela cor. Desligado: a luz é só a intensidade, na cor escolhida.
    const vec3 tint = max(p.color.rgb, vec3(0.0));
    vec3 rays = p.p2.x > 0.5 ? acc * tint : vec3(aurea_luma(acc)) * tint;
    if (abs(p.p2.y) > 1e-4) {
        const float c = cos(radians(p.p2.y)), s = sin(radians(p.p2.y));
        rays = vec3(rays.r * c - rays.g * s, rays.r * s + rays.g * c, rays.b);
    }
    rays = max(rays, vec3(0.0));

    // Pré-multiplicado: luz SOMADA sobre a fonte. Onde a fonte é opaca dá o
    // mesmo de antes (fonte + raios); no transparente o raio vira cobertura.
    const float coverage = clamp(max(rays.r, max(rays.g, rays.b)), 0.0, 1.0);
    o_color = vec4(src.rgb * src.a + rays, src.a + coverage * (1.0 - src.a));
}
