#version 450
// =============================================================================
//  Aurea / shaders / effects / shatter3d.frag
//
//  Uma face de uma peça do 3D Shatter: o pedaço da camada (no uv dele) com a
//  luz da face e o esmaecer. Pré-multiplicado, como toda imagem de camada.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in vec2 v_info;
layout(location = 0) out vec4 o_color;

void main() {
    const vec4 s = texture(u_tex0, v_uv);
    o_color = vec4(s.rgb * v_info.x, s.a) * v_info.y;
}
