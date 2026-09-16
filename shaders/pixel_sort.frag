#version 460 core
#include <flutter/runtime_effect.glsl>

// S_PIXELSORT (Sapphire), aba Estilizar lote 2.
//
// Ordenar de verdade nao cabe numa leitura por pixel. O que roda aqui:
// cada linha de ordenacao (reta, raio ou circulo) e cortada em blocos de
// 28 pontos de reticula; o pixel le os 28 pontos do proprio bloco, acha a
// faixa acima (ou abaixo) do limiar que o contem — faixas escuras mais
// curtas que o Soften sao atravessadas, a ponta avanca um sorteio no
// escuro —, recorta pelos reinicios aleatorios, amostra a faixa em 20
// pontos, ordena as amostras pela chave e devolve o quantil da propria
// posicao. Tudo e funcao da linha e do bloco, entao todos os pixels de uma
// faixa chegam as mesmas pontas e ao mesmo degrade.
//
// Os numeros de reticula, ponte, avanco e reinicio foram ajustados contra o
// render do AE (comprimentos das faixas, fracao alterada e distancia ao
// claro batendo com o original). Ver pixel_sort_sapphire.dart.

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
const int K = 28;
const float KF = 28.0;
const int N = 20;
const float NF = 20.0;

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

float hashLinha(float linha, float a, float b) {
  return hash3(vec3(fract(linha * .0007123) * 1000.0 + linha * .013, a, b));
}

// Chave de ordenacao (Sort Type 1..10) sobre a cor reta.
float chave(vec4 t, float tipo) {
  vec3 c = t.a > .00001 ? clamp(t.rgb / t.a, 0.0, 1.0) : vec3(0.0);
  float mx = max(c.r, max(c.g, c.b));
  float mn = min(c.r, min(c.g, c.b));
  if (tipo < 1.5) return dot(c, LUMA601);
  if (tipo < 2.5) return (c.r + c.g + c.b) / 3.0;
  if (tipo < 3.5) return mn;
  if (tipo < 4.5) return mx;
  if (tipo < 5.5) return c.r;
  if (tipo < 6.5) return c.g;
  if (tipo < 7.5) return c.b;
  if (tipo < 8.5) {
    float d = mx - mn;
    if (d < .00001) return 0.0;
    float h;
    if (mx == c.r) {
      h = (c.g - c.b) / d;
      if (h < 0.0) h += 6.0;
    } else if (mx == c.g) {
      h = (c.b - c.r) / d + 2.0;
    } else {
      h = (c.r - c.g) / d + 4.0;
    }
    return h / 6.0;
  }
  if (tipo < 9.5) return mx > .00001 ? (mx - mn) / mx : 0.0;
  return .5 * (mx + mn);
}

