#version 460 core
#include <flutter/runtime_effect.glsl>

// S_VHSDamage (Sapphire), uma passada. Contas medidas em renders do AE 2026
// do dono (comp 736x736, sondas isoladas por estagio) — ver
// lib/src/features/editor/domain/vhs_damage.dart. As LUTs "VHS color" NAO
// foram copiadas: sao gradacoes proprias em formula.
//
// Floats: tamanho[0,1] filtro[2] logico[3,4] escalaRef[5] tempo[6] modo[7]
// p0..p15[8..71] c0,c1[72..79]. Ordem dos p* em valoresVhsDamage.

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

const vec3 W601 = vec3(.299, .587, .114);
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

vec3 reta(vec4 c) { return c.a > .00001 ? c.rgb / c.a : vec3(0.0); }

float h31(vec3 p) {
  p = fract(p * vec3(.1031, .1030, .0973));
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float bit(float mask, float b) { return mod(floor(mask / pow(2.0, b)), 2.0); }

// ruido de valor suave (-1..1)
float vnoise(vec2 q, float z) {
  vec2 i = floor(q);
  vec2 f = q - i;
  f = f * f * (3.0 - 2.0 * f);
  float a = h31(vec3(i, z)), b = h31(vec3(i + vec2(1, 0), z));
  float c = h31(vec3(i + vec2(0, 1), z)), d = h31(vec3(i + vec2(1, 1), z));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y) * 2.0 - 1.0;
}

// Gradacoes proprias (nao sao as LUTs do Sapphire). k = 1..4.
vec3 gradacao(vec3 c, float k) {
  float y = dot(c, W601);
  vec3 ch = c - y;
  if (k < 1.5) { // 01: sombras frias, luz neutra
    ch *= 1.1;
    ch += vec3(-.02, .005, .03) * (1.0 - y);
  } else if (k < 2.5) { // 02: magenta leve e pretos levantados
    ch *= 1.05;
    ch += vec3(.012, -.012, .006);
    y = .02 + .98 * y;
  } else if (k < 3.5) { // 03: pele quente, croma sobe com a luz
    // Ganho e desvio ajustados contra o quadro padrao do AE: as medias de
    // R, G e B batem (87,1 69,4 67,3 contra 86,7 69,1 67,4).
    float g = .85 + .35 * smoothstep(.25, .75, y) - .1 * smoothstep(.8, 1.0, y);
    ch *= g;
    ch += vec3(0.0, -.012, -.03) * y * y;
  } else { // 04: desbotado esverdeado
    ch *= .8;
    ch += vec3(-.01, .015, -.01);
    y = .04 + .92 * y;
  }
  return y + ch;
}

// VintageColor3Strip: croma ajustado (quadratico) nos renders do AE.
vec3 vintage(vec3 c) {
  float R = c.r, G = c.g, B = c.b;
  vec3 o = vec3(.58122, -.23815, -.29817) * R + vec3(-.48418, .35364, -.55102) * G +
           vec3(-.08553, -.11568, .81997) * B + vec3(.87328, -.38189, -.32403) * R * R +
           vec3(-.87661, .60196, -.8004) * G * G + vec3(-.1434, -.14458, 1.12057) * B * B +
           vec3(.04012, -.21238, .98831) * R * G + vec3(-.62799, .46147, -.72909) * R * B +
           vec3(.72001, -.32162, -.23241) * G * B;
  return o; // croma (soma de luma zero)
}

