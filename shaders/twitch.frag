#version 460 core
#include <flutter/runtime_effect.glsl>

// Twitch (Video Copilot), aba Distorcer (16/09). Operadores Blur, Light,
// Scale, Slide e Color numa passada (<= 60 leituras com borrao ligado); os
// envelopes de cada tique saem prontos do Dart (ver valoresTwitch). Time
// precisa de outros quadros e nao existe num filtro de imagem unica.

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

vec4 texelN(vec2 p) { return texel(floor(p) + .5); }

vec4 bilinear(vec2 p) {
  vec2 b = p - .5;
  vec2 i = floor(b);
  vec2 f = b - i;
  vec4 a = texel(i + vec2(.5, .5));
  vec4 c = texel(i + vec2(1.5, .5));
  vec4 d = texel(i + vec2(.5, 1.5));
  vec4 e = texel(i + vec2(1.5, 1.5));
  return mix(mix(a, c, f.x), mix(d, e, f.x), f.y);
}

float texelsPorPixel() { return .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0)); }

float h3(vec3 p) {
  p = fract(p * vec3(.1031, .1030, .0973));
  p += dot(p, p.yxz + 33.33);
  return fract((p.x + p.y) * p.z);
}

vec2 borda(vec2 q) {
  if (p0.z < .5) return q;
  if (p0.z < 1.5) {
    vec2 m = mod(q, 2.0 * uSize);
    return uSize - abs(m - uSize);
  }
  return q;
}

vec4 ler(vec2 q) {
  vec2 b = borda(q);
  if (p0.z > 1.5 && (b.x < 0.0 || b.y < 0.0 || b.x > uSize.x || b.y > uSize.y)) return vec4(0.0);
  return texelN(b);
}

vec3 reta(vec4 c) { return c.a > 1e-4 ? c.rgb / c.a : vec3(0.0); }

vec3 mistura(vec3 b, vec3 s, float m) {
  if (m < .5) return s;
  if (m < 1.5) return min(b + s, 1.0);
  if (m < 2.5) return 1.0 - (1.0 - b) * (1.0 - s);
  if (m < 3.5) return b * s;
  if (m < 4.5) return abs(b - s);
  if (m < 5.5) return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(.5, b));
  if (m < 6.5) return max(b, s);
  if (m < 7.5) return min(b, s);
  return mix(2.0 * b * s + b * b * (1.0 - 2.0 * s), sqrt(b) * (2.0 * s - 1.0) + 2.0 * b * (1.0 - s), step(.5, s));
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  float esc = uEscalaRef * texelsPorPixel();
  float blurR = p1.x * esc, blurOp = p1.y, blurMode = p1.z, blurBoost = p1.w;
  float luz = p2.x, escE = p2.y;
  vec2 ancora = p2.zw * uSize;
  vec2 slide = p3.xy * uSize;
  float mb = p3.z, split = p3.w * esc;
  float corE = p4.x;
  vec3 tint = p4.yzw;

  // Escala + deslize com borrao de movimento; separacao RGB no deslize.
  vec2 dirS = length(slide) > 1e-3 ? normalize(slide) : vec2(1.0, 0.0);
  vec4 nitido = vec4(0.0);
  for (int k = 0; k < 8; k++) {
    float fk = float(k) / 7.0;
    float u = mix(1.0 - mb, 1.0, fk);
    vec2 q = ancora + (p - ancora) / (1.0 + escE * u) - slide * u;
    vec4 r = ler(q + dirS * split);
    vec4 g = ler(q);
    vec4 b = ler(q - dirS * split);
    nitido += vec4(r.r, g.g, b.b, max(r.a, max(g.a, b.a)));
    if (mb <= 0.0) { nitido *= 8.0; break; }
  }
  nitido /= 8.0;
  vec4 cor = nitido;

  if (blurR > .5 && blurOp > 0.0) {
    vec4 bor = vec4(0.0);
    for (int k = 0; k < 12; k++) {
      float fk = float(k) / 11.0;
      float u = mix(1.0 - mb, 1.0, fk);
      float ang = float(k) * 2.39996;
      vec2 d = blurR * sqrt((float(k) + .5) / 12.0) * vec2(cos(ang), sin(ang));
      vec2 q = ancora + (p - ancora) / (1.0 + escE * u) - slide * u + d;
      vec4 r = ler(q + dirS * split);
      vec4 g = ler(q);
      vec4 b = ler(q - dirS * split);
      bor += vec4(r.r, g.g, b.b, max(r.a, max(g.a, b.a)));
    }
    bor /= 12.0;
    vec3 sb = reta(bor) * (1.0 + blurBoost);
    vec3 nb = reta(nitido);
    float a = max(nitido.a, bor.a * blurOp);
    cor = vec4(mix(nb, mistura(nb, sb, blurMode), blurOp) * a, a);
  }

  vec3 s = reta(cor);
  s *= pow(2.0, luz);
  s = mix(s, s * tint * 1.8, corE);
  fragColor = vec4(clamp(s, 0.0, 1.0) * cor.a, cor.a);
}
