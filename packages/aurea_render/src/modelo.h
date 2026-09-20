// O MODELO COMO O AUREA O GUARDA: UM FORMATO PROPRIO, SEM ASSIMP DENTRO.
//
// POR QUE NAO GUARDAR O `aiScene` E PRONTO. O Assimp e um IMPORTADOR, e
// nao um runtime: o `aiScene` mantem viva a arvore inteira do arquivo, com
// os nos de formato que ninguem mais le, e o custo disso e memoria presa
// pelo tempo todo em que o projeto fica aberto. Aqui o Assimp entra,
// converte, e SAI — o `Modelo` abaixo nao inclui um cabecalho do Assimp
// sequer, e quem o consome (o renderizador) nao sabe que formato o arquivo
// tinha.
//
// OS MESMOS CAMPOS VALEM PARA GLB, GLTF, FBX E OBJ, porque o que esta aqui
// e o que a GPU e o painel precisam — e nao o que cada formato oferece.
#ifndef AUREA_RENDER_MODELO_H
#define AUREA_RENDER_MODELO_H

#include <cstdint>
#include <string>
#include <vector>

#include "base.h"
#include "vetor.h"

namespace aurea::render::tresd {

using geo::Mat4;
using geo::Quat;
using geo::Vec2;
using geo::Vec3;
using geo::Vec4;

/// O VERTICE, NA ORDEM QUE O SHADER LE.
///
/// `uv1` existe porque o glTF pode trazer dois jogos de coordenadas de
/// textura (o segundo costuma carregar a oclusao ambiente quando o material
/// foi exportado de um pipeline que a separa). Sem ele, o segundo jogo era
/// jogado fora na importacao — e nao havia como saber que faltava.
///
/// A TANGENTE VAI COM SINAL NO `w`: a bitangente nao e guardada porque ela
/// e o produto vetorial das outras duas vezes esse sinal, e o sinal e o que
/// o mapa de normais precisa para nao sair espelhado.
struct Vertice {
  Vec3 posicao;
  Vec3 normal;
  Vec2 uv0;
  Vec2 uv1;
  Vec4 tangente{0.0F, 0.0F, 0.0F, 1.0F};
  /// RGBA8 na ordem de bytes, ja multiplicado pelo alfa do material quando
  /// o arquivo traz cor por vertice (FBX e OBJ trazem).
  std::uint32_t cor = 0xFFFFFFFFU;
  /// Quatro ossos por vertice e seus pesos. Zero quando a malha e estatica.
  std::uint16_t ossos[4] = {0, 0, 0, 0};
  float pesos[4] = {0.0F, 0.0F, 0.0F, 0.0F};
};

/// UMA FAIXA DE INDICES COM UM MATERIAL SO.
///
/// NAO SE PODE ASSUMIR UMA MALHA POR MODELO NEM UM MATERIAL POR MALHA: um
/// GLB comum tem varias primitivas por malha, cada uma com o seu material,
/// e as vezes varias malhas. Uma faixa e exatamente o que a GPU desenha num
/// `DrawIndexed` — o material, o primeiro indice e quantos.
struct Faixa {
  std::uint32_t primeiro_indice = 0;
  std::uint32_t quantidade = 0;
  std::uint32_t base_do_vertice = 0;
  std::int32_t material = -1;
  /// O material pede alfa? Resolvido na importacao para o renderizador nao
  /// ter de consultar o material duas vezes por faixa em cada quadro.
  std::uint8_t transparente = 0;
  std::uint8_t mascarado = 0;
  std::uint8_t face_dupla = 0;
};

struct Malha {
  std::string nome;
  std::vector<Vertice> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<Faixa> faixas;
  /// Uma caixa por malha: e o que responde ao teste de corte e ao toque
  /// antes de olhar triangulo por triangulo.
  geo::Caixa limites;
  /// Os nos onde esta malha esta pendurada (uma malha instanciada em
  /// varios lugares do arquivo aparece em todos eles). Uma malha em varios
  /// nos e o caso de instancia do plano (§19): uma copia na memoria, uma
  /// transformacao por no.
  std::vector<std::int32_t> nos;

  /// A MALHA E DEFORMADA POR ESQUELETO? Quando e, os vertices ja foram
  /// trazidos para o ESPACO DA LIGACAO na importacao (ver o comentario
  /// longo no `importador.cpp`), e o no onde ela pendura nao entra mais na
  /// conta — a deformacao ja responde por tudo.
  bool esqueletica = false;
};

/// A IMAGEM JA DECODIFICADA, EM RGBA8.
///
/// DECODIFICAR NA IMPORTACAO, E NAO NA GPU, e uma escolha: o glTF embute
/// PNG e JPEG dentro do proprio arquivo, e aceitar isso como formato de
/// textura obrigaria a carregar um decodificador de PNG e outro de JPEG no
/// caminho de upload. Decodificado, o dado e o mesmo que qualquer outra
/// textura do app e o upload e uma copia.
struct Textura {
  std::string nome;
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
  /// Vem do arquivo ja em espaco linear? Mapa de cor e emissivo vem em
  /// sRGB e PRECISAM ser convertidos na amostragem; normal, rugosidade e
  /// oclusao ja sao lineares e nao podem ser convertidos de novo (§6).
  bool srgb = false;
  /// Tem canal alfa de verdade? Uma textura sem alfa e opaca mesmo quando
  /// o arquivo diz que o formato tem quatro canais, e tratar assim evita
  /// uma passada de transparencia inteira por engano.
  bool tem_alfa = false;
  std::vector<std::uint8_t> pixels;
};

/// O MATERIAL NO MODELO METALICO-RUGOSIDADE, que e o do glTF e o que os
/// outros formatos foram traduzidos para.
struct Material {
  std::string nome;
  Cor cor_base{255, 255, 255, 255};
  float metalico = 1.0F;
  float rugosidade = 1.0F;
  Cor emissivo{0, 0, 0, 255};
  float forca_emissiva = 1.0F;
  int32_t textura_cor = -1;
  int32_t textura_normal = -1;
  int32_t textura_metalico_rugosidade = -1;
  int32_t textura_emissiva = -1;
  int32_t textura_oclusao = -1;
  /// Oclusao ambiente: quanto da luz do ambiente o material aceita.
  float forca_da_oclusao = 1.0F;

