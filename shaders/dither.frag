#version 460 core
#include <flutter/runtime_effect.glsl>

// DITHERING DE SAIDA.
//
// O defeito que denuncia motion amador e a FAIXA em gradiente escuro:
// de #000 a #0A1E3C em tela cheia, 8 bits por canal nao tem degrau
// suficiente e o olho ve listras. A correcao e ruido — meio degrau de
// ruido antes de quantizar espalha o erro e a faixa some.
//
// O ruido e TRIANGULAR (soma de dois uniformes), que e o que a
// literatura de audio e imagem usa: distribui o erro sem deixar o
// granulado visivel que um uniforme deixaria.
//
// A conversao para linear e de volta existe porque o degrau de 8 bits
// mora no espaco de exibicao: e la que o ruido precisa ter meio degrau.

uniform vec2 uSize;
uniform float uStrength;   // 0..2, em degraus de 8 bits
uniform float uSeed;       // muda por quadro para nao "grudar" a textura
uniform sampler2D uTexture;

out vec4 fragColor;

float hash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}

void main() {
  vec2 pos = FlutterFragCoord().xy;
  // uSize is the input texture size. Flutter 3.47 stores render targets
  // top-down on GLES, Metal and Vulkan; Canvas snapshots are upright too.
  vec2 uv = pos / uSize;
  vec4 c = texture(uTexture, uv);

  // Dois uniformes independentes -> distribuicao triangular em [-1, 1].
  float a = hash(pos + uSeed);
  float b = hash(pos * 1.7 + uSeed + 19.19);
  float n = (a + b - 1.0);

  // Meio degrau de 8 bits, escalado pela forca.
  float amount = uStrength / 255.0;

  fragColor = vec4(c.rgb + n * amount, c.a);
}
