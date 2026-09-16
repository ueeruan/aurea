#version 460 core
#include <flutter/runtime_effect.glsl>

// Glow e Luz + S_Rays (beta 89). p0.x escolhe o efeito:
// 1 Brilho, 2 Deep Glow, 3 S_SpotLight, 4 S_Glint, 5 S_GlintRainbow,
// 6 S_GlowRings, 7 S_EdgeRays, 8 S_Rays. Medidas em px da camada viram texels
// por texelsPorPixel(); a entrada chega pre-multiplicada e sem interpolar.

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

// Fora da imagem nao ha luz (sem repetir a borda).
vec4 amostra(vec2 p) {
  if (p.x < 0.0 || p.y < 0.0 || p.x > uSize.x || p.y > uSize.y) return vec4(0.0);
  return bilinear(p);
}

float tpp() { return .5 * (uSize.x / max(uLogico.x, 1.0) + uSize.y / max(uLogico.y, 1.0)); }

float luma(vec3 c) { return dot(c, vec3(.2126, .7152, .0722)); }

// A parte da imagem que acende: luminancia acima do limiar, com rampa.
vec3 fonte(vec2 p, float limiar, float suave) {
  vec4 c = amostra(p);
  float y = luma(c.rgb);
  float s = max(suave, .002);
  return c.rgb * smoothstep(limiar - s * .5, limiar + s * .5, y);
}

