#version 460 core
#include <flutter/runtime_effect.glsl>

// ESPACO LINEAR: a conta de luz precisa dele.
//
// O valor que um PNG guarda nao e "quanta luz tem" — e um numero
// corrigido para o olho, com a curva do sRGB por cima. Somar, borrar ou
// misturar esses numeros direto e somar as coisas erradas:
//
//   glow acinzentado e fraco, porque a soma de dois meios-tons da menos
//   luz do que deveria;
//   desfoque com halo escuro na borda entre claro e escuro;
//   dissolve escurecendo no meio do caminho;
//   gradiente escuro com faixa.
//
// A correcao e desfazer a curva ANTES da conta e refazer DEPOIS. E o que
// separa "parece o Alight Motion" de "nao parece nem um pouco" na
// familia inteira de luz e desfoque.
//
// uMode: 0 = sRGB -> linear (entrada)   1 = linear -> sRGB (saida)
//
// O alfa NAO leva curva: ele ja e linear por definicao. E as cores
// chegam PRE-MULTIPLICADAS, entao desfazem-se antes e refazem-se depois
// — aplicar a curva no valor pre-multiplicado escurece a borda de tudo,
// que e a franja classica.

uniform vec2 uSize;
uniform float uMode;
uniform sampler2D uTexture;

out vec4 fragColor;

float srgbParaLinear(float c) {
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float linearParaSrgb(float c) {
  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

void main() {
  // uSize e gravado pelo MOTOR com o tamanho da textura de entrada (e o
  // contrato do ImageFilter.shader: o primeiro vec2 e o tamanho). No
  // GLES anterior ao Flutter 3.47 precisa da correcao; no motor atual
  // os render targets ja tem a mesma orientacao de Metal e Vulkan.
  vec2 uv = FlutterFragCoord().xy / uSize;
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  uv.y = 1.0 - uv.y;
  #endif
  vec4 p = texture(uTexture, uv);

  float a = p.a;
  if (a <= 0.0) {
    fragColor = vec4(0.0);
    return;
  }
  vec3 c = clamp(p.rgb / a, 0.0, 1.0);

  vec3 o;
  if (uMode < 0.5) {
    o = vec3(srgbParaLinear(c.r), srgbParaLinear(c.g), srgbParaLinear(c.b));
  } else {
    o = vec3(linearParaSrgb(c.r), linearParaSrgb(c.g), linearParaSrgb(c.b));
  }

  fragColor = vec4(o * a, a);
}
