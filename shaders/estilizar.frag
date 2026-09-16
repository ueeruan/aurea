#version 460 core
#include <flutter/runtime_effect.glsl>

// ABA ESTILIZAR, LOTE 1 (16/09). Contas ajustadas contra quadros do After
// Effects 2026 do dono — ver lib/src/features/editor/domain/estilizar.dart.
// A entrada do filtro chega sem interpolacao (Impeller): toda leitura cai
// no centro de um texel.
//
// Floats: tamanho[0..1] filtro[2] logico[3..4] escalaRef[5] modo[6]
// tempo[7] p0..p3[8..23] c0..c4[24..43].

uniform vec2 uSize;
uniform float uFilter;
uniform vec2 uLogico;
uniform float uEscalaRef;
uniform float uModo;
uniform float uTempo;
uniform vec4 p0;
uniform vec4 p1;
uniform vec4 p2;
uniform vec4 p3;
uniform vec4 c0;
uniform vec4 c1;
uniform vec4 c2;
uniform vec4 c3;
uniform vec4 c4;
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

vec3 reta(vec4 c) { return c.a > .00001 ? c.rgb / c.a : vec3(0.0); }

float hash3(vec3 p) {
  p = fract(p * .1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}

float texelsPorPixel() {
  return .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0));
}

// Gaussiana amostrada: nucleo exato ate sigma 1,62 texel; acima, 24
// amostras por importancia giradas por pixel.
vec4 desfocar(vec2 p, float sigma) {
  if (sigma < .3) return texel(p);
  vec4 soma = vec4(0.0);
  float peso = 0.0;
  if (sigma < 1.62) {
    float k = -.5 / (sigma * sigma);
    for (int j = -3; j <= 3; j++) {
      for (int i = -3; i <= 3; i++) {
        float d2 = float(i * i + j * j);
        if (d2 <= 10.0) {
          float w = exp(k * d2);
          soma += texel(p + vec2(float(i), float(j))) * w;
          peso += w;
        }
      }
    }
    return soma / peso;
  }
  float giro = TAU * fract(52.9829189 * fract(dot(floor(p), vec2(.06711056, .00583715))));
  vec2 dir = vec2(cos(giro), sin(giro));
  for (int s = 0; s < 24; s++) {
    float u = (float(s) + .5) / 24.0;
    soma += texel(p + floor(dir * sigma * sqrt(-2.0 * log(1.0 - u)) + .5));
    dir = vec2(-.7373688 * dir.x - .6754903 * dir.y, .6754903 * dir.x - .7373688 * dir.y);
  }
  return soma / 24.0;
}

// Gradiente da luma desfocada (derivada da gaussiana), em luma por texel.
vec2 gradiente(vec2 p, float sigma) {
  sigma = max(sigma, .5);
  vec2 g = vec2(0.0);
  if (sigma < 1.62) {
    float k = -.5 / (sigma * sigma);
    float peso = 0.0;
    for (int j = -3; j <= 3; j++) {
      for (int i = -3; i <= 3; i++) {
        vec2 d = vec2(float(i), float(j));
        float d2 = dot(d, d);
        if (d2 <= 10.0) {
          float w = exp(k * d2);
          g += dot(reta(texel(p + d)), LUMA601) * d * w;
          peso += w;
        }
      }
    }
    return g / (peso * sigma * sigma);
  }
  float giro = TAU * fract(52.9829189 * fract(dot(floor(p), vec2(.06711056, .00583715))));
  vec2 dir = vec2(cos(giro), sin(giro));
  for (int s = 0; s < 32; s++) {
    float u = (float(s) + .5) / 32.0;
    vec2 d = floor(dir * sigma * sqrt(-2.0 * log(1.0 - u)) + .5);
    g += dot(reta(texel(p + d)), LUMA601) * d;
    dir = vec2(-.7373688 * dir.x - .6754903 * dir.y, .6754903 * dir.x - .7373688 * dir.y);
  }
  return g / (32.0 * sigma * sigma);
}