vec3 matiz(float h) {
  vec3 k = abs(fract(vec3(h) + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0;
  return clamp(k, 0.0, 1.0);
}

// Mistura em modo TELA (1-(1-a)(1-b)), como os renders do AE: a luz clareia
// sem estourar em branco chapado e o detalhe por baixo continua visivel.
vec4 somar(vec4 base, vec3 luz) {
  vec3 l = clamp(luz, 0.0, 1.0);
  vec3 rgb = base.rgb + l * (1.0 - base.rgb);
  float a = clamp(max(base.a, max(rgb.r, max(rgb.g, rgb.b))), 0.0, 1.0);
  return vec4(min(rgb, vec3(a)), a);
}

vec3 tingir(vec3 luz, float quanto) {
  return mix(luz, vec3(luma(luz)) * c0.rgb, quanto);
}

const float OURO = 2.39996323;

// Ruido por pixel: desloca as amostras ao longo do traco para a estrela nao
// virar uma trama de pontos quando o braco e muito mais longo que o passo.
float ruido(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
  q += dot(q, q.yxz + 33.33);
  return fract((q.x + q.y) * q.z);
}

void main() {
  vec2 p = FlutterFragCoord().xy;
  vec4 base = texel(p);
  float efeito = floor(p0.x + .5);
  float k = tpp();

  if (efeito == 1.0 || efeito == 2.0) {
    // BRILHO: disco gaussiano. DEEP GLOW: raios concentrados no centro
    // (densidade 1/r), a queda longa e exponencial do Deep Glow.
    float raio = max(p0.y * k, .001);
    vec3 soma = vec3(0.0);
    float peso = 0.0;
    for (int i = 0; i < 64; i++) {
      float f = (float(i) + .5) / 64.0;
      float r = efeito == 1.0 ? raio * sqrt(f) : raio * f * f;
      float w = efeito == 1.0 ? exp(-2.5 * f) : 1.0;
      float a = float(i) * OURO;
      soma += fonte(p + r * vec2(cos(a), sin(a)), p0.w, p1.x) * w;
      peso += w;
    }
    vec3 luz = soma / peso * p0.z * (efeito == 2.0 ? 3.0 : 1.2);
    fragColor = somar(base, tingir(luz, p1.y));
    return;
  }

  if (efeito == 3.0) {
    // S_SpotLight: elipse girada, borda suave; fora dela o fundo escurece.
    vec2 c = vec2(p0.y, p0.z) * uSize;
    vec2 d = p - c;
    float cr = cos(p1.y), sr = sin(p1.y);
    d = vec2(cr * d.x + sr * d.y, -sr * d.x + cr * d.y);
    vec2 semi = max(vec2(p0.w, p1.x) * .5 * k, vec2(1.0));
    float dist = length(d / semi);
    float suave = max(p1.z, .001);
    float m = 1.0 - smoothstep(1.0 - suave, 1.0 + suave * .25, dist);
    vec3 luz = mix(vec3(p2.x), vec3(p1.w) * c0.rgb, m);
    fragColor = vec4(min(base.rgb * luz, vec3(base.a)), base.a);
    return;
  }

  if (efeito == 4.0 || efeito == 5.0) {
    // S_Glint / S_GlintRainbow: bracos de estrela saindo dos pontos claros.
    float comp = p0.z * k;
    float bracos = clamp(p0.w, 1.0, 4.0);
    vec3 soma = vec3(0.0);
    float salto = ruido(p);
    for (int b = 0; b < 4; b++) {
      float ativo = step(float(b) + .5, bracos);
      float ang = p1.x + float(b) * 3.14159265 / bracos;
      vec2 dir = vec2(cos(ang), sin(ang));
      for (int s = 0; s < 24; s++) {
        float f = (float(s) + salto) / 24.0;
        // Traco fino e longo: pouco peso colado ao ponto (senao vira mancha)
        // e queda suave ate a ponta, como o S_Glint do AE.
        float queda = pow(1.0 - f, 1.0 + p1.z * 2.0) * smoothstep(0.0, .35, f);
        vec3 cor = efeito == 5.0 ? mix(vec3(1.0), matiz(f * p1.w + p2.x / 6.2831853), .6) * 1.2 : c0.rgb;
        vec3 l = fonte(p + dir * f * comp, p0.y, .08) + fonte(p - dir * f * comp, p0.y, .08);
        soma += l * cor * queda * ativo;
      }
    }
    fragColor = somar(base, soma / (efeito == 5.0 ? 6.2 : 8.5) * p1.y);
    return;
  }

  if (efeito == 6.0) {
    // S_GlowRings: um halo curto e aneis em volta dos pontos claros.
    float aneis = clamp(p0.w, 1.0, 5.0);
    float raioAnel = p0.z * k;
    float espessura = p1.x * raioAnel * .5;
    vec3 soma = vec3(0.0);
    for (int j = 1; j <= 5; j++) {
      float r = raioAnel * float(j);
      float w = step(float(j) - .5, aneis) / float(j);
      for (int a = 0; a < 20; a++) {
        float ang = float(a) * 6.2831853 / 20.0;
        vec2 dir = vec2(cos(ang), sin(ang));
        float rr = r + espessura * (mod(float(a), 2.0) - .5);
        // Cada anel puxa a cor para um lado do espectro, como a franja
        // colorida dos aneis do AE.
        soma += fonte(p + dir * rr, p0.y, .08) * w * mix(c0.rgb, matiz(float(j) * .17 + .5), .45);
      }
    }
    vec3 halo = vec3(0.0);
    float rh = max(p1.y * k, .001);
    for (int i = 0; i < 16; i++) {
      float f = (float(i) + .5) / 16.0;
      float a = float(i) * OURO;
      halo += fonte(p + rh * sqrt(f) * vec2(cos(a), sin(a)), p0.y, .08);
    }
    fragColor = somar(base, (soma / 14.0 + halo / 16.0 * .8) * p1.z);
    return;
  }

  if (efeito == 7.0 || efeito == 8.0) {
    // S_EdgeRays (bordas) e S_Rays (partes claras) puxadas para o centro.
    vec2 c = vec2(p0.y, p0.z) * uSize;
    float comp = p0.w;
    float decai = mix(.99, .9, p1.z);
    vec3 soma = vec3(0.0);
    float peso = 0.0, w = 1.0;
    for (int i = 0; i < 40; i++) {
      float f = float(i) / 40.0;
      vec2 q = p + (c - p) * f * comp;
      vec3 l;
      if (efeito == 8.0) {
        l = fonte(q, p1.x, .1);
      } else {
        float e = 1.5;
        float gx = luma(amostra(q + vec2(e, 0.0)).rgb) - luma(amostra(q - vec2(e, 0.0)).rgb);
        float gy = luma(amostra(q + vec2(0.0, e)).rgb) - luma(amostra(q - vec2(0.0, e)).rgb);
        float borda = smoothstep(p1.x, p1.x + .1, length(vec2(gx, gy)));
        l = vec3(borda);
      }
      soma += l * w;
      peso += w;
      w *= decai;
    }
    fragColor = somar(base, soma / max(peso, 1e-4) * p1.y * (efeito == 7.0 ? 3.0 : 1.45) * c0.rgb);
    return;
  }

  fragColor = base;
}
