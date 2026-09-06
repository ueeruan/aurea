#version 460 core
#include <flutter/runtime_effect.glsl>

// MODOS DE MESCLA QUE O FLUTTER NAO TEM.
//
// BlendMode do Flutter cobre os 17 do PDF; o Alight Motion e o After
// Effects tem mais uns dez que sao justamente os que dao o "look":
// Linear Burn fecha sombra sem lavar, Vivid Light e o contraste de
// grade de cor, Hard Mix e o cartaz de duas cores, Dissolve e a
// granulacao que nenhum fade consegue imitar.
//
// Aqui o fundo e o topo chegam como duas texturas ja alinhadas (o
// compositor desenhou as duas em imagens do tamanho da composicao) e a
// conta e a formula separavel classica, com Porter-Duff por cima:
//
//   Co = (1 - ab)*as*Cs + (1 - as)*ab*Cb + as*ab*B(Cb, Cs)
//   ao = as + ab*(1 - as)
//
// As imagens do Flutter chegam com alfa PRE-MULTIPLICADO; a formula
// pede cor pura, entao desfazemos na entrada e refazemos na saida.

uniform vec2 uSize;
uniform float uMode;
uniform float uSeed;      // so o Dissolve usa
uniform sampler2D uBase;  // o que ja estava embaixo
uniform sampler2D uTop;   // a camada

out vec4 fragColor;

const float MODE_LINEAR_BURN = 0.0;
const float MODE_LINEAR_LIGHT = 1.0;
const float MODE_VIVID_LIGHT = 2.0;
const float MODE_PIN_LIGHT = 3.0;
const float MODE_HARD_MIX = 4.0;
const float MODE_DIVIDE = 5.0;
const float MODE_SUBTRACT = 6.0;
const float MODE_DARKER_COLOR = 7.0;
const float MODE_LIGHTER_COLOR = 8.0;
const float MODE_DISSOLVE = 9.0;

float hash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}

float luma(vec3 c) {
  return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float burn1(float b, float s) {
  return s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b) / s);
}

float dodge1(float b, float s) {
  return s >= 1.0 ? 1.0 : min(1.0, b / (1.0 - s));
}

// Separaveis: a conta acontece canal a canal.
vec3 blendSeparable(float mode, vec3 b, vec3 s) {
  if (mode < MODE_LINEAR_LIGHT) {
    // Linear Burn: soma o que falta para o branco e tira do preto.
    return clamp(b + s - 1.0, 0.0, 1.0);
  }
  if (mode < MODE_VIVID_LIGHT) {
    // Linear Light: queima abaixo de 0,5 e clareia acima, em linha reta.
    return clamp(b + 2.0 * s - 1.0, 0.0, 1.0);
  }
  if (mode < MODE_PIN_LIGHT) {
    // Vivid Light: a mesma ideia, mas com queima/clareamento por divisao
    // — e o que aumenta contraste sem achatar o meio-tom.
    vec3 o;
    for (int i = 0; i < 3; i++) {
      o[i] = s[i] <= 0.5
          ? burn1(b[i], 2.0 * s[i])
          : dodge1(b[i], 2.0 * (s[i] - 0.5));
    }
    return o;
  }
  if (mode < MODE_HARD_MIX) {
    // Pin Light: troca o pixel so quando o topo e mais extremo.
    vec3 o;
    for (int i = 0; i < 3; i++) {
      o[i] = s[i] <= 0.5
          ? min(b[i], 2.0 * s[i])
          : max(b[i], 2.0 * (s[i] - 0.5));
    }
    return o;
  }
  if (mode < MODE_DIVIDE) {
    // Hard Mix: cada canal vai para 0 ou 1. Cartaz de duas cores.
    return step(1.0, b + s);
  }
  if (mode < MODE_SUBTRACT) {
    // Divide: clareia proporcional. Base de correcao de dominante.
    vec3 o;
    for (int i = 0; i < 3; i++) {
      o[i] = s[i] <= 0.0 ? 1.0 : min(1.0, b[i] / s[i]);
    }
    return o;
  }
  // Subtrair.
  return clamp(b - s, 0.0, 1.0);
}

void main() {
  vec2 pos = FlutterFragCoord().xy;
  vec2 uv = pos / uSize;

  vec4 pb = texture(uBase, uv);
  vec4 pt = texture(uTop, uv);

  float ab = pb.a;
  float as = pt.a;
  vec3 cb = ab > 0.0 ? pb.rgb / ab : vec3(0.0);
  vec3 cs = as > 0.0 ? pt.rgb / as : vec3(0.0);

  float mode = uMode;

  // DISSOLVE nao mistura cor: sorteia, pixel a pixel, se o topo aparece
  // inteiro ou nao aparece. E por isso que ele granula em vez de
  // esmaecer.
  if (mode >= MODE_DISSOLVE - 0.5) {
    float r = hash(pos + uSeed);
    fragColor = r < as ? vec4(cs, 1.0) : pb;
    return;
  }

  vec3 blended;
  if (mode >= MODE_DARKER_COLOR - 0.5 && mode < MODE_DISSOLVE - 0.5) {
    // Cor mais escura / mais clara: escolhe o PIXEL inteiro, nao canal a
    // canal — e o que preserva a matiz em vez de inventar cor nova.
    bool escuro = mode < MODE_LIGHTER_COLOR - 0.5;
    float lb = luma(cb);
    float ls = luma(cs);
    blended = (escuro ? (ls < lb) : (ls > lb)) ? cs : cb;
  } else {
    blended = blendSeparable(mode, cb, cs);
  }

  vec3 co = (1.0 - ab) * as * cs + (1.0 - as) * ab * cb + as * ab * blended;
  float ao = as + ab * (1.0 - as);

  fragColor = vec4(co, ao);
}
