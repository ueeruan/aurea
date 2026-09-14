// Classes C++ do nucleo temporal. A API C (timecore.h) e so uma casca
// sobre isto; o motor do Android usa estas classes direto.
#pragma once

#include <cstdint>
#include <vector>

#include "timecore.h"

namespace atc {

enum class EaseType : int32_t {
  cubicBezier = 0,
  bounce = 1,
  elastic = 2,
  cyclic = 3,
  random = 4,
  steps = 5,
  elasticSteps = 6,
  spring = 7,
  hold = 8,
};

struct Ease {
  EaseType type = EaseType::cubicBezier;
  int32_t count = 4;
  double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
  double smooth = 1.0, intensity = 0.5, response = 0.55, damping = 0.825,
         velocity = 0;

  static Ease fromC(const atc_keyframe& k);
  void toC(atc_keyframe& k) const;

  bool isLinear() const {
    return type == EaseType::cubicBezier && x1 == 0 && y1 == 0 && x2 == 1 &&
           y2 == 1;
  }

  // Progresso 0..1 -> progresso com easing. Mesma formula do Dart, exceto
  // a bezier, que aqui e resolvida com precisao (o Cubic do Flutter para
  // com erro de 1e-3 em x).
  double transform(double t) const;
  // d(transform)/dt.
  double derivative(double t) const;
};

// Parametro u da bezier com x(u) = x (x1, x2 ja em 0..1).
double bezierParamForX(double x1, double x2, double x);
inline double bezier1D(double p1, double p2, double u) {
  const double v = 1 - u;
  return 3 * v * v * u * p1 + 3 * v * u * u * p2 + u * u * u;
}
inline double bezier1DDerivative(double p1, double p2, double u) {
  const double v = 1 - u;
  return 3 * v * v * p1 + 6 * v * u * (p2 - p1) + 3 * u * u * (1 - p2);
}

struct Keyframe {
  int64_t t = 0;
  double v = 0;
  Ease e;
};

enum class LoopMode : int32_t { none = 0, cycle = 1, pingPong = 2, offset = 3, continueValue = 4 };
enum class LoopWhen : int32_t { after = 0, before = 1, both = 2 };
enum class Extrap : int32_t { hold = 0, linear = 1 };

class Curve {
 public:
  Curve(double base, std::vector<Keyframe> keyframes, LoopMode mode,
        LoopWhen when, int32_t loopCount);

  double value(int64_t t, Extrap mode) const;
  double slope(int64_t t, Extrap mode) const;  // por segundo
  void range(double& lo, double& hi) const;

  const std::vector<Keyframe>& keyframes() const { return k_; }
  double base() const { return base_; }
  bool loopActive() const { return mode_ != LoopMode::none; }

  // Para o recorte: em que trecho [i, i+1] o instante cai (-1 fora).
  int segmentIndex(int64_t t) const;

 private:
  double raw(int64_t t) const;       // _rawAt do Dart
  double rawValue(int64_t t) const;  // _rawValueAt do Dart
  bool loopsAfter() const {
    return loopActive() && (when_ == LoopWhen::after || when_ == LoopWhen::both);
  }
  bool loopsBefore() const {
    return loopActive() && (when_ == LoopWhen::before || when_ == LoopWhen::both);
  }
  void loopRange(int64_t t, int64_t& from, int64_t& to) const;
  bool loopRemap(int64_t t, int64_t from, int64_t to, int64_t& time,
                 double& cycles, bool& backwards) const;
  double segmentSlope(int i, int64_t t) const;
  double extrapSlope(int64_t t, Extrap mode) const;

  double base_;
  std::vector<Keyframe> k_;
  LoopMode mode_;
  LoopWhen when_;
  int32_t loopCount_;
};

// Faixa de um trecho [a, b] com easing (extremos reais, inclui overshoot).
void segmentRange(const Keyframe& a, const Keyframe& b, double& lo, double& hi);

struct Layer {
  int64_t sourceOffset = 0;
  int64_t duration = 0;
  double speed = 1;
  bool reverse = false;
  const Curve* curve = nullptr;

  static Layer fromC(const atc_layer& l);
};

int64_t layerSpan(const Layer& l);
int64_t layerSource(const Layer& l, int64_t local);
int64_t layerAbsoluteSource(const Layer& l, int64_t local);
double layerRate(const Layer& l, int64_t local);

// Recorte do mapeamento (ver atc_layer_slice). Saida relativa a `from`,
// ainda COM o minimo; *minValue recebe o minimo real.
void sliceLayer(const Layer& l, int64_t from, int64_t to, int64_t toleranceUs,
                std::vector<Keyframe>& out, double& minValue);

// Velocidade -> tempo (ver atc_speed_to_value).
void speedToValue(const std::vector<Keyframe>& speed, double startValue,
                  int64_t toleranceUs, std::vector<Keyframe>& out);

class FrameIndex {
 public:
  FrameIndex(const int64_t* pts, int32_t n, int64_t lastDuration);
  int32_t count() const { return static_cast<int32_t>(pts_.size()); }
  int64_t pts(int32_t i) const { return pts_[static_cast<size_t>(i)]; }
  int32_t floorIndex(int64_t s) const;
  bool bracket(int64_t s, int64_t snapUs, double snapAlpha, atc_bracket& out) const;
  int64_t lastDuration() const { return lastDuration_; }

 private:
  std::vector<int64_t> pts_;
  int64_t lastDuration_;
};

}  // namespace atc
