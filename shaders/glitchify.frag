#version 460 core
#include <flutter/runtime_effect.glsl>

// Glitchify (CSpice), aba Distorcer (16/09). Uma passada, ~12 leituras.
// Nomes e padroes lidos do After Effects 2026 do dono; o desenho segue os
// renders do plugin sobre a imagem de teste (faixas, blocos, riscos de pixel,
// quadrados de cor, separacao de canais). Os passos de tempo chegam prontos
// do Dart (ver valoresGlitchify). Floats: ABI comum do lote 2.

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
  if (p13.x < .5 && (p.x < 0.0 || p.y < 0.0 || p.x > uSize.x || p.y > uSize.y)) return vec4(0.0);
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

float vruido(vec2 x, float s) {
  vec2 i = floor(x);
  vec2 f = x - i;
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = h3(vec3(i, s));
  float b = h3(vec3(i + vec2(1, 0), s));
  float c = h3(vec3(i + vec2(0, 1), s));
  float d = h3(vec3(i + vec2(1, 1), s));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 x, float s, float ganho, float lac, float oitavas) {
  float soma = 0.0, amp = 1.0, norma = 0.0;
  for (int i = 0; i < 6; i++) {
    if (float(i) >= max(oitavas, 1.0)) break;
    soma += amp * vruido(x, s + float(i) * 7.0);
    norma += amp;
    amp *= .5 * ganho / 1.5;
    x *= max(lac * .2, 1.01);
  }
  return soma / max(norma, 1e-4);
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

vec3 hsv(float h, float s, float v) {
  vec3 k = clamp(abs(fract(h + vec3(0, 2, 1) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
  return v * mix(vec3(1.0), k, s);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  float esc = uEscalaRef * texelsPorPixel();

  float amount = p0.x, compl = p0.y, soft = p0.z, seed = p0.w;
  float stG = p1.x, stS = p1.y, stC = p1.z, stB = p1.w;
  vec2 tfOff = p2.xy * esc;
  float tfScale = p2.z, tfOp = p2.w;
  float tfMode = p3.x, compOver = p3.y;
  vec2 crop0 = p3.zw * uSize, crop1 = p4.xy * uSize;
  float tfOn = p4.z, chOn = p4.w;
  float splitCh = p5.x, splitH = p5.y, splitV = p5.z, chScale = p5.w;
  float scCh = p6.x, scVert = p6.y, scOff = p6.z, chMode = p6.w;
  float colOn = p7.x, colOff = p7.y, colAmt = p7.z, colOp = p7.w;
  float colMode = p8.x, imgOn = p8.y, streak = p8.z, vSort = p8.w;
  float slAmt = p9.x, slPos = p9.y, slOp = p9.z, slMode = p9.w;
  float blAmt = p10.x, blW = max(p10.y * esc, 2.0), blH = max(p10.z * esc, 2.0), blDir = p10.w;
  float blMode = p11.x, cmpOn = p11.y, cmpMul = p11.z, dith = p11.w;
  float jpeg = p12.x, ganho = p12.y, lac = p12.z, oit = p12.w;

  float slShift = p13.y, slQuant = max(p13.z, 1.0), blGroup = p13.w;
  float cutoff = p14.x, useNoise = p14.y, slQual = p14.z, scQuant = max(p14.w, 1.0);
  float fillGaps = p15.x;

  // Transicao: Completion com Softness sobre um ruido grosso.
  float nT = fbm(p / (60.0 * esc), seed + 3.0, ganho, lac, oit);
  float g = amount * 2.0 * smoothstep(compl - soft * .5, compl + soft * .5 + 1e-4, nT);

  vec2 c = uSize * .5;
  vec2 q = p;
  if (tfOn > .5) q = c + (q - c - tfOff) / max(abs(tfScale), 1e-3) * sign(tfScale + 1e-6);
  bool dentroCrop = all(greaterThanEqual(q, crop0)) && all(lessThanEqual(q, crop1));

  vec4 orig = texelN(p);
  vec4 cor;

  if (g <= 1e-4) {
    cor = texelN(q);
  } else {
    // --- Pixel Streak: riscos esticados a partir do inicio do trecho ---
    vec2 qs = q;
    if (imgOn > .5 && streak > 0.0) {
      vec2 e = vSort > .5 ? q.yx : q;
      float faixa = floor(e.y / (3.0 * esc));
      float faixa2 = floor(faixa / (1.0 + floor(h3(vec3(faixa, seed, 11.0)) * 4.0)));
      float L = mix(40.0, 220.0, h3(vec3(faixa2, seed, 12.0))) * esc;
      float off = h3(vec3(faixa2, seed, 13.0)) * L;
      float x0 = floor((e.x + off) / L) * L - off;
      float gate;
      if (useNoise > .5) {
        gate = fbm(vec2(x0 / (90.0 * esc), faixa2 * 3.0 * esc / 90.0), seed + 21.0, ganho, lac, oit);
      } else {
        vec4 lu = texelN(vSort > .5 ? vec2(e.y, x0) : vec2(x0, e.y));
        gate = dot(reta(lu), vec3(.299, .587, .114));
      }
      float ativo = step(1.0 - cutoff * .6, gate) * step(h3(vec3(faixa2, x0, seed + 14.0)), streak * g * 1.6);
      if (ativo > .5) {
        e.x = x0 + (e.x - x0) * .06;
        qs = vSort > .5 ? e.yx : e;
      }
    }

    // --- Separacao de canais ---
    vec2 dR = vec2(0.0), dB = vec2(0.0), dG = vec2(0.0);
    if (chOn > .5) {
      float fx = floor(q.y / (18.0 * esc));
      float r = (h3(vec3(fx, stS, seed + 31.0)) * 2.0 - 1.0);
      float r2 = (vruido(vec2(q.y / (120.0 * esc), stS), seed + 32.0) * 2.0 - 1.0);
      vec2 d = vec2(splitH, splitV) / 100.0 * 14.0 * esc * g * (.35 + .65 * abs(r2)) * sign(r + r2 * .5);
      if (splitCh < .5) { dR = d; dB = -d; }
      else if (splitCh < 1.5) { dR = d; }
      else if (splitCh < 2.5) { dG = d; }
      else if (splitCh < 3.5) { dB = d; }
      else { dR = d; dG = -d; }
    }
    vec4 aR = texelN(qs + dR);
    vec4 aG = texelN(qs + dG);
    vec4 aB = texelN(qs + dB);
    vec4 base = vec4(aR.r, aG.g, aB.b, max(aR.a, max(aG.a, aB.a)));

    // --- Escala de canal em faixas ---
    if (chOn > .5 && abs(chScale) > 0.0) {
      float eixo = scVert > .5 ? q.x : q.y;
      float fx = floor(eixo / (40.0 * esc));
      if (h3(vec3(fx, stC, seed + 41.0)) < .25 * g) {
        float k = 1.0 + floor(h3(vec3(fx, stC, seed + 42.0)) * scQuant + .5) / scQuant * chScale / 100.0 * .25;
        vec2 cc = scVert > .5 ? vec2(uSize.x * .5, scOff * uSize.y) : vec2(scOff * uSize.x, uSize.y * .5);
        vec2 qc = qs;
        if (scVert > .5) qc.y = cc.y + (qc.y - cc.y) / k; else qc.x = cc.x + (qc.x - cc.x) / k;
        vec4 s = bilinear(qc);
        if (fillGaps < .5 || s.a > .01) {
          if (scCh < .5) base.r = s.r; else if (scCh < 1.5) base.g = s.g; else if (scCh < 2.5) base.b = s.b;
          else if (scCh < 3.5) { base.r = s.r; base.b = s.b; } else { base.g = s.g; base.b = s.b; }
        }
      }
    }
    vec3 bs = reta(base);
    vec3 os = reta(texelN(qs));
    bs = mix(os, mistura(os, bs, chMode), 1.0);
    float alfa = base.a;

    if (imgOn > .5) {
      // --- Glitch Slice: faixas horizontais deslocadas ---
      float grosso = floor(q.y / (96.0 * esc));
      float alt = mix(6.0, 48.0 + slQual * .4, h3(vec3(grosso, stG, seed + 51.0))) * esc;
      float fatia = floor(q.y / alt);
      if (h3(vec3(fatia, grosso, stG + seed + 52.0)) < slAmt * g * .35) {
        float r = h3(vec3(fatia, grosso, stG + seed + 53.0));
        r = floor(r * slQuant + .5) / slQuant;
        float sh = (r - (1.0 - slPos)) * slShift * uSize.x;
        vec4 s = texelN(q + vec2(sh, 0.0));
        bs = mix(bs, mistura(bs, reta(s), slMode), slOp);
        alfa = max(alfa, s.a * slOp);
      }
      // --- Glitch Block ---
      vec2 cel = floor(q / vec2(blW, blH));
      float grupo = max(blGroup, 1.0);
      vec2 celG = floor(cel / vec2(1.0 + floor(h3(vec3(cel.y, stB, seed + 61.0)) * min(grupo, 8.0)), 1.0));
      if (h3(vec3(celG, stB + seed + 62.0)) < blAmt * g * .05) {
        vec2 r = vec2(h3(vec3(celG, stB + 63.0)), h3(vec3(celG, stB + 64.0))) * 2.0 - 1.0;
        vec2 d = vec2(r.x * blW * 2.0, r.y * blH * 2.0);
        if (blDir < .5) d.y = 0.0; else if (blDir < 1.5) d.x = 0.0;
        vec4 s = texelN(q + d);
        bs = mistura(bs, reta(s), blMode);
      }
    }

    // --- Color Glitch: quadrados de cor ---
    if (colOn > .5) {
      float lado = 16.0 * esc;
      vec2 cq = floor(q / lado);
      float dens = vruido(cq / 6.0, seed + 71.0);
      if (h3(vec3(cq, seed + 72.0)) < colAmt * g * .09 * smoothstep(.35, .75, dens)) {
        float lum = dot(bs, vec3(.299, .587, .114));
        vec3 cc = hsv(colOff + (h3(vec3(cq, seed + 73.0)) - .5) * .06, colAmt, mix(.55, .75, lum));
        bs = mix(bs, mistura(bs, cc, colMode), clamp(colOp * 3.2, 0.0, 1.0));
      }
    }

    // --- Compression Glitch ---
    if (cmpOn > .5) {
      float bloco = 8.0 * esc * (1.0 + cmpMul * 2.0);
      vec2 cb = (floor(q / bloco) + .5) * bloco;
      vec3 s = reta(texelN(cb));
      bs = mix(bs, s, jpeg * .8);
      float niveis = mix(32.0, 3.0, cmpMul);
      float dz = (h3(vec3(p, seed + 81.0)) - .5) * dith;
      bs = floor(bs * niveis + .5 + dz) / niveis;
    }
    cor = vec4(clamp(bs, 0.0, 1.0) * alfa, alfa);
  }

  if (tfOn > .5) {
    if (!dentroCrop) cor = vec4(0.0);
    cor *= tfOp;
    if (compOver > .5) {
      vec3 b = reta(orig), s = reta(cor);
      float a = cor.a + orig.a * (1.0 - cor.a);
      vec3 m = mix(b, mistura(b, s, tfMode), cor.a);
      cor = vec4(m * a, a);
    }
  }
  fragColor = cor;
}
