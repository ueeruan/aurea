#version 460 core
#include <flutter/runtime_effect.glsl>

// ABA DISTORCER (16/09): CC Lens, Optics Compensation e Turbulent Displace.
// Mapeamentos medidos anel a anel em quadros do After Effects do dono:
//   CC Lens            r_fonte = r * (1 - conv/100 * (r/R)^2), R = Size% da
//                      meia-diagonal; fora do disco, transparente.
//   Optics Comp.       f = 0,78 * meia-largura / tan(FOV/2);
//                      r_fonte = f*tan(r/f) (ou f*atan(r/f) invertido).
//   Turbulent Displace ruido fractal (a base de ruido da Adobe e fechada:
//                      tamanho, forca, complexidade e evolucao seguem o AE).
// ABI do lote Sapphire: 0,1 tamanho; 2 filtro; 3,4 logico; 5 escalaRef;
// 6 tempo; 7 passada; 8.. valores (p0.x = efeito).

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
uniform sampler2D uImage;

out vec4 fragColor;

vec4 texel(vec2 p) {
  vec2 uv = clamp(p / uSize, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, uv);
}

// Bilinear manual (a entrada chega sem interpolacao). Fora = transparente.
vec4 amostra(vec2 q) {
  if (q.x < 0.0 || q.y < 0.0 || q.x > uSize.x || q.y > uSize.y) return vec4(0.0);
  vec2 g = q - .5;
  vec2 b = floor(g);
  vec2 f = g - b;
  vec4 a = texel(b + .5), c = texel(b + vec2(1.5, .5));
  vec4 d = texel(b + vec2(.5, 1.5)), e = texel(b + 1.5);
  return mix(mix(a, c, f.x), mix(d, e, f.x), f.y);
}

float hash3(vec3 p) {
  p = fract(p * .1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float ruido(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(
    mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x),
        mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
    mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x),
        mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y),
    f.z) * 2.0 - 1.0;
}

float fbm(vec3 x, float oitavas) {
  float soma = 0.0, peso = 1.0, total = 0.0;
  for (int i = 0; i < 10; i++) {
    float w = clamp(oitavas - float(i), 0.0, 1.0);
    if (w <= 0.0) break;
    soma += ruido(x) * peso * w;
    total += peso * w;
    peso *= .5;
    x = x * 2.0 + vec3(17.1, 9.3, 3.7);
  }
  return soma / max(total, .0001);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  int efeito = int(p0.x + .5);
  float tpp = .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0));

  if (efeito == 1) {
    // CC LENS. p0.yz = centro (0..1), p0.w = Size (%), p1.x = Convergence.
    vec2 centro = p0.yz * uSize;
    float R = p0.w / 100.0 * .5 * length(uSize);
    vec2 d = p - centro;
    float r = length(d);
    if (r >= R || R <= 0.0) {
      fragColor = vec4(0.0);
      return;
    }
    float x = r / R;
    fragColor = amostra(centro + d * (1.0 - p1.x / 100.0 * x * x));
    return;
  }

  if (efeito == 2) {
    // OPTICS COMPENSATION. p0.y = FOV (graus), p0.z = inverter,
    // p0.w = orientacao (1 horiz, 2 vert, 3 diag), p1.xy = centro (0..1).
    float fov = radians(clamp(p0.y, 0.0, 179.9));
    if (fov < .0001) {
      fragColor = texel(p);
      return;
    }
    float meia = p0.w < 1.5 ? .5 * uSize.x : (p0.w < 2.5 ? .5 * uSize.y : .5 * length(uSize));
    float f = .78 * meia / tan(fov / 2.0);
    vec2 centro = p1.xy * uSize;
    vec2 d = p - centro;
    float r = length(d);
    if (r < .0001) {
      fragColor = texel(p);
      return;
    }
    float rs;
    if (p0.z > .5) {
      rs = f * atan(r / f);
    } else {
      float ang = r / f;
      if (ang >= 1.5707) {
        fragColor = vec4(0.0);
        return;
      }
      rs = f * tan(ang);
    }
    fragColor = amostra(centro + d * (rs / r));
    return;
  }

  // TURBULENT DISPLACE. p0.y = tipo (1..11), p0.z = quantidade (px AE),
  // p0.w = tamanho (px AE); p1.xy = deslocar (px AE); p1.z = complexidade;
  // p1.w = evolucao (graus); p2.x = semente; p2.y = fixacao (1..17).
  float esc = uEscalaRef * tpp;
  float tipo = floor(p0.y + .5);
  float quantidade = p0.z * esc;
  float tamanho = max(p0.w * esc, 1.0);
  vec2 q = (p - p1.xy * esc) / tamanho;
  float fase = p1.w / 360.0;
  float oitavas = clamp(p1.z, 1.0, 10.0);
  // Versoes "mais suaves" usam menos detalhe.
  if (tipo == 4.0 || tipo == 5.0 || tipo == 6.0) oitavas = max(1.0, oitavas * .5);
  float s = p2.x * 1.37;
  vec2 desloc = vec2(fbm(vec3(q, fase + s), oitavas), fbm(vec3(q + 31.7, fase + s + 7.1), oitavas));
  vec2 alvo;
  if (tipo == 2.0 || tipo == 5.0) {
    // Protuberancia: empurra para fora do centro do ruido.
    float n = fbm(vec3(q, fase + s), oitavas);
    alvo = p - normalize(p - .5 * uSize + .0001) * n * quantidade;
  } else if (tipo == 3.0 || tipo == 6.0) {
    // Torcer: gira em volta do ponto.
    float n = fbm(vec3(q, fase + s), oitavas) * quantidade / tamanho;
    vec2 d = p - .5 * uSize;
    alvo = .5 * uSize + mat2(cos(n), sin(n), -sin(n), cos(n)) * d;
  } else if (tipo == 7.0) {
    alvo = p + vec2(0.0, desloc.y * quantidade);
  } else if (tipo == 8.0) {
    alvo = p + vec2(desloc.x * quantidade, 0.0);
  } else if (tipo == 9.0) {
    alvo = p + vec2(desloc.x, desloc.x) * quantidade;
  } else {
    alvo = p + desloc * quantidade;
  }
  // FIXACAO: 1 nenhuma; 2 cantos; 3 todas as bordas (padrao) ... as demais
  // variantes seguem "bordas" nas duas direcoes.
  float fix = floor(p2.y + .5);
  if (fix >= 2.0) {
    vec2 borda = min(p, uSize - p) / max(tamanho, 1.0);
    float w = fix == 2.0
        ? clamp(min(length(p), min(length(uSize - p), min(length(vec2(uSize.x - p.x, p.y)), length(vec2(p.x, uSize.y - p.y))))) / tamanho, 0.0, 1.0)
        : clamp(min(borda.x, borda.y), 0.0, 1.0);
    alvo = mix(p, alvo, w);
  }
  fragColor = amostra(alvo);
}