void main() {
  vec2 p = floor(FlutterFragCoord().xy) + .5;
  int modo = int(uModo + .5);
  vec4 o = texel(p);
  vec3 c = reta(o);
  float a = o.a;
  float esc = uEscalaRef * texelsPorPixel();

  if (modo == 1) {
    // CC THRESHOLD: luma Rec.601 (ou um canal) contra o limiar.
    float ch = p0.y;
    float v = ch < .5 ? dot(c, LUMA601) : (ch < 1.5 ? c.r : (ch < 2.5 ? c.g : c.b));
    float b = v >= p0.x ? 1.0 : 0.0;
    if (p0.z > .5) b = 1.0 - b;
    c = mix(vec3(b), c, p0.w);
  } else if (modo == 2) {
    // CC THRESHOLD RGB: o mesmo, canal a canal.
    vec3 b = step(p0.xyz, c);
    b = mix(b, 1.0 - b, step(vec3(.5), p1.xyz));
    c = mix(b, c, p0.w);
  } else if (modo == 3) {
    // CC VIGNETTE: queda cos^4 da lente.
    vec2 centro = p0.zw * uSize;
    float t = length(p - centro) / (.5 * uSize.x) * p0.y;
    float f = 1.0 / ((1.0 + t * t) * (1.0 + t * t));
    float k = p0.x * (1.0 - f);
    float protege = p1.x * smoothstep(.5, 1.0, dot(c, LUMA601));
    k *= 1.0 - protege;
    c = k >= 0.0 ? c * max(1.0 - k, 0.0) : c + (1.0 - c) * min(-k, 1.0);
  } else if (modo == 4) {
    // CC BLOCK LOAD: varreduras de blocos 2^(n-1-k), de cima para baixo.
    float n = max(p0.y, 1.0);
    float q = clamp(p0.x, 0.0, 1.0) * n;
    if (q < n - .00001) {
      // c varreduras prontas nesta linha: bloco 2^(n-1-c) (medido: 50 % de
      // 4 varreduras = blocos de 2 px; 25 % = 4 px).
      float k = floor(q);
      float frente = (q - k) * uSize.y;
      float prontas = p.y < frente ? k + 1.0 : k;
      if (prontas < .5) {
        if (p0.z > .5) {
          fragColor = vec4(0.0);
          return;
        }
        prontas = 0.0;
      }
      float lado = pow(2.0, n - 1.0 - prontas) * max(uEscalaRef * texelsPorPixel(), .0001);
      lado = max(floor(lado + .5), 1.0);
      if (lado > 1.0) {
        if (p0.w > .5) {
          vec2 g = (p - .5 * lado) / lado;
          vec2 base = floor(g);
          vec2 fr = g - base;
          vec4 s00 = texel((base + .5) * lado);
          vec4 s10 = texel((base + vec2(1.5, .5)) * lado);
          vec4 s01 = texel((base + vec2(.5, 1.5)) * lado);
          vec4 s11 = texel((base + 1.5) * lado);
          fragColor = mix(mix(s00, s10, fr.x), mix(s01, s11, fr.x), fr.y);
        } else {
          fragColor = texel((floor(p / lado) + .5) * lado);
        }
        return;
      }
    }
  } else if (modo == 5) {
    // S_SCANLINES: p = .5+.5*S*sen, saida = 2*p*entrada^gama.
    if (p3.y > .001) {
      o = desfocar(p, p3.y * esc);
      c = reta(o);
      a = o.a;
    }
    float periodo = uSize.x / (2.0 * max(p0.x, .0001));
    vec2 rel = p - .5 * uSize;
    float v = -rel.y * cos(p0.z) + rel.x * sin(p0.z);
    vec3 fase = v / periodo + p0.w + vec3(p1.x, p1.y, p1.z);
    vec3 perfil = clamp(.5 + .5 * p0.y * sin(TAU * fase), 0.0, 1.0);
    // x <= .5: 2*p*x; acima, a luz que passaria de 1 vai para a faixa
    // escura (medido: fundo das linhas claras nao chega a zero).
    vec3 x = pow(clamp(c, 0.0, 1.0), vec3(p2.w));
    c = x + (2.0 * perfil - 1.0) * min(x, 1.0 - x);
    if (p1.w > 0.0) {
      vec2 cel = floor(p / max(periodo / max(p2.x, .01), 1.0));
      float quadro = floor(uTempo * 30.0);
      c += p1.w * (vec3(hash3(vec3(cel, quadro)), hash3(vec3(cel, quadro + 17.0)), hash3(vec3(cel, quadro + 31.0))) - .5);
    }
    c = c * p2.y * c0.rgb + p2.z;
    c = mix(vec3(dot(c, LUMA601)), c, p3.x);
  } else if (modo == 6) {
    // S_HALFTONE: grade de pontos de periodo largura/freq.
    if (p1.z > .001) {
      o = desfocar(p, p1.z * esc);
      c = reta(o);
      a = o.a;
    }
    float periodo = uSize.x / max(p0.y, .0001);
    vec2 q = p - .5 * uSize - vec2(p1.w, p2.x) * esc;
    float ca = cos(-p0.z), sa = sin(-p0.z);
    q = vec2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
    q.x /= max(p0.w, .01);
    vec2 u = q / periodo;
    float s = .5 - .25 * (cos(TAU * u.x) + cos(TAU * u.y));
    // Contraste 1,5 no cinza medio antes da retícula (ajustado no render).
    float lum = clamp((dot(c, LUMA601) + p1.y - .5) * 1.5 + .5, 0.0, 1.0);
    float w;
    if (p0.x < .5) {
      w = clamp((s - (1.0 - lum)) * p1.x + .5, 0.0, 1.0);
    } else {
      w = 1.0 - clamp((s - lum) * p1.x + .5, 0.0, 1.0);
    }
    c = mix(c1.rgb, c0.rgb, w);
  } else if (modo == 7) {
    // S_EDGECOLORIZE: sigma = Edge Smooth/2, ganho 3,4, cor pela direcao.
    float sigma = .5 * p0.x * esc;
    vec2 g = gradiente(p, sigma);
    float cr = cos(p0.z), sr = sin(p0.z);
    g = vec2(cr * g.x - sr * g.y, sr * g.x + cr * g.y);
    float m = length(g) * max(sigma, .5) * 3.4 * p0.y;
    vec2 n = length(g) > 1e-6 ? normalize(g) : vec2(0.0);
    vec3 borda = c1.rgb * max(n.y, 0.0) + c3.rgb * max(-n.y, 0.0)
        + c4.rgb * max(n.x, 0.0) + c2.rgb * max(-n.x, 0.0);
    c = c0.rgb + borda * m;
  }
  a = clamp(a, 0.0, 1.0);
  fragColor = vec4(clamp(c, 0.0, 1.0) * a, a);
}
