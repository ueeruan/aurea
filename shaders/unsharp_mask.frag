#version 460 core
#include <flutter/runtime_effect.glsl>

// UNSHARP MASK (16/09), a conta do After Effects e do Photoshop:
//
//   desfoque = gaussiana de sigma = raio
//   detalhe  = original - desfoque           (por canal, pre-multiplicado)
//   saida    = original + quantidade * detalhe, onde |detalhe| >= limiar
//
// POR QUE O DESFOQUE MORA AQUI DENTRO. O detalhe precisa do original E do
// desfoque ao mesmo tempo, e um filtro de imagem do Flutter so recebe uma
// entrada. Duas copias da camada somadas por modo de mesclagem nao fecham a
// conta (nao ha "subtrair"), e fotografar a camada nao pega a textura de
// video. Entao o desfoque e amostrado aqui.
//
// E O IMPELLER LE A ENTRADA DO FILTRO SEM INTERPOLAR (amostragem "nearest",
// runtime_effect_filter_contents + ToSamplerDescriptor({})). O truque
// classico de pegar dois texels numa leitura so nao funciona; toda amostra
// cai no centro de um texel, e o nucleo e escolhido pelo sigma:
//
//   sigma < 0,62 texel  3x3 exato ................ 9 leituras
//   sigma < 1,12        disco de raio^2 <= 5 .... 21 leituras
//   sigma < 1,62        disco de raio^2 <= 10 ... 37 leituras
//   acima               amostragem por importancia: N pontos com a
//                       distribuicao da propria gaussiana (espiral de
//                       Vogel), girada por pixel — o serrilhado de uma grade
//                       esparsa vira grao fino, e grao fino some dentro da
//                       textura, que e justamente onde a nitidez age.
//
// O raio chega em pixel pensado em 1080p. uLogico e o tamanho da caixa em
// pixels logicos: uSize / uLogico e quantos texels a GPU usou por pixel, e
// e isso que faz a previa pequena e a exportacao 4K afiarem a mesma borda.
//
// Ordem dos floats: tamanho[0..1], filtro[2], logico[3..4], escalaRef[5],
// quantidade[6], raio[7], limiar[8], soLuma[9], amostras[10].

uniform vec2 uSize;
uniform float uFilter;
uniform vec2 uLogico;
uniform float uEscalaRef;
uniform float uQuantidade;
uniform float uRaio;
uniform float uLimiar;
uniform float uLuma;
uniform float uAmostras;
uniform sampler2D uImage;

out vec4 fragColor;

const vec3 LUMA = vec3(.2126, .7152, .0722);

// p em coordenadas de pixel, sempre no centro de um texel.
vec4 texel(vec2 p) {
  vec2 uv = clamp(p / uSize, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, uv);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  vec4 o = texel(p);

  float texelsPorPixel = .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0));
  float sigma = max(uRaio * uEscalaRef * texelsPorPixel, .05);
  float k = -.5 / (sigma * sigma);

  vec4 soma = vec4(0.0);
  float peso = 0.0;
  if (sigma < .62) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        float w = exp(k * float(i * i + j * j));
        soma += texel(p + vec2(float(i), float(j))) * w;
        peso += w;
      }
    }
  } else if (sigma < 1.12) {
    for (int j = -2; j <= 2; j++) {
      for (int i = -2; i <= 2; i++) {
        float d2 = float(i * i + j * j);
        if (d2 <= 5.0) {
          float w = exp(k * d2);
          soma += texel(p + vec2(float(i), float(j))) * w;
          peso += w;
        }
      }
    }
  } else if (sigma < 1.62) {
    for (int j = -3; j <= 3; j++) {
      for (int i = -3; i <= 3; i++) {
        float d2 = float(i * i + j * j);
        if (d2 <= 10.0) {
          float w = exp(k * d2);
          soma += texel(p + vec2(float(i), float(j))) * w;
          peso += w;
        }
      }
    }
  } else {
    // Giro por pixel (ruido de gradiente intercalado) e o angulo de ouro
    // aplicado por rotacao: nada de seno e cosseno por amostra.
    float giro = 6.2831853 * fract(52.9829189 * fract(dot(floor(p), vec2(.06711056, .00583715))));
    vec2 direcao = vec2(cos(giro), sin(giro));
    const float cosOuro = -.7373688;
    const float senOuro = .6754903;
    float n = clamp(floor(uAmostras + .5), 4.0, 48.0);
    for (int s = 0; s < 48; s++) {
      float fs = float(s);
      if (fs >= n) break;
      float u = (fs + .5) / n;
      vec2 d = floor(direcao * sigma * sqrt(-2.0 * log(1.0 - u)) + .5);
      soma += texel(p + d);
      direcao = vec2(cosOuro * direcao.x - senOuro * direcao.y, senOuro * direcao.x + cosOuro * direcao.y);
    }
    peso = n;
  }

  vec4 d = o - soma / max(peso, .00001);
  vec4 r;
  if (uLuma > .5) {
    float dy = dot(d.rgb, LUMA);
    float w = uLimiar > 0.0 ? smoothstep(uLimiar - .5 / 255.0, uLimiar + .5 / 255.0, abs(dy)) : 1.0;
    r = vec4(o.rgb + vec3(dy * uQuantidade * w), o.a);
  } else {
    vec4 w = uLimiar > 0.0
        ? smoothstep(vec4(uLimiar - .5 / 255.0), vec4(uLimiar + .5 / 255.0), abs(d))
        : vec4(1.0);
    r = o + d * uQuantidade * w;
  }
  r.a = clamp(r.a, 0.0, 1.0);
  fragColor = vec4(clamp(r.rgb, vec3(0.0), vec3(r.a)), r.a);
}
