#include "avaliador_da_timeline.h"

#include <cmath>

namespace aurea::render {

namespace {
constexpr double kPi = 3.14159265358979323846;

/// A BEZIER, POR BISSECCAO — copia de `_bezier` do Dart.
///
/// O DART PARA EM `while (true)` porque o intervalo sempre cai pela
/// metade e a tolerancia de 0,001 chega. Aqui o laco tem teto explicito:
/// um `NaN` em `t` faria a comparacao ser sempre falsa e o laco do Dart
/// girar para sempre; no C++ isso seria um quadro travado sem aviso.
[[nodiscard]] double bezier(double a, double b, double c, double d,
                            double t) noexcept {
  const auto avaliar = [](double p, double q, double m) noexcept {
    return 3 * p * (1 - m) * (1 - m) * m + 3 * q * (1 - m) * m * m + m * m * m;
  };
  double inicio = 0.0, fim = 1.0;
  for (int i = 0; i < 64; ++i) {
    const double meio = (inicio + fim) / 2;
    const double estimativa = avaliar(a, c, meio);
    if (std::fabs(t - estimativa) < 0.001) return avaliar(b, d, meio);
    if (estimativa < t) {
      inicio = meio;
    } else {
      fim = meio;
    }
  }
  // NAO CONVERGIU: `t` nao e um numero util. O valor do meio e a resposta
  // menos ruim, e e melhor que devolver lixo.
  return avaliar(b, d, (inicio + fim) / 2);
}

/// A MOLA — copia de `Easing._spring`.
[[nodiscard]] double mola(double t, const Curva& c) noexcept {
  const double r = prender(c.resposta, 0.05, 10.0);
  const double zeta = prender(c.amortecimento, 0.0, 4.0);
  const double omega = 2 * kPi / r;
  const double v0 = c.velocidade_inicial;
  double valor = 0.0;
  if (std::fabs(zeta - 1.0) < 1e-6) {
    valor = 1 - std::exp(-omega * t) * (1 + (omega - v0) * t);
  } else if (zeta < 1.0) {
    const double wd = omega * std::sqrt(1 - zeta * zeta);
    const double cte = (zeta * omega - v0) / wd;
    valor = 1 - std::exp(-zeta * omega * t) *
                    (std::cos(wd * t) + cte * std::sin(wd * t));
  } else {
    const double raiz = std::sqrt(zeta * zeta - 1);
    const double a = -omega * (zeta - raiz);
    const double b = -omega * (zeta + raiz);
    const double c2 = (v0 + a) / (b - a);
    const double c1 = -1 - c2;
    valor = 1 + c1 * std::exp(a * t) + c2 * std::exp(b * t);
  }
  return prender(valor, 0.0, 1.009);
}
}  // namespace

double Curva::transformar(double t) const noexcept {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;

  switch (tipo) {
    case TipoDeCurva::bezier:
      if (linear()) return t;
      return bezier(prender(x1, 0.0, 1.0), y1, prender(x2, 0.0, 1.0), y2, t);

    case TipoDeCurva::quicar: {
      const double n = contagem < 1 ? 1 : contagem;
      const double decaimento = std::pow(1 - t, 2.0 * (1.5 - intensidade));
      return prender(1 - decaimento * std::fabs(std::cos(kPi * n * t)), 0.0,
                     1.0);
    }

    case TipoDeCurva::elastico: {
      const double n = contagem < 1 ? 1 : contagem;
      const double freio = std::pow(2.0, -10.0 * t / (0.35 + intensidade));
      return freio * std::sin((t * n - 0.25) * 2 * kPi) + 1;
    }

    case TipoDeCurva::degraus: {
      const double n = contagem < 2 ? 2 : contagem;
      return prender(std::floor(t * n) / (n - 1), 0.0, 1.0);
    }

    case TipoDeCurva::ciclico: {
      const double c = contagem < 1 ? 1 : contagem;
      const double u = t * c;
      const double f = u - std::floor(u);
      const double suave = 0.5 - 0.5 * std::cos(kPi * f);
      return suavidade * suave + (1 - suavidade) * f;
    }

    case TipoDeCurva::aleatorio: {
      // RUIDO DETERMINISTICO: o mesmo projeto tem de dar o mesmo quadro
      // ao voltar. Nada de `rand()`.
      const double envelope = 4 * t * (1 - t);
      const double ruido =
          std::sin(t * 27.4) * 0.62 + std::sin(t * 61.7) * 0.38;
      return prender(t + intensidade * 0.3 * envelope * ruido, 0.0, 1.0);
    }

    case TipoDeCurva::mola:
      return mola(t, *this);

    case TipoDeCurva::segurar:
      return t >= 1 ? 1 : 0;

    case TipoDeCurva::degrausElasticos:
    case TipoDeCurva::naoEspelhada:
      // NAO ESPELHADA, E DITO. O progresso cru e a resposta honesta:
      // inventar uma curva aqui seria pior do que nao ter nenhuma.
      return t;
  }
  return t;
}

double transformar_curva(const Curva& c, double t) noexcept {
  return c.transformar(t);
}

ResultadoDaAvaliacao avaliar(std::span<const Keyframe> quadros, double tempo_s,
                             double base) noexcept {
  ResultadoDaAvaliacao r;
  r.valor = base;

  if (quadros.empty()) return r;

  // AS PONTAS VALEM SEMPRE, e a curva do primeiro keyframe nao entra: nao
  // ha trecho antes dele para remapear.
  if (quadros.size() == 1) {
    r.valor = quadros.front().valor;
    return r;
  }
  if (tempo_s <= quadros.front().tempo_s) {
    r.valor = quadros.front().valor;
    return r;
  }
  if (tempo_s >= quadros.back().tempo_s) {
    r.valor = quadros.back().valor;
    return r;
  }

  // BUSCA BINARIA PELA MESMA REGRA DO DART (`hi - lo > 1`): o segmento e
  // `[lo, hi]` com `quadros[lo].tempo <= t`.
  std::size_t lo = 0;
  std::size_t hi = quadros.size() - 1;
  while (hi - lo > 1) {
    const std::size_t meio = (lo + hi) >> 1;
    if (quadros[meio].tempo_s <= tempo_s) {
      lo = meio;
    } else {
      hi = meio;
    }
  }

  const Keyframe& a = quadros[lo];
  const Keyframe& b = quadros[hi];
  const double intervalo = b.tempo_s - a.tempo_s;
  double f = intervalo == 0.0 ? 1.0 : (tempo_s - a.tempo_s) / intervalo;
  r.espelhado = a.curva.espelhada();
  f = a.curva.transformar(prender(f, 0.0, 1.0));
  r.valor = a.valor + (b.valor - a.valor) * f;
  r.no_trecho = true;
  return r;
}

}  // namespace aurea::render
