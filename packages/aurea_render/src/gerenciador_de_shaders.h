// O GERENCIADOR DE SHADERS — compila uma vez, usa um quadro inteiro.
//
// A COMPILACAO DE SHADER E A PIOR TRAVADA DE UM EDITOR. No Metal e no
// Vulkan ela custa dezenas de milissegundos, e acontece exatamente quando
// o efeito entra em cena pela primeira vez — no meio de um play, com o
// dedo ja na tela. O resultado e um quadro de meio segundo e a sensacao
// de que o app engasgou.
//
// A DEFESA TEM DUAS PARTES:
//   1. A CHAVE E O CONTEUDO. `nome` + `versao` + backend identificam um
//      programa. Mudou a fonte, muda a versao, e o cache invalida sozinho
//      — nao ha "esqueci de limpar o cache".
//   2. RE-AQUECIMENTO ANTES DE PRECISAR. `pre_aquecer` compila em lote,
//      fora do caminho do quadro: quando o projeto abre, quando o efeito e
//      adicionado, quando o app volta do segundo plano.
//
// RECOMPILAR DURANTE O PLAYBACK E CONTADO COMO DEFEITO. `recompilacoes`
// sobe quando um programa que JA estava no cache e compilado de novo. O
// numero tem de ser zero num play inteiro, e ha teste para isso — um
// cache que "funciona" mas perde a entrada a cada quadro nao aparece em
// nenhum placar de FPS, so no dedo.
#ifndef AUREA_RENDER_GERENCIADOR_DE_SHADERS_H
#define AUREA_RENDER_GERENCIADOR_DE_SHADERS_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base.h"
// `CargaDoRecurso` vem daqui: a carga de um shader e um recurso como
// qualquer outro, com dono e tempo de vida, e o gerenciador de recursos e
// quem manda soltar.
#include "gerenciador_de_recursos.h"

namespace aurea::render {

enum class Etapa : std::uint8_t {
  vertice = 0,
  fragmento = 1,
  computo = 2,
};

enum class Backend : std::uint8_t {
  referencia = 0,  // CPU: nao compila nada, mas passa pelo mesmo caminho
  metal = 1,
  vulkan = 2,
  gles = 3,
};

[[nodiscard]] constexpr std::string_view nome_do_backend(Backend b) noexcept {
  switch (b) {
    case Backend::referencia: return "referencia";
    case Backend::metal: return "metal";
    case Backend::vulkan: return "vulkan";
    case Backend::gles: return "gles";
  }
  return "?";
}

struct DescricaoDoShader {
  /// O NOME ESTAVEL, escrito por quem registra: "correcao_de_cor/leitura".
  /// E ele que aparece no relatorio de erro, e nao um numero.
  std::string nome;
  Etapa etapa = Etapa::fragmento;
  std::string fonte;

  /// A VERSAO DO CONTEUDO. Quem muda a fonte sobe a versao; quem esquece
  /// recebe um erro de chave duplicada com fonte diferente, e nao um
  /// programa silenciosamente velho.
  std::uint32_t versao = 1;
};

struct EstatisticasDeShaders {
  std::uint32_t compilados = 0;
  std::uint32_t reaproveitados = 0;
  std::uint32_t falhas = 0;
  std::uint32_t recompilacoes = 0;
  std::uint32_t vivos = 0;
};

/// O QUE O BACKEND DEVOLVE. O de referencia nao compila: devolve uma
/// carga vazia, e o caminho inteiro (chave, cache, falha, pre-aquecimento)
/// e exercitado do mesmo jeito — e por isso que ele serve de teste.
class CargaDeShader : public CargaDoRecurso {
 public:
  virtual ~CargaDeShader() = default;
  CargaDeShader() = default;
  CargaDeShader(const CargaDeShader&) = delete;
  CargaDeShader& operator=(const CargaDeShader&) = delete;
};

/// QUEM COMPILA. O backend de GPU implementa; o de referencia aceita tudo.
class CompiladorDeShaders {
 public:
  virtual ~CompiladorDeShaders() = default;
  CompiladorDeShaders() = default;
  CompiladorDeShaders(const CompiladorDeShaders&) = delete;
  CompiladorDeShaders& operator=(const CompiladorDeShaders&) = delete;

  /// Devolve a carga, ou `Erro::argumento` quando a fonte nao compila.
  [[nodiscard]] virtual Resulta<std::unique_ptr<CargaDeShader>> compilar(
      const DescricaoDoShader& d, Backend b) = 0;
};

struct Shader {
  std::string nome;
  std::uint32_t versao = 1;
  Etapa etapa = Etapa::fragmento;
  Backend backend = Backend::referencia;
  std::unique_ptr<CargaDeShader> carga;
};

class GerenciadorDeShaders {
 public:
  explicit GerenciadorDeShaders(CompiladorDeShaders& compilador,
                                Backend backend) noexcept
      : compilador_(compilador), backend_(backend) {}

  GerenciadorDeShaders(const GerenciadorDeShaders&) = delete;
  GerenciadorDeShaders& operator=(const GerenciadorDeShaders&) = delete;

  /// COMPILA OU DEVOLVE O QUE JA ESTA. Chamar com o mesmo nome, a mesma
  /// versao e a mesma fonte e barato: nao compila.
  [[nodiscard]] Resulta<std::uint32_t> obter(const DescricaoDoShader& d);

  /// COMPILA MUITOS DE UMA VEZ, antes de precisar. A entrada que ja esta
  /// no cache apenas conta como reaproveitada.
  Resulta<std::uint32_t> pre_aquecer(
      std::span<const DescricaoDoShader> descricoes);

  /// A FONTE MUDOU SEM SUBIR A VERSAO. Devolve erro em vez de servir o
  /// programa velho: um shader desatualizado e um efeito errado que
  /// ninguem consegue explicar olhando o codigo.
  [[nodiscard]] bool conferir_versao(std::string_view nome,
                                     std::uint32_t versao,
                                     std::string_view fonte) const noexcept;

  [[nodiscard]] const Shader* achar(std::uint32_t id) const noexcept;

  [[nodiscard]] EstatisticasDeShaders estatisticas() const noexcept;

  /// SOLTA TUDO. O `VkDevice` morreu: as cargas apontam para objetos que
  /// nao existem mais.
  void soltar_tudo() noexcept;

 private:
  [[nodiscard]] static std::string chave(Backend b,
                                         const DescricaoDoShader& d);

  struct Entrada {
    Shader shader;
    std::string fonte;
    std::uint32_t id = 0;
  };

  CompiladorDeShaders& compilador_;
  Backend backend_;
  std::unordered_map<std::string, Entrada> cache_;

  /// ID -> CHAVE. Existe porque o `unordered_map` re-hasheia: guardar o
  /// endereco da entrada como identificador daria um id que muda de
  /// lugar, e um id que muda de lugar nao serve para nada.
  std::unordered_map<std::uint32_t, std::string> ids_;
  std::uint32_t proximo_id_ = 1;
  EstatisticasDeShaders stats_{};
};

}  // namespace aurea::render

#endif  // AUREA_RENDER_GERENCIADOR_DE_SHADERS_H
