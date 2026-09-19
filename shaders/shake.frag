#version 460 core
#include <flutter/runtime_effect.glsl>

// S_Shake e S_DissolveShake (Sapphire), aba Distorcer (16/09). O tremor por
// quadro vem pronto do Dart (lib/src/features/editor/domain/shake.dart):
//   p0..p2  R, G, B no inicio do obturador: (tx, ty em px do AE, escala, angulo)
//   p3..p5  R, G, B no fim do obturador
//   p6      (borda X, borda Y [0 nenhuma, 1 repetir, 2 espelhar], amostras, mono)
//   p7      (opacidade, MISTURA, -, -)
//
// MISTURA (Advanced Shake): 0 deixa a imagem como ela chegou e 1 entrega o
// tremor inteiro. E o unico parametro que dosa o efeito sem mexer nos
// numeros dos eixos. O atalho abaixo evita a leitura a mais quando a
// mistura esta cheia, que e o caso comum.
// Aqui so se reamostra a imagem (bilinear manual: a entrada chega nearest).

uniform vec2 uSize;
uniform float uFilter;
uniform vec2 uLogico;
uniform float uEscalaRef;
uniform float uTempo;
uniform float uModo;
uniform vec4 p0;
uniform vec4 p1;
uniform vec4 p2;
uniform vec4 p3;
uniform vec4 p4;
uniform vec4 p5;
uniform vec4 p6;
uniform vec4 p7;
uniform vec4 p8;
uniform vec4 p9;
uniform vec4 p10;
uniform vec4 p11;
uniform vec4 p12;
uniform vec4 p13;
uniform vec4 p14;
uniform vec4 p15;
uniform vec4 c0;
uniform vec4 c1;
uniform sampler2D uImage;

out vec4 fragColor;

vec4 texel(vec2 p) {
  vec2 uv = clamp(p / uSize, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, uv);
}

float texelsPorPixel() { return .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0)); }

// Indice inteiro de texel (como float) dobrado pela borda; peso 0 fora quando "nenhuma".
float dobra(float i, float n, float modo, inout float dentro) {
  if (modo < .5) {
    if (i < 0.0 || i > n - 1.0) dentro = 0.0;
    return clamp(i, 0.0, n - 1.0);
  }
  if (modo < 1.5) return i - n * floor(i / n);
  float m = i - 2.0 * n * floor(i / (2.0 * n));
  return m < n ? m : 2.0 * n - 1.0 - m;
}

vec4 bilinear(vec2 q) {
  vec2 b = q - .5;
  vec2 i0 = floor(b);
  vec2 f = b - i0;
  vec4 soma = vec4(0.0);
  for (int k = 0; k < 4; k++) {
    vec2 o = vec2(float(k == 1 || k == 3), float(k >= 2));
    float w = mix(1.0 - f.x, f.x, o.x) * mix(1.0 - f.y, f.y, o.y);
    float dentro = 1.0;
    float x = dobra(i0.x + o.x, uSize.x, p6.x, dentro);
    float y = dobra(i0.y + o.y, uSize.y, p6.y, dentro);
    soma += w * dentro * texel(vec2(x, y) + .5);
  }
  return soma;
}

vec2 origem(vec2 p, vec4 tr, float esc) {
  vec2 c = uSize * .5;
  vec2 d = p - c - tr.xy * esc;
  float s = sin(-tr.w), co = cos(-tr.w);
  d = vec2(co * d.x - s * d.y, s * d.x + co * d.y);
  return c + d / max(tr.z, .001);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  float esc = uEscalaRef * texelsPorPixel();
  float n = clamp(floor(p6.z + .5), 1.0, 8.0);
  bool mono = p6.w > .5;
  vec4 acc = vec4(0.0);
  for (int i = 0; i < 8; i++) {
    float fi = float(i);
    if (fi >= n) break;
    float a = n > 1.0 ? fi / (n - 1.0) : 0.0;
    if (mono) {
      acc += bilinear(origem(p, mix(p1, p4, a), esc));
    } else {
      vec4 r = bilinear(origem(p, mix(p0, p3, a), esc));
      vec4 g = bilinear(origem(p, mix(p1, p4, a), esc));
      vec4 bb = bilinear(origem(p, mix(p2, p5, a), esc));
      // pre-multiplicado: cada canal leva a sua alfa; a alfa final e a maior
      acc += vec4(r.r, g.g, bb.b, max(max(r.a, g.a), bb.a));
    }
  }
  acc /= n;
  float mistura = clamp(p7.y, 0.0, 1.0);
  if (mistura < .999) acc = mix(bilinear(p), acc, mistura);
  fragColor = acc * clamp(p7.x, 0.0, 1.0);
}
