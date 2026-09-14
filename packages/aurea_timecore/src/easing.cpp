// EASING — porte 1:1 de Easing.transform (lib/.../domain/keyframe.dart).
//
// Toda formula abaixo espelha o Dart linha a linha, com UMA excecao
// deliberada: a bezier. O Cubic do Flutter para a bissecao quando o erro
// em x fica abaixo de 1e-3; num trecho de 5 s de fonte isso chega a
// ~10 ms, um terco de quadro a 30 fps. Para o Time Remap a correcao
// temporal vem antes da compatibilidade com esse arredondamento, entao a
// bezier aqui e resolvida ate 1e-12.
#include <algorithm>
#include <cmath>

#include "timecore_internal.hpp"

namespace atc {

namespace {

const double kPi = 3.14159265358979323846;

double clampD(double v, double lo, double hi) {
  // clamp do Dart: NaN passa direto; aqui vira lo para nao contaminar.
  if (!(v >= lo)) return lo;
  if (v > hi) return hi;
  return v;
}

// Curves.elasticOut do Flutter (period 0.4), com o atalho de
// Curve.transform para 0 e 1.
double elasticOut(double t) {
  if (t == 0.0 || t == 1.0) return t;
  const double period = 0.4;
  const double s = period / 4.0;
  return std::pow(2.0, -10 * t) * std::sin((t - s) * (kPi * 2.0) / period) + 1.0;
}

}  // namespace

Ease Ease::fromC(const atc_keyframe& k) {
  Ease e;
  e.type = (k.type >= 0 && k.type <= 8) ? static_cast<EaseType>(k.type)
                                         : EaseType::cubicBezier;
  e.count = k.count;
  e.x1 = std::isfinite(k.x1) ? k.x1 : 0;
  e.y1 = std::isfinite(k.y1) ? k.y1 : 0;
  e.x2 = std::isfinite(k.x2) ? k.x2 : 1;
  e.y2 = std::isfinite(k.y2) ? k.y2 : 1;
  e.smooth = std::isfinite(k.smooth) ? k.smooth : 1;
  e.intensity = std::isfinite(k.intensity) ? k.intensity : 0.5;
  e.response = std::isfinite(k.response) ? k.response : 0.55;
  e.damping = std::isfinite(k.damping) ? k.damping : 0.825;
  e.velocity = std::isfinite(k.velocity) ? k.velocity : 0;
  return e;
}

void Ease::toC(atc_keyframe& k) const {
  k.type = static_cast<int32_t>(type);
  k.count = count;
  k.x1 = x1;
  k.y1 = y1;
  k.x2 = x2;
  k.y2 = y2;
  k.smooth = smooth;
  k.intensity = intensity;
  k.response = response;
  k.damping = damping;
  k.velocity = velocity;
}

double bezierParamForX(double x1, double x2, double x) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  // Newton com salvaguarda de bissecao: x(u) e monotona para x1, x2 em
  // [0, 1], entao o intervalo [lo, hi] sempre contem a raiz.
  double lo = 0, hi = 1, u = x;
  for (int i = 0; i < 64; ++i) {
    const double fx = bezier1D(x1, x2, u) - x;
    if (std::fabs(fx) < 1e-12) return u;
    if (fx < 0) {
      lo = u;
    } else {
      hi = u;
    }
    const double d = bezier1DDerivative(x1, x2, u);
    double next = (d > 1e-12) ? u - fx / d : 0.5 * (lo + hi);
    if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
    if (hi - lo < 1e-15) return next;
    u = next;
  }
  return u;
}

