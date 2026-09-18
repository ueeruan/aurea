#include <flutter/runtime_effect.glsl>
precision highp float;
uniform vec2 uSize;
uniform vec2 uLado;      // lado da camada em pixels logicos
uniform vec2 uCentro;    // ponto da dobra, em pixels logicos
uniform vec2 uNormal;    // normal do vinco (aponta para o lado que enrola)
uniform vec4 uCor;       // cor do papel (o verso)
uniform float uRaio;
uniform float uLuz;      // direcao da luz, em graus
uniform float uVerso;    // 0..1: quanto do verso aparece
uniform float uBrilho;   // forca do reflexo especular
uniform sampler2D uImage;
out vec4 fragColor;

/// LEITURA COM INTERPOLACAO A MAO.
///
/// O `ImageFilter.shader` entrega a entrada em NEAREST — o filtro de
/// shader do Impeller nao interpola. Sem esta conta, a dobra sairia com
/// degraus de texel, e degrau em superficie curva aparece de longe.
vec4 ler(vec2 p) {
  vec2 texel = uSize;
  vec2 c = p - 0.5;
  vec2 base = floor(c);
  vec2 f = c - base;
  vec2 o = 1.0 / texel;
  vec4 a = texture(uImage, (base + 0.5) * o);
  vec4 b = texture(uImage, (base + vec2(1.0, 0.0) + 0.5) * o);
  vec4 d = texture(uImage, (base + vec2(0.0, 1.0) + 0.5) * o);
  vec4 e = texture(uImage, (base + vec2(1.0, 1.0) + 0.5) * o);
  return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}

void main() {
  // TRABALHAMOS EM PIXELS LOGICOS DA CAMADA, e nao em uv: a dobra tem
  // raio em pixels, e um quadro nao quadrado distorceria um circulo se a
  // conta fosse normalizada.
  vec2 p = FlutterFragCoord().xy / uSize * uLado;
  float d = dot(p - uCentro, uNormal);

  // LADO PLANO: nada muda. A dobra so age de um lado da reta.
  if (d <= 0.0 || uRaio <= 0.5) {
    fragColor = ler(p);
    return;
  }

  // A PAGINA ENROLA NUM CILINDRO DE RAIO `uRaio`. A folha anda o
  // comprimento de arco `s` sobre o cilindro; um ponto que estava a `s` da
  // dobra aparece na tela a `raio*sin(s/raio)`. O mundo tem tres eixos: o
  // eixo do vinco, a normal `n` no plano, e `z` saindo da tela.
  if (d >= uRaio) {
    // FORA DO ROLO: a pagina ja passou por cima e nao cobre mais nada.
    fragColor = vec4(0.0);
    return;
  }
  float phi = asin(clamp(d / uRaio, 0.0, 1.0));
  float s = uRaio * phi;
  vec2 fonte = p + uNormal * (s - d);

  // O VERSO: passando de 90 graus, quem olha ve o outro lado da folha. E
  // ele que da o miolo claro da dobra, e e o que o "Paper Color" pinta.
  float virada = smoothstep(1.1, 2.4, phi);
  vec4 frente = ler(fonte);
  vec3 papel = mix(frente.rgb, uCor.rgb, virada * uVerso);
  float alfa = mix(frente.a, max(frente.a, uCor.a * uVerso), virada);

  // LUZ. A normal do cilindro em `phi` e `(-sin, cos)` no par
  // (normal no plano, z). A luz vem da direcao pedida, inclinada para a
  // frente — e a inclinacao que da o reflexo correndo pela dobra.
  float lr = radians(uLuz);
  vec3 L = normalize(vec3(cos(lr), sin(lr), 0.75));
  vec3 N = normalize(vec3(-sin(phi) * uNormal, cos(phi)));
  float difusa = max(0.0, dot(N, L));
  vec3 V = vec3(0.0, 0.0, 1.0);
  vec3 H = normalize(L + V);
  float especular = pow(max(0.0, dot(N, H)), 48.0) * uBrilho;

  // A FACE PLANA tem normal (0,0,1): com a mesma luz, ela clareia igual —
  // o que mantem a dobra coerente com o resto da camada em vez de
  // parecer colada por cima.
  vec3 cor = papel * (0.55 + 0.45 * difusa) + especular;
  fragColor = vec4(clamp(cor, 0.0, 1.0), alfa);
}
