#version 460 core
#include <flutter/runtime_effect.glsl>

// S_AUTOPAINT (Sapphire), aba Estilizar lote 2. Pinceladas procedurais:
// uma pincelada por celula de uma grade com jitter; a direcao vem do
// gradiente da luma medido na propria grade (Sobel entre os centros), a
// cor e a da fonte no centro, e a ordem de pintura e sorteada.
// Parametros em lib/src/features/editor/domain/auto_paint.dart.

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

const vec3 LUMA601 = vec3(.299, .587, .114);
const float TAU = 6.2831853;

vec4 texel(vec2 p) {
  vec2 uv = clamp(p / uSize, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, uv);
}

float texelsPorPixel() {
  return .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0));
}

float hash3(vec3 p) {
  p = fract(p * .1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  vec4 fonte = texel(p);
  float esc = max(uEscalaRef * texelsPorPixel(), .0001);

  // Estilo 1 Van Gogh, 2 Hairy Paint, 3 Pointalize. Comprimento negativo
  // troca Van Gogh e Hairy Paint (documentacao).
  float estilo = floor(p0.x + .5);
  float comp = p0.z;
  if (comp < 0.0) {
    if (estilo < 1.5) {
      estilo = 2.0;
    } else if (estilo < 2.5) {
      estilo = 1.0;
    }
    comp = -comp;
  }
  float s = max(uSize.x / max(p0.y, .1), 2.0);
  float alinhar = max(p0.w, 0.0);
  float suavizar = 1.0 - exp(-max(p1.x, 0.0) * esc / (.5 * s));
  float quadro = floor(uTempo * 30.0 + .001);
  float jit = floor(p1.z + .5);
  float fase = jit > .5 ? floor(quadro / jit) : 0.0;
  float semente = p1.y * 7.31 + fase * 13.7;

  vec2 o = floor(p / s - .5);
  vec4 cor[36];
  vec2 cen[36];
  float lum[36];
  float pri[36];
  for (int k = 0; k < 36; k++) {
    float fk = float(k);
    vec2 cel = o + vec2(fk - 6.0 * floor(fk / 6.0) - 2.0, floor(fk / 6.0) - 2.0);
    vec2 h = vec2(hash3(vec3(cel, semente + 1.0)), hash3(vec3(cel, semente + 2.0)));
    vec2 c = (cel + .5 + .9 * (h - .5)) * s;
    cen[k] = c;
    vec4 t = texel(floor(c) + .5);
    cor[k] = t;
    lum[k] = t.a > .00001 ? dot(t.rgb / t.a, LUMA601) : 0.0;
    pri[k] = hash3(vec3(cel, semente + 3.0));
  }

  // Direcao e tamanho de cada pincelada candidata (as 16 do meio).
  vec2 eixo[36];
  vec4 corS[36];
  float meiaL = .4 * s * (1.0 + comp);
  float meiaW = .4 * s;
  for (int k = 7; k < 29; k++) {
    float fk = float(k);
    float col = fk - 6.0 * floor(fk / 6.0);
    eixo[k] = vec2(1.0, 0.0);
    corS[k] = cor[k];
    if (col > .5 && col < 4.5) {
      float gxC = (lum[k + 1] - lum[k - 1]);
      float gyC = (lum[k + 6] - lum[k - 6]);
      float gxS = (lum[k - 5] + 2.0 * lum[k + 1] + lum[k + 7]) - (lum[k - 7] + 2.0 * lum[k - 1] + lum[k + 5]);
      float gyS = (lum[k + 5] + 2.0 * lum[k + 6] + lum[k + 7]) - (lum[k - 7] + 2.0 * lum[k - 6] + lum[k - 5]);
      float m = clamp(alinhar * 5.0, 0.0, 1.0);
      float gx = mix(gxC, .25 * gxS, m);
      float gy = mix(gyC, .25 * gyS, m);
      // Normal da borda = gradiente; Van Gogh segue a tangente.
      vec2 g = vec2(gx, gy);
      if (dot(g, g) < 1e-8) {
        float a = TAU * pri[k];
        g = vec2(cos(a), sin(a));
      }
      g = normalize(g);
      eixo[k] = estilo < 1.5 ? vec2(-g.y, g.x) : g;
      if (suavizar > .001) {
        vec4 viz = .25 * (cor[k - 1] + cor[k + 1] + cor[k - 6] + cor[k + 6]);
        corS[k] = mix(cor[k], viz, suavizar);
      }
    }
  }

  // Pintura em 7 posicoes: o pixel e o anel do Sharpen.
  float raioNitidez = max(p2.x, 0.0) * 2.0 * meiaL;
  vec4 centro = vec4(0.0);
  vec4 anel = vec4(0.0);
  for (int m = 0; m < 7; m++) {
    vec2 q = p;
    if (m > 0) {
      float a = TAU * float(m) / 6.0;
      q = p + raioNitidez * vec2(cos(a), sin(a));
    }
    float melhorP = -1.0;
    vec4 melhor = vec4(0.0);
    float menorF = 1e9;
    vec4 perto = vec4(0.0);
    for (int k = 7; k < 29; k++) {
      float fk = float(k);
      float col = fk - 6.0 * floor(fk / 6.0);
      if (col > .5 && col < 4.5) {
        vec2 d = q - cen[k];
        float f;
        if (estilo > 2.5) {
          // Pointalize: celula pontuda (losango girado por celula).
          float a = TAU * pri[k];
          vec2 r = vec2(cos(a) * d.x + sin(a) * d.y, -sin(a) * d.x + cos(a) * d.y);
          f = (abs(r.x) + abs(r.y)) / (.75 * s);
        } else {
          vec2 e = eixo[k];
          float u = dot(d, e) / meiaL;
          float v = (e.x * d.y - e.y * d.x) / meiaW;
          f = u * u + abs(v);
        }
        if (f < 1.0 && pri[k] > melhorP) {
          melhorP = pri[k];
          melhor = corS[k];
        }
        if (f < menorF) {
          menorF = f;
          perto = corS[k];
        }
      }
    }
    vec4 pintado = (melhorP >= 0.0 && estilo < 2.5) ? melhor : perto;
    if (m == 0) {
      centro = pintado;
    } else {
      anel += pintado / 6.0;
    }
  }
  vec4 res = centro + p1.w * (centro - anel);
  res.a = clamp(res.a, 0.0, 1.0);
  res.rgb = clamp(res.rgb, vec3(0.0), vec3(res.a));
  res = mix(res, fonte, clamp(p2.y, 0.0, 1.0));
  // Toca os uniformes reservados (a ordem dos floats e a ABI do lote).
  float reservado = uModo + p2.z + p2.w + p3.x + p3.y + p3.z + p3.w + p4.x + p4.y + p4.z + p4.w
      + p5.x + p5.y + p5.z + p5.w + p6.x + p6.y + p6.z + p6.w + p7.x + p7.y + p7.z + p7.w
      + p8.x + p8.y + p8.z + p8.w + p9.x + p9.y + p9.z + p9.w + p10.x + p10.y + p10.z + p10.w
      + p11.x + p11.y + p11.z + p11.w + p12.x + p12.y + p12.z + p12.w + p13.x + p13.y + p13.z + p13.w
      + p14.x + p14.y + p14.z + p14.w + p15.x + p15.y + p15.z + p15.w + c0.x + c0.y + c0.z + c0.w
      + c1.x + c1.y + c1.z + c1.w;
  if (reservado != reservado) res = fonte;
  fragColor = res;
}
