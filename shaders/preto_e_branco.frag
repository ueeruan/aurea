#version 460 core
#include <flutter/runtime_effect.glsl>

// BLACK & WHITE (19/09), na conta do After Effects.
//
// NAO E `rgb -> luminancia`. Seis faixas de cor — vermelhos, amarelos,
// verdes, ciano, azuis e magentas — dizem QUANTO cada familia de cor
// entra no cinza. Um vermelho puro com Vermelhos em 40 sai cinza escuro
// (0,40); com Vermelhos em 200 sai claro (2,00); com Vermelhos em -100
// sai preto. Um cinza puro NAO se mexe, seja qual for a faixa: e o que
// separa "tirar a cor" de "estragar a imagem".
//
// COMO AS FAIXAS SE DIVIDEM. Cada uma tem um centro de matiz (0, 60, 120,
// 180, 240 e 300 graus) e perde forca linearmente ate 60 graus de
// distancia. As seis somam exatamente 1 em qualquer matiz, entao cada
// pixel pertence a no maximo duas faixas vizinhas — e arrastar uma faixa
// desloca a fronteira com a vizinha, sem buraco no meio.
//
// A SATURACAO PESA QUANTO A FAIXA MANDA. O fator da faixa entra misturado
// pela saturacao do pixel: cor cheia obedece a faixa inteira, cinza
// obedece a ninguem, e um tom lavado fica no meio.
//
// Floats: tamanho[0..1] filtro[2] logico[3..4] escalaRef[5] tempo[6] modo[7]
// p0 = (vermelhos, amarelos, verdes, ciano)   [8..11]
// p1 = (azuis, magentas, mistura, tingir)     [12..15]
// c0 = cor do tingimento                      [72..75]

uniform vec2 uSize;
uniform float uFilter;
uniform vec2 uLogico;
uniform float uEscalaRef;
uniform float uTempo;
uniform float uModo;
// p2..p15 NAO SAO USADOS, E ESTAO AQUI DE PROPOSITO: o ABI do
// `MotorSapphire` poe as cores em 72..79, depois dos dezesseis p. Declarar
// so o que se usa deslocaria o c0 para 16 e a cor do tingimento chegaria
// como zero — sem erro nenhum na tela.
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
uniform sampler2D uImage;

out vec4 fragColor;

const vec3 LUMA = vec3(.2126, .7152, .0722);

vec4 texel(vec2 uv) {
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0 - uv.y;
  #endif
  return texture(uImage, clamp(uv, vec2(.5) / uSize, 1.0 - vec2(.5) / uSize));
}

vec3 rgbParaHsl(vec3 c) {
  float mx = max(c.r, max(c.g, c.b));
  float mn = min(c.r, min(c.g, c.b));
  float l = (mx + mn) * .5;
  float d = mx - mn;
  float h = 0.0;
  float s = 0.0;
  if (d > .00001) {
    s = l > .5 ? d / max(2.0 - mx - mn, .00001) : d / max(mx + mn, .00001);
    if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
    else h = (c.r - c.g) / d + 4.0;
    h /= 6.0;
  }
  return vec3(h, s, l);
}

// A faixa em [centro] graus, com queda linear ate 60 graus de distancia.
float pesoDaFaixa(float h, float centro) {
  float d = abs(h - centro);
  d = min(d, 360.0 - d);
  return max(0.0, 1.0 - d / 60.0);
}

void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  vec4 original = texel(uv);
  if (original.a <= .00001) {
    fragColor = vec4(0.0);
    return;
  }
  vec3 c = original.rgb / original.a;

  // O VALOR, e nao a luminancia: e o que faz um vermelho puro com a faixa
  // em 100 sair cinza medio, como no After Effects — e nao cinza escuro.
  float valor = max(c.r, max(c.g, c.b));
  float mn = min(c.r, min(c.g, c.b));

  // A SATURACAO DO PIXEL decide o quanto a faixa manda nele.
  float saturacao = valor <= .00001 ? 0.0 : (valor - mn) / valor;
  float matiz = rgbParaHsl(c).x * 360.0;

  float faixa =
      pesoDaFaixa(matiz, 0.0) * p0.x +
      pesoDaFaixa(matiz, 60.0) * p0.y +
      pesoDaFaixa(matiz, 120.0) * p0.z +
      pesoDaFaixa(matiz, 180.0) * p0.w +
      pesoDaFaixa(matiz, 240.0) * p1.x +
      pesoDaFaixa(matiz, 300.0) * p1.y;

  float fator = mix(1.0, faixa, clamp(saturacao, 0.0, 1.0));
  float cinza = clamp(valor * fator, 0.0, 1.0);

  vec3 resultado = vec3(cinza);
  // TINGIR: o cinza toma a cor escolhida. Multiplicar e o que mantem o
  // preto preto e o branco claro — somar lavaria a imagem inteira.
  float tingir = clamp(p1.w, 0.0, 1.0);
  if (tingir > .0001) {
    resultado = mix(resultado, resultado * clamp(c0.rgb, 0.0, 1.0), tingir);
  }

  float mistura = clamp(p1.z, 0.0, 1.0);
  resultado = mix(c, resultado, mistura);

  fragColor = vec4(clamp(resultado, 0.0, 1.0) * original.a, original.a);
}
