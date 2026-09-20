// O NUCLEO — a thread do render, dona do contexto, do quadro e do placar.
//
// A UI NAO DESENHA. Ela publica um ESTADO (a cena, imutavel, pronta) e
// pede um quadro. Quem desenha e esta thread, que e a unica a tocar no
// alvo, no gerenciador de recursos e no compositor — e por isso nao ha
// trava em nenhum dos tres. A unica coisa compartilhada entre as duas
// threads e a fila de comandos (sem trava) e a cena publicada (um
// `shared_ptr` atomico, C++20).
//
// POR QUE UMA THREAD PROPRIA, E NAO A DA UI: o quadro de uma composicao
// pesada custa milissegundos; na thread da UI isso e um engasgo no
// gesto. Separar tambem e o que permite, mais adiante, a thread de
// decodificacao entregar quadros sem parar o desenho.
//
// O QUE AINDA NAO EXISTE AQUI, E O RELATORIO DIZ: pipeline de video,
// texto, efeitos, 3D, particulas e a apresentacao (Texture do Flutter).
// O que existe e o esqueleto que eles vao usar: quadro agendado com
// orcamento, recursos com teto, shaders com cache, avaliador de timeline
// deterministico e um compositor que ja compoe de verdade.
#ifndef AUREA_RENDER_NUCLEO_H
#define AUREA_RENDER_NUCLEO_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "avaliador_da_timeline.h"
#include "base.h"
#include "compositor.h"
#include "fila_de_comandos.h"
#include "gerenciador_de_recursos.h"
#include "gerenciador_de_shaders.h"
#include "relogio_do_quadro.h"

namespace aurea::render {

/// A CENA PUBLICADA PELA UI. Imutavel depois de publicada: quem desenha
/// le, e ninguem escreve por baixo. E o que dispensa trava entre as duas
/// threads.
struct Cena {
  std::vector<Camada> camadas;
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;

  /// UM NUMERO QUE SO MUDA QUANDO A CENA MUDA. O nucleo compara com o do
  /// quadro anterior: cena igual e quadro igual, e o pedido pode ser
  /// respondido com o quadro que ja esta no alvo — sem recompor.
  std::uint64_t impressao = 0;
};

/// A QUALIDADE ADAPTATIVA.
///
/// O OBJETIVO NAO E BONITO, E SUSTENTAVEL. Num celular, gastar o
/// orcamento inteiro todo quadro aquece; aquecido, o aparelho derruba o
/// relogio, e o resultado e 20 fps depois de dois minutos — pior do que
/// 30 desde o comeco. O controle olha o p95 e reage devagar, com
/// histerese: descer rapido, subir so depois de um bom tempo de folga.
/// Sem a histerese ele oscilaria entre dois niveis a cada segundo, e a
/// oscilacao e mais incomoda que o nivel baixo.
struct Qualidade {
  /// Amostras por eixo no compositor: 1, 2 ou 3.
  std::uint32_t amostras = 2;

  /// Fracao da resolucao da composicao em que o quadro e composto. 1,0 e
  /// resolucao cheia; 0,5 custa um quarto dos pixels.
  double escala_interna = 1.0;

