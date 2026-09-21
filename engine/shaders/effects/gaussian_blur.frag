#version 450
// =============================================================================
//  Aurea / shaders / effects / gaussian_blur.frag
//
//  Uma passada (horizontal OU vertical) de um gaussiano separável.
//
//  Os pesos vêm do C++, já normalizados e já AGRUPADOS EM PARES: duas amostras
//  vizinhas com pesos w1 e w2 viram uma única amostra bilinear no ponto
//  (o1*w1 + o2*w2)/(w1+w2) com peso w1+w2. É exato (a interpolação bilinear
//  faz a média ponderada de graça) e corta as leituras pela metade.
//
//  Raio grande não chega aqui em resolução cheia: o EffectGraph reduz a imagem
//  até o sigma caber em ~8 texels e borra lá. Sem isso, um blur de raio 200 em
//  4K seria centenas de leituras por pixel.
//
//  A borda (repetir ou transparente) é escolhida pelo SAMPLER que o C++
//  amarra, não por ramo no shader.
// =============================================================================
#include "../common/bindings.glsl"

#define MAX_PAIRS 24

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;                  // uv de entrada = v_uv * xy + zw
    vec4 dir;                    // xy = passo de um texel na direção, em uv de entrada
    vec4 header;                 // x = peso central, y = quantidade de pares
    vec4 pairs[MAX_PAIRS / 2];   // (deslocamento, peso) x2 por vec4
} p;

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 acc = texture(u_tex0, uv) * p.header.x;
    int count = int(p.header.y + 0.5);
    for (int i = 0; i < MAX_PAIRS; ++i) {
        if (i >= count) break;
        vec4 pr = p.pairs[i / 2];
        vec2 ow = (i % 2 == 0) ? pr.xy : pr.zw;
        vec2 d = p.dir.xy * ow.x;
        acc += (texture(u_tex0, uv + d) + texture(u_tex0, uv - d)) * ow.y;
    }
    o_color = acc;
}
