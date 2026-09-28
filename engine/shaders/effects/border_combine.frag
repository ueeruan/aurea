#version 450
// =============================================================================
//  Aurea / shaders / effects / border_combine.frag
//
//  Borda, 2º de 2 passes: fecha a dilatação no eixo perpendicular (a silhueta
//  já dilatada chega em `u_tex0`) e pinta o ANEL — o que a dilatação ganhou
//  menos o que a camada já cobria.
//
//  A subtração `m - layer.a` é o que separa a borda do miolo: sem ela o traço
//  invadiria a camada e escureceria a imagem por dentro. O `clamp` existe
//  porque em meia precisão o máximo pode voltar um fio abaixo do alfa da
//  própria camada e dar um negativo.
//
//  A borda entra por BAIXO da camada (mesma composição do "over" com a camada
//  por cima): uma franja semitransparente ganha a cor do traço sem apagar o
//  que já estava lá. `u_tex1` está na região da camada, daí o mapa extra em
//  `p2`; o passo chega em pixels da camada e vira uv com `p3`.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // silhueta dilatada (alfa em .a)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada como o efeito a recebeu

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // região do efeito → região da silhueta (identidade: mesma região)
    vec4 texel;
    vec4 p0;      // x,y = passo por amostra, em PIXELS da camada, z = reservado, w = opacidade
    vec4 p1;
    vec4 p2;      // mapa de uv da camada: x,y = escala, z,w = deslocamento
    vec4 p3;      // x,y = px de layer por uv da silhueta (largura, altura da região)
    vec4 color;   // cor da borda, linear
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
    const vec4 layer = texture(u_tex1, v_uv * p.p2.xy + p.p2.zw);   // pré-multiplicado
    const float ring = clamp(m - layer.a, 0.0, 1.0) * clamp(p.p0.w, 0.0, 1.0) * clamp(p.color.a, 0.0, 1.0);
    const vec4 stroke = vec4(p.color.rgb * ring, ring);
    o_color = layer + stroke * (1.0 - layer.a);
}
