#version 450
// =============================================================================
//  Aurea / shaders / effects / stroke3d.frag
//
//  Um trecho do 3D Stroke: a cápsula entre a e b (raio variando ao longo
//  dela), com um texel de antialiasing na borda e a cor do degradê.
//  Pré-multiplicado.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_pos;
layout(location = 1) flat in vec4 v_seg;
layout(location = 2) flat in vec4 v_radius;
layout(location = 3) flat in vec4 v_colorA;
layout(location = 4) flat in vec4 v_colorB;
layout(location = 0) out vec4 o_color;

void main() {
    const vec2 a = v_seg.xy, b = v_seg.zw;
    const vec2 ab = b - a;
    const float h = clamp(dot(v_pos - a, ab) / max(dot(ab, ab), 1e-8), 0.0, 1.0);
    const float d = length(v_pos - (a + ab * h));
    const float r = mix(v_radius.x, v_radius.y, h);
    const float cov = clamp((r - d) / max(v_radius.w, 1e-3) + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    o_color = mix(v_colorA, v_colorB, h) * (cov * v_radius.z);
}