void main() {
  vec2 pf = floor(FlutterFragCoord().xy) + .5;
  float esc = max(uEscalaRef * texelsPorPixel(), .0001);

  // Downsample: a ordenacao roda numa grade de q texels.
  float q = 1.0;
  if (p5.z > .5) q = max(1.0, floor(uSize.x / max(p5.w, 1.0) + .5));
  vec2 p = q > 1.0 ? (floor(pf / q) + .5) * q : pf;
  vec4 fonte = texel(p);

  float modo = floor(p0.x + .5);
  float tipo = floor(p4.w + .5);
  float mostrar = floor(p6.z + .5);
  float semente = p6.x * 17.0;
  float limiar = p4.x;
  bool acima = p4.y > 1.5;

  if (mostrar > 1.5 && mostrar < 2.5) {
    float k = chave(fonte, tipo);
    fragColor = vec4(vec3(k), 1.0);
    return;
  }

  // ---- geometria da linha: id, t, faixa [tIni, tFim] e origem dos blocos
  float linha = 0.0;
  float t = 0.0;
  float tIni = -1e9;
  float tFim = 1e9;
  vec2 origem = .5 * uSize;
  vec2 dirL = vec2(1.0, 0.0);
  vec2 perpL = vec2(0.0, 1.0);
  float ang0 = 0.0;
  float delta = 1.0;
  float raioL = 1.0;
  bool valido = true;

  if (modo < 1.5) {
    // Linear: retas paralelas no Sort Angle.
    dirL = vec2(cos(p0.y), sin(p0.y));
    perpL = vec2(-dirL.y, dirL.x);
    vec2 v = p - origem;
    linha = floor(dot(v, perpL) / q);
    t = dot(v, dirL);
    vec2 base = origem + perpL * (linha + .5) * q;
    // Entrada e saida da reta no quadro.
    float lo = -1e9, hi = 1e9;
    if (abs(dirL.x) > .00001) {
      float a = (.5 - base.x) / dirL.x, b = (uSize.x - .5 - base.x) / dirL.x;
      lo = max(lo, min(a, b));
      hi = min(hi, max(a, b));
    }
    if (abs(dirL.y) > .00001) {
      float a = (.5 - base.y) / dirL.y, b = (uSize.y - .5 - base.y) / dirL.y;
      lo = max(lo, min(a, b));
      hi = min(hi, max(a, b));
    }
    tIni = lo;
    tFim = hi + 1.0;
  } else if (modo < 2.5) {
    // Radial: raios a partir do Center.
    origem = p0.zw * uSize;
    vec2 v = p - origem;
    float r = length(v);
    float rMax = length(uSize) + length(origem - .5 * uSize);
    delta = q / max(rMax, 1.0);
    float a = mod(atan(v.y, v.x) - p1.x, TAU);
    if (a > p1.y) valido = false;
    linha = floor(a / delta);
    ang0 = p1.x + (linha + .5) * delta;
    dirL = vec2(cos(ang0), sin(ang0));
    perpL = vec2(-dirL.y, dirL.x);
    t = r;
    float r0 = p1.z * esc + p2.x * hashLinha(linha, semente, 5.0) * p1.w * esc * .5;
    tIni = r0;
    tFim = r0 + p1.w * esc;
    // Saida do raio no quadro.
    float hi = 1e9;
    if (abs(dirL.x) > .00001) hi = min(hi, max((.5 - origem.x) / dirL.x, (uSize.x - .5 - origem.x) / dirL.x));
    if (abs(dirL.y) > .00001) hi = min(hi, max((.5 - origem.y) / dirL.y, (uSize.y - .5 - origem.y) / dirL.y));
    tFim = min(tFim, hi + 1.0);
  } else {
    // Circular: circulos concentricos no Circle Center.
    origem = p3.xy * uSize;
    vec2 v = p - origem;
    float r = length(v);
    linha = floor(r / q);
    raioL = (linha + .5) * q;
    if (raioL < p3.z * esc || raioL > (p3.z + p3.w) * esc) valido = false;
    ang0 = p2.y + p2.w * hashLinha(linha, semente, 6.0) * TAU;
    float a = mod(atan(v.y, v.x) - ang0, TAU);
    if (a > p2.z) valido = false;
    t = a * raioL;
    tIni = 0.0;
    tFim = p2.z * raioL;
  }

  // Reticula e blocos (em texels).
  float h = max(12.0 * esc, q);
  float lb = KF * h;
  float soften = max(p5.y, 0.0) * esc;
  float rho = soften;
  float nb = max(1.0, ceil(2.33 * soften / h - .001));
  float avanco = min(1.25 * soften, .5 * (nb + 1.0) * h);

  float desloc = modo < 1.5 ? tIni - hashLinha(linha, semente, 1.0) * lb : tIni;
  float bloco = floor((t - desloc) / lb);
  float bIni = max(desloc + bloco * lb, tIni);
  float bFim = min(desloc + (bloco + 1.0) * lb, tFim);
  if (t < tIni || t >= tFim) valido = false;

  bool achou = false;
  float sIni = 0.0;
  float sFim = 0.0;
  if (valido) {
    float segA = -1.0;
    float ult = -1.0;
    float escuro = 0.0;
    float antes = 0.0;
    float bxBase = desloc + bloco * lb;
    for (int j = 0; j <= K; j++) {
      float fj = float(j);
      float tj = bxBase + (fj + .5) * h;
      bool claro = false;
      bool fim = j == K || tj >= bFim;
      if (!fim && tj >= bIni) {
        vec2 x;
        vec2 perp;
        if (modo < 1.5) {
          x = origem + perpL * (linha + .5) * q + dirL * tj;
          perp = perpL;
        } else if (modo < 2.5) {
          x = origem + dirL * tj;
          perp = perpL;
        } else {
          float an = ang0 + tj / raioL;
          perp = vec2(cos(an), sin(an));
          x = origem + perp * raioL;
        }
        x += perp * rho * .5 * (fj - 2.0 * floor(fj * .5) < .5 ? 1.0 : -1.0);
        float bruto = 0.0;
        if (x.x >= 0.0 && x.y >= 0.0 && x.x < uSize.x && x.y < uSize.y) {
          float k = chave(texel(floor(x / q) * q + .5 * q), tipo);
          bruto = (acima ? k > limiar : k < limiar) ? 1.0 : 0.0;
        }
        claro = bruto > .5 && (j == 0 || antes > .5);
        antes = bruto;
      }
      if (claro) {
        if (segA < 0.0) segA = fj;
        ult = fj;
        escuro = 0.0;
      } else if (segA >= 0.0) {
        escuro += 1.0;
        if (escuro >= nb || fim) {
          float ta = max(bxBase + (segA + .5) * h - avanco * hashLinha(linha, bloco * 31.0 + segA, semente + 11.0), bIni);
          float tb = min(bxBase + (ult + .5) * h + avanco * hashLinha(linha, bloco * 31.0 + ult, semente + 12.0), bFim);
          if (t >= ta && t < tb) {
            achou = true;
            sIni = ta;
            sFim = tb;
          }
          segA = -1.0;
          escuro = 0.0;
        }
      }
      if (achou || fim) break;
    }
  }

  if (mostrar > 2.5 && mostrar < 3.5 || mostrar > 4.5) {
    fragColor = vec4(vec3(achou ? 1.0 : 0.0), 1.0);
    return;
  }

  // Reinicios aleatorios: celulas de meio espacamento, metade com um corte.
  float reinicio = max(p5.x, 0.0);
  float corteAntes = -1e9;
  float corteDepois = 1e9;
  bool pixelDeCorte = false;
  if (reinicio > .001) {
    float lc = max(25000.0 / reinicio * esc, 2.0 * q);
    float offC = hashLinha(linha, semente, 21.0) * lc;
    float ci = floor((t - offC) / lc);
    for (int m = -8; m <= 8; m++) {
      float cc = ci + float(m);
      if (hashLinha(linha, cc, semente + 22.0) < .5) {
        float tc = offC + (cc + hashLinha(linha, cc, semente + 23.0)) * lc;
        if (tc <= t) corteAntes = max(corteAntes, tc);
        if (tc > t) corteDepois = min(corteDepois, tc);
        if (abs(tc - t) < .5 * q) pixelDeCorte = true;
      }
    }
  }
  if (mostrar > 3.5 && mostrar < 4.5) {
    fragColor = vec4(vec3(pixelDeCorte ? 1.0 : 0.0), 1.0);
    return;
  }

  vec4 res = fonte;
  if (achou) {
    float t0 = max(sIni, corteAntes);
    float t1 = min(sFim, corteDepois);
    float len = t1 - t0;
    if (len >= 2.0 * q) {
      vec4 cor[20];
      float ch[20];
      for (int i = 0; i < N; i++) {
        float ti = t0 + (float(i) + .5) / NF * len;
        vec2 x;
        if (modo < 1.5) {
          x = origem + perpL * (linha + .5) * q + dirL * ti;
        } else if (modo < 2.5) {
          x = origem + dirL * ti;
        } else {
          float an = ang0 + ti / raioL;
          x = origem + vec2(cos(an), sin(an)) * raioL;
        }
        vec4 c = texel(floor(x / q) * q + .5 * q);
        cor[i] = c;
        ch[i] = chave(c, tipo);
      }
      float u = clamp((t - t0) / len * NF - .5, 0.0, NF - 1.0);
      if (p4.z > .5) u = NF - 1.0 - u;
      float i0 = floor(u);
      float fr = u - i0;
      float i1 = min(i0 + 1.0, NF - 1.0);
      vec4 a = vec4(0.0);
      vec4 b = vec4(0.0);
      for (int i = 0; i < N; i++) {
        float rank = 0.0;
        for (int j = 0; j < N; j++) {
          if (ch[j] < ch[i] || (ch[j] == ch[i] && j < i)) rank += 1.0;
        }
        if (rank == i0) a = cor[i];
        if (rank == i1) b = cor[i];
      }
      res = mix(a, b, fr);
    }
  }
  res = mix(res, fonte, clamp(p6.y, 0.0, 1.0));
  float reservado = uTempo + uModo + p6.w + p7.x + p7.y + p7.z + p7.w
      + p8.x + p8.y + p8.z + p8.w + p9.x + p9.y + p9.z + p9.w + p10.x + p10.y + p10.z + p10.w
      + p11.x + p11.y + p11.z + p11.w + p12.x + p12.y + p12.z + p12.w + p13.x + p13.y + p13.z + p13.w
      + p14.x + p14.y + p14.z + p14.w + p15.x + p15.y + p15.z + p15.w + c0.x + c0.y + c0.z + c0.w
      + c1.x + c1.y + c1.z + c1.w;
  if (reservado != reservado) res = fonte;
  fragColor = res;
}
