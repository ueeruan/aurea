// O IMPORTADOR: DO ARQUIVO PARA O `Modelo` DO AUREA.
//
// O ASSIMP ENTRA E SAI DENTRO DE UMA CHAMADA. Nada do que ele aloca
// sobrevive ao retorno: o que sai daqui e o `Modelo`, que nao inclui um
// cabecalho do Assimp sequer. E o que o plano pede (§4) e tambem o que a
// memoria pede — a arvore do Assimp de um modelo de 200 MB e maior do que
// o modelo convertido.
//
// A POSE TAMBEM E AMOSTRADA AQUI, e nao no renderizador. Amostrar uma
// animacao e andar na arvore de nos do modelo, e a arvore e do modelo.
#ifndef AUREA_RENDER_IMPORTADOR_H
#define AUREA_RENDER_IMPORTADOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "base.h"
#include "modelo.h"
#include "vetor.h"

namespace aurea::render::tresd {

/// O QUE O CHAMADOR QUER DA IMPORTACAO.
struct OpcoesDeImportacao {
  /// Trava de seguranca. Um arquivo maior do que isto nem e aberto: um
  /// modelo de dois gigabytes derruba o processo antes de qualquer
  /// mensagem de erro aparecer (§21), e "nao abriu" e melhor do que
  /// "fechou o editor".
  std::uint64_t teto_de_bytes_do_arquivo = 512ULL * 1024ULL * 1024ULL;
  /// Nao importa animacao nem esqueleto. Vale para o modelo que entra so
  /// como cenario, onde o esqueleto custa memoria e nao faz nada.
  bool sem_animacao = false;
  /// Escala aplicada a tudo. O glTF e o FBX as vezes vem em metros e o
  /// quadro do Aurea e em pixels; quem chama decide o fator.
  float escala = 1.0F;
  /// Grava uma textura neutra cinza no lugar da que faltou (§43), em vez
  /// de deixar o material sem textura. O azul de depuracao nao entra: ele
  /// aparece em producao.
  bool textura_neutra_quando_faltar = true;
};

/// O QUE A IMPORTACAO PRODUZIU, para a ficha da camada e para os testes.
struct RelatoDaImportacao {
  std::uint64_t bytes_do_arquivo = 0;
  std::uint32_t malhas = 0;
  std::uint32_t materiais = 0;
  std::uint32_t texturas = 0;
  std::uint32_t nos = 0;
  std::uint32_t ossos = 0;
  std::uint32_t animacoes = 0;
  std::uint32_t triangulos = 0;
  std::uint32_t vertices = 0;
  std::uint64_t bytes_em_memoria = 0;
  std::uint32_t avisos = 0;
  std::string primeiro_aviso;
};

/// LE O ARQUIVO. `caminho` e um caminho de sistema de arquivos de verdade:
/// o app copia o modelo para a pasta dele antes de chamar.
[[nodiscard]] Resulta<Modelo> importar(const std::string& caminho,
                                       const OpcoesDeImportacao& opcoes,
                                       RelatoDaImportacao* relato = nullptr);

/// A MESMA COISA, COM OS BYTES JA NA MAO. Usada pelo teste, que nao quer
/// escrever em disco, e pelo caminho que recebe o arquivo por outro meio.
[[nodiscard]] Resulta<Modelo> importar_memoria(const std::uint8_t* bytes,
                                               std::size_t tamanho,
                                               const std::string& extensao,
                                               const OpcoesDeImportacao& opcoes,
                                               RelatoDaImportacao* relato = nullptr);

// ------------------------------------------------------------------ pose

/// A POSE AMOSTRADA: uma matriz por no e, quando ha esqueleto, uma por
/// osso ja multiplicada pela ligacao inversa — que e o que o shader le.
struct Pose {
  std::vector<Mat4> nos;
  std::vector<Mat4> ossos;
  std::vector<Vec3> posicoes;
  std::vector<Quat> rotacoes;
  std::vector<Vec3> escalas;
  bool vazia = true;
};

/// A POSE DE REPOUSO. E o que se desenha quando nenhuma animacao esta
/// escolhida, e o que impede o modelo de aparecer dobrado no primeiro
/// quadro antes de a primeira amostra chegar.
void pose_de_repouso(const Modelo& modelo, Pose& saida);

/// A POSE NO INSTANTE `tempo` (segundos do clipe). `tempo` fora do
/// intervalo do clipe e PRENDIDO nas pontas, e nao repetido: quem decide
/// se a animacao cicla e a timeline, nao o amostrador.
void amostrar_pose(const Modelo& modelo, std::int32_t animacao, double tempo,
                   Pose& saida);

/// A DURACAO DO CLIPE, em segundos. Zero quando o indice nao existe.
[[nodiscard]] double duracao_da_animacao(const Modelo& modelo,
                                         std::int32_t animacao) noexcept;

}  // namespace aurea::render::tresd

#endif  // AUREA_RENDER_IMPORTADOR_H
