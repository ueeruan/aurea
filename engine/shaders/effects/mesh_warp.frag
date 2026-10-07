#version 450
// Malha de deformação: a camada no uv da grade regular (fora dela = transparente).
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
void main() { o_color = texture(u_tex0, v_uv); }
