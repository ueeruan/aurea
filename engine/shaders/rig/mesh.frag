#version 450
// =============================================================================
//  Aurea / shaders / rig / mesh.frag
//
//  Amostra a imagem (linear, pré-multiplicada) no uv do vértice. O blend
//  Normal do hardware junta os triângulos que se sobrepõem na dobra.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

void main() {
    o_color = texture(u_tex0, v_uv);
}
