#version 450
// =============================================================================
//  Aurea / shaders / effects / motion_detect.frag
//
//  DETECTAR MOVIMENTO — a diferença entre a camada agora (tex0) e a mesma
//  camada num quadro anterior (tex1). Onde nada mudou, preto; onde mudou, a
//  cor da mudança.
//
//  A diferença é tirada nos valores de TELA (sRGB), como o olho vê: no linear
//  uma mudança na sombra quase não aparece. O resultado volta para linear.
//
//    Todos       |agora − antes|
//    Mais claro  max(agora − antes, 0)   (só onde clareou)
//    Mais escuro max(antes − agora, 0)   (só onde escureceu)
//
//  depois × Brilho, + Levantar escuros × 0,1 (cinza nas regiões paradas, ou
//  negativo para apagar o ruído), a Saturação em torno da luminância, e a
//  Mistura com a imagem original.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_now;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_before;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // uv da saída → uv do passado: * xy + zw
    vec4 params;   // x brilho, y levantar escuros, z saturação, w modo (0 todos, 1 mais claro, 2 mais escuro)
    vec4 extra;    // x mistura, y 1 = há passado
} p;

void main() {
    const vec4 nowP = texture(u_now, v_uv);
    const vec4 now = unpremultiply(nowP);
    vec4 before = now;
    if (p.extra.y > 0.5) before = unpremultiply(texture(u_before, v_uv * p.uvMap.xy + p.uvMap.zw));

    const vec3 a = aurea_linear_to_srgb(clamp(now.rgb, 0.0, 1.0));
    const vec3 b = aurea_linear_to_srgb(clamp(before.rgb, 0.0, 1.0));
    const vec3 d = a - b;
    const int mode = int(p.params.w + 0.5);
    vec3 m = mode == 1 ? max(d, vec3(0.0)) : mode == 2 ? max(-d, vec3(0.0)) : abs(d);

    m = m * p.params.x + vec3(p.params.y * 0.1);
    const float lum = dot(m, vec3(0.2126, 0.7152, 0.0722));
    m = max(vec3(lum) + (m - vec3(lum)) * p.params.z, vec3(0.0));

    const vec3 result = aurea_srgb_to_linear(clamp(m, 0.0, 1.0));
    const vec3 rgb = mix(now.rgb, result, clamp(p.extra.x, 0.0, 1.0));
    o_color = premultiply(vec4(rgb, now.a));
}
