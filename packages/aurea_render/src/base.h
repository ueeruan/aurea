// TIPOS DE BASE DO RENDERCORE.
//
// C++20, sem excecao nenhuma atravessando o FFI e sem alocacao escondida
// nos caminhos quentes. O que pode falhar devolve `Resulta<T>`; o que
// nao pode falhar e `noexcept`.
//
// POR QUE UM `Resulta` PROPRIO E NAO `std::expected`:
// `std::expected` e C++23 e depende de uma libc++ recente. O Aurea
// compila com o NDK que estiver no aparelho de teste e com o MSVC do
// Windows — o mesmo fonte tem de valer nos dois. O `Resulta` daqui e a
// fatia minima: ou o valor, ou um codigo de erro de 32 bits.
#ifndef AUREA_RENDER_BASE_H
#define AUREA_RENDER_BASE_H

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace aurea::render {

// ---------------------------------------------------------------- erros

/// O QUE DEU ERRADO. Um numero, e nao uma string: o erro atravessa o FFI
/// como `int32_t` e nunca aloca. `nome_do_erro` traduz do lado de fora.
enum class Erro : std::int32_t {
  nenhum = 0,
  sem_memoria = 1,
  sem_recurso = 2,
  orcamento_estourado = 3,
  capacidade = 4,
  estado_invalido = 5,
  argumento = 6,
  ja_existe = 7,
  nao_existe = 8,
  nao_suportado = 9,
  interno = 10,
};

[[nodiscard]] constexpr std::string_view nome_do_erro(Erro e) noexcept {
  switch (e) {
    case Erro::nenhum: return "nenhum";
    case Erro::sem_memoria: return "sem_memoria";
    case Erro::sem_recurso: return "sem_recurso";
    case Erro::orcamento_estourado: return "orcamento_estourado";
    case Erro::capacidade: return "capacidade";
    case Erro::estado_invalido: return "estado_invalido";
    case Erro::argumento: return "argumento";
    case Erro::ja_existe: return "ja_existe";
    case Erro::nao_existe: return "nao_existe";
    case Erro::nao_suportado: return "nao_suportado";
    case Erro::interno: return "interno";
  }
  return "desconhecido";
}

/// OU O VALOR, OU O ERRO. Sem estado invalido possivel: `tem_valor()` e
/// `tem_erro()` sao sempre opostos, e `valor()` so existe se houver valor.
template <typename T>
class [[nodiscard]] Resulta {
 public:
  Resulta(T v) : valor_(std::move(v)) {}
  Resulta(Erro e) : erro_(e) {}

  [[nodiscard]] bool tem_valor() const noexcept { return valor_.has_value(); }
  [[nodiscard]] bool tem_erro() const noexcept { return !valor_.has_value(); }
  [[nodiscard]] Erro erro() const noexcept { return erro_; }

  T& valor() & noexcept { return *valor_; }
  const T& valor() const& noexcept { return *valor_; }
  T&& valor() && noexcept { return std::move(*valor_); }

  /// O valor, ou [padrao] quando deu erro. Para leituras de estatistica,
  /// onde zero e uma resposta honesta e nao vale um `if`.
  [[nodiscard]] T ou(T padrao) const noexcept {
    return valor_.has_value() ? *valor_ : std::move(padrao);
  }

 private:
  std::optional<T> valor_;
  Erro erro_ = Erro::nenhum;
};

// ------------------------------------------------------------------ cor

/// RGBA nao-premultiplicado, 8 bits por canal — o que o projeto guarda.
struct Cor {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;

  /// ARGB, E NAO RGBA — A ORDEM DO `Color.value` DO FLUTTER.
  ///
  /// O projeto guarda cor como ARGB desde sempre (`Color` do `dart:ui`), e
  /// a ponte entrega o numero cru. Escolher RGBA aqui obrigaria o lado
  /// Dart a remontar cada cor antes de enviar — e uma troca de canais
  /// silenciosa (vermelho vira azul) e o tipo de erro que so aparece numa
  /// captura de tela, nunca numa excecao.
  [[nodiscard]] static constexpr Cor de_argb(std::uint32_t v) noexcept {
    return Cor{static_cast<std::uint8_t>((v >> 16) & 0xFF),
               static_cast<std::uint8_t>((v >> 8) & 0xFF),
               static_cast<std::uint8_t>(v & 0xFF),
               static_cast<std::uint8_t>((v >> 24) & 0xFF)};
  }

  [[nodiscard]] friend constexpr bool operator==(const Cor& x,
                                                 const Cor& y) noexcept {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
  }
};

// ------------------------------------------------------------ geometria

struct Retangulo {
  float x = 0.0F;
  float y = 0.0F;
  float largura = 0.0F;
  float altura = 0.0F;

  [[nodiscard]] constexpr bool vazio() const noexcept {
    return largura <= 0.0F || altura <= 0.0F;
  }
};

// --------------------------------------------------------- conta miuda

[[nodiscard]] constexpr float prender(float v, float lo, float hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] constexpr double prender(double v, double lo,
                                       double hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] constexpr float misturar(float a, float b, float t) noexcept {
  return a + (b - a) * t;
}

}  // namespace aurea::render

#endif  // AUREA_RENDER_BASE_H
