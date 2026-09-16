#version 460 core
#include <flutter/runtime_effect.glsl>

// S_JPEGDAMAGE (16/09): compressao JPEG DE VERDADE, em duas passadas.
//
//   modo 0 (codificar): nos 8x8 primeiros texels de cada bloco grava os
//     coeficientes DCT quantizados com as tabelas do padrao JPEG (Anexo K)
//     escaladas pela qualidade (conta da IJG). Luma em R,G (16 bits); croma
//     4:2:0 no bloco de cima a esquerda do macrobloco, em B,A (16 bits):
//     Cb nas colunas 0..3 e Cr nas colunas 4..7 (so os 4x4 de baixa
//     frequencia — com a tabela de croma na qualidade de dano, o resto e zero).
//   modo 1 (decodificar): IDCT por pixel, com as escalas de frequencia e os
//     erros de decodificacao aplicados nos coeficientes.
//
// Floats: tamanho[0..1] filtro[2] logico[3..4] escalaRef[5] tempo[6] modo[7]
// p0..p5[8..31] QL0..QL15[32..95] QC0..QC3[96..111].

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
uniform vec4 QL0;
uniform vec4 QL1;
uniform vec4 QL2;
uniform vec4 QL3;
uniform vec4 QL4;
uniform vec4 QL5;
uniform vec4 QL6;
uniform vec4 QL7;
uniform vec4 QL8;
uniform vec4 QL9;
uniform vec4 QL10;
uniform vec4 QL11;
uniform vec4 QL12;
uniform vec4 QL13;
uniform vec4 QL14;
uniform vec4 QL15;
uniform vec4 QC0;
uniform vec4 QC1;
uniform vec4 QC2;
uniform vec4 QC3;
uniform sampler2D uImage;

out vec4 fragColor;

const float PI = 3.14159265;

vec4 texel(vec2 p) {
  vec2 uv = clamp(p / uSize, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, uv);
}

float componente(vec4 v, int i) {
  if (i == 0) return v.x;
  if (i == 1) return v.y;
  if (i == 2) return v.z;
  return v.w;
}

// Tabela de luma: linha v, coluna u (QL[2v] = colunas 0..3, QL[2v+1] = 4..7).
float qLuma(int u, int v) {
  int k = 2 * v + (u >= 4 ? 1 : 0);
  int c = u >= 4 ? u - 4 : u;
  vec4 linha = QL0;
  if (k == 1) linha = QL1; else if (k == 2) linha = QL2; else if (k == 3) linha = QL3;
  else if (k == 4) linha = QL4; else if (k == 5) linha = QL5; else if (k == 6) linha = QL6;
  else if (k == 7) linha = QL7; else if (k == 8) linha = QL8; else if (k == 9) linha = QL9;
  else if (k == 10) linha = QL10; else if (k == 11) linha = QL11; else if (k == 12) linha = QL12;
  else if (k == 13) linha = QL13; else if (k == 14) linha = QL14; else if (k == 15) linha = QL15;
  return componente(linha, c);
}

float qCroma(int u, int v) {
  vec4 linha = v == 0 ? QC0 : (v == 1 ? QC1 : (v == 2 ? QC2 : QC3));
  return componente(linha, u);
}

float cc(int k) { return k == 0 ? .70710678 : 1.0; }

