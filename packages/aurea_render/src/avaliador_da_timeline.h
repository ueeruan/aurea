// O AVALIADOR DA TIMELINE — o mesmo tempo para o preview e para a exportacao.
//
// O QUE ESTE ARQUIVO E: a copia fiel, em C++, da conta que o Dart faz em
// `AnimatedDouble.valueAt` (lib/src/features/editor/domain/keyframe.dart).
// A REGRA E A DIVERGENCIA ZERO: a mesma lista de keyframes, com as mesmas
// curvas, avaliada no mesmo instante, tem de dar o mesmo numero nas duas
// linguagens. Nao e "parecido" — e o mesmo `f = (t - a)/(b - a)`, o mesmo
// `f.clamp(0,1)`, a mesma bisseccao de 0,001 na bezier, o mesmo `lerp`.
//
// POR QUE A COPIA EXISTE: enquanto o C++ avalia sozinho, preview e
// exportacao podem divergir sem ninguem perceber — e a divergencia so
// aparece no arquivo final, depois de uma hora de render. Com o mesmo
// avaliador alimentando os dois, a divergencia deixa de ser possivel.
// Enquanto o avaliador C++ nao for o unico, ele roda EM SOMBRA contra o
// Dart: o teste `nucleo_avaliador_test.dart` compara os dois numero a
// numero, e e o portao para que ele assuma.
//
// O QUE AINDA NAO ESTA ESPELHADO: loop de keyframes (LoopSpec), expressoes
// e o animador automatico. Quem chama sabe disso pela resposta de
// [espelhado], e nao por suposicao.
#ifndef AUREA_RENDER_AVALIADOR_DA_TIMELINE_H
#define AUREA_RENDER_AVALIADOR_DA_TIMELINE_H

#include <cstdint>
#include <span>
#include <vector>

#include "base.h"

namespace aurea::render {

/// OS TIPOS DE CURVA, NA MESMA ORDEM DO `EasingType` DO DART.
///
/// A ORDEM E CONTRATO: `test/nucleo_avaliador_test.dart` compara os dois
/// avaliadores por estes numeros, e um deles fora de lugar faria o teste
/// acusar divergencia onde nao ha. Um tipo NOVO no Dart entra aqui no FIM.
/// Os que nao estao espelhados devolvem o progresso cru e contam em
/// [ResultadoDaAvaliacao::espelhado] = false.
enum class TipoDeCurva : std::int32_t {
  bezier = 0,
  quicar = 1,
  elastico = 2,
  ciclico = 3,
  aleatorio = 4,
  degraus = 5,
  degrausElasticos = 6,  // nao espelhado
  mola = 7,
  segurar = 8,
  // 9.. sao as familias da v1.1.1 (quicarAoContrario, elasticoAoContrario,
  // degrausSorteados, serra...). Nao espelhadas: o Dart manda, e o C++
  // devolve o progresso cru ate que cada uma seja copiada e provada.
  naoEspelhada = 100,
};

struct Curva {
  TipoDeCurva tipo = TipoDeCurva::bezier;
  double x1 = 0.0, y1 = 0.0, x2 = 1.0, y2 = 1.0;
  std::int32_t contagem = 4;
  double suavidade = 1.0;
  double intensidade = 0.5;
  double resposta = 0.55;
  double amortecimento = 0.825;
  double velocidade_inicial = 0.0;

  [[nodiscard]] bool linear() const noexcept {
    return tipo == TipoDeCurva::bezier && x1 == 0.0 && y1 == 0.0 &&
           x2 == 1.0 && y2 == 1.0;
  }

  /// O REMAPEAMENTO DO PROGRESSO (0..1). Copia de `Easing.transform`.
  [[nodiscard]] double transformar(double t) const noexcept;

  /// ESTA CURVA ESTA MESMO COPIADA DO DART?
  ///
  /// A LISTA E FECHADA, e nao um intervalo. O `EasingType` do Dart tem
  /// quinze membros; os nove primeiros estao aqui. Um `tipo` igual a 12
  /// (o "oscillate" do projeto) nao casa com nenhum `case` do
  /// `transformar` e cairia no `return t` do fim — devolver o progresso
  /// cru E dizer que espelhou seria a pior resposta possivel: o teste de
  /// divergencia passaria a comparar duas contas diferentes e acusaria
  /// erro onde nao ha.
  [[nodiscard]] bool espelhada() const noexcept {
    switch (tipo) {
      case TipoDeCurva::bezier:
      case TipoDeCurva::quicar:
      case TipoDeCurva::elastico:
      case TipoDeCurva::ciclico:
      case TipoDeCurva::aleatorio:
      case TipoDeCurva::degraus:
      case TipoDeCurva::mola:
      case TipoDeCurva::segurar:
        return true;
      case TipoDeCurva::degrausElasticos:
      case TipoDeCurva::naoEspelhada:
        return false;
    }
    return false;
  }
};

struct Keyframe {
  /// O INSTANTE, em segundos desde o inicio da camada. O Dart guarda
  /// microssegundos inteiros; aqui e double, e a conversao acontece uma
  /// vez, na entrada — dividir em cada avaliacao acumularia erro.
  double tempo_s = 0.0;
  double valor = 0.0;

  /// A CURVA DO TRECHO QUE SAI DESTE KEYFRAME.
  Curva curva;
};

struct ResultadoDaAvaliacao {
  double valor = 0.0;

  /// FALSO quando a curva pedida nao esta espelhada: o valor sai do
  /// progresso cru, e quem le sabe que nao pode comparar com o Dart.
  bool espelhado = true;

  /// FALSO quando o tempo caiu fora do trecho com keyframes: vale a ponta.
  bool no_trecho = false;
};

/// AVALIA A PILHA NUM INSTANTE.
///
/// `base` e o valor de quando nao ha keyframe nenhum — o mesmo `base` do
/// `AnimatedDouble`. Fora do primeiro e do ultimo keyframe vale a ponta,
/// como no Dart: nao ha extrapolacao.
[[nodiscard]] ResultadoDaAvaliacao avaliar(std::span<const Keyframe> quadros,
                                           double tempo_s,
                                           double base = 0.0) noexcept;

/// A CURVA SOZINHA, para o teste de divergencia comparar curva a curva.
[[nodiscard]] double transformar_curva(const Curva& c, double t) noexcept;

}  // namespace aurea::render

#endif  // AUREA_RENDER_AVALIADOR_DA_TIMELINE_H
