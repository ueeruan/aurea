// O AGENDADOR DE QUADRO — quanto custou, quanto sobra, quanto se perdeu.
//
// O RENDERCORE NAO PERSEGUE 60 FPS. Ele persegue um ORCAMENTO. Num
// celular, estourar o orcamento nao da "mais um quadro": da aquecimento,
// e aquecimento vira throttling, e throttling vira 24 fps sustentados
// depois de dois minutos de edicao — pior do que 30 estaveis desde o
// comeco. Por isso o relogio mede, guarda a distribuicao e responde a
// pergunta que o compositor precisa fazer antes de desenhar: "cabe?".
//
// DONO: a thread do render. Nada aqui e atomico nem travado — se algum
// dia outra thread precisar ler, leia uma copia das estatisticas.
#ifndef AUREA_RENDER_RELOGIO_DO_QUADRO_H
#define AUREA_RENDER_RELOGIO_DO_QUADRO_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "base.h"

namespace aurea::render {

/// QUANTOS QUADROS CABEM NA JANELA DE MEDICAO. Cento e vinte quadros sao
/// dois segundos a 60 Hz: perto o bastante para reagir a uma travada, e
/// longo o bastante para o p95 nao ser um susto.
inline constexpr std::size_t kJanelaDeQuadros = 120;

/// O ORCAMENTO PADRAO, em milissegundos. 16,667 ms = 60 Hz. O alvo do
/// Aurea e este; quem escolhe outro e a qualidade adaptativa, e nao o
/// usuario.
inline constexpr double kOrcamentoPadraoMs = 1000.0 / 60.0;

struct EstatisticasDoQuadro {
  /// Tempo de CPU do ultimo quadro e a distribuicao da janela.
  double cpu_ultimo_ms = 0.0;
  double cpu_mediana_ms = 0.0;
  double cpu_p95_ms = 0.0;
  double cpu_max_ms = 0.0;

  /// Tempo de GPU quando o backend souber informar (Metal e Vulkan
  /// sabem; o backend de referencia, nao — e zero e a resposta certa).
  double gpu_ultimo_ms = 0.0;

  /// Intervalo entre quadros apresentados: e o que o dedo sente.
  double intervalo_mediana_ms = 0.0;

  double fps_efetivo = 0.0;

  std::uint32_t quadros = 0;
  std::uint32_t quadros_atrasados = 0;  // estouraram o orcamento
  std::uint32_t quadros_pulados = 0;    // decididos fora, sem desenhar
  std::uint32_t quadros_na_janela = 0;
};

class RelogioDoQuadro {
 public:
  explicit RelogioDoQuadro(double orcamento_ms = kOrcamentoPadraoMs,
                           std::size_t janela = kJanelaDeQuadros) noexcept;

  /// MARCA O COMECO. Chamado no primeiro instante do quadro.
  void comecar_quadro() noexcept;

  /// MARCA O FIM. Fecha o tempo de CPU, calcula o intervalo desde o
  /// quadro anterior e atualiza a distribuicao.
  void terminar_quadro() noexcept;

  /// O BACKEND INFORMA O TEMPO DE GPU do quadro que acabou.
  void registrar_gpu(double ms) noexcept;

  /// UM QUADRO QUE NAO FOI DESENHADO (a fila estava vazia, a cena nao
  /// mudou, o orcamento do anterior estourou). Conta para o placar: um
  /// renderizador que pula sem contar mente para quem le o HUD.
  void registrar_pulo() noexcept;

  /// O ORCAMENTO DESTE QUADRO JA ACABOU? O compositor chama isto entre
  /// etapas pesadas e desiste do que for secundario.
  [[nodiscard]] bool orcamento_estourado() const noexcept;

  /// QUANTO SOBRA DO ORCAMENTO, em milissegundos. Negativo = estourou.
  [[nodiscard]] double folga_ms() const noexcept;

  /// O ALVO DESTE QUADRO. A qualidade adaptativa mexe aqui — 30 Hz
  /// sustentados aquecem menos do que 60 Hz com metade dos quadros
  /// perdidos.
  void definir_orcamento(double ms) noexcept { orcamento_ms_ = ms; }
  [[nodiscard]] double orcamento_ms() const noexcept { return orcamento_ms_; }

  [[nodiscard]] EstatisticasDoQuadro estatisticas() const noexcept;

  /// ZERA OS CONTADORES, mantendo a janela de medidas. Usado quando o
  /// projeto troca e o placar antigo nao diz mais nada.
  void reiniciar() noexcept;

 private:
  using Relogio = std::chrono::steady_clock;

  static double em_ms(Relogio::duration d) noexcept;

  double orcamento_ms_;
  std::size_t janela_;

  /// Buffer circular: escrever e uma atribuicao, e a mediana le a janela
  /// inteira so quando alguem pede as estatisticas.
  std::array<double, kJanelaDeQuadros> cpu_ms_{};
  std::array<double, kJanelaDeQuadros> intervalo_ms_{};
  std::size_t escritos_ = 0;

  Relogio::time_point inicio_do_quadro_{};
  Relogio::time_point fim_do_quadro_anterior_{};
  bool tem_anterior_ = false;

  double cpu_ultimo_ms_ = 0.0;
  double gpu_ultimo_ms_ = 0.0;
  std::uint32_t quadros_ = 0;
  std::uint32_t atrasados_ = 0;
  std::uint32_t pulados_ = 0;
};

}  // namespace aurea::render

#endif  // AUREA_RENDER_RELOGIO_DO_QUADRO_H
