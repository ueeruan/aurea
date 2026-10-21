#version 450
// =============================================================================
//  Aurea / shaders / effects / constellation_compose.frag
//
//  Fecha a Constelação: a rede (já somada ou sobreposta no alvo dela) presa
//  em 0..1 e, com "Mostrar camada", por cima da imagem da camada — soma na
//  mistura aditiva, "sobre" na normal. Sem ela, só a rede. As duas texturas
//  cobrem a MESMA região (o efeito não muda a caixa da camada).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a rede
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 mode;    // x = somar (0/1), y = mostrar a camada (0/1)
} p;

void main() {
    const vec4 s = clamp(texture(u_tex0, v_uv), 0.0, 1.0);
    if (p.mode.y < 0.5) {
        o_color = s;
        return;
    }
    const vec4 src = texture(u_tex1, v_uv);
    o_color = p.mode.x > 0.5 ? vec4(src.rgb + s.rgb, min(src.a + s.a, 1.0)) : s + src * (1.0 - s.a);
}
