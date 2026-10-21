#version 450
// =============================================================================
//  Aurea / shaders / effects / plugin_prepare.frag
//
//  O preparo da entrada de um plugin "shader" (plugins/PluginEffects.cpp):
//  copia a imagem da camada para a REGIÃO DE TRABALHO do plugin (a caixa mais
//  a margem que ele declara, com borda transparente) e, nos plugins
//  `colorSpace: srgb` (ISF), troca o linear pré-multiplicado do motor pelo
//  sRGB de alfa reto que o shader do autor espera. Assim toda textura que o
//  shader do plugin vê cobre o mesmo retângulo, com o mesmo `uv`.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // uv de entrada = v_uv * xy + zw
    vec4 mode;    // x = 1: sRGB de alfa reto
} p;

void main() {
    vec4 c = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    if (p.mode.x > 0.5) {
        c = c.a > 1e-5 ? vec4(linear_to_srgb(c.rgb / c.a), c.a) : vec4(0.0);
    }
    o_color = c;
}
