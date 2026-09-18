#include <flutter/runtime_effect.glsl>
precision highp float;
uniform vec2 uSize;
uniform vec2 uLado;
uniform vec2 uPasso;      // o vetor de um passo da marcha, em px logicos
uniform float uPassos;    // quantos passos a marcha da
uniform vec4 uCor;
uniform float uQueda;
uniform sampler2D uImage;
out vec4 fragColor;

void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  vec2 p = uv * uLado;

  // A SOMBRA E A UNIAO DE TODAS AS COPIAS da silhueta deslocadas de 0 ate
  // o comprimento. Em vez de desenhar as copias, a marcha pergunta por
  // pixel: andando na direcao do deslocamento, algum ponto ate o fim esta
  // dentro da silhueta? O `max` e a uniao.
  //
  // A MARCHA ANDA PARA TRAS: o pixel esta na sombra quando algum ponto
  // ENTRE ELE E A SILHUETA esta dentro da silhueta. `uPasso` aponta para
  // onde a sombra cai, entao quem caminha contra o passo e quem procura a
  // forma — e o pixel que a acha por perto e o que esta mais perto dela.
  //
  // O PESO DE CADA PASSO cai de 1 ate (1 - queda): e o que faz a sombra
  // desvanecer em vez de acabar de repente. Com queda 0 o peso e 1 em
  // todos, e a sombra e chapada.
  float sombra = 0.0;
  for (int i = 0; i < 96; i++) {
    if (float(i) >= uPassos) break;
    float t = float(i) + 0.5;
    vec2 q = (p - uPasso * t) / uLado;
    // FORA DA CAIXA NAO HA O QUE AMOSTRAR, e o sampler nao garante o que
    // devolve ali: quem amostra em uv negativo nao pode contar com um
    // zero. O teste de pixel pegou isso — sem a guarda, o outro lado da
    // textura voltava por conta do modo de repeticao e a sombra saia um
    // quadrado cheio, em vez do losango da uniao de copias.
    if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0) continue;
    float alfa = texture(uImage, q).a;
    if (alfa <= 0.0) continue;
    float peso = 1.0 - uQueda * (t / max(1.0, uPassos));
    sombra = max(sombra, alfa * max(0.0, peso));
    if (sombra >= 1.0 && uQueda <= 0.0) break;
  }

  fragColor = vec4(uCor.rgb, uCor.a * sombra);
}