// dropouts (streaks/sparkles): a = quantidade, opacidade, frequencia, comprimento;
// b = pretos, freq das faixas, variacao, tamanho; c = deslocar, variacao, rolagem, id.
vec2 dropout(vec2 p, float t, float frame, vec4 a, vec4 b, vec4 c, float semente) {
  float r1 = h31(vec3(frame, semente, 11.0 + c.w));
  float r2 = h31(vec3(frame, semente, 23.0 + c.w));
  float f = b.y + b.z * r1;
  if (f <= 0.0 || a.x <= 0.0) return vec2(0.0);
  float u = fract(p.y / uSize.y * f + c.x + c.z * t + c.y * r2 * 4.0);
  float tam = clamp(b.w, 0.0, 1.0);
  if (u >= tam) return vec2(0.0);
  float env = sin(3.14159 * u / max(tam, .001));
  float cel = max(uSize.x / max(a.z, 1.0), .5);
  float lin = floor(p.y / cel);
  float comp = max(a.w * cel * .75, 1.0);
  float desl = h31(vec3(lin, frame, semente + c.w)) * comp;
  float k = floor((p.x + desl) / comp);
  float s = fract((p.x + desl) / comp);
  float h = h31(vec3(lin, k, frame + semente * 7.0 + c.w));
  if (h > clamp(a.x * env * .12, 0.0, 1.0)) return vec2(0.0);
  float len = .25 + .75 * h31(vec3(k, lin, frame + 5.0 + c.w));
  if (s > len) return vec2(0.0);
  float forca = clamp(a.y * (.15 + .6 * h31(vec3(k, lin + 9.0, frame))) * (1.0 - s / len), 0.0, 1.0);
  float branco = h31(vec3(k + 3.0, lin, frame + semente)) >= b.x ? 1.0 : 0.0;
  if (s * comp < 1.0) branco = 1.0 - branco;
  return vec2(forca, branco);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  float esc = max(uEscalaRef * texelsPorPixel(), 1e-4);
  float t = uTempo;
  float frame = floor(t * 30.0 + .001);
  float mask = p15.x;
  float semente = p15.y;
  float misturar = clamp(p15.z, 0.0, 1.0);

  vec4 orig = texel(p);
  float alfa = orig.a;

  // Tape Noise (campo medido: independe da imagem, muda a cada quadro, dy = -DY/DX * dx)
  vec2 q = p;
  if (bit(mask, 6.0) > .5) {
    float s = .9 * max(p6.y, 0.0) * esc;
    float cel = max(s * 3.0, 1.0);
    float stdC = (193.0 * p6.x * esc / 1.732) / max(1.0, 3.545 * s);
    // 0,6 e 0,3: ajustados contra o quadro do AE (o sorteio nunca coincide,
    // e a amplitude medida sozinha deixava a borda mais serrilhada que no AE).
    float n = vnoise(p / cel, frame + 1.7) * stdC / .38 * .6;
    n += (h31(vec3(p, frame + 91.0)) * 2.0 - 1.0) * 184.0 * p6.z * esc * .3;
    q = p + vec2(p6.w * n, -p7.x * n);
  }
  // Fast Forward: faixas rasgadas
  if (p7.y > 0.0) {
    float f = p7.z + p7.w * h31(vec3(frame, semente, 41.0));
    if (f > 0.0) {
      float u = fract(p.y / uSize.y * f + p8.y + p15.w * h31(vec3(frame, 43.0, semente)));
      if (u < p8.x) q.x += p7.y * uSize.x * .08 * (1.0 - u / max(p8.x, .001));
    }
  }
  // Downsample
  if (bit(mask, 0.0) > .5 && p0.x > 0.0 && p0.x < uSize.x) {
    float fx = uSize.x / p0.x;
    q = (floor(q / fx) + .5) * fx;
  }

  vec3 cq = reta(texel(q));
  float Y = dot(cq, W601);
  // Blur Luma: sigma = 44 px do AE por unidade; abaixo de ~.6 px vira caixa de 3
  if (bit(mask, 5.0) > .5) {
    float sx = p5.y * 44.0 * p5.z * esc;
    float sy = p5.y * 44.0 * p5.w * esc;
    if (sx > .02 || sy > .02) {
      float dx = sx < .6 ? 1.0 : sx * 1.2;
      float dy = sy < .6 ? 1.0 : sy * 1.2;
      float wx = sx < .6 ? min(sx * sx * 1.2, .33) : .3;
      float wy = sy < .6 ? min(sy * sy * 1.2, .33) : .3;
      if (sy >= .45 && sy < .6) wy = .33;
      if (sx >= .45 && sx < .6) wx = .33;
      float soma = 0.0;
      float peso = 0.0;
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          float w = (i == 0 ? 1.0 - 2.0 * wx : wx) * (j == 0 ? 1.0 - 2.0 * wy : wy);
          if (w > .0005) {
            soma += dot(reta(texel(q + vec2(float(i) * dx, float(j) * dy))), W601) * w;
            peso += w;
          }
        }
      }
      Y = soma / max(peso, 1e-4);
    }
  }

  // croma por canal (R-Y, G-Y, B-Y); deslocamento do canal = Amount - Shift do canal (medido)
  float aber = bit(mask, 3.0);
  float offR = aber * (p2.x - p2.z) * esc;
  float offG = aber * (p2.x - p2.w) * esc;
  float offB = aber * (p2.x - p3.x) * esc;
  vec3 ch = cq - dot(cq, W601);
  if (bit(mask, 2.0) > .5) {
    // Color Downsample: celula x = 106,7/escala, y = 1,2*x/rel altura (px do AE), media + bilinear
    float cx = max(106.7 / max(p1.z, .01) * esc, 1.0);
    float cy = max(1.2 * 106.7 / max(p1.z, .01) / max(p1.w, .01) * esc, 1.0);
    vec2 cel = vec2(cx, cy);
    float offM = (offR + offG + offB) / 3.0;
    vec2 g = (q - vec2(offM, 0.0)) / cel - .5;
    vec2 k = floor(g);
    vec3 v00 = vec3(0.0);
    vec3 v10 = vec3(0.0);
    vec3 v01 = vec3(0.0);
    vec3 v11 = vec3(0.0);
    for (int j = 0; j <= 1; j++) {
      for (int i = 0; i <= 1; i++) {
        vec2 centro = (k + vec2(float(i), float(j)) + .5) * cel;
        vec3 acc = vec3(0.0);
        for (int n = 0; n < 4; n++) {
          vec2 o = vec2((n == 0 || n == 2) ? -.3 : .3, n < 2 ? -.3 : .3) * cel;
          vec3 c = reta(texel(centro + o));
          acc += c - dot(c, W601);
        }
        acc *= .25;
        if (i == 0 && j == 0) v00 = acc;
        if (i == 1 && j == 0) v10 = acc;
        if (i == 0 && j == 1) v01 = acc;
        if (i == 1 && j == 1) v11 = acc;
      }
    }
    vec2 fr = g - k;
    float tR = fr.x + (offM - offR) / cx;
    float tG = fr.x + (offM - offG) / cx;
    float tB = fr.x + (offM - offB) / cx;
    vec3 cc = vec3(mix(mix(v00, v10, tR), mix(v01, v11, tR), fr.y).r,
                   mix(mix(v00, v10, tG), mix(v01, v11, tG), fr.y).g,
                   mix(mix(v00, v10, tB), mix(v01, v11, tB), fr.y).b);
    ch = cc - dot(cc, W601);
  } else if (aber > .5) {
    vec3 a = reta(texel(q - vec2(offR, 0.0)));
    vec3 b = reta(texel(q - vec2(offG, 0.0)));
    vec3 d = reta(texel(q - vec2(offB, 0.0)));
    vec3 cc = vec3(a.r - dot(a, W601), b.g - dot(b, W601), d.b - dot(d, W601));
    ch = cc - dot(cc, W601);
  }
  if (aber > .5) {
    // Sharpen - Soften do croma: unsharp, sigma ~3,3 px do AE
    float sh = p3.z;
    if (abs(sh) > .001) {
      float r = 3.3 * esc;
      vec3 m = reta(texel(q + vec2(r, 0.0))) + reta(texel(q - vec2(r, 0.0))) +
               reta(texel(q + vec2(0.0, r))) + reta(texel(q - vec2(0.0, r)));
      m *= .25;
      vec3 chm = m - dot(m, W601);
      ch += sh * .5 * (ch - chm);
    }
    ch *= max(p3.y, 0.0);
  }

  vec3 cor = Y + ch;
  // Color Style: gradacoes proprias + Tint
  if (bit(mask, 1.0) > .5) {
    float l1 = floor(p0.y + .5);
    float l2 = floor(p0.w + .5);
    if (l1 >= 1.0) cor = mix(cor, gradacao(cor, l1), clamp(p0.z, 0.0, 1.0));
    if (l2 >= 1.0) cor = mix(cor, gradacao(cor, l2), clamp(p1.x, 0.0, 1.0));
    cor += vec3(.5, -1.0, .5) * p1.y * .05;
    Y = dot(cor, W601);
    ch = cor - Y;
  }
  // Color Bloom: Vintage3Strip (mistura linear, medida) e saturacao
  if (bit(mask, 4.0) > .5) {
    vec3 v = vintage(clamp(Y + ch, 0.0, 1.0));
    ch = (ch + (v - ch) * max(p4.x, 0.0)) * max(p4.y, 0.0);
  }
  // Luma Adjust: Y' = off + (escala - off) * Y^(1/gama) (medido)
  if (bit(mask, 5.0) > .5) {
    Y = p4.z + (p5.x - p4.z) * pow(max(Y, 0.0), 1.0 / max(p4.w, .1));
  }
  cor = Y + ch;

  // Scanlines: periodo = altura/(2*freq), x = c^(1+.5*min(I,1)), x + clamp(I sen)*min(x,1-x)
  if (bit(mask, 7.0) > .5 && p8.z > 0.0) {
    float I = max(p8.w, 0.0);
    float per = uSize.y / (2.0 * p8.z);
    float v = uSize.y * .5 - p.y;
    float s = clamp(I * sin(TAU * (v / per + p9.x * t)), -1.0, 1.0);
    vec3 x = pow(clamp(cor, 0.0, 1.0), vec3(1.0 + .5 * min(I, 1.0)));
    cor = x + s * min(x, 1.0 - x);
  }

  // Streaks e Sparkles
  if (bit(mask, 8.0) > .5) {
    vec2 d = dropout(p, t, frame, vec4(p9.y, p9.z, p9.w, p10.x), vec4(p10.y, p10.z, p10.w, p11.x),
                     vec4(p11.y, p11.z, p11.w, 0.0), semente);
    if (d.x > 0.0) {
      vec3 ruido = vec3(h31(vec3(p, frame)), h31(vec3(p, frame + 1.0)), h31(vec3(p, frame + 2.0))) - .5;
      cor = mix(cor, vec3(d.y) + ruido * p14.w, d.x);
    }
  }
  if (bit(mask, 9.0) > .5) {
    vec2 d = dropout(p, t, frame, p12, p13, vec4(p14.x, p14.y, p14.z, 1.0), semente);
    if (d.x > 0.0) cor = mix(cor, vec3(d.y), d.x);
  }

  cor = clamp(cor, 0.0, 1.0);
  cor = mix(cor, reta(orig), misturar);
  fragColor = vec4(cor * alfa, alfa);
}
