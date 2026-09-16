#version 460 core
#include <flutter/runtime_effect.glsl>

// S_TVDamage (Sapphire), lote 2 da aba Estilizar (16/09). Contas medidas
// contra renders do After Effects 2026 com o plugin original (quadros
// isolados por componente, ver lib/src/features/editor/domain/tv_damage.dart):
//   estatico   B-spline de ruido por celula de TV (0,9-1 x largura/TvPixels),
//              zona morta 1-sqrt(densidade), ganho 1/densidade, por canal
//   interf.    pontos numa rede de varredura: tau = linha + x/L, ponto a cada
//              1/Frequencia; linha = 2,88 px de TV; cor aleatoria +-1
//   fantasmas  (img + soma h_i img_i) / max(1 + soma h_i, 1); negativo = -g/2
//   barras     s = .5 - .75(1-2w) + nitidez (cos - (1-2w)); b = 2/3 s1 s2 - 1/3
//   listras    x + A sen(fase +-120 graus) min(x, 1-x)
//   linhas     x + A sen(2 pi y / (7,2 px de TV / freq rel)) min(x, 1-x)
// Floats: ABI comum do lote 2 (ver valoresTvDamage).

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

const float TAU = 6.2831853;

vec4 texel(vec2 p) {
  vec2 uv = clamp(p / uSize, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, uv);
}

vec3 reta(vec4 c) { return c.a > .00001 ? c.rgb / c.a : vec3(0.0); }

