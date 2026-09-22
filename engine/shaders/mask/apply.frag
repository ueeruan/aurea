#version 450
// =============================================================================
//  Aurea / shaders / mask / apply.frag
//
//  Camada × cobertura das máscaras. Cor pré-multiplicada: multiplicar os
//  quatro canais é multiplicar o alfa sem mexer na cor reta.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // cobertura (R8)

void main() {
    o_color = texture(u_tex0, v_uv) * texture(u_tex1, v_uv).r;
}
