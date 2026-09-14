// MAPEAMENTO DA CAMADA, TABELA DE QUADROS, RECORTE E VELOCIDADE -> TEMPO.
#include <algorithm>
#include <cmath>
#include <limits>

#include "timecore_internal.hpp"

namespace atc {

namespace {

const int64_t kMinSpanUs = 34000;  // _minimumSourceSpan do Dart

int64_t roundUs(double us) {
  if (!std::isfinite(us)) return 0;
  if (us > 9.2e18) return std::numeric_limits<int64_t>::max();
  if (us < -9.2e18) return std::numeric_limits<int64_t>::min();
  return static_cast<int64_t>(std::llround(us));  // round do Dart: meio para longe do zero
}

// Instante de fonte SEM arredondar (segundos, relativo a sourceOffset).
double mappingSeconds(const Layer& l, int64_t t, double spanSeconds) {
  const double forward = l.curve ? l.curve->value(t, Extrap::linear)
                                 : static_cast<double>(t) * l.speed / 1e6;
  return l.reverse ? spanSeconds - forward : forward;
}

double mappingSlope(const Layer& l, int64_t t) {
  const double s = l.curve ? l.curve->slope(t, Extrap::linear) : l.speed;
  return l.reverse ? -s : s;
}

// Easing bezier com x linear (x1 = 1/3, x2 = 2/3): y(u) cubica em u = g.
Ease hermiteEase(double m0, double m1, double dtSeconds, double dv) {
  Ease e;
  const double y1 = m0 * dtSeconds / (3 * dv);
  const double y2 = 1 - m1 * dtSeconds / (3 * dv);
  if (std::fabs(y1 - 1.0 / 3) < 1e-9 && std::fabs(y2 - 2.0 / 3) < 1e-9) return e;  // linear
  e.x1 = 1.0 / 3;
  e.x2 = 2.0 / 3;
  e.y1 = y1;
  e.y2 = y2;
  return e;
}

// Pedacos de Hermite ate o erro ficar abaixo da tolerancia. fn(t) em
// segundos e slope(t) em segundos por segundo; so os pontos de inicio sao
// emitidos (o chamador fecha com a ultima marca).
template <typename F, typename S>
void hermitePieces(int64_t t0, int64_t t1, const F& fn, const S& slope, double tolS,
                   std::vector<Keyframe>& out, int depth = 0) {
  const double v0 = fn(t0), v1 = fn(t1);
  const double dt = static_cast<double>(t1 - t0) / 1e6;
  const double dv = v1 - v0;
  // m1 e o limite pela ESQUERDA em t1 (em t1 pode comecar outro trecho).
  const double m0 = slope(t0, false), m1 = slope(t1, true);
  const bool tooSmall = (t1 - t0) <= 1000 || depth >= 24 || out.size() > 20000;
  Keyframe k;
  k.t = t0;
  k.v = v0;
  if (std::fabs(dv) < 1e-12) {
    const bool flat = std::fabs(m0) * dt < tolS && std::fabs(m1) * dt < tolS &&
                      std::fabs(fn(t0 + (t1 - t0) / 2) - v0) < tolS;
    if (flat || tooSmall) {
      out.push_back(k);  // linear plano
      return;
    }
  } else {
    k.e = hermiteEase(m0, m1, dt, dv);
    bool ok = true;
    for (double g : {0.25, 0.5, 0.75}) {
      const int64_t tg = t0 + static_cast<int64_t>(std::llround((t1 - t0) * g));
      const double y = k.e.transform(static_cast<double>(tg - t0) / (t1 - t0));
      const double approx = v0 * (1 - y) + v1 * y;
      if (std::fabs(approx - fn(tg)) > tolS) {
        ok = false;
        break;
      }
    }
    if (ok || tooSmall) {
      if (!ok) k.e = Ease();  // linear no ultimo recurso
      out.push_back(k);
      return;
    }
  }
  const int64_t mid = t0 + (t1 - t0) / 2;
  hermitePieces(t0, mid, fn, slope, tolS, out, depth + 1);
  hermitePieces(mid, t1, fn, slope, tolS, out, depth + 1);
}

// ∫_0^f E(φ) dφ para o easing do trecho.
double easeIntegral(const Ease& e, double f) {
  if (f <= 0) return 0;
  if (f > 1) f = 1;
  switch (e.type) {
    case EaseType::hold:
      return 0;
    case EaseType::cubicBezier: {
      if (e.isLinear()) return f * f / 2;
      // ∫ y(u) x'(u) du de 0 a u(f): polinomio de grau 5; Gauss-Legendre
      // de 3 pontos e exato.
      const double ax = std::min(1.0, std::max(0.0, e.x1));
      const double bx = std::min(1.0, std::max(0.0, e.x2));
      const double uf = bezierParamForX(ax, bx, f);
      const double w[3] = {5.0 / 9, 8.0 / 9, 5.0 / 9};
      const double x[3] = {-std::sqrt(3.0 / 5), 0, std::sqrt(3.0 / 5)};
      double sum = 0;
      for (int i = 0; i < 3; ++i) {
        const double u = uf * (x[i] + 1) / 2;
        sum += w[i] * bezier1D(e.y1, e.y2, u) * bezier1DDerivative(ax, bx, u);
      }
      return sum * uf / 2;
    }
    default: {
      // Simpson composto fino: as formulas sao limitadas e suaves por partes.
      const int n = 2048;
      const double h = f / n;
      double sum = e.transform(0) + e.transform(f - 1e-12);
      for (int i = 1; i < n; ++i) sum += e.transform(i * h) * ((i % 2) ? 4 : 2);
      return sum * h / 3;
    }
  }
}

}  // namespace

Layer Layer::fromC(const atc_layer& c) {
  Layer l;
  l.sourceOffset = c.source_offset_us;
  l.duration = c.duration_us;
  l.speed = std::isfinite(c.speed) ? c.speed : 1.0;
  l.reverse = c.reverse != 0;
  l.curve = reinterpret_cast<const Curve*>(c.curve);
  return l;
}

int64_t layerSpan(const Layer& l) {
  if (!l.curve) return roundUs(static_cast<double>(l.duration) * l.speed);
  double lo = 0, hi = 0;
  l.curve->range(lo, hi);
  const double seconds = std::max(0.0, hi - std::min(0.0, lo));
  const int64_t span = roundUs(seconds * 1e6);
  return span < kMinSpanUs ? kMinSpanUs : span;
}

int64_t layerSource(const Layer& l, int64_t local) {
  const int64_t forward = l.curve ? roundUs(l.curve->value(local, Extrap::linear) * 1e6)
                                  : roundUs(static_cast<double>(local) * l.speed);
  if (!l.reverse) return forward;
  return layerSpan(l) - forward;
}

int64_t layerAbsoluteSource(const Layer& l, int64_t local) {
  const int64_t abs = l.sourceOffset + layerSource(l, local);
  return abs < 0 ? 0 : abs;
}

double layerRate(const Layer& l, int64_t local) { return mappingSlope(l, local); }

void sliceLayer(const Layer& l, int64_t from, int64_t to, int64_t toleranceUs,
                std::vector<Keyframe>& out, double& minValue) {
  out.clear();
  if (to < from) std::swap(from, to);
  const double spanS = static_cast<double>(layerSpan(l)) / 1e6;
  const double tolS = static_cast<double>(std::max<int64_t>(1, toleranceUs)) / 1e6;
  auto fn = [&](int64_t t) { return mappingSeconds(l, t, spanS); };
  auto sl = [&](int64_t t, bool left) {
    return mappingSlope(l, left && t - 1 > from ? t - 1 : t);
  };

  std::vector<int64_t> cuts = {from, to};
  if (l.curve) {
    for (const auto& k : l.curve->keyframes()) {
      if (k.t > from && k.t < to) cuts.push_back(k.t);
    }
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

  std::vector<Keyframe> abs;  // tempo absoluto por enquanto
  for (size_t c = 0; c + 1 < cuts.size(); ++c) {
    const int64_t t0 = cuts[c], t1 = cuts[c + 1];
    Keyframe k;
    k.t = t0;
    k.v = fn(t0);
    const Curve* cv = l.curve;
    if (!cv || cv->keyframes().size() < 2) {
      abs.push_back(k);  // linear (ou constante)
      continue;
    }
    const auto& ks = cv->keyframes();
    const bool outside = t1 <= ks.front().t || t0 >= ks.back().t;
    if (outside) {
      if (cv->loopActive()) {
        hermitePieces(t0, t1, fn, sl, tolS, abs);
      } else {
        abs.push_back(k);  // extrapolacao secante: linear exato
      }
      continue;
    }
    int i = cv->segmentIndex(t0 + (t1 - t0) / 2);
    if (i < 0) i = 0;
    const Keyframe& a = ks[static_cast<size_t>(i)];
    const Keyframe& b = ks[static_cast<size_t>(i + 1)];
    const double span = static_cast<double>(b.t - a.t);
    const double f0 = (t0 - a.t) / span, f1 = (t1 - a.t) / span;
    const Ease& e = a.e;
    if (e.isLinear() || a.v == b.v) {
      abs.push_back(k);
    } else if (e.type == EaseType::cubicBezier) {
      if (f0 <= 0 && f1 >= 1) {
        k.e = e;
        abs.push_back(k);
        continue;
      }
      // DE CASTELJAU: sub-bezier entre u0 e u1, renormalizada.
      const double ax = std::min(1.0, std::max(0.0, e.x1));
      const double bx = std::min(1.0, std::max(0.0, e.x2));
      const double u0 = bezierParamForX(ax, bx, f0), u1 = bezierParamForX(ax, bx, f1);
      const double du = u1 - u0;
      const double X0 = bezier1D(ax, bx, u0), X3 = bezier1D(ax, bx, u1);
      const double Y0 = bezier1D(e.y1, e.y2, u0), Y3 = bezier1D(e.y1, e.y2, u1);
      const double X1 = X0 + du * bezier1DDerivative(ax, bx, u0) / 3;
      const double Y1 = Y0 + du * bezier1DDerivative(e.y1, e.y2, u0) / 3;
      const double X2 = X3 - du * bezier1DDerivative(ax, bx, u1) / 3;
      const double Y2 = Y3 - du * bezier1DDerivative(e.y1, e.y2, u1) / 3;
      const double dx = X3 - X0, dy = Y3 - Y0;
      if (dx > 1e-12 && std::fabs(dy) > 1e-9) {
        k.e.x1 = std::min(1.0, std::max(0.0, (X1 - X0) / dx));
        k.e.x2 = std::min(1.0, std::max(0.0, (X2 - X0) / dx));
        k.e.y1 = (Y1 - Y0) / dy;
        k.e.y2 = (Y2 - Y0) / dy;
        abs.push_back(k);
      } else {
        hermitePieces(t0, t1, fn, sl, tolS, abs);
      }
    } else if (e.type == EaseType::hold) {
      if (f1 >= 1) k.e = e;  // segura ate a marca e salta nela
      abs.push_back(k);      // senao e plano: linear com valores iguais
    } else {
      hermitePieces(t0, t1, fn, sl, tolS, abs);
    }
  }
  Keyframe last;
  last.t = to;
  last.v = fn(to);
  abs.push_back(last);

  minValue = abs.front().v;
  for (size_t i = 0; i < abs.size(); ++i) {
    minValue = std::min(minValue, abs[i].v);
    if (i + 1 < abs.size() && abs[i + 1].t > abs[i].t) {
      double lo = minValue, hi = minValue;
      segmentRange(abs[i], abs[i + 1], lo, hi);
      minValue = std::min(minValue, lo);
    }
  }
  out.reserve(abs.size());
  for (auto& k : abs) {
    k.t -= from;
    out.push_back(k);
  }
}

void speedToValue(const std::vector<Keyframe>& speedIn, double startValue,
                  int64_t toleranceUs, std::vector<Keyframe>& out) {
  out.clear();
  if (speedIn.empty()) return;
  std::vector<Keyframe> sp = speedIn;
  std::stable_sort(sp.begin(), sp.end(),
                   [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
  const double tolS = static_cast<double>(std::max<int64_t>(1, toleranceUs)) / 1e6;
  // Valor acumulado no inicio de cada trecho.
  std::vector<double> T(sp.size(), startValue);
  for (size_t i = 0; i + 1 < sp.size(); ++i) {
    const double dt = static_cast<double>(sp[i + 1].t - sp[i].t) / 1e6;
    T[i + 1] = T[i] + dt * (sp[i].v + (sp[i + 1].v - sp[i].v) * easeIntegral(sp[i].e, 1));
  }
  for (size_t i = 0; i + 1 < sp.size(); ++i) {
    const Keyframe& a = sp[i];
    const Keyframe& b = sp[i + 1];
    if (b.t <= a.t) continue;
    const double spanS = static_cast<double>(b.t - a.t) / 1e6;
    auto fn = [&](int64_t t) {
      const double f = static_cast<double>(t - a.t) / static_cast<double>(b.t - a.t);
      return T[i] + spanS * (a.v * f + (b.v - a.v) * easeIntegral(a.e, f));
    };
    auto sl = [&](int64_t t, bool) {
      const double f = static_cast<double>(t - a.t) / static_cast<double>(b.t - a.t);
      const double ff = std::min(f, 1.0 - 1e-9);
      return a.v + (b.v - a.v) * a.e.transform(ff);
    };
    hermitePieces(a.t, b.t, fn, sl, tolS, out);
  }
  Keyframe last;
  last.t = sp.back().t;
  last.v = T.back();
  out.push_back(last);
}

FrameIndex::FrameIndex(const int64_t* pts, int32_t n, int64_t lastDuration)
    : lastDuration_(lastDuration > 0 ? lastDuration : 0) {
  const int64_t kInvalid = -(static_cast<int64_t>(1) << 62);
  if (pts && n > 0) {
    pts_.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
      if (pts[i] > kInvalid) pts_.push_back(pts[i]);
    }
  }
  std::sort(pts_.begin(), pts_.end());
  pts_.erase(std::unique(pts_.begin(), pts_.end()), pts_.end());
}

int32_t FrameIndex::floorIndex(int64_t s) const {
  if (pts_.empty()) return -1;
  auto it = std::upper_bound(pts_.begin(), pts_.end(), s);
  if (it == pts_.begin()) return 0;
  return static_cast<int32_t>((it - pts_.begin()) - 1);
}

bool FrameIndex::bracket(int64_t s, int64_t snapUs, double snapAlpha, atc_bracket& out) const {
  out = atc_bracket{};
  const int32_t n = count();
  if (n == 0) return false;
  if (s <= pts_.front()) {
    out.kind = ATC_BRACKET_BEFORE;
    out.pts_a_us = out.pts_b_us = pts_.front();
    return true;
  }
  const int32_t i = floorIndex(s);
  if (i >= n - 1) {
    out.kind = ATC_BRACKET_AFTER;
    out.a = out.b = n - 1;
    out.pts_a_us = out.pts_b_us = pts_.back();
    return true;
  }
  const int64_t pa = pts_[static_cast<size_t>(i)], pb = pts_[static_cast<size_t>(i + 1)];
  const double alpha = static_cast<double>(s - pa) / static_cast<double>(pb - pa);
  out.a = i;
  out.b = i + 1;
  out.pts_a_us = pa;
  out.pts_b_us = pb;
  if (s - pa <= snapUs || alpha <= snapAlpha) {
    out.kind = ATC_BRACKET_EXACT;
    out.b = i;
    out.pts_b_us = pa;
    return true;
  }
  if (pb - s <= snapUs || alpha >= 1 - snapAlpha) {
    out.kind = ATC_BRACKET_EXACT;
    out.a = out.b = i + 1;
    out.pts_a_us = out.pts_b_us = pb;
    return true;
  }
  out.kind = ATC_BRACKET_INTERPOLATE;
  out.alpha = alpha;
  return true;
}

}  // namespace atc
