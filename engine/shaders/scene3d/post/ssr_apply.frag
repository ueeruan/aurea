#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / ssr_apply.frag
//
//  Aplica os reflexos de tela (ssr.frag, meia resolução) na cena HDR antes
//  da neblina, do DOF e do bloom. O reflexo substitui metade da luz do ponto
//  na proporção do peso (a reflexão do ambiente do pbr.frag que ele cobre) e
//  soma o resto: espelho escuro fica escuro, piso polido ganha o reflexo.
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_scene;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D t_ssr;

void main() {
    vec4 s = textureLod(t_scene, v_uv, 0.0);
    vec4 r = textureLod(t_ssr, v_uv, 0.0);
    vec3 scene = aurea_hdr_decode(s.rgb);
    float w = clamp(r.a, 0.0, 1.0);
    // O rgb vem pré-multiplicado pelo peso (ssr.frag): divide de volta.
    vec3 refl = w > 1e-4 ? r.rgb / w : vec3(0.0);
    vec3 outc = scene * (1.0 - 0.5 * w) + refl * w;
    o_color = vec4(aurea_hdr_encode(outc), s.a);
}
