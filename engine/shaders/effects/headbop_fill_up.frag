#version 450
// =============================================================================
//  Aurea / shaders / effects / headbop_fill_up.frag
//
//  Headbop, fundo preenchido (push-pull, metade "pull"): o buraco de cada
//  nível recebe o nível mais grosso, na proporção do peso que falta.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // este nível (push)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // o nível mais grosso (já preenchido)

void main() {
    vec4 d = texture(u_tex0, v_uv);
    vec4 c = texture(u_tex1, v_uv);
    float a = clamp(d.a, 0.0, 1.0);
    o_color = d + (1.0 - a) * c;
}
