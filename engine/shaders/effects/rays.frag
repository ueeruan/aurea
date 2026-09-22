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
    vec4 p1;   // x = centro X, y = centro Y, z = amostras (8..64), w = joelho do limiar
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
        const vec3 s = max(unpremultiply(texture(u_tex0, uv * p.uvMap.xy + p.uvMap.zw)).rgb, vec3(0.0));
        // Limiar com joelho suave: sem ele apareceria um degrau no meio do raio.
        const float luma = aurea_luma(aurea_linear_to_srgb(s));
        const float gate = smoothstep(threshold - knee, threshold + knee, luma);
        acc += s * gate * weight;
        wsum += weight;
        weight *= 1.0 - decay * 0.35;
    }
    acc *= p.p0.x / max(wsum, 1e-4);

    // Os raios podem guardar a cor da fonte ou receber uma cor própria.
    vec3 rays = p.p2.x > 0.5 ? acc * max(p.color.rgb, vec3(0.0)) : acc;
    if (abs(p.p2.y) > 1e-4) {
        const float c = cos(radians(p.p2.y)), s = sin(radians(p.p2.y));
        rays = vec3(rays.r * c - rays.g * s, rays.r * s + rays.g * c, rays.b);
    }

    o_color = premultiply(vec4(max(src.rgb + rays, vec3(0.0)), src.a));
}
