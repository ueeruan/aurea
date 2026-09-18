// O COMPOSITOR 2D — a pilha de camadas virando pixels.
//
// AQUI MORA O BACKEND DE REFERENCIA, e ele NAO e um lugar de espera: e o
// rasterizador de CPU que define o resultado CORRETO. Quando o backend de
// Metal e o de Vulkan entrarem, e contra estes pixels que eles vao ser
// comparados — sem uma referencia exata nao ha como saber se a GPU esta
// certa, e "esta bonito na tela" nao e prova de nada. Ele tambem e o
// caminho de emergencia: se o contexto de GPU cair no aparelho, o
// Aurea ainda compoe, ainda que devagar.
//
// O QUE ELE AINDA NAO FAZ, E O RELATORIO DIZ: texto, video, mascara,
// efeitos, 3D e particulas. A pilha que ele compoe hoje e cor solida e
// textura com transformacao, opacidade e mistura — que e o degrau 2 do
// plano (uma camada simples) mais o comeco do degrau 6.
//
// ESPACO DE COR: o framebuffer e RGBA8 PREMULTIPLICADO. E o que a GPU
// espera e o que a formula de Porter-Duff pede; guardar nao-premultiplicado
// obrigaria a uma divisao por pixel em cada composicao. A leitura para
// teste desfaz a multiplicacao, para o numero lido ser o que a pessoa ve.
#ifndef AUREA_RENDER_COMPOSITOR_H
#define AUREA_RENDER_COMPOSITOR_H

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "base.h"
#include "gerenciador_de_recursos.h"

namespace aurea::render {

/// OS MODOS DE MISTURA. Os numeros sao CONTRATO com o Dart: o cliente
/// traduz `BlendMode` por NOME para estes valores, e nunca por indice do
/// enum do `dart:ui` — que pode ganhar membro novo a qualquer versao.
enum class Mistura : std::uint32_t {
  normal = 0,
  multiplicar = 1,
  tela = 2,
  sobrepor = 3,
  somar = 4,
  escurecer = 5,
  clarear = 6,
  diferenca = 7,
};

enum class TipoDeCamada : std::uint32_t {
  vazia = 0,
  cor = 1,
  textura = 2,
};

struct Camada {
  TipoDeCamada tipo = TipoDeCamada::vazia;

  /// Para [TipoDeCamada::textura]: o recurso de textura.
  IdDeRecurso textura = kRecursoInvalido;

  /// PARA ONDE VAI A ANCORA E QUANTO A CAIXA OCUPA, na composicao.
  /// (x, y) e o ponto da composicao onde cai a ancora; largura e altura
  /// sao o tamanho da caixa local, antes de escalar.
  float x = 0.0F;
  float y = 0.0F;
  float largura = 0.0F;
  float altura = 0.0F;

  /// A ANCORA DENTRO DA CAIXA, em 0..1. (0,5, 0,5) e o centro — o pivo do
  /// Aurea quando ninguem mexeu nele.
  float ancora_x = 0.5F;
  float ancora_y = 0.5F;

  float escala_x = 1.0F;
  float escala_y = 1.0F;
  float rotacao_graus = 0.0F;

  /// 0..1. Entra na conta como multiplicador do alfa da camada.
  float opacidade = 1.0F;

  Mistura mistura = Mistura::normal;

  /// Para [TipoDeCamada::cor].
  Cor cor{};
};

/// OS PIXELS DE UMA TEXTURA. Nao-premultiplicado na entrada — e o que um
/// PNG ou um quadro decodificado entrega —, premultiplicado ao amostrar.
class CargaDeTextura final : public CargaDoRecurso {
 public:
  CargaDeTextura(std::uint32_t largura, std::uint32_t altura,
                 std::vector<std::uint8_t> rgba)
      : largura(largura), altura(altura), rgba(std::move(rgba)) {}

  std::uint32_t largura;
  std::uint32_t altura;
  std::vector<std::uint8_t> rgba;  // 4 bytes por pixel

  [[nodiscard]] bool valida() const noexcept {
    return largura > 0 && altura > 0 &&
           rgba.size() >= static_cast<std::size_t>(largura) * altura * 4;
  }
};

/// OS PIXELS DE UM ALVO: RGBA8 premultiplicado. E o framebuffer.
class CargaDeAlvo final : public CargaDoRecurso {
 public:
  CargaDeAlvo(std::uint32_t largura, std::uint32_t altura)
      : largura(largura),
        altura(altura),
        pixels(static_cast<std::size_t>(largura) * altura * 4, 0) {}

  std::uint32_t largura;
  std::uint32_t altura;
  std::vector<std::uint8_t> pixels;

  void limpar() noexcept { std::fill(pixels.begin(), pixels.end(), 0); }
};

struct EstatisticasDoCompositor {
  std::uint32_t camadas_desenhadas = 0;
  std::uint32_t camadas_fora = 0;    // a caixa nao tocou o alvo
  std::uint32_t camadas_invalidas = 0;
  std::uint64_t pixels_escritos = 0;
  std::uint32_t amostras_por_pixel = 1;
};

class Compositor {
 public:
  explicit Compositor(GerenciadorDeRecursos& recursos) noexcept
      : recursos_(recursos) {}

  Compositor(const Compositor&) = delete;
  Compositor& operator=(const Compositor&) = delete;

  /// COMPOE A PILHA INTEIRA NO ALVO.
  ///
  /// A ORDEM E A DO ARRAY: indice 0 primeiro, ou seja, o FUNDO. Quem
  /// monta a lista e o nucleo, e ele a monta de tras para frente — a
  /// inversao da ordem da timeline acontece uma vez so, la, e nao aqui.
  ///
  /// [amostras] e o supermuestreamento por eixo: 1 nao tem antisserrilhado,
  /// 2 da 4 amostras por pixel. E o botao que a qualidade adaptativa gira
  /// quando o aparelho esquenta, e por isso ele e por quadro e nao global.
  [[nodiscard]] Resulta<std::uint32_t> desenhar(
      CargaDeAlvo& alvo, std::span<const Camada> camadas,
      std::uint32_t amostras = 2);

  [[nodiscard]] const EstatisticasDoCompositor& estatisticas() const noexcept {
    return stats_;
  }

 private:
  GerenciadorDeRecursos& recursos_;
  EstatisticasDoCompositor stats_{};
};

/// DESFAZ A PREMULTIPLICACAO, para o teste ler o que a pessoa veria.
/// Um pixel de alfa zero continua preto e transparente: nao ha cor para
/// recuperar, e inventar uma seria mentir.
[[nodiscard]] Cor desmultiplicar(std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b, std::uint8_t a) noexcept;

/// A MISTURA DE UM PIXEL, em premultiplicado — separavel, na forma de
/// Porter-Duff com a funcao B de cada modo.
///
/// Co = (1 - ab)*Cs + (1 - as)*Cb + as*ab*B(Cs/as, Cb/ab)
/// ao = as + ab*(1 - as)
void misturar_pixel(float* destino, const float* fonte,
                                  Mistura modo) noexcept;

}  // namespace aurea::render

#endif  // AUREA_RENDER_COMPOSITOR_H
