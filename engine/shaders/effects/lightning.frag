#version 450
// =============================================================================
//  Aurea / shaders / effects / lightning.frag
//
//  Raio (0.4.6): uma descarga elétrica do ponto A ao ponto B, desenhada sobre
//  a camada. O caminho é a reta A→B desviada na perpendicular por ruído
//  fractal (as pontas ficam presas: o desvio some em A e em B); o núcleo é
//  claro e o brilho é a cor. `evolução` anima a forma (e é determinística:
//  o mesmo quadro dá o mesmo raio no preview e no export).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // xy = A (px da região), zw = B (px da região)
    vec4 p1;      // x = desvio (px), y = espessura (px), z = brilho (px), w = evolução
    vec4 p2;      // x = detalhe (ondulações ao longo do raio), y = intensidade, z = galhos 0..1
    vec4 p3;      // xy = tamanho da região (px)
    vec4 color;
} p;

// Desvio perpendicular no ponto t (0..1) do caminho: preso nas pontas.
float bolt_offset(float t, float seed) {
    const float pin = sin(3.14159265 * clamp(t, 0.0, 1.0));
    return (aurea_fbm(vec2(t * p.p2.x, seed), 5, 0.55) * 2.0 - 1.0) * pin;
}

float bolt_distance(vec2 pos, vec2 a, vec2 b, float amp, float seed) {
    const vec2 ab = b - a;
    const float len = max(length(ab), 1e-3);
    const vec2 dir = ab / len, nrm = vec2(-dir.y, dir.x);
    // Busca local: o ponto da reta mais perto e uns vizinhos ao longo dela.
    const float t0 = clamp(dot(pos - a, dir) / len, 0.0, 1.0);
    float best = 1e9;
    for (int i = -8; i <= 8; ++i) {
        const float t = clamp(t0 + float(i) * (amp / len) * 0.25, 0.0, 1.0);
        const vec2 q = a + dir * (t * len) + nrm * (bolt_offset(t, seed) * amp);
        best = min(best, length(pos - q));
    }
    return best;
}

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 base = texture(u_tex0, inUv);
    const vec2 pos = v_uv * p.p3.xy;
    const float seed = floor(p.p1.w) * 13.17 + fract(p.p1.w) * 2.0;
    float d = bolt_distance(pos, p.p0.xy, p.p0.zw, p.p1.x, seed);
    // Galhos: dois ramos curtos que saem do meio do raio.
    if (p.p2.z > 0.0) {
        for (int k = 0; k < 2; ++k) {
            const float t = 0.35 + 0.3 * float(k);
            const vec2 ab = p.p0.zw - p.p0.xy;
            const vec2 dir = normalize(ab + vec2(1e-3)), nrm = vec2(-dir.y, dir.x);
            const vec2 start = p.p0.xy + ab * t + nrm * (bolt_offset(t, seed) * p.p1.x);
            const float side = k == 0 ? 1.0 : -1.0;
            const vec2 end = start + (dir * 0.35 + nrm * side * 0.45) * length(ab) * 0.35 * p.p2.z;
            d = min(d, bolt_distance(pos, start, end, p.p1.x * 0.5, seed + 31.0 * float(k + 1)) + p.p1.y * 0.4);
        }
    }
    const float core = 1.0 - smoothstep(p.p1.y * 0.5, p.p1.y * 0.5 + 1.0, d);
    const float glow = exp(-d / max(p.p1.z, 0.5)) * 0.8;
    const float inten = max(p.p2.y, 0.0);
    // Núcleo branco + brilho da cor, somados por cima (luz).
    const vec3 light = (vec3(1.0) * core + p.color.rgb * glow) * inten * p.color.a;
    const float a = clamp(max(core, glow) * inten * p.color.a, 0.0, 1.0);
    o_color = vec4(base.rgb + light, max(base.a, a));
}
