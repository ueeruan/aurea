// O GERENCIADOR DE RECURSOS DE GPU — quem tem, quem pode sair, quanto cabe.
//
// TODO RECURSO TEM DONO, VIDA E TETO. Textura, buffer e alvo de render
// nascem por um pedido explicito, sao entregues por uma ALCA com posse e
// voltam sozinhos quando a alca morre — nao existe caminho em que alguem
// esqueca de soltar. Quem nao esta em uso pode ser despejado para caber
// no orcamento, e quem esta em uso nunca e: despejar um alvo que o
// compositor esta lendo no meio do quadro seria uma tela rasgada, e nao
// um ganho de memoria.
//
// NAO HA CACHE INFINITO. O teto e o orcamento, e o orcamento e uma
// opiniao do chamador (o nucleo calcula pela memoria do aparelho). Quando
// o pedido nao cabe nem com a casa vazia, ele falha com
// `Erro::orcamento_estourado` — o que e uma resposta, e nao um vazamento.
//
// DONO: a thread do render. As alcas podem ser seguradas por outra
// thread, mas a contagem usa `shared_ptr` e a decisao de despejo so
// acontece na thread do render.
#ifndef AUREA_RENDER_GERENCIADOR_DE_RECURSOS_H
#define AUREA_RENDER_GERENCIADOR_DE_RECURSOS_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base.h"

namespace aurea::render {

using IdDeRecurso = std::uint32_t;
inline constexpr IdDeRecurso kRecursoInvalido = 0;

enum class TipoDeRecurso : std::uint8_t {
  textura = 0,
  buffer = 1,
  alvo = 2,  // superficie de renderizacao
};

struct DescricaoDoRecurso {
  TipoDeRecurso tipo = TipoDeRecurso::textura;
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;

  /// O ID DO LADO DE FORA — o quadro do decodificador, o bitmap que veio
  /// do Dart. Zero quando o recurso nasce aqui dentro (um alvo).
  std::uint64_t origem = 0;

  /// QUANTOS BYTES ESTE RECURSO CUSTA. Calculado por quem pede, porque so
  /// ele sabe o formato: 4 bytes por pixel para RGBA, 1,5 para NV12, e o
  /// que for para um buffer cru.
  std::uint64_t bytes = 0;
};

/// O QUE O RECURSO GUARDA ALEM DA DESCRICAO.
///
/// A memoria de verdade (o `MTLTexture`, o `VkImage`, o buffer do
/// rasterizador) mora aqui, e o backend a preenche. O gerenciador nao
/// sabe o que e — so sabe quanto pesa e quando pode sair.
class CargaDoRecurso {
 public:
  virtual ~CargaDoRecurso() = default;
  CargaDoRecurso(const CargaDoRecurso&) = delete;
  CargaDoRecurso& operator=(const CargaDoRecurso&) = delete;
  CargaDoRecurso() = default;
};

struct Recurso {
  IdDeRecurso id = kRecursoInvalido;
  DescricaoDoRecurso descricao;
  std::unique_ptr<CargaDoRecurso> carga;

  /// QUANTOS QUADROS ATRAS FOI USADO PELA ULTIMA VEZ. E a idade do LRU:
  /// quem nao e tocado ha mais tempo sai primeiro.
  std::uint64_t ultimo_uso = 0;

  [[nodiscard]] std::uint64_t bytes() const noexcept { return descricao.bytes; }
};

/// A ALCA COM POSSE. Enquanto ela existir, o recurso nao pode ser
/// despejado. Morre sozinha no fim do escopo.
class AlcaDeRecurso {
 public:
  AlcaDeRecurso() = default;
  explicit AlcaDeRecurso(std::shared_ptr<Recurso> r) : recurso_(std::move(r)) {}

  [[nodiscard]] bool viva() const noexcept { return recurso_ != nullptr; }
  [[nodiscard]] IdDeRecurso id() const noexcept {
    return recurso_ ? recurso_->id : kRecursoInvalido;
  }
  [[nodiscard]] const DescricaoDoRecurso* descricao() const noexcept {
    return recurso_ ? &recurso_->descricao : nullptr;
  }

  /// VAZADA PARA O BACKEND. Nao transfere posse — a alca continua sendo
  /// quem manda soltar.
  [[nodiscard]] CargaDoRecurso* carga() const noexcept {
    return recurso_ ? recurso_->carga.get() : nullptr;
  }

