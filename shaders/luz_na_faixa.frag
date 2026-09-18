#include <flutter/runtime_effect.glsl>
precision highp float;
uniform vec2 uSize;
uniform vec2 uLado;
uniform vec2 uCentro;
uniform vec2 uNormal;
uniform vec4 uCor;
uniform float uLargura;
uniform float uIntensidade;
uniform float uRecepcao;
uniform sampler2D uImage;
out vec4 fragColor;

void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  vec4 base = texture(uImage, uv);

  // A FAIXA E UMA ONDA PLANA. O que decide a luz de um pixel e a distancia
  // dele ate a RETA que passa pelo centro na direcao da luz — e nao a
  // distancia ate o centro. E por isso que a faixa atravessa o quadro
  // inteiro em vez de virar uma mancha redonda no meio. A conta e em
  // pixels logicos da camada, senao um quadro nao quadrado entortaria a
  // faixa.
  vec2 p = uv * uLado;
  float dist = dot(p - uCentro, uNormal);

  // PERFIL QUADRATICO, medido no render do AE: `(1 - d/semi)^2`, com a
  // semi-extensao em 2 vezes a Largura. Ajustado sobre 22 mil pixels de
  // dois renders independentes, o erro medio ficou em 0,6 e 2,1 niveis
  // (num pico de 63), e o pico ajustado saiu 63,4 e 62,6 — que e
  // exatamente `255 * 25/100`. Reta, cosseno, gaussiana e smoothstep
  // erram de 2 a 7 vezes mais.
  float semi = max(1.0, uLargura * 2.0);
  float t = clamp(abs(dist) / semi, 0.0, 1.0);
  float faixa = (1.0 - t) * (1.0 - t);

  // A INTENSIDADE E ABSOLUTA, e nao um fator da cor de origem: no render
  // do AE o mesmo +63 aparece sobre um cinza 102 e sobre um cinza 146. A
  // luz SOMA um valor, do jeito que uma luz soma.
  vec3 luz = uCor.rgb * (faixa * uIntensidade);

  // RECEPCAO. 0 = somar (o padrao medido), 1 = tela. A diferenca aparece
  // nos realces: somar estoura em branco, tela preserva a cor de baixo.
  vec3 resultado = uRecepcao > 0.5
      ? 1.0 - (1.0 - base.rgb) * (1.0 - clamp(luz, 0.0, 1.0))
      : base.rgb + luz;

  fragColor = vec4(clamp(resultado, 0.0, 1.0), base.a);
}
