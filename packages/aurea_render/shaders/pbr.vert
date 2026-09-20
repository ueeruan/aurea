// O VERTICE DO PBR — E O MESMO PARA A MALHA COMUM E PARA A DEFORMADA.
//
// ============================ POR QUE UM ARQUIVO SO ====================
// O `PELE` NAO MUDA O SHADER, MUDA A CONTA DE UMA LINHA. Compilar dois
// fontes quase iguais garantiria que, um dia, alguem consertasse a
// iluminacao num deles e esquecesse o outro — e o defeito apareceria so em
// modelo animado, que e o caso menos testado.
//
// A POSTURA TAMBEM NAO TEM SHADER PROPRIO, e isso e decisao de arquitetura
// (§13): as matrizes de osso chegam prontas num bloco e o vertice e
// pesado aqui, na GPU. Transformar milhares de vertices por quadro na CPU
// e exatamente o que nao se faz num celular.
#version 450

// O `#version` VEM ANTES DE TUDO, e nao e estilo: o compilador exige que
// ele seja o primeiro token que nao seja comentario. Com o bloco do `PELE`
// na frente, o glslc le o arquivo como se fosse GLSL 110 e recusa.
#ifndef PELE
#define PELE 0
#endif

// OS ATRIBUTOS. Os numeros sao CONTRATO com o `LayoutElement` do lado C++:
// `InputIndex` vira `location` direto (o Diligent copia um no outro), entao
// trocar a ordem aqui sem trocar la desenha lixo e nao avisa.
layout(location = 0) in vec3 a_posicao;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec2 a_uv1;
layout(location = 4) in vec4 a_tangente;
layout(location = 5) in vec4 a_cor;
layout(location = 6) in uvec4 a_ossos;
layout(location = 7) in vec4 a_pesos;

layout(location = 0) out vec3 v_posicao_mundo;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv0;
layout(location = 3) out vec2 v_uv1;
layout(location = 4) out vec4 v_tangente;
layout(location = 5) out vec4 v_cor;
layout(location = 6) out vec4 v_posicao_luz;

// ------------------------------------------------------------- os blocos
layout(binding = 0) uniform Quadro {
  mat4 vista;
  mat4 projecao;
  mat4 vista_projecao;
  mat4 luz_espaco;
  vec4 olho;        // xyz = olho, w = sombra ligada (0/1)
  vec4 ambiente;    // rgb = ambiente linear
  vec4 ajustes;     // x = quantas luzes, y = PCF, z = tamanho do mapa, w = inclinacao
} u_quadro;

// O BLOCO E O MESMO NO VERTICE E NO FRAGMENTO, CAMPO A CAMPO. O std140
// alinha por 16 bytes e nao guarda nome nenhum: um campo a mais de um lado
// so desloca tudo o que vem depois, e o resultado e a cor base chegando
// como normal, sem erro de compilacao para avisar.
layout(binding = 2) uniform Desenho {
  mat4 mundo;
  mat4 normal;
  vec4 cor_base;      // rgb linear, a = alfa final (ja com a opacidade)
  vec4 parametros;    // x metalico, y rugosidade, z forca emissiva, w alfa corte
  vec4 emissivo;      // rgb linear, w = modo (0 opaco, 1 mascarado, 2 transparente)
  vec4 bandeiras;     // x cor, y normal, z metalico-rugosidade, w oclusao
  vec4 bandeiras2;    // x emissiva, y face dupla, z reservado (1.0), w base do osso
  vec4 tinta;         // rgb multiplicativo, a = 1 quando neutro
} u_desenho;

// AS MATRIZES DE OSSO. E um buffer de ARMAZENAMENTO e nao um de
// uniformes por um motivo pratico: um esqueleto de 300 ossos passaria do
// tamanho minimo garantido de um bloco de uniformes (16 KB na Vulkan), e o
// sintoma seria o modelo sumir nos aparelhos mais antigos — sem erro, sem
// aviso, so uma parte da cena que nao aparece.
layout(std430, binding = 3) readonly buffer Ossos {
  mat4 ossos[];
};

void main() {
  vec4 posicao = vec4(a_posicao, 1.0);
  vec3 normal = a_normal;
  vec4 tangente = a_tangente;

#if PELE
  // O PESO E O QUE DECIDE. Um vertice com a soma dos pesos zero (arquivo
  // mal formado, ou vertice fora de qualquer osso) ficaria na origem e
  // puxaria um triangulo ate o centro do modelo — entao ele fica onde o
  // arquivo o pos.
  float soma = a_pesos.x + a_pesos.y + a_pesos.z + a_pesos.w;
  if (soma > 0.0001) {
    // O DESLOCAMENTO NA FATIA DESTA CAMADA. Duas camadas do mesmo modelo em
    // instantes diferentes tem blocos distintos, e `base` e o que separa uma
    // da outra. Sem ele, a segunda pose sobrescreveria a primeira e as duas
    // camadas apareceriam na mesma posicao.
    uint base = uint(u_desenho.bandeiras2.w);
    mat4 pele = mat4(0.0);
    pele += ossos[base + a_ossos.x] * a_pesos.x;
    pele += ossos[base + a_ossos.y] * a_pesos.y;
    pele += ossos[base + a_ossos.z] * a_pesos.z;
    pele += ossos[base + a_ossos.w] * a_pesos.w;
    // A SOMA DOS PESOS E DIVIDIDA, e nao confiada: o `LimitBoneWeights` do
    // importador normaliza, mas um arquivo pode trazer pesos que somam
    // 0,98 e a diferenca de 2% encolhe o modelo inteiro de leve — o tipo de
    // defeito que ninguem acha e todo mundo ve.
    posicao = (pele * posicao) / soma;
    normal = mat3(pele) * normal;
    tangente = vec4(mat3(pele) * tangente.xyz, tangente.w);
  }
#endif

  // A COR DO VERTICE VEM EM sRGB, e e para linear aqui e nao no shader de
  // fragmento: a normal e a luz se encontram em linear, e uma cor lida em
  // sRGB tingiria a superficie de escuro sem que nada estivesse errado.
  v_cor = vec4(pow(a_cor.rgb, vec3(2.2)), a_cor.a);

  vec4 mundo = u_desenho.mundo * posicao;
  v_posicao_mundo = mundo.xyz;
  v_normal = normalize((u_desenho.normal * vec4(normal, 0.0)).xyz);
  v_tangente = vec4(normalize((u_desenho.normal * vec4(tangente.xyz, 0.0)).xyz),
                    tangente.w);
  v_uv0 = a_uv0;
  v_uv1 = a_uv1;
  v_posicao_luz = u_quadro.luz_espaco * mundo;

  gl_Position = u_quadro.vista_projecao * mundo;
}
