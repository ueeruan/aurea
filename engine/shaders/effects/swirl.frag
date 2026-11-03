#version 450
// =============================================================================
//  Aurea / shaders / effects / swirl.frag
//
//  Redemoinho (0.4.6): gira a imagem em volta de um centro, mais no meio e
//  nada na borda do raio (a queda é suave, sem emenda). Distância medida em
//  unidades de ALTURA: o raio é um círculo no quadro.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // xy = centro (uv), z = raio (fração da altura), w = ângulo (rad)
    vec4 p1;      // x = mistura
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const float aspect = max(p.texel.y / max(p.texel.x, 1e-9), 1e-9);
    vec2 d = inUv - p.p0.xy;
    d.x *= aspect;
    const float r = length(d);
    const float R = max(p.p0.z, 1e-4);
    vec2 uv = inUv;
    if (r < R) {
        const float t = 1.0 - r / R;
        const float a = p.p0.w * t * t;                // mais forte no centro
        const float c = cos(a), s = sin(a);
        vec2 q = vec2(c * d.x - s * d.y, s * d.x + c * d.y);
        q.x /= aspect;
        uv = p.p0.xy + q;
    }
    o_color = mix(texture(u_tex0, inUv), texture(u_tex0, uv), clamp(p.p1.x, 0.0, 1.0));
}