  /// O ORCAMENTO DE QUADRO, em milissegundos — A POLITICA, e nao um
  /// degrau. 16,667 = 60 Hz; 33,333 = 30 Hz (quem esta com a bateria no
  /// fim, ou com o aparelho quente). Quem cede para caber nele e a
  /// resolucao, e nao o alvo.
  double orcamento_ms = kOrcamentoPadraoMs;
};

struct Configuracao {
  std::uint32_t largura = 1920;
  std::uint32_t altura = 1080;
  std::uint64_t orcamento_de_recursos_bytes = 0;
  Backend backend = Backend::referencia;
  Qualidade qualidade{};
  bool com_thread = true;
};

struct EstatisticasDoNucleo {
  EstatisticasDoQuadro quadro;
  EstatisticasDeRecursos recursos;
  EstatisticasDeShaders shaders;
  EstatisticasDoCompositor compositor;
  Qualidade qualidade;
  std::uint32_t fila_pendente = 0;
  std::uint32_t quadros_reaproveitados = 0;  // cena igual: nao recompôs
  std::uint32_t quadros_falhos = 0;
  std::uint64_t tempo_publicado = 0;  // o instante do ultimo quadro
};

/// QUEM COMPILA NO BACKEND DE REFERENCIA. Nao ha shader a compilar: o
/// rasterizador e codigo de CPU. Ele existe para que o caminho inteiro
/// (chave, cache, falha, pre-aquecimento) seja exercitado do mesmo jeito —
/// trocar de backend nao muda nada acima desta linha.
class CompiladorDeReferencia final : public CompiladorDeShaders {
 public:
  [[nodiscard]] Resulta<std::unique_ptr<CargaDeShader>> compilar(
      const DescricaoDoShader& d, Backend b) override;
};

class Nucleo {
 public:
  [[nodiscard]] static Resulta<std::unique_ptr<Nucleo>> abrir(
      const Configuracao& c);

  ~Nucleo();
  Nucleo(const Nucleo&) = delete;
  Nucleo& operator=(const Nucleo&) = delete;

  /// A UI PUBLICA O ESTADO. Barato: troca um ponteiro.
  void publicar_cena(std::shared_ptr<const Cena> cena) noexcept;

  /// A UI PEDE UM QUADRO. Nao espera — o desenho acontece na thread do
  /// render e o chamador segue.
  void pedir_quadro() noexcept;

  /// DESENHA AGORA, NA THREAD DE QUEM CHAMOU. Existe para o teste e para
  /// a bancada: sem thread nao ha corrida, e o resultado e deterministico.
  ///
  /// NAO E `noexcept`, e de proposito: compor aloca (a lista de camadas
  /// escaladas). Uma excecao de alocacao aqui dentro de um `noexcept`
  /// chamaria `std::terminate` e derrubaria o app — exatamente o que o
  /// fallback existe para evitar. Quem chama pega na porta (`api.cpp`).
  Resulta<std::uint32_t> desenhar_agora();

  [[nodiscard]] EstatisticasDoNucleo estatisticas() const noexcept;

  /// A QUALIDADE. `automatica` deixa o controle decidir pelo p95; fixar
  /// serve para a bancada comparar dois niveis sem o controle no meio.
  void definir_qualidade(const Qualidade& q) noexcept;
  [[nodiscard]] Qualidade qualidade() const noexcept;

  void definir_automatica(bool ligada) noexcept;
  [[nodiscard]] bool automatica() const noexcept;

  /// REGISTRA UMA TEXTURA. O conteudo vem do chamador (PNG decodificado,
  /// quadro de video) — o nucleo nao decodifica nada ainda.
  [[nodiscard]] Resulta<IdDeRecurso> registrar_textura(
      std::uint32_t largura, std::uint32_t altura,
      std::vector<std::uint8_t> rgba);

  /// A MESMA COISA, COM A COR JA MULTIPLICADA PELO ALFA. O alvo do motor 3D
  /// sai premultiplicado do rasterizador; sem esta porta ele entraria na
  /// composicao sendo multiplicado uma segunda vez.
  [[nodiscard]] Resulta<IdDeRecurso> registrar_textura_premultiplicada(
      std::uint32_t largura, std::uint32_t altura,
      std::vector<std::uint8_t> rgba);

  /// COMPILA EM LOTE, FORA DO QUADRO. Devolve quantos ficaram prontos.
  Resulta<std::uint32_t> pre_aquecer(
      std::span<const DescricaoDoShader> descricoes);

  /// OS PIXELS DO ULTIMO QUADRO, EM RGBA8 NAO-PREMULTIPLICADO.
  ///
  /// E LEITURA DE TESTE E DE BANCADA, e esta aqui de proposito: e o unico
  /// ponto do nucleo que traz pixels da memoria de volta para a CPU. O
  /// caminho de producao NAO passa por aqui — o quadro vai para a tela
  /// pela superficie do backend. Se algum dia isto aparecer no meio de um
  /// play, o zero-copy acabou.
  [[nodiscard]] Resulta<std::uint32_t> ler_pixels(
      std::vector<std::uint8_t>& destino);

