#version 450
// =============================================================================
//  Aurea / shaders / effects / drop_shadow_blur.frag
//
//  Sombra projetada, 1º de 2 passes: a SILHUETA da camada (o alfa dela) já
//  deslocada pela direção/distância, borrada ao longo de UM eixo.
//
//  A sombra é só alfa — a cor entra no segundo passe. Guardar a silhueta no
//  alfa de um alvo próprio é o que deixa o borrão ser separável (uma linha por
//  passe em vez de uma grade) sem carregar a imagem junto.
//
//  Tudo é medido em PIXELS DA CAMADA e convertido para uv aqui dentro, com a
//  densidade que o C++ manda em `p3` (px de layer por uv do que está sendo
//  amostrado): o mesmo número desloca o mesmo tanto na horizontal e na
//  vertical, e o mesmo tanto no preview e no export. O primeiro passe anda pela
//  DIREÇÃO, o segundo pela PERPENDICULAR — é o par que fecha o borrão
//  isotrópico, que a versão antiga (segundo passe sempre na vertical) não
//  conseguia numa sombra na diagonal.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // uv da saída → uv da entrada
    vec4 texel;
    vec4 p0;      // x,y = passo do borrão por amostra, em PIXELS da camada
    vec4 p1;      // x,y = deslocamento da sombra, em PIXELS da camada
    vec4 p2;
    vec4 p3;      // x,y = px de layer por uv do que se amostra (largura, altura)
    vec4 color;
} p;

const int kTaps = 8;

void main() {
    // px de layer → uv, com o eixo certo para cada dimensão.
    const vec2 uvPerPx = vec2(1.0) / max(p.p3.xy, vec2(1e-6));
    const vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 base = uv - p.p1.xy * uvPerPx;
    const vec2 axis = p.p0.xy * uvPerPx;
    float sum = 0.0, weight = 0.0;
    for (int i = -kTaps; i <= kTaps; ++i) {
        // Gaussiana de cauda curta: 17 amostras bastam para a sombra não
        // mostrar degrau.
        const float w = exp(-3.0 * float(i * i) / float(kTaps * kTaps));
        sum += texture(u_tex0, base + axis * float(i)).a * w;
        weight += w;
    }
    o_color = vec4(0.0, 0.0, 0.0, sum / max(weight, 1e-6));
}
