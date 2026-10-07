#version 450
// =============================================================================
//  Aurea / shaders / effects / puppet.frag
//
//  Fantoche: a camada amostrada no ponto de repouso do triângulo deformado.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

void main() { o_color = texture(u_tex0, v_uv); }