  [[nodiscard]] std::uint32_t largura() const noexcept { return cfg_.largura; }
  [[nodiscard]] std::uint32_t altura() const noexcept { return cfg_.altura; }
  [[nodiscard]] Backend backend() const noexcept { return cfg_.backend; }

  /// PARA A THREAD E SOLTA TUDO. Idempotente.
  void fechar() noexcept;

 private:
  explicit Nucleo(const Configuracao& c) noexcept;
  Resulta<std::uint32_t> abrir_recursos();

  /// O CORPO DE `registrar_textura`, COM O `premultiplicada` NO FIM. As
  /// duas portas publicas sao a mesma funcao com um booleano diferente, e
  /// nao duas copias da mesma validacao.
  [[nodiscard]] Resulta<IdDeRecurso> registrar_textura_com(
      std::uint32_t largura, std::uint32_t altura,
      std::vector<std::uint8_t> rgba, bool premultiplicada);

  /// PEDE UM ALVO E GARANTE QUE ELE TENHA PIXELS.
  ///
  /// O gerenciador de recursos nao sabe o que ha dentro de um alvo — ele
  /// so sabe quanto pesa. Quem sabe e o backend, e por isso a carga e
  /// presa aqui: um alvo reaproveitado ja vem com a dele, e um alvo novo
  /// ganha uma agora. Sem isto, `desenhar` receberia um alvo sem pixels e
  /// devolveria "estado invalido" no primeiro quadro.
  Resulta<AlcaDeRecurso> pegar_alvo(std::uint32_t largura,
                                    std::uint32_t altura);
  void laco_da_thread() noexcept;
  void ajustar_qualidade() noexcept;

  /// APLICA O DEGRAU ATUAL — resolucao e amostras, nunca o orcamento.
  void aplicar_degrau() noexcept;

  Configuracao cfg_;
  RelogioDoQuadro relogio_;
  GerenciadorDeRecursos recursos_;
  CompiladorDeReferencia compilador_;
  GerenciadorDeShaders shaders_;
  Compositor compositor_;

  /// A CENA PUBLICADA, SOB A TRAVA.
  ///
  /// `std::atomic<std::shared_ptr<T>>` seria o caminho sem trava, mas ele
  /// e C++20 tardio: a libc++ que o Android traz nem sempre o tem, e o
  /// Aurea compila com o NDK que estiver no aparelho. Aqui a trava e
  /// tomada DUAS VEZES por quadro (uma para publicar, uma para ler) —
  /// nao e o caminho quente, e o custo e um par de instrucoes.
  std::shared_ptr<const Cena> cena_;

  FilaCircular<std::uint32_t> fila_;

  AlcaDeRecurso alvo_;
  std::shared_ptr<const Cena> cena_desenhada_;
  std::uint64_t impressao_desenhada_ = 0;

  mutable std::mutex trava_;
  std::condition_variable sinal_;
  bool parar_ = false;
  bool pedido_ = false;
  std::atomic<bool> automatica_{true};
  Qualidade qualidade_{};

  /// `std::thread`, E NAO `std::jthread`.
  ///
  /// O `jthread` promete juntar-se sozinho e parar por `stop_token`, e
  /// seria a escolha moderna — mas a libc++ do NDK 28 NAO O TEM: o
  /// compilador do Android recusa `std::jthread` e `std::stop_token`, e o
  /// build para arm64 para ali. A parada ja e explicita aqui (`parar_`
  /// sob a trava, e `join` no `fechar`), entao o que se perde e o
  /// acucar, e nao o comportamento.
  std::thread thread_;

  std::uint32_t quadros_reaproveitados_ = 0;
  std::uint32_t quadros_falhos_ = 0;
  std::uint64_t tempo_publicado_ = 0;
  std::uint32_t nivel_de_qualidade_ = 2;  // 0 = minimo, 2 = cheio
};

}  // namespace aurea::render

#endif  // AUREA_RENDER_NUCLEO_H
