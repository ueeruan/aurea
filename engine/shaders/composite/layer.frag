#version 450
// =============================================================================
//  Aurea / shaders / composite / layer.frag
//
//  Amostra a textura da layer e aplica a opacidade. O blend `Normal`
//  (pré-multiplicado: src + dst*(1-srcA)) é do hardware — o shader não lê o
//  destino, e por isso não há custo de leitura de framebuffer por layer.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

void main() {
    o_color = texture(u_tex0, v_uv) * pc.params.x;
}
