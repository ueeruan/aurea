#version 450
// =============================================================================
//  Aurea / shaders / effects / border_dilate.frag
//
//  Borda, 1º de 2 passes: a silhueta da camada DILATADA ao longo de um eixo.
//
//  Dilatar é um MÁXIMO, não uma média: a borda é o quanto a silhueta cresceu,
//  e uma média a deixaria desbotada justamente onde ela precisa ser sólida. O
//  resultado é a máscara da borda, do lado de fora da camada, em `.a`.
//
//  A caixa é de meia-largura `width` por passe, com 17 amostras; passos mais
//  largos que o traço não mudam nada, porque o máximo já pegou o pixel de
//  dentro. O passo chega em PIXELS da camada e vira uv com `p3`.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x,y = passo por amostra, em PIXELS da camada
    vec4 p1;
    vec4 p2;
    vec4 p3;   // x,y = px de layer por uv do que se amostra
    vec4 color;
} p;

const int kTaps = 8;

void main() {
    const vec2 uvPerPx = vec2(1.0) / max(p.p3.xy, vec2(1e-6));
    const vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 axis = p.p0.xy * uvPerPx;
    float m = 0.0;
    for (int i = -kTaps; i <= kTaps; ++i) {
        m = max(m, texture(u_tex0, uv + axis * float(i)).a);
    }
    o_color = vec4(0.0, 0.0, 0.0, m);
}