  /// COMO O MATERIAL SE COMPORTA NA PASSADA. Os tres modos do plano (§16) e
  /// a mesma distincao que o glTF faz entre OPAQUE, MASK e BLEND.
  enum class Modo : std::uint8_t { opaco = 0, mascarado = 1, transparente = 2 };
  Modo modo = Modo::opaco;
  /// O corte do modo mascarado, em [0, 1]. O padrao do glTF e 0,5.
  float alfa_corte = 0.5F;
  bool face_dupla = false;
  /// Indice de refracao para a reflexao de Fresnel na superficie.
  float indice_de_refracao = 1.5F;
};

/// UM NO DA ARVORE DO ARQUIVO. Guardado porque a animacao de no (§12) e
/// feita em cima dele, e porque a hierarquia e o que faz a maquina girar
/// junto com o corpo em vez de girar no lugar.
struct No {
  std::string nome;
  std::int32_t pai = -1;
  Mat4 local;
  bool tem_matriz = false;
  Vec3 posicao{0.0F, 0.0F, 0.0F};
  Quat rotacao = Quat::identidade();
  Vec3 escala{1.0F, 1.0F, 1.0F};
};

// ------------------------------------------------------------- animacao

/// A CHAVE. Tempo em SEGUNDOS do clipe, e nao do projeto: quem converte
/// para o tempo da timeline e quem chama, porque a mesma animacao pode
/// tocar em duas velocidades diferentes em duas camadas.
struct ChaveDeVetor {
  double tempo = 0.0;
  Vec3 valor;
};

struct ChaveDeQuat {
  double tempo = 0.0;
  Quat valor;
};

/// UMA TRILHA DE NO: o que muda, e quando. Uma por propriedade que o
/// arquivo anima — as que nao aparecem ficam constantes e nao custam nada.
struct TrilhaDeVetor {
  std::int32_t alvo = -1;
  std::vector<ChaveDeVetor> chaves;
};

struct TrilhaDeQuat {
  std::int32_t alvo = -1;
  std::vector<ChaveDeQuat> chaves;
};

struct Animacao {
  std::string nome;
  double duracao = 0.0;
  std::vector<TrilhaDeVetor> posicoes;
  std::vector<TrilhaDeVetor> escalas;
  std::vector<TrilhaDeQuat> rotacoes;
  /// Pesos de forma (morph), por alvo de forma. Vazio no caso comum.
  std::vector<TrilhaDeVetor> pesos_de_forma;
  /// Anima os OSSOS, e nao os nos. As duas listas sao separadas porque um
  /// modelo esqueletico traz as duas coisas com o mesmo nome.
  bool esqueletica = false;
};

/// O OSSO, COM A MATRIZ QUE O LEVA DO ESPACO DO MODELO PARA O ESPACO DELE.
///
/// A `ligacao_inversa` vem pronta do arquivo e e o que permite desenhar a
/// malha no lugar certo: sem ela, o modelo aparece dobrado sobre a origem
/// no primeiro quadro — e o defeito nao parece um erro de conta, parece o
/// modelo rasgado.
struct Osso {
  std::string nome;
  std::int32_t pai = -1;
  /// O NO DO ARQUIVO QUE E ESTE OSSO. A animacao anda pela arvore de nos —
  /// e nao ha uma segunda arvore so para os ossos. Os indices de osso que
  /// os vertices carregam sao a POSICAO deste osso na lista do modelo.
  std::int32_t no = -1;
  Mat4 ligacao_inversa;
  Vec3 posicao{0.0F, 0.0F, 0.0F};
  Quat rotacao = Quat::identidade();
  Vec3 escala{1.0F, 1.0F, 1.0F};
};

/// O MODELO INTEIRO. E o que a camada 3D aponta, e o que o cache guarda.
struct Modelo {
  std::vector<Malha> malhas;
  std::vector<Material> materiais;
  std::vector<Textura> texturas;
  std::vector<No> nos;
  std::vector<Osso> ossos;
  std::vector<Animacao> animacoes;
  /// Os nos do esqueleto, na ordem em que o arquivo os declara. Uma malha
  /// sem esqueleto tem a lista vazia e e desenhada direto.
  std::vector<std::int32_t> ordem_dos_ossos;
  /// Quais nos do arquivo sao ossos. O resto e transformacao pura.
  std::vector<std::uint8_t> no_e_osso;

  geo::Caixa limites;

  // ------------------------------------------------------- contabilidade
  // Os numeros que o orcamento de memoria do plano (§21) consulta. Nao ha
  // estimativa: e o tamanho real dos vetores que estao aqui.
  std::uint64_t bytes_de_malha = 0;
  std::uint64_t bytes_de_textura = 0;
  std::uint32_t triangulos = 0;
  std::uint32_t vertices = 0;

  [[nodiscard]] bool tem_esqueleto() const noexcept {
    return !ossos.empty() && !ordem_dos_ossos.empty();
  }

  [[nodiscard]] std::uint64_t bytes() const noexcept {
    return bytes_de_malha + bytes_de_textura;
  }
};

}  // namespace aurea::render::tresd

#endif  // AUREA_RENDER_MODELO_H