float hash1(vec3 p) {
  p = fract(p * .1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

vec3 hash3(vec3 p) {
  p = fract(p * vec3(.1031, .1030, .0973));
  p += dot(p, p.yxz + 33.33);
  return fract((p.xxy + p.yxx) * p.zyx);
}

// Ruido 1D suave (gradiente) em [-1, 1].
float ruido1(float x, float s) {
  float i = floor(x);
  float f = x - i;
  float g0 = hash1(vec3(i, s, 7.1)) * 2.0 - 1.0;
  float g1 = hash1(vec3(i + 1.0, s, 7.1)) * 2.0 - 1.0;
  float u = f * f * (3.0 - 2.0 * f);
  return mix(g0 * f, g1 * (f - 1.0), u) * 2.0;
}

// Leitura da imagem com os fantasmas (ate 8).
vec4 fonte(vec2 p) {
  vec4 base = texel(p);
  float amp = p1.w;
  float n = min(floor(p2.x + .5), 8.0);
  if (amp <= 0.0 || n < 1.0) return base;
  float W = uSize.x;
  float sp = p2.z * W;
  float soma = 1.0;
  vec4 acc = base;
  float borrao = p3.y * .037 * W / 736.0;
  for (int i = 0; i < 8; i++) {
    float fi = float(i);
    if (fi >= n) break;
    vec3 r = hash3(vec3(fi, p1.z * 97.0, 3.7));
    float t = n < 1.5 ? .5 : fi / (n - 1.0);
    float pos = sp * (t - (1.0 - p3.x) * .5) + p2.w * sp * (r.x - .5);
    float h = amp * r.y;
    if (r.z < p2.y) h *= -.5;
    vec4 g = texel(p - vec2(pos, 0.0));
    if (borrao > .3) {
      g = .5 * g + .25 * (texel(p - vec2(pos + borrao, 0.0)) + texel(p - vec2(pos - borrao, 0.0)));
    }
    acc += h * g;
    soma += h;
  }
  return acc / max(soma, 1.0);
}

void main() {
  vec2 frag = floor(FlutterFragCoord().xy) + .5;
  float W = uSize.x;
  float H = uSize.y;
  float tv = max(p15.x, 1.0);
  float cel = W / tv;
  float quadro = p15.z;

  vec2 p = frag;
  if (p15.y > .5 && cel > 1.25) p = (floor(frag / cel) + .5) * cel;

  // Desligar: a imagem encolhe para uma linha e depois para um ponto.
  float desl = p13.w;
  vec2 cen = uSize * .5;
  float escY = 1.0 - .995 * smoothstep(0.0, .55, desl);
  float escX = 1.0 - .995 * smoothstep(.45, .95, desl);
  vec2 q = cen + (p - cen) / vec2(escX, escY);
  bool fora = abs(q.x - cen.x) > cen.x || abs(q.y - cen.y) > cen.y;

  // Olho de peixe.
  if (abs(p14.w) > .0001) {
    vec2 d = (q - cen) / cen.x;
    q = cen + d * cen.x / (1.0 + p14.w * .5 * (1.0 - clamp(dot(d, d), 0.0, 2.0) * .5));
  }

  float yu = H - q.y;
  float x = q.x;

  // Vertical hold: rola com periodo H (1 + borda).
  float bh = p4.w * H;
  float perV = H + bh;
  float yy = mod(yu + p4.z * perV, perV);
  bool bordaV = yy >= H;

  // Horizontal hold: ondas por linha, borda preta e a copia do outro lado.
  float desloc = 0.0;
  if (p3.z > 0.0) {
    float f = p3.w * 1.5;
    float a = 1.0;
    float s = 0.0;
    float norma = 0.0;
    float oct = clamp(floor(p4.x + .5), 1.0, 6.0);
    for (int o = 0; o < 6; o++) {
      if (float(o) >= oct) break;
      s += a * ruido1((yy / H) * f + uTempo * .35 * f + float(o) * 13.7, p1.z + float(o));
      norma += a;
      a *= .5;
      f *= 2.0;
    }
    desloc = p3.z * .17 * W * s / max(norma, 1.0) * 1.6;
  }

  // Avanco rapido: faixas rasgadas.
  float faixaFF = 0.0;
  float uFF = 0.0;
  if (p8.x > 0.0 && p8.w > 0.0) {
    float ph = fract(yy * p8.y / H + 2.0 * p8.z);
    float ext = .75 * p8.w;
    if (ph < ext) {
      uFF = ph / ext;
      faixaFF = 1.0;
      desloc -= .095 * W * p8.x * pow(uFF, 1.25);
    }
  }

  float bw = p4.y * .5 * W;
  float perH = W + bw;
  float xx = mod(x - desloc, perH);
  bool bordaH = xx >= W;

  vec3 cor;
  float alfa;
  if (bordaV) {
    // Intervalo vertical: preto com tracos de dados.
    float yb = yy - H;
    float linha = floor(yb / max(cel, 1.0));
    float pos = x / max(cel, 1.0) + hash1(vec3(linha, 4.0, p1.z)) * 40.0;
    float seg = floor(pos / 24.0);
    vec3 r = hash3(vec3(linha, seg, p1.z + 5.0));
    float dentro = fract(pos / 24.0) * 24.0;
    float traco = (r.x < .55 && dentro < 4.0 + r.y * 18.0) ? (.35 + .65 * r.z) : 0.0;
    cor = vec3(traco * p5.x);
    alfa = 1.0;
  } else if (bordaH) {
    cor = vec3(0.0);
    alfa = 1.0;
  } else {
    vec4 s = fonte(vec2(xx, H - yy));
    alfa = s.a;
    cor = reta(s);
  }

  // Listras de cor.
  if (p7.x > 0.0) {
    float ang = p7.z;
    float fase = TAU * (2.0 * p7.y * (x * sin(ang) + yu * cos(ang)) / W + p7.w);
    vec3 o = vec3(sin(fase), sin(fase - TAU / 3.0), sin(fase + TAU / 3.0));
    cor = cor + p7.x * o * min(cor, 1.0 - cor);
  }

  // Barras (duas familias).
  if (p5.y > 0.0) {
    float y2 = 2.0 * p6.x * yu / H - p5.z;
    float w = p6.y;
    float c1v = cos(TAU * (y2 - .45));
    float s1 = clamp(.5 - .75 * (1.0 - 2.0 * w) + p5.w * (c1v - (1.0 - 2.0 * w)), 0.0, 1.0);
    float c2v = cos(TAU * (p6.z * y2 - .45));
    float s2 = 2.0 * clamp(.5 + p6.w * c2v, 0.0, 1.0);
    cor += p5.y * ((2.0 / 3.0) * s1 * s2 - 1.0 / 3.0);
  }

  // Interferencia: pontos na ordem da varredura.
  if (p0.z > 0.0 && p0.w > 0.0) {
    float hL = 2.88 * cel;
    float yl = yu / hL + p1.y;
    float Lb = floor(yl);
    float fy = yl - Lb;
    float tau = Lb + x / W + p1.x;
    float qd = tau * p0.w;
    float k = floor(qd + .5);
    float dx = abs(qd - k) / p0.w * W;
    float wx = clamp((2.2 * cel - dx) / max(cel, .5), 0.0, 1.0);
    float wy = clamp(min(fy, 1.0 - fy) * hL / max(.6 * cel, .5), 0.0, 1.0);
    vec3 u = hash3(vec3(k, quadro, p1.z * 31.0)) * 2.0 - 1.0;
    cor += p0.z * u * wx * wy;
  }

  // Estatico: B-spline quadratica por celula, zona morta pela densidade.
  if (p0.x > 0.0) {
    float c = cel * 1.04;
    vec2 pc = vec2(x, yu) / c;
    vec2 b = floor(pc);
    vec2 d = pc - b - .5;
    vec3 wxs = vec3(.5 * (.5 - d.x) * (.5 - d.x), .75 - d.x * d.x, .5 * (.5 + d.x) * (.5 + d.x));
    vec3 wys = vec3(.5 * (.5 - d.y) * (.5 - d.y), .75 - d.y * d.y, .5 * (.5 + d.y) * (.5 + d.y));
    vec3 N = vec3(0.0);
    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 3; i++) {
        vec3 h = hash3(vec3(b.x + float(i - 1), b.y + float(j - 1), quadro + p1.z * 53.0)) * 2.0 - 1.0;
        N += h * wxs[i] * wys[j];
      }
    }
    N *= 1.07;
    float dens = clamp(p0.y, .01, 1.0);
    float t = 1.0 - sqrt(dens);
    vec3 st = sign(N) * max(abs(N) - t, 0.0) / dens;
    cor += p0.x * st;
  }

  // Avanco rapido: riscos na faixa.
  if (faixaFF > 0.0) {
    float linha = floor(yu / max(cel, 1.0));
    float seg = floor((x / max(cel, 1.0) + hash1(vec3(linha, quadro, 9.0)) * 30.0) / 12.0);
    vec3 r = hash3(vec3(linha, seg, quadro + 11.0));
    if (r.x < .35) cor = mix(cor, vec3(.25 + .75 * r.y), .85);
  }

  // Dropouts: riscos brancos em faixas de linhas.
  if (p9.x > 0.0) {
    float nb = .5 + .5 * ruido1(yu / H * p9.w + p10.y * 17.0, p10.y);
    if (nb > .5 + .5 * p10.x) {
      float linha = floor(yu / max(1.5 * cel, 1.0));
      float per = max((p9.y + p9.z) * .28, .01);
      float pos = x / W + hash1(vec3(linha, p10.y, 2.0)) * 7.0;
      float k = floor(pos / per);
      vec3 r = hash3(vec3(linha, k, p10.y + 3.0));
      float dentro = fract(pos / per) * per;
      if (r.x < .6 && dentro < p9.y * .28 * (.3 + 1.4 * r.y)) {
        cor = mix(cor, vec3(1.0), clamp(p9.x, 0.0, 1.0));
      }
    }
  }

  // Linhas de varredura.
  if (p11.z > 0.0) {
    float per = cel * 7.2 / max(p11.w, .001);
    float s = -sin(TAU * (frag.y) / per);
    cor = cor + p11.z * s * min(cor, 1.0 - cor);
  }

  // Orthicon: escurece em volta do que passa do limiar.
  if (p12.x > 0.0) {
    float rr = max(p12.z * .1 * W, 1.0);
    float acc = 0.0;
    for (int i = 0; i < 6; i++) {
      float a = TAU * float(i) / 6.0 + .5;
      vec3 v = reta(texel(vec2(xx, H - yy) + vec2(cos(a), sin(a)) * rr));
      acc += max(dot(v, vec3(.299, .587, .114)) - p12.y, 0.0);
    }
    cor *= 1.0 - clamp(p12.x * acc / 6.0 * 2.0, 0.0, 1.0);
  }

  // Correcao de cor.
  if (abs(p12.w) > .0001) {
    float cs = cos(p12.w);
    float sn = sin(p12.w);
    vec3 k = vec3(.57735);
    cor = cor * cs + cross(k, cor) * sn + k * dot(k, cor) * (1.0 - cs);
  }
  float lum = dot(cor, vec3(.299, .587, .114));
  cor = mix(vec3(lum), cor, p13.x);
  cor = cor * p13.y * c0.rgb + (vec3(p13.z) + c1.rgb) * (1.0 - clamp(cor, 0.0, 1.0));

  // Vinheta eliptica.
  if (p10.z > 0.0) {
    vec2 d = (frag - cen) / cen.x;
    d.y /= max(p11.y, .1) * (W / H);
    float r = length(d) * .7071;
    float v = smoothstep(p10.w - p11.x * .5, p10.w + p11.x * .5, r * 1.4142 * .75);
    cor *= 1.0 - p10.z * v;
  }

  // Desligar: clareia, flare e apaga.
  if (desl > 0.0) {
    cor = mix(cor, vec3(1.0), smoothstep(0.0, .6, desl));
    vec2 d = (frag - cen);
    float fw = max(p14.x * cel * .02, 1.0);
    float flare = p14.y * exp(-dot(d, d) / (fw * fw)) * smoothstep(.75, 1.0, desl);
    float corte = p14.z <= 0.0 ? (desl >= 1.0 ? 1.0 : 0.0) : smoothstep(1.0 - p14.z, 1.0, desl);
    if (fora) cor = vec3(0.0);
    cor = (cor + vec3(flare)) * (1.0 - corte);
    alfa = max(alfa, 1.0);
  }

  cor = clamp(cor, 0.0, 1.0);
  alfa = clamp(alfa, 0.0, 1.0);
  fragColor = vec4(cor * alfa, alfa);
}
