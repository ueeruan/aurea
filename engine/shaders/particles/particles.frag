#version 450
// =============================================================================
//  Aurea / shaders / particles / particles.frag
//
//  Partícula redonda e macia (borda suave), cor já pré-multiplicada.
// =============================================================================
layout(location = 0) in vec2 v_local;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main() {
    const float r = length(v_local);
    const float a = 1.0 - smoothstep(0.35, 1.0, r);
    o_color = v_color * a;
}
