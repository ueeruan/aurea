#version 460 core
#include <flutter/runtime_effect.glsl>

// CORRECAO DE COR, FUNDIDA (16/09).
//
// Levels, Brightness & Contrast, Hue/Saturation e Exposure olham um pixel
// so. Quatro efeitos seguidos na pilha viram UMA passada aqui, e nao
// quatro texturas fora da tela — e sem o arredondamento de 8 bits entre
// um efeito e o outro.
//
// Cada operacao sao dois vec4 (a, b) com o modo em a.x; os numeros ja
// chegam mastigados do Dart (1/gama, 2^stops). A mesma conta, linha a
// linha, esta em lib/src/features/editor/domain/correcao_de_cor.dart, e
// o teste compara as duas.
//
// Ordem dos floats: tamanho[0..1], filtro[2], quantas[3], operacoes[4..35].

uniform vec2 uSize;
uniform float uFilter;
uniform float uQuantas;
uniform vec4 uA0;
uniform vec4 uB0;
uniform vec4 uA1;
uniform vec4 uB1;
uniform vec4 uA2;
uniform vec4 uB2;
uniform vec4 uA3;
uniform vec4 uB3;
uniform sampler2D uImage;

out vec4 fragColor;

const vec3 LUMA = vec3(.2126, .7152, .0722);

vec4 amostra(vec2 uv) {
  // Mesma correcao do effects_v2: so os motores GLES antigos entregam a
  // entrada do filtro de cabeca para baixo.
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, clamp(uv, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize));
}

vec3 srgbParaLinear(vec3 c) {
  return mix(c / 12.92, pow((c + .055) / 1.055, vec3(2.4)), step(vec3(.04045), c));
}

vec3 linearParaSrgb(vec3 c) {
  c = max(c, vec3(0.0));
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - .055, step(vec3(.0031308), c));
}

vec3 rgbParaHsl(vec3 c) {
  float mx = max(c.r, max(c.g, c.b));
  float mn = min(c.r, min(c.g, c.b));
  float l = (mx + mn) * .5;
  float d = mx - mn;
  float h = 0.0;
  float s = 0.0;
  if (d > .00001) {
    s = l > .5 ? d / max(2.0 - mx - mn, .00001) : d / max(mx + mn, .00001);
    if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
    else h = (c.r - c.g) / d + 4.0;
    h /= 6.0;
  }
  return vec3(h, s, l);
}

vec3 hslParaRgb(float h, float s, float l) {
  vec3 k = clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  return vec3(l) + (1.0 - abs(2.0 * l - 1.0)) * s * (k - .5);
}

// 1 - LEVELS. a = (1, preto, 1/extensao, 1/gama); b = (saida preto, saida extensao).
vec3 niveis(vec3 c, vec4 a, vec4 b) {
  vec3 x = clamp((c - a.y) * a.z, 0.0, 1.0);
  if (abs(a.w - 1.0) > .0001) x = pow(x, vec3(a.w));
  return b.x + b.y * x;
}

// 2 - BRIGHTNESS & CONTRAST. a = (2, legado, brilho, contraste).
vec3 brilhoContraste(vec3 c, vec4 a) {
  if (a.y > .5) return (c + a.z - .5) * a.w + .5;
  vec3 x = clamp(c, 0.0, 1.0);
  if (abs(a.z - 1.0) > .0001) x = pow(x, vec3(a.z));
  if (abs(a.w - 1.0) > .0001) {
    vec3 baixo = .5 * pow(2.0 * x, vec3(a.w));
    vec3 alto = 1.0 - .5 * pow(max(2.0 - 2.0 * x, 0.0), vec3(a.w));
    x = mix(baixo, alto, step(vec3(.5), x));
  }
  return x;
}

// 3 - HUE/SATURATION. a = (3, colorir, matiz 0..1, saturacao -1..1); b = (luminosidade -1..1).
vec3 matizSaturacao(vec3 c, vec4 a, vec4 b) {
  c = clamp(c, 0.0, 1.0);
  float claro = b.x;
  if (a.y > .5) {
    float y = dot(c, LUMA);
    y = claro >= 0.0 ? y + (1.0 - y) * claro : y * (1.0 + claro);
    return hslParaRgb(a.z, a.w, y);
  }
  if (abs(a.z) > .00001) {
    vec3 hsl = rgbParaHsl(c);
    c = hslParaRgb(fract(hsl.x + a.z), hsl.y, hsl.z);
  }
  float sat = a.w;
  if (abs(sat) > .00001) {
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float l = (mx + mn) * .5;
    float d = mx - mn;
    float s = d < .00001 ? 0.0 : (l > .5 ? d / max(2.0 - mx - mn, .00001) : d / max(mx + mn, .00001));
    float k = sat < 0.0 ? sat : 1.0 / max(max(1.0 - sat, s), .0001) - 1.0;
    c += (c - vec3(l)) * k;
  }
  if (abs(claro) > .00001) c = claro >= 0.0 ? c + (1.0 - c) * claro : c * (1.0 + claro);
  return c;
}

// 4 - EXPOSURE. a = (4, linear, 2^stops, deslocamento); b = (1/gama).
vec3 exposicao(vec3 c, vec4 a, vec4 b) {
  bool linear = a.y > .5;
  vec3 x = linear ? srgbParaLinear(clamp(c, 0.0, 1.0)) : c;
  x = max(x * a.z + a.w, 0.0);
  if (abs(b.x - 1.0) > .0001) x = pow(x, vec3(b.x));
  return linear ? linearParaSrgb(x) : x;
}

vec3 aplicar(vec3 c, vec4 a, vec4 b) {
  float modo = a.x;
  if (modo < 1.5) c = niveis(c, a, b);
  else if (modo < 2.5) c = brilhoContraste(c, a);
  else if (modo < 3.5) c = matizSaturacao(c, a, b);
  else c = exposicao(c, a, b);
  return clamp(c, 0.0, 1.0);
}

void main() {
  vec4 original = amostra(FlutterFragCoord().xy / uSize);
  if (original.a <= .00001) {
    fragColor = vec4(0.0);
    return;
  }
  vec3 c = original.rgb / original.a;
  // UM LACO, e nao quatro chamadas: com quatro, o compilador copia as
  // quatro contas quatro vezes e o shader fica quatro vezes maior — e o
  // tamanho e o que o aparelho paga para montar o pipeline no primeiro uso.
  for (int k = 0; k < 4; k++) {
    float fk = float(k);
    if (fk > uQuantas - .5) break;
    vec4 a = fk < .5 ? uA0 : (fk < 1.5 ? uA1 : (fk < 2.5 ? uA2 : uA3));
    vec4 b = fk < .5 ? uB0 : (fk < 1.5 ? uB1 : (fk < 2.5 ? uB2 : uB3));
    c = aplicar(c, a, b);
  }
  fragColor = vec4(clamp(c, 0.0, 1.0) * original.a, original.a);
}
