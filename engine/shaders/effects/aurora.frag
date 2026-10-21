#version 450
// =============================================================================
//  Aurea / shaders / effects / aurora.frag
//
//  Aurora, passe 2 (AuroraEffect.cpp). Cada pixel é um raio da câmera (a 3D
//  da composição, se seguir, mais a inclinação e o giro do grupo Ambiente).
//  Acima do horizonte: o céu, a luz da aurora lida da CÚPULA (passe 1,
//  aurora_dome.frag) e as estrelas. Abaixo: o raio espelhado na água, com
//  ondulação — a mesma cúpula, sem refazer as lâminas por pixel.
//
//  Cores lineares; saída opaca sobre a camada (pré-multiplicada).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"
#include "aurora_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_dome;

/// Raio da câmera pelo pixel (px da camada), y para cima.
vec3 view_ray(vec2 px) {
    const vec2 size = max(p.q2.zw, vec2(1.0));
    vec2 ndc = (px - 0.5 * size) / (0.5 * size.y);
    ndc.y = -ndc.y + p.p3.y;
    return normalize(p.right.xyz * ndc.x + p.up.xyz * ndc.y + p.forward.xyz * p.q1.w);
}

/// A luz da aurora na direção `d` (acima do horizonte), da cúpula.
vec3 aurora_at(vec3 d) {
    if (p.q3.z < 0.5 || d.y <= 0.002) return vec3(0.0);
    return texture(u_dome, aurora_dome_uv(d)).rgb;
}

/// Estrelas: uma por célula de um mapa da esfera, com cintilação.
vec3 stars(vec3 d, float t) {
    if (p.q0.w <= 0.0 || d.y <= 0.0) return vec3(0.0);
    const vec2 sph = vec2(atan(d.x, d.z), asin(clamp(d.y, -1.0, 1.0)));
    const vec2 grid = sph * 120.0;
    const vec2 cell = floor(grid);
    const float h = aurea_hash2(cell);
    const float density = p.q0.w * 0.35;
    if (h > density) return vec3(0.0);
    const vec2 pos = vec2(aurea_hash3(cell, 11u), aurea_hash3(cell, 23u));
    const float r = length(fract(grid) - pos);
    const float twinkle = 0.6 + 0.4 * sin(t * 3.0 + h * 40.0);
    const float glow = exp(-r * r * 160.0) * twinkle * smoothstep(0.0, 0.25, d.y);
    return vec3(0.75, 0.82, 1.0) * glow * (0.3 + 1.2 * aurea_hash3(cell, 41u));
}

/// O céu inteiro numa direção acima do horizonte.
vec3 sky(vec3 d, float t) {
    const float up = clamp(d.y, 0.0, 1.0);
    const vec3 aur = aurora_at(d);
    vec3 col = p.q1.rgb * (0.55 + 0.9 * pow(1.0 - up, 3.0));
    // Brilho do céu: a luz da aurora espalhada perto do horizonte.
    const vec3 glowTint = mix(p.color.rgb, p.q0.rgb, 0.35);
    col += glowTint * p.p1.w * 0.12 * pow(1.0 - up, 5.0) * p.p0.y;
    col += aur * (1.0 + p.p1.w * 0.6);
    col += stars(d, t) * (1.0 - clamp(dot(aur, vec3(0.33)) * 3.0, 0.0, 0.9));
    return col;
}

void main() {
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float amount = clamp(p.q3.x, 0.0, 1.0);
    if (amount <= 0.0) { o_color = src; return; }
    const vec2 px = p.p2.xy + v_uv * p.p2.zw;
    const float t = p.p3.x;
    const vec3 d = view_ray(px);
    vec3 col;
    if (d.y >= 0.0) {
        col = sky(d, t);
    } else {
        // Reflexo: o raio espelhado na água, com ondulação que cresce perto.
        const float ripple = p.p3.w;
        vec3 r = vec3(d.x, -d.y, d.z);
        if (ripple > 0.0) {
            const vec2 water = d.xz / max(-d.y, 0.02);
            const vec2 wave = vec2(aurea_fbm(water * 0.8 + vec2(t * 0.15, 0.0), 3, 0.5),
                                   aurea_fbm(water * 0.8 + vec2(3.7, t * 0.12), 3, 0.5)) - 0.5;
            r = normalize(vec3(d.x + wave.x * ripple * 0.06, -d.y + abs(wave.y) * ripple * 0.03, d.z));
        }
        // Fresnel simples: água calma é quase espelho; mais ainda perto do horizonte.
        const float fresnel = mix(0.6, 1.0, pow(1.0 - clamp(-d.y, 0.0, 1.0), 3.0));
        const vec3 deep = p.q1.rgb * 0.35;
        const float reflection = clamp(p.p3.z, 0.0, 1.0);
        col = deep + (reflection > 0.0 ? sky(r, t) * reflection * fresnel : vec3(0.0));
        // Faixa de névoa na linha do horizonte.
        col = mix(col, p.q1.rgb * 1.4 + mix(p.color.rgb, p.q0.rgb, 0.4) * p.p1.w * 0.05,
                  exp(-abs(d.y) * 220.0) * 0.5);
    }
    // Exposição: 40 é a referência neutra; cada 10 pontos ≈ meia parada.
    col *= exp2((p.p1.x - 40.0) / 20.0);
    col = max(col, vec3(0.0));
    o_color = mix(src, vec4(col, 1.0), amount);
}
