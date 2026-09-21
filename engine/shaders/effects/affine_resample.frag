#version 450
// =============================================================================
//  Aurea / shaders / effects / affine_resample.frag
//
//  Reamostragem afim: cada pixel de saída busca o ponto correspondente na
//  entrada pela matriz INVERSA. É o efeito Transform quando ele não pode ser
//  dobrado na composição (há um efeito de vizinhança depois dele, que precisa
//  ver a imagem já transformada).
//
//  Fora da entrada: transparente (sampler com borda transparente amarrado pelo
//  C++), nunca a borda esticada.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 row0;     // uv de entrada = (row0.xyz . (v_uv,1), row1.xyz . (v_uv,1))
    vec4 row1;
    vec4 params;   // x = opacidade
} p;

void main() {
    vec3 h = vec3(v_uv, 1.0);
    vec2 uv = vec2(dot(p.row0.xyz, h), dot(p.row1.xyz, h));
    o_color = texture(u_tex0, uv) * p.params.x;
}
