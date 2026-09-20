// O VERTICE DO PASSE DE SOMBRA.
//
// ======================= POR QUE ELE EXISTE SEPARADO ===================
// Nao da para reaproveitar o `pbr.vert` para o mapa de sombra. La o vertice
// carrega cor, uv, tangente e posicao de luz; aqui so a POSICAO importa, e
// um alvo de profundidade puro nao tem onde guardar o resto. O ganho nao e
// so de banda: sem os outros atributos o passe de sombra le um terco dos
// bytes, e num celular a banda e o que decide se uma cena de vinte modelos
// anda ou nao.
//
// ELE TAMBEM DEFORMA, e isso nao e opcional. Um personagem animado que
// projeta sombra na pose de repouso deixaria o rastro do corpo parado no
// chao enquanto anda — o defeito mais visivel que existe numa cena 3D.
// Por isso o `PELE` esta aqui identico ao do PBR, com o mesmo bloco de
// ossos e a mesma conta.
//
// O QUE NAO TEM AQUI: NEBIINA, e nao por esquecimento. O passe de sombra
// nao desenha nada alem de profundidade, entao nada que so mude a cor
// teria efeito — mante-lo daria a impressao de que a neblina esta ligada
// quando ela nao estaria.
#version 450

// O `#version` ANTES DO BLOCO DO `PELE`, e nao depois: o compilador exige
// que ele seja o primeiro token que nao seja comentario, e com o `#ifndef`
// na frente ele le o arquivo como GLSL 110 e recusa.
#ifndef PELE
#define PELE 0
#endif

// OS MESMOS NUMEROS DO `pbr.vert`, e pela mesma razao: o `InputIndex` do
// lado C++ vira `location` aqui. O `a_uv0` fica na posicao 2 mesmo sem uso
// porque o layout de vertices e um so — tirar ele daqui exigiria um segundo
// formato de vertice na GPU para o mesmo modelo, e o gasto de memoria nao
// se justifica por um atributo.
layout(location = 0) in vec3 a_posicao;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec2 a_uv1;
layout(location = 4) in vec4 a_tangente;
layout(location = 5) in vec4 a_cor;
layout(location = 6) in uvec4 a_ossos;
layout(location = 7) in vec4 a_pesos;

layout(binding = 0) uniform Quadro {
  mat4 vista;
  mat4 projecao;
  mat4 vista_projecao;
  mat4 luz_espaco;
  vec4 olho;
  vec4 ambiente;
  vec4 ajustes;
} u_quadro;

layout(binding = 2) uniform Desenho {
  mat4 mundo;
  mat4 normal;
  vec4 cor_base;
  vec4 parametros;
  vec4 emissivo;
  vec4 bandeiras;
  vec4 bandeiras2;
  vec4 tinta;
} u_desenho;

layout(std430, binding = 3) readonly buffer Ossos {
  mat4 ossos[];
};

void main() {
  vec4 posicao = vec4(a_posicao, 1.0);

#if PELE
  float soma = a_pesos.x + a_pesos.y + a_pesos.z + a_pesos.w;
  if (soma > 0.0001) {
    uint base = uint(u_desenho.bandeiras2.w);
    mat4 pele = mat4(0.0);
    pele += ossos[base + a_ossos.x] * a_pesos.x;
    pele += ossos[base + a_ossos.y] * a_pesos.y;
    pele += ossos[base + a_ossos.z] * a_pesos.z;
    pele += ossos[base + a_ossos.w] * a_pesos.w;
    posicao = (pele * posicao) / soma;
  }
#endif

  // A MATRIZ E A DO ESPACO DA LUZ, e nao a da camera: e o que faz o mapa
  // ser visto de onde a luz esta. E a mesma `luz_espaco` que o PBR usa para
  // projetar o fragmento, entao os dois numeros sao comparaveis por
  // construcao — projetar cada um com uma matriz diferente daria uma sombra
  // que nao bate com a superficie que a projeta.
  gl_Position = u_quadro.luz_espaco * u_desenho.mundo * posicao;
}
