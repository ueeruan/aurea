#version 450
// Ampliação com preservação de detalhes (referência do AE): entrada
// pré-multiplicada -> cor reta (modo 0); alfa replicado nos três canais para o
// caminho com detalhe (modo 2); cor ampliada x alfa ampliado (modo 1).
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_color;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_alpha;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 mode;   // modo, largura, altura, alfa no canal (0 = .a, 1 = .r)
} p;

void main() {
    ivec2 at = clamp(ivec2(floor(v_uv * vec2(p.mode.yz))), ivec2(0), p.mode.yz - 1);
    vec4 c = texelFetch(u_color, at, 0);
    if (p.mode.x == 0) {
        o_color = c.a > 0.0 ? vec4(c.rgb / c.a, c.a) : vec4(0.0);
    } else if (p.mode.x == 2) {
        o_color = vec4(c.aaa, c.a);
    } else {
        vec4 a = texelFetch(u_alpha, at, 0);
        float alpha = clamp(p.mode.w == 0 ? a.a : a.r, 0.0, 1.0);
        o_color = vec4(c.rgb * alpha, alpha);
    }
}
