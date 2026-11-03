#version 450
// =============================================================================
//  Aurea / shaders / effects / tunnel.frag
//
//  Túnel (0.4.6): a imagem forra as paredes de um túnel infinito visto de
//  frente. u = ângulo em volta do centro, v = profundidade (1/raio). A
//  imagem se repete ao longo do túnel; `deslocamento` anima a viagem e
//  `torção` gira as paredes com a profundidade. O fundo escurece (névoa).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // xy = centro (uv), z = profundidade (repetições), w = deslocamento
    vec4 p1;      // x = torção (voltas por repetição), y = névoa 0..1, z = mistura
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const float aspect = max(p.texel.y / max(p.texel.x, 1e-9), 1e-9);
    vec2 d = inUv - p.p0.xy;
    d.x *= aspect;
    const float r = max(length(d), 1e-4);
    const float depth = max(p.p0.z, 0.05) * 0.25 / r;          // longe = perto do centro
    const float ang = atan(d.y, d.x) / AUREA_TAU + 0.5 + p.p1.x * depth;
    const vec2 uv = vec2(fract(ang), fract(depth + p.p0.w));
    vec4 wall = texture(u_tex0, uv);
    // Névoa: o fundo (depth alto) some no escuro, sem quebrar o pré-multiplicado.
    wall *= 1.0 - clamp(p.p1.y, 0.0, 1.0) * clamp(depth / (4.0 * max(p.p0.z, 0.05)), 0.0, 1.0);
    o_color = mix(texture(u_tex0, inUv), wall, clamp(p.p1.z, 0.0, 1.0));
}
