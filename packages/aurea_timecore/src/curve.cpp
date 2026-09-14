// CURVA — porte de AnimatedDouble._rawAt/_rawValueAt/_loopRemap
// (keyframe.dart) mais a extrapolacao linear de cut_ops._extendedTrackValue.
// Uma so avaliacao para video, palco, precomp e editor de curva.
#include <algorithm>
#include <cmath>

#include "timecore_internal.hpp"

namespace atc {

namespace {

double lerpD(double a, double b, double t) {
  if (a == b) return a;  // lerpDouble do Flutter devolve a exato
  return a * (1.0 - t) + b * t;
}

}  // namespace

Curve::Curve(double base, std::vector<Keyframe> keyframes, LoopMode mode,
             LoopWhen when, int32_t loopCount)
    : base_(std::isfinite(base) ? base : 0),
      k_(std::move(keyframes)),
      mode_(mode),
      when_(when),
      loopCount_(loopCount) {
  for (auto& k : k_) {
    if (!std::isfinite(k.v)) k.v = 0;
  }
  // O Dart mantem a lista ordenada; ordenar de novo so protege contra
  // projeto corrompido. Estavel: empate de tempo preserva a ordem.
  std::stable_sort(k_.begin(), k_.end(),
                   [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
}

int Curve::segmentIndex(int64_t t) const {
  const int n = static_cast<int>(k_.size());
  if (n < 2) return -1;
  if (t <= k_.front().t || t >= k_.back().t) return -1;
  int lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    const int mid = (lo + hi) >> 1;
    if (k_[static_cast<size_t>(mid)].t <= t) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

double Curve::rawValue(int64_t t) const {
  if (t <= k_.front().t) return k_.front().v;
  if (t >= k_.back().t) return k_.back().v;
  const int i = segmentIndex(t);
  const Keyframe& a = k_[static_cast<size_t>(i)];
  const Keyframe& b = k_[static_cast<size_t>(i + 1)];
  const int64_t span = b.t - a.t;
  double f = span == 0 ? 1.0 : static_cast<double>(t - a.t) / static_cast<double>(span);
  f = a.e.transform(std::min(1.0, std::max(0.0, f)));
  return lerpD(a.v, b.v, f);
}

void Curve::loopRange(int64_t t, int64_t& from, int64_t& to) const {
  const int n = static_cast<int>(k_.size());
  const int c = loopCount_;
  if (c <= 0 || c >= n) {
    from = k_.front().t;
    to = k_.back().t;
    return;
  }
  if (t > k_.back().t) {
    from = k_[static_cast<size_t>(n - c)].t;
    to = k_.back().t;
    return;
  }
  from = k_.front().t;
  to = k_[static_cast<size_t>(c - 1)].t;
}

bool Curve::loopRemap(int64_t t, int64_t from, int64_t to, int64_t& time,
                      double& cycles, bool& backwards) const {
  backwards = false;
  if (!loopActive()) return false;
  const int64_t span = to - from;
  if (span <= 0) return false;
  const int64_t first = k_.front().t;
  const int64_t last = k_.back().t;
  if (t > last && loopsAfter()) {
    const int64_t over = t - last;
    const int64_t n = over / span;
    const int64_t rem = over % span;
    switch (mode_) {
      case LoopMode::cycle:
      case LoopMode::offset:
        time = from + rem;
        cycles = static_cast<double>(n + 1);
        return true;
      case LoopMode::pingPong: {
        const bool back = (n % 2) == 0;  // voltas impares correm para tras
        time = back ? to - rem : from + rem;
        backwards = back;
        cycles = 0;
        return true;
      }
      default:
        return false;
    }
  }
  if (t < first && loopsBefore()) {
    const int64_t under = first - t;
    const int64_t n = under / span;
    const int64_t rem = under % span;
    switch (mode_) {
      case LoopMode::cycle:
      case LoopMode::offset:
        time = to - rem;
        cycles = -static_cast<double>(n + 1);
        return true;
      case LoopMode::pingPong: {
        const bool forwards = (n % 2) == 0;
        time = forwards ? from + rem : to - rem;
        backwards = !forwards;
        cycles = 0;
        return true;
      }
      default:
        return false;
    }
  }
  return false;
}

double Curve::raw(int64_t t) const {
  if (k_.empty()) return base_;
  const Keyframe& first = k_.front();
  const Keyframe& last = k_.back();
  const size_t n = k_.size();
  if (loopActive() && n >= 2) {
    if (mode_ == LoopMode::continueValue) {
      if (t > last.t && loopsAfter()) {
        const Keyframe& prev = k_[n - 2];
        const int64_t dt = last.t - prev.t;
        if (dt <= 0) return last.v;
        const double v = (last.v - prev.v) / static_cast<double>(dt);
        return last.v + v * static_cast<double>(t - last.t);
      }
      if (t < first.t && loopsBefore()) {
        const Keyframe& next = k_[1];
        const int64_t dt = next.t - first.t;
        if (dt <= 0) return first.v;
        const double v = (next.v - first.v) / static_cast<double>(dt);
        return first.v - v * static_cast<double>(first.t - t);
      }
    } else {
      int64_t from = 0, to = 0;
      loopRange(t, from, to);
      int64_t time = 0;
      double cycles = 0;
      bool backwards = false;
      if (loopRemap(t, from, to, time, cycles, backwards)) {
        const double delta = rawValue(to) - rawValue(from);
        return rawValue(time) + (mode_ == LoopMode::offset ? delta * cycles : 0);
      }
    }
  }
  return rawValue(t);
}

double Curve::value(int64_t t, Extrap mode) const {
  if (k_.empty()) return base_;
  const Keyframe& first = k_.front();
  const Keyframe& last = k_.back();
  if (t >= first.t && t <= last.t) return rawValue(t);
  if (mode == Extrap::hold || loopActive()) return raw(t);
  // LINEAR: _extendedTrackValue de cut_ops.
  const size_t n = k_.size();
  if (n < 2) return t < first.t ? first.v : last.v;
  const Keyframe& a = t < first.t ? k_[0] : k_[n - 2];
  const Keyframe& b = t < first.t ? k_[1] : k_[n - 1];
  const int64_t dt = b.t - a.t;
  if (dt == 0) return t < first.t ? first.v : last.v;
  const double slope = (b.v - a.v) / static_cast<double>(dt);
  const Keyframe& anchor = t < first.t ? a : b;
  return anchor.v + static_cast<double>(t - anchor.t) * slope;
}

double Curve::segmentSlope(int i, int64_t t) const {
  const Keyframe& a = k_[static_cast<size_t>(i)];
  const Keyframe& b = k_[static_cast<size_t>(i + 1)];
  const int64_t span = b.t - a.t;
  if (span <= 0) return 0;
  const double f = std::min(1.0, std::max(0.0, static_cast<double>(t - a.t) /
                                                   static_cast<double>(span)));
  if (a.v == b.v) return 0;
  return (b.v - a.v) * a.e.derivative(f) / (static_cast<double>(span) / 1e6);
}

double Curve::extrapSlope(int64_t t, Extrap mode) const {
  const size_t n = k_.size();
  const Keyframe& first = k_.front();
  const Keyframe& last = k_.back();
  if (loopActive() && n >= 2) {
    if (mode_ == LoopMode::continueValue) {
      const bool after = t > last.t;
      if ((after && loopsAfter()) || (!after && loopsBefore())) {
        const Keyframe& a = after ? k_[n - 2] : k_[0];
        const Keyframe& b = after ? k_[n - 1] : k_[1];
        const int64_t dt = b.t - a.t;
        return dt <= 0 ? 0 : (b.v - a.v) / (static_cast<double>(dt) / 1e6);
      }
    } else {
      int64_t from = 0, to = 0;
      loopRange(t, from, to);
      int64_t time = 0;
      double cycles = 0;
      bool backwards = false;
      if (loopRemap(t, from, to, time, cycles, backwards)) {
        // Mesmo trecho, no instante remapeado; o pingue-pongue na volta
        // de tras para frente inverte o sinal.
        int i = segmentIndex(time);
        if (i < 0) {
          i = time <= first.t ? 0 : static_cast<int>(n) - 2;
        }
        const double s = segmentSlope(i, time);
        return backwards ? -s : s;
      }
    }
  }
  if (mode == Extrap::hold || n < 2) return 0;
  const Keyframe& a = t < first.t ? k_[0] : k_[n - 2];
  const Keyframe& b = t < first.t ? k_[1] : k_[n - 1];
  const int64_t dt = b.t - a.t;
  return dt == 0 ? 0 : (b.v - a.v) / (static_cast<double>(dt) / 1e6);
}

double Curve::slope(int64_t t, Extrap mode) const {
  const size_t n = k_.size();
  if (n < 2) return 0;
  const Keyframe& first = k_.front();
  const Keyframe& last = k_.back();
  if (t < first.t || t > last.t) return extrapSlope(t, mode);
  if (t == last.t) {
    // Na ultima marca vale o trecho que chega nela (nao ha trecho depois).
    return segmentSlope(static_cast<int>(n) - 2, t);
  }
  int i = segmentIndex(t);
  if (i < 0) i = 0;  // t == first.t
  // Instantes repetidos: o trecho de largura zero nao tem derivada;
  // avanca para o primeiro trecho com largura.
  while (i + 1 < static_cast<int>(n) - 1 &&
         k_[static_cast<size_t>(i + 1)].t == k_[static_cast<size_t>(i)].t) {
    ++i;
  }
  return segmentSlope(i, t);
}

void segmentRange(const Keyframe& a, const Keyframe& b, double& lo, double& hi) {
  lo = std::min(lo, std::min(a.v, b.v));
  hi = std::max(hi, std::max(a.v, b.v));
  // lerp de valores iguais e constante, qualquer que seja o easing.
  if (a.v == b.v) return;
  const double dv = b.v - a.v;
  switch (a.e.type) {
    case EaseType::hold:
    case EaseType::steps:
      return;  // valores so entre as pontas
    case EaseType::cubicBezier: {
      if (a.e.isLinear()) return;
      // Extremos de y(u): raizes de y'(u) = 0.
      //   y'(u)/3 = (1-u)^2 p1 + 2(1-u)u (p2-p1) + u^2 (1-p2)
      //           = u^2 (3p1 - 3p2 + 1) + u (2p2 - 4p1) + p1
      const double p1 = a.e.y1, p2 = a.e.y2;
      const double A = 3 * p1 - 3 * p2 + 1, B = 2 * p2 - 4 * p1, C = p1;
      auto consider = [&](double u) {
        if (u > 0 && u < 1) {
          const double y = bezier1D(p1, p2, u);
          const double v = a.v + dv * y;
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
      };
      if (std::fabs(A) < 1e-12) {
        if (std::fabs(B) > 1e-12) consider(-C / B);
      } else {
        const double disc = B * B - 4 * A * C;
        if (disc >= 0) {
          const double sq = std::sqrt(disc);
          consider((-B + sq) / (2 * A));
          consider((-B - sq) / (2 * A));
        }
      }
      return;
    }
    default: {
      // Formulas oscilantes: amostragem densa (count <= dezenas).
      const int samples = 1024;
      for (int s = 1; s < samples; ++s) {
        const double y = a.e.transform(static_cast<double>(s) / samples);
        const double v = a.v * (1.0 - y) + b.v * y;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      return;
    }
  }
}

void Curve::range(double& lo, double& hi) const {
  lo = base_;
  hi = base_;
  for (size_t i = 0; i < k_.size(); ++i) {
    lo = std::min(lo, k_[i].v);
    hi = std::max(hi, k_[i].v);
    if (i + 1 < k_.size() && k_[i + 1].t > k_[i].t) {
      segmentRange(k_[i], k_[i + 1], lo, hi);
    }
  }
}

}  // namespace atc