float hash3(vec3 p) {
  p = fract(p * .1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float ruidoSuave(vec2 x, float s) {
  vec2 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash3(vec3(i, s)), b = hash3(vec3(i + vec2(1, 0), s));
  float c = hash3(vec3(i + vec2(0, 1), s)), d = hash3(vec3(i + vec2(1, 1), s));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec4 empacotar16(float qA, float qB) {
  float a = clamp(floor(qA + .5) + 32768.0, 0.0, 65535.0);
  float b = clamp(floor(qB + .5) + 32768.0, 0.0, 65535.0);
  float aHi = floor(a / 256.0), bHi = floor(b / 256.0);
  return vec4(aHi, a - aHi * 256.0, b - bHi * 256.0, bHi) / 255.0;
}

float luma16(vec4 t) { return floor(t.r * 255.0 + .5) * 256.0 + floor(t.g * 255.0 + .5) - 32768.0; }
float croma16(vec4 t) { return floor(t.a * 255.0 + .5) * 256.0 + floor(t.b * 255.0 + .5) - 32768.0; }

// Escala de frequencia do Sapphire sobre um coeficiente AC.
float escala(int u, int v, float afeta) {
  if (u == 0 && v == 0) return 1.0;
  float s = p0.z;
  if (u > 0) s *= p0.w;
  if (v > 0) s *= p1.x;
  float r = float(u + v);
  float baixa = 1.0 - smoothstep(2.0, 5.0, r);
  float alta = smoothstep(7.0, 11.0, r);
  float media = max(0.0, 1.0 - baixa - alta);
  s *= baixa * p1.y + media * p1.z + alta * p1.w;
  return 1.0 + (s - 1.0) * afeta;
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  float tpp = .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0));
  // Um "pixel" do JPEG: Res Factor pixels da composicao (Res Rel X estica).
  float lado = max(p0.x * tpp, 1.0);
  vec2 R = vec2(max(lado / max(p0.y, .01), 1.0), lado);
  vec2 B = 8.0 * R;
  vec2 b = floor(p / B);
  // Bloco parcial na borda direita/baixa usa o ultimo bloco inteiro.
  vec2 ultimo = max(floor((uSize - 8.0) / B), vec2(0.0));
  vec2 bL = min(b, ultimo);
  vec2 origem = bL * B;
  vec2 macro = floor(b / 2.0);
  vec2 origemM = min(macro * 2.0, floor(ultimo / 2.0) * 2.0) * B;

  if (uModo < .5) {
    vec2 local = p - b * B;
    if (local.x >= 8.0 || local.y >= 8.0) {
      fragColor = vec4(0.0);
      return;
    }
    int u = int(local.x), v = int(local.y);
    float sY = 0.0, sC = 0.0;
    bool cabeca = mod(b.x, 2.0) < .5 && mod(b.y, 2.0) < .5;
    bool temCroma = cabeca && v < 4;
    int uc = u >= 4 ? u - 4 : u;
    for (int y = 0; y < 8; y++) {
      float cy = cos((2.0 * float(y) + 1.0) * float(v) * PI / 16.0);
      float cyC = cos((2.0 * float(y) + 1.0) * float(v) * PI / 16.0);
      for (int x = 0; x < 8; x++) {
        vec4 t = texel(b * B + (vec2(float(x), float(y)) + .5) * R);
        vec3 rgb = (t.a > .00001 ? t.rgb / t.a : vec3(0.0)) * 255.0;
        sY += (dot(rgb, vec3(.299, .587, .114)) - 128.0) * cos((2.0 * float(x) + 1.0) * float(u) * PI / 16.0) * cy;
        if (temCroma) {
          // Croma 4:2:0: um pixel de croma a cada 2x2 do macrobloco.
          vec4 tc = texel(b * B + (vec2(float(2 * x), float(2 * y)) + .5) * R);
          vec3 c = (tc.a > .00001 ? tc.rgb / tc.a : vec3(0.0)) * 255.0;
          float valor = u >= 4
              ? .5 * c.r - .4187 * c.g - .0813 * c.b
              : -.1687 * c.r - .3313 * c.g + .5 * c.b;
          sC += valor * cos((2.0 * float(x) + 1.0) * float(uc) * PI / 16.0) * cyC;
        }
      }
    }
    float qY = .25 * cc(u) * cc(v) * sY / qLuma(u, v);
    float qC = temCroma ? .25 * cc(uc) * cc(v) * sC / qCroma(uc, v) : 0.0;
    fragColor = empacotar16(qY, qC);
    return;
  }

  // ---------------------------------------------------------- decodificar
  vec2 local = p - b * B;
  int x = int(clamp(floor(local.x / R.x), 0.0, 7.0));
  int y = int(clamp(floor(local.y / R.y), 0.0, 7.0));
  vec2 localM = p - macro * 2.0 * B;
  int xc = int(clamp(floor(localM.x / (2.0 * R.x)), 0.0, 7.0));
  int yc = int(clamp(floor(localM.y / (2.0 * R.y)), 0.0, 7.0));

  // Erros de decodificacao (Error Rate, densidade, amplitude, coerencia).
  float quadro = p3.z > .5 ? floor(uTempo * 30.0 / max(p3.z, 1.0)) : 0.0;
  vec2 chave = vec2(bL.x, p4.w > .5 ? -bL.y : bL.y);
  float grupo = p3.y > .001 ? ruidoSuave(chave / (1.0 + p3.y * 3.0), p3.w * 97.0 + quadro) : hash3(vec3(chave, p3.w * 97.0 + quadro));
  bool comErro = p2.z > 0.0 && grupo < p2.w;
  float chanceErro = clamp(p2.z / 64.0, 0.0, 1.0);

  float sY = 0.0;
  for (int v = 0; v < 8; v++) {
    float cv = cc(v) * cos((2.0 * float(y) + 1.0) * float(v) * PI / 16.0);
    for (int u = 0; u < 8; u++) {
      float q = luma16(texel(origem + vec2(float(u), float(v)) + .5));
      if (comErro && hash3(vec3(chave + vec2(float(u), float(v)) * 13.0, quadro + p3.w * 31.0)) < chanceErro) {
        q += (hash3(vec3(chave * 7.0 + vec2(float(v), float(u)), quadro + 5.0)) - .5) * 16.0 * p3.x;
      }
      float f = q * qLuma(u, v) * escala(u, v, p2.x);
      sY += cc(u) * f * cos((2.0 * float(x) + 1.0) * float(u) * PI / 16.0) * cv;
    }
  }
  float sCb = 0.0, sCr = 0.0;
  for (int v = 0; v < 4; v++) {
    float cv = cc(v) * cos((2.0 * float(yc) + 1.0) * float(v) * PI / 16.0);
    for (int u = 0; u < 4; u++) {
      float cu = cc(u) * cos((2.0 * float(xc) + 1.0) * float(u) * PI / 16.0) * cv;
      float e1 = escala(u, v, p2.y);
      sCb += croma16(texel(origemM + vec2(float(u), float(v)) + .5)) * qCroma(u, v) * e1 * cu;
      sCr += croma16(texel(origemM + vec2(float(u + 4), float(v)) + .5)) * qCroma(u, v) * e1 * cu;
    }
  }
  float Y = 128.0 + .25 * sY;
  float Cb = .25 * sCb, Cr = .25 * sCr;
  vec3 rgb = vec3(Y + 1.402 * Cr, Y - .34414 * Cb - .71414 * Cr, Y + 1.772 * Cb) / 255.0;
  rgb = clamp(rgb, 0.0, 1.0);
  // Scale Lights, Offset Darks, Saturation.
  rgb = rgb * p4.x;
  rgb += p4.y * (1.0 - clamp(rgb, 0.0, 1.0));
  rgb = mix(vec3(dot(rgb, vec3(.299, .587, .114))), rgb, p4.z);
  fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
