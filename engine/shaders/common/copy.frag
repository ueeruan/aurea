#version 450
// =============================================================================
//  Aurea / shaders / common / copy.frag
//
//  Cópia com mudança de escala/região. Usada quando a textura de uma layer
//  precisa ser realinhada (região de efeito diferente da entrada) e pelo cache
//  de frames renderizados.
// =============================================================================
#include "bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // uv de entrada = v_uv * xy + zw
} p;

void main() {
    o_color = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
}