double Ease::transform(double t) const {
  if (!(t > 0)) return 0;
  if (t >= 1) return 1;
  switch (type) {
    case EaseType::cubicBezier: {
      if (isLinear()) return t;
      const double ax = clampD(x1, 0.0, 1.0);
      const double bx = clampD(x2, 0.0, 1.0);
      const double u = bezierParamForX(ax, bx, t);
      return bezier1D(y1, y2, u);
    }
    case EaseType::bounce: {
      const int n = count < 1 ? 1 : count;
      const double decaimento = std::pow(1 - t, 2.0 * (1.5 - intensity));
      return clampD(1 - decaimento * std::fabs(std::cos(kPi * n * t)), 0.0, 1.0);
    }
    case EaseType::elastic: {
      const int n = count < 1 ? 1 : count;
      const double freio = std::pow(2.0, -10.0 * t / (0.35 + intensity));
      return freio * std::sin((t * n - 0.25) * 2 * kPi) + 1;
    }
    case EaseType::steps: {
      const int n = count < 2 ? 2 : count;
      return clampD(std::floor(t * n) / (n - 1), 0.0, 1.0);
    }
    case EaseType::elasticSteps: {
      const int n = count < 2 ? 2 : count;
      const double s = std::floor(t * n);
      const double frac = t * n - s;
      return clampD((s + elasticOut(frac)) / n, 0.0, 1.5);
    }
    case EaseType::cyclic: {
      const int c = count < 1 ? 1 : count;
      const double u = t * c;
      const double f = u - std::floor(u);
      const double smoothed = 0.5 - 0.5 * std::cos(kPi * f);
      return smooth * smoothed + (1 - smooth) * f;
    }
    case EaseType::random: {
      const double envelope = 4 * t * (1 - t);
      const double noise = std::sin(t * 27.4) * 0.62 + std::sin(t * 61.7) * 0.38;
      return clampD(t + intensity * 0.3 * envelope * noise, 0.0, 1.0);
    }
    case EaseType::spring: {
      const double r = clampD(response, 0.05, 10.0);
      const double zeta = clampD(damping, 0.0, 4.0);
      const double omega = 2 * kPi / r;
      double value;
      if (std::fabs(zeta - 1) < 1e-6) {
        value = 1 - std::exp(-omega * t) * (1 + (omega - velocity) * t);
      } else if (zeta < 1) {
        const double wd = omega * std::sqrt(1 - zeta * zeta);
        const double c = (zeta * omega - velocity) / wd;
        value = 1 - std::exp(-zeta * omega * t) *
                        (std::cos(wd * t) + c * std::sin(wd * t));
      } else {
        const double root = std::sqrt(zeta * zeta - 1);
        const double a = -omega * (zeta - root);
        const double b = -omega * (zeta + root);
        const double c2 = (velocity + a) / (b - a);
        const double c1 = -1 - c2;
        value = 1 + c1 * std::exp(a * t) + c2 * std::exp(b * t);
      }
      return clampD(value, 0.0, 1.009);
    }
    case EaseType::hold:
      return 0;  // t < 1 aqui
  }
  return t;
}

double Ease::derivative(double t) const {
  const double tc = clampD(t, 0.0, 1.0);
  switch (type) {
    case EaseType::hold:
    case EaseType::steps:
      return 0;  // constante por partes
    case EaseType::cubicBezier: {
      if (isLinear()) return 1;
      const double ax = clampD(x1, 0.0, 1.0);
      const double bx = clampD(x2, 0.0, 1.0);
      const double u = bezierParamForX(ax, bx, tc);
      double dx = bezier1DDerivative(ax, bx, u);
      double dy = bezier1DDerivative(y1, y2, u);
      // Ponta degenerada (alca em cima da ancora): a direcao vem da outra
      // alca — a mesma regra de Easing.speedAt no Dart.
      if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9) {
        if (u < 0.5) {
          dx = bx;
          dy = y2;
        } else {
          dx = 1 - ax;
          dy = 1 - y1;
        }
        if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9) return 1;
      }
      if (std::fabs(dx) < 1e-9) return dy >= 0 ? 1e9 : -1e9;
      return dy / dx;
    }
    default: {
      // Formula fechada e suave: diferenca central (unilateral nas pontas).
      const double h = 1e-5;
      const double a = clampD(tc - h, 0.0, 1.0);
      const double b = clampD(tc + h, 0.0, 1.0);
      if (b <= a) return 0;
      // transform(1) salta para 1 nos tipos que nao terminam em 1; na
      // ponta direita usa o lado de dentro.
      const double fb = (b >= 1.0) ? transform(1.0 - 1e-9) : transform(b);
      const double fa = transform(a);
      const double bb = (b >= 1.0) ? 1.0 - 1e-9 : b;
      return (fb - fa) / (bb - a);
    }
  }
}

}  // namespace atc
