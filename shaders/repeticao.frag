#include <flutter/runtime_effect.glsl>
precision highp float;

// REPETICAO: a camada aparece varias vezes num passe so.
//
// Uma copia por widget custaria uma arvore inteira por copia e nao
// funcionaria com video ao vivo. Aqui o alvo de render e ampliado e
// cada pixel PROCURA de qual copia ele veio: e um laco de amostragem,
// e nao um laco de camadas.
uniform vec2 uSize;      // alvo de render, em pixels
uniform vec2 uOutput;    // quantas vezes o alvo e maior que a camada
uniform float uModo;     // 0 linha · 1 grade · 2 circulo · 3 espalhar
uniform float uCopias;   // total de copias (a primeira e a original)
uniform vec2 uPasso;     // deslocamento por copia, em fracao da camada
uniform float uGiro;     // graus por copia
uniform float uEscala;   // escala por copia
uniform float uAlfa;     // opacidade por copia
uniform float uRaio;     // circulo e espalhar, em fracao da camada
uniform float uAbertura; // graus que o circulo ocupa
uniform float uOrientacao;
uniform float uSemente;
uniform vec2 uGrade;     // colunas, linhas
uniform float uAspecto;  // largura/altura da camada
uniform float uFilter;
uniform sampler2D uImage;
out vec4 fragColor;

vec2 paraQuadrado(vec2 v) { return vec2(v.x * uAspecto, v.y); }
vec2 deQuadrado(vec2 v) { return vec2(v.x / uAspecto, v.y); }
vec2 girar(vec2 v, float a) {
  float s = sin(a), c = cos(a);
  return mat2(c, s, -s, c) * v;
}

float sorteio(float i, float k) {
  return fract(sin(i * 127.1 + k * 311.7 + uSemente * 74.7) * 43758.5453);
}

vec4 amostra(vec2 q) {
  vec2 uv = q + 0.5;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return vec4(0.0);
  // O conteudo mora no meio do alvo ampliado.
  vec2 alvo = 0.5 + (uv - 0.5) / uOutput;
  vec2 meio = 0.5 / uSize;
  alvo = clamp(alvo, 0.5 - 0.5 / uOutput + meio, 0.5 + 0.5 / uOutput - meio);
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > 0.5) alvo.y = 1.0 - alvo.y;
  #endif
  return texture(uImage, alvo);
}

void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  vec2 p = (uv - 0.5) * uOutput;
  int n = int(clamp(uCopias, 1.0, 64.0) + 0.5);
  int modo = int(uModo + 0.5);
  float colunas = max(1.0, floor(uGrade.x + 0.5));
  vec4 acc = vec4(0.0);

  // De tras para a frente: a copia 0 (a original) termina por cima.
  for (int k = 63; k >= 0; k--) {
    if (k >= n) continue;
    float i = float(k);
    vec2 t = vec2(0.0);
    float giro = radians(uGiro) * i;
    float escala = pow(max(0.05, uEscala), i);
    float alfa = pow(clamp(uAlfa, 0.0, 1.0), i);

    if (modo == 0) {
      t = uPasso * i;
    } else if (modo == 1) {
      float col = mod(i, colunas);
      float lin = floor(i / colunas);
      float linhas = max(1.0, ceil(float(n) / colunas));
      t = vec2(
        (col - (colunas - 1.0) * 0.5) * uPasso.x,
        (lin - (linhas - 1.0) * 0.5) * uPasso.y
      );
      // Numa grade, a contagem manda na posicao, e nao no encolhimento.
      escala = pow(max(0.05, uEscala), min(col, lin));
    } else if (modo == 2) {
      float passo = float(n) <= 1.0 ? 0.0 : uAbertura / float(n);
      float ang = radians(uOrientacao + passo * i);
      t = deQuadrado(vec2(cos(ang), sin(ang)) * uRaio);
      giro += ang + radians(90.0);
    } else {
      float ang = sorteio(i, 1.0) * 6.2831853;
      float r = sqrt(sorteio(i, 2.0)) * uRaio;
      t = deQuadrado(vec2(cos(ang), sin(ang)) * r);
      giro = radians(uGiro) * (sorteio(i, 3.0) * 2.0 - 1.0);
      escala = mix(1.0, max(0.05, uEscala), sorteio(i, 4.0));
    }
    if (k == 0 && modo != 1) {
      // A ORIGINAL nao gira nem encolhe: sem isso, "sem repeticao"
      // ainda mexeria na camada.
      giro = 0.0;
      escala = 1.0;
      alfa = 1.0;
      if (modo != 2) t = vec2(0.0);
    }
    vec2 q = deQuadrado(girar(paraQuadrado(p - t), -giro)) / escala;
    vec4 c = amostra(q) * alfa;
    acc = c + acc * (1.0 - c.a);
  }
  fragColor = acc;
}