  [[nodiscard]] const std::shared_ptr<Recurso>& ponteiro() const noexcept {
    return recurso_;
  }

 private:
  std::shared_ptr<Recurso> recurso_;
};

struct EstatisticasDeRecursos {
  std::uint64_t bytes_em_uso = 0;
  std::uint64_t bytes_orcamento = 0;
  std::uint64_t bytes_pico = 0;
  std::uint32_t vivos = 0;
  std::uint32_t despejados = 0;
  std::uint32_t criados = 0;
  std::uint32_t reaproveitados = 0;
  std::uint32_t recusados = 0;
};

class GerenciadorDeRecursos {
 public:
  /// [orcamento_bytes] e o teto. Zero significa "sem teto", e so serve
  /// para os testes de unidade — em producao quem calcula e o nucleo.
  explicit GerenciadorDeRecursos(std::uint64_t orcamento_bytes = 0) noexcept;

  GerenciadorDeRecursos(const GerenciadorDeRecursos&) = delete;
  GerenciadorDeRecursos& operator=(const GerenciadorDeRecursos&) = delete;

  /// CRIA E DEVOLVE A ALCA. Pode despejar recursos ociosos para caber.
  [[nodiscard]] Resulta<AlcaDeRecurso> criar(const DescricaoDoRecurso& d);

  /// PEDE UM ALVO DE RENDER DO TAMANHO PEDIDO, REAPROVEITANDO O QUE JA
  /// EXISTE. E o cache de quadro: alvo do mesmo tamanho volta para a mao
  /// em vez de ser alocado de novo, e um quadro de video nunca aloca.
  [[nodiscard]] Resulta<AlcaDeRecurso> alvo(std::uint32_t largura,
                                            std::uint32_t altura);

  /// MARCA O USO E DEVOLVE A ALCA. Alca morta = o recurso saiu.
  [[nodiscard]] AlcaDeRecurso usar(IdDeRecurso id);

  /// O AVANCO DO TEMPO. Chamado uma vez por quadro: e o que envelhece o
  /// LRU. Sem isto todo recurso teria a mesma idade e o despejo seria
  /// arbitrario.
  void avancar_quadro() noexcept { quadro_++; }

  /// DESPEJA O OCIOSO ATE CABER NO ORCAMENTO. Devolve quantos sairam.
  std::uint32_t aparar() noexcept;

  /// ABRE ESPACO PARA UMA ALOCACAO NOVA.
  ///
  /// E DIFERENTE DE [aparar]. `aparar` faz o que ja existe CABER no
  /// orcamento; aqui o que existe ja cabe — falta lugar para o que
  /// CHEGOU. Com so o `aparar`, um pedido de 256 bytes num orcamento de
  /// 1024 ja cheio era recusado com quatro recursos ociosos na mao:
  /// `bytes_ > orcamento` era falso (1024 nao e maior que 1024), o laco
  /// nao rodava e a resposta era "nao cabe". Quem paga por um cache que
  /// nao despeja e o usuario, com uma textura que nao carrega.
  [[nodiscard]] bool abrir_espaco(std::uint64_t bytes) noexcept;

  void definir_orcamento(std::uint64_t bytes) noexcept;

  [[nodiscard]] std::uint64_t bytes_em_uso() const noexcept;
  [[nodiscard]] std::uint64_t orcamento() const noexcept {
    return orcamento_bytes_;
  }
  [[nodiscard]] EstatisticasDeRecursos estatisticas() const noexcept;

  /// SOLTA TUDO. Chamado no fechamento do nucleo e na perda do contexto
  /// de GPU (o `VkDevice` morreu: as cargas apontam para memoria que nao
  /// existe mais e continuar com elas seria usar memoria liberada).
  void soltar_tudo() noexcept;

 private:
  [[nodiscard]] bool cabe(std::uint64_t bytes) const noexcept;

  std::unordered_map<IdDeRecurso, std::shared_ptr<Recurso>> recursos_;
  std::uint64_t orcamento_bytes_;
  std::uint64_t pico_bytes_ = 0;
  std::uint64_t bytes_ = 0;
  IdDeRecurso proximo_id_ = 1;
  std::uint64_t quadro_ = 0;
  std::uint32_t criados_ = 0;
  std::uint32_t despejados_ = 0;
  std::uint32_t reaproveitados_ = 0;
  std::uint32_t recusados_ = 0;
};

}  // namespace aurea::render

#endif  // AUREA_RENDER_GERENCIADOR_DE_RECURSOS_H
