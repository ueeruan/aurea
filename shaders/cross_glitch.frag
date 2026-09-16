#version 460 core
#include <flutter/runtime_effect.glsl>

// Cross Glitch (BCC Cross Glitch, Boris FX Continuum), aba Distorcer (16/09).
// Grupos Block Damage, Shift, Shake e Flicker numa passada (3 leituras);
// intervalo, duracao e pico de cada glitch saem prontos do Dart (ver
// valoresCrossGlitch).

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
  if (p0.z < .5) return mod(q, uSize);
  vec2 m = mod(q, 2.0 * uSize);
  return uSize - abs(m - uSize);
}

vec4 ler(vec2 q) { return texelN(borda(q)); }

vec3 hsv(float h, float s, float v) {
  vec3 k = clamp(abs(fract(h + vec3(0, 2, 1) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
  return v * mix(vec3(1.0), k, s);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  float esc = uEscalaRef * texelsPorPixel();
  float mestre = p0.x, seed = p0.y, quadro = p0.w;
  float blI = p1.x * mestre, blSize = max(p1.y * esc, 2.0), blRun = p1.z, blSat = p1.w;
  float patAmt = p2.x, patCx = p2.y, patOp = p2.z, patCor = p2.w;
  float shI = p3.x * mestre, dup = p3.y, shAmt = p3.z, shDens = p3.w;
  float shRun = p4.x, skew = p4.y, jit = p4.z, drop = p4.w;
  float dropDens = p5.x;
  vec2 shake = p5.yz * esc;
  float rgb = p5.w * esc;
  float rot = p6.x, shSkew = p6.y, brilho = p6.z, sat = p6.w;
  float usaFundo = p7.x, stBl = p7.y, stSh = p7.z;

  vec2 c = uSize * .5;
  vec2 q = p;
  // Shake: deslocamento, giro e inclinacao.
  vec2 d = q - c;
  float cs = cos(-rot), sn = sin(-rot);
  d = vec2(cs * d.x - sn * d.y, sn * d.x + cs * d.y);
  d.x -= shSkew * d.y;
  q = c + d - shake;

  bool some = false;
  if (shI > 0.0) {
    float altF = mix(2.0, 90.0, shRun) * esc;
    float f = floor(q.y / altF);
    if (h3(vec3(f, stSh, seed + 1.0)) < shDens * shI) {
      float r = h3(vec3(f, stSh, seed + 2.0)) * 2.0 - 1.0;
      float dentro = (q.y - f * altF) / altF;
      q.x += r * shAmt * uSize.x * shI + skew * (dentro - .5) * altF;
      if (h3(vec3(f, stSh, seed + 3.0)) < dup * shI) q.y = f * altF + .5;
    }
    float linha = floor(q.y / max(esc, 1.0));
    q.x += (h3(vec3(linha, quadro, seed + 4.0)) - .5) * jit * 16.0 * esc * shI;
    float bandaDrop = floor(linha / 3.0);
    if (h3(vec3(bandaDrop, stSh, seed + 5.0)) < drop * dropDens * shI * .5) some = true;
  }

  vec3 pat = vec3(0.0);
  float patA = 0.0;
  if (blI > 0.0) {
    vec2 cel = floor(q / blSize);
    float run = 1.0 + floor(h3(vec3(cel.y, stBl, seed + 11.0)) * blRun * 12.0);
    vec2 celR = vec2(floor(cel.x / run), cel.y);
    if (h3(vec3(celR, stBl + seed + 12.0)) < blI * .18) {
      vec2 alvo = floor(vec2(h3(vec3(celR, stBl + 13.0)), h3(vec3(celR, stBl + 14.0))) * uSize / blSize);
      q = (alvo + fract(q / blSize)) * blSize;
      vec2 lp = fract(p / blSize);
      float listra = step(.5, fract((lp.x + lp.y * patCx) * (1.0 + floor(patCx * 4.0))));
      float hue = h3(vec3(celR, stBl + 15.0)) * patCor + .95;
      pat = hsv(hue, blSat, .9) * listra;
      patA = patAmt * patOp * (h3(vec3(celR, stBl + 16.0)) < patAmt ? 1.0 : 0.0);
    }
  }

  vec2 dS = vec2(rgb, 0.0);
  vec4 r = ler(q + dS);
  vec4 g = ler(q);
  vec4 b = ler(q - dS);
  float a = max(g.a, max(r.a, b.a));
  vec3 s = a > 1e-4 ? vec3(r.r, g.g, b.b) / a : vec3(0.0);
  s = mix(s, pat, patA * step(.001, dot(pat, vec3(1.0))));
  float lum = dot(s, vec3(.299, .587, .114));
  s = mix(vec3(lum), s, sat) * brilho;
  if (some) {
    if (usaFundo > .5) { s = c0.rgb; a = 1.0; } else { a = 0.0; }
  }
  fragColor = vec4(clamp(s, 0.0, 1.0) * a, a);
}
