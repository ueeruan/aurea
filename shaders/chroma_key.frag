#version 460 core
#include <flutter/runtime_effect.glsl>

// Chroma Key (beta 89): keyer por DIFERENCA DE COR, como os keyers de
// estudio (o canal da tela contra a mistura dos outros dois, pela cor da
// tela), com a DISTANCIA DE COR em CbCr para telas que nao sao verde/azul.
// Depois: clip preto/branco, estreitar/alargar e suavizar a borda do matte
// (vizinhanca de 8), e remover o derramamento da cor da tela.
// p0 = metodo, ganho, equilibrio, tolerancia; p1 = suavidade, clip preto,
// clip branco, derramamento; p2 = estreitar px, suavizar px, ver.

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

float tpp() { return .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0)); }

vec3 desmultiplicar(vec4 c) { return c.a > 1e-4 ? c.rgb / c.a : vec3(0.0); }

// Mascaras do canal dominante da cor da tela (A) e dos outros dois (B, C).
vec3 canalA(vec3 k) {
  if (k.g >= k.r && k.g >= k.b) return vec3(0.0, 1.0, 0.0);
  if (k.b >= k.r && k.b >= k.g) return vec3(0.0, 0.0, 1.0);
  return vec3(1.0, 0.0, 0.0);
}
vec3 canalB(vec3 k) {
  if (k.g >= k.r && k.g >= k.b) return vec3(1.0, 0.0, 0.0);
  if (k.b >= k.r && k.b >= k.g) return vec3(1.0, 0.0, 0.0);
  return vec3(0.0, 1.0, 0.0);
}
vec3 canalC(vec3 k) {
  if (k.g >= k.r && k.g >= k.b) return vec3(0.0, 0.0, 1.0);
  if (k.b >= k.r && k.b >= k.g) return vec3(0.0, 1.0, 0.0);
  return vec3(0.0, 0.0, 1.0);
}

vec2 cbcr(vec3 c) {
  return vec2(-.1146 * c.r - .3854 * c.g + .5 * c.b, .5 * c.r - .4542 * c.g - .0458 * c.b);
}

// Alfa de UM pixel (1 = fica, 0 = e tela).
float matte(vec2 p) {
  vec4 t = texel(p);
  if (t.a < 1e-4) return 0.0;
  vec3 c = desmultiplicar(t);
  vec3 k = c0.rgb;
  float a;
  if (p0.x < .5) {
    vec3 A = canalA(k), B = canalB(k), C = canalC(k);
    float bal = p0.z;
    float dc = dot(c, A) - (dot(c, B) * bal + dot(c, C) * (1.0 - bal));
    float dk = dot(k, A) - (dot(k, B) * bal + dot(k, C) * (1.0 - bal));
    a = 1.0 - clamp(dc * p0.y / max(dk, 1e-3), 0.0, 1.0);
  } else {
    float d = distance(cbcr(c), cbcr(k)) / .7;
    a = smoothstep(p0.w, p0.w + max(p1.x, .001), d);
  }
  a = clamp((a - p1.y) / max(p1.z - p1.y, 1e-3), 0.0, 1.0);
  return a * t.a;
}

void main() {
  vec2 p = FlutterFragCoord().xy;
  vec4 t = texel(p);
  float k = tpp();
  float m = matte(p);

  // Estreitar (>0 come a borda) ou alargar (<0), depois suavizar.
  float choke = p2.x * k;
  if (abs(choke) > .05) {
    float r = abs(choke);
    for (int i = 0; i < 8; i++) {
      float ang = float(i) * .78539816;
      float v = matte(p + r * vec2(cos(ang), sin(ang)));
      m = choke > 0.0 ? min(m, v) : max(m, v);
    }
  }
  float soft = p2.y * k;
  if (soft > .05) {
    float soma = m;
    for (int i = 0; i < 8; i++) {
      float ang = float(i) * .78539816 + .3927;
      soma += matte(p + soft * vec2(cos(ang), sin(ang)));
    }
    m = min(m, soma / 9.0) * .5 + soma / 18.0;
  }

  if (p2.z > .5) {
    fragColor = vec4(vec3(m), 1.0);
    return;
  }

  // Derramamento: o canal da tela nao passa da mistura dos outros dois.
  vec3 c = desmultiplicar(t);
  vec3 A = canalA(c0.rgb), B = canalB(c0.rgb), C = canalC(c0.rgb);
  float limite = dot(c, B) * p0.z + dot(c, C) * (1.0 - p0.z);
  float excesso = max(dot(c, A) - limite, 0.0) * p1.w;
  c -= A * excesso;
  // Devolve o brilho que o canal perdeu, em cinza, para a pele nao escurecer.
  c += vec3(excesso * .35);
  c = clamp(c, 0.0, 1.0);
  fragColor = vec4(c * m, m);
}
