// Casca C da API. Nenhuma excecao atravessa a fronteira: alocacao usa
// nothrow e entrada invalida devolve neutro.
#include <cstring>
#include <new>
#include <vector>

#include "timecore_internal.hpp"

using namespace atc;

namespace {

std::vector<Keyframe> fromC(const atc_keyframe* k, int32_t n) {
  std::vector<Keyframe> out;
  if (!k || n <= 0) return out;
  out.reserve(static_cast<size_t>(n));
  for (int32_t i = 0; i < n; ++i) {
    Keyframe x;
    x.t = k[i].time_us;
    x.v = k[i].value;
    x.e = Ease::fromC(k[i]);
    out.push_back(x);
  }
  return out;
}

int32_t toC(const std::vector<Keyframe>& v, double subtract, atc_keyframe* out,
            int32_t capacity) {
  const int32_t n = static_cast<int32_t>(v.size());
  if (!out || n > capacity) return -n;
  for (int32_t i = 0; i < n; ++i) {
    std::memset(&out[i], 0, sizeof(atc_keyframe));
    out[i].time_us = v[static_cast<size_t>(i)].t;
    out[i].value = v[static_cast<size_t>(i)].v - subtract;
    v[static_cast<size_t>(i)].e.toC(out[i]);
  }
  return n;
}

Layer layerOf(const atc_layer* l) { return l ? Layer::fromC(*l) : Layer(); }

}  // namespace

extern "C" {

int32_t atc_version(void) { return ATC_VERSION; }
int32_t atc_sizeof_keyframe(void) { return sizeof(atc_keyframe); }
int32_t atc_sizeof_layer(void) { return sizeof(atc_layer); }
int32_t atc_sizeof_bracket(void) { return sizeof(atc_bracket); }

double atc_ease_transform(const atc_keyframe* ease, double t) {
  return ease ? Ease::fromC(*ease).transform(t) : t;
}

double atc_ease_derivative(const atc_keyframe* ease, double t) {
  return ease ? Ease::fromC(*ease).derivative(t) : 1;
}

atc_curve* atc_curve_create(double base, const atc_keyframe* keyframes, int32_t count,
                            int32_t loop_mode, int32_t loop_when, int32_t loop_count) {
  const LoopMode mode = (loop_mode >= 0 && loop_mode <= 4) ? static_cast<LoopMode>(loop_mode)
                                                           : LoopMode::none;
  const LoopWhen when = (loop_when >= 0 && loop_when <= 2) ? static_cast<LoopWhen>(loop_when)
                                                           : LoopWhen::after;
  Curve* c = new (std::nothrow) Curve(base, fromC(keyframes, count), mode, when, loop_count);
  return reinterpret_cast<atc_curve*>(c);
}

void atc_curve_destroy(void* curve) { delete static_cast<Curve*>(curve); }

int32_t atc_curve_count(const atc_curve* curve) {
  return curve ? static_cast<int32_t>(reinterpret_cast<const Curve*>(curve)->keyframes().size())
               : 0;
}

double atc_curve_value(const atc_curve* curve, int64_t t_us, int32_t extrap) {
  if (!curve) return 0;
  return reinterpret_cast<const Curve*>(curve)->value(
      t_us, extrap == ATC_EXTRAP_LINEAR ? Extrap::linear : Extrap::hold);
}

double atc_curve_slope(const atc_curve* curve, int64_t t_us, int32_t extrap) {
  if (!curve) return 0;
  return reinterpret_cast<const Curve*>(curve)->slope(
      t_us, extrap == ATC_EXTRAP_LINEAR ? Extrap::linear : Extrap::hold);
}

void atc_curve_values(const atc_curve* curve, const int64_t* t_us, int32_t n, int32_t extrap,
                      double* out) {
  if (!t_us || !out || n <= 0) return;
  for (int32_t i = 0; i < n; ++i) out[i] = atc_curve_value(curve, t_us[i], extrap);
}

int32_t atc_curve_range(const atc_curve* curve, double* lo, double* hi) {
  if (!curve || !lo || !hi) return 0;
  const Curve* c = reinterpret_cast<const Curve*>(curve);
  c->range(*lo, *hi);
  return c->keyframes().empty() ? 0 : 1;
}

int64_t atc_layer_span_us(const atc_layer* layer) { return layerSpan(layerOf(layer)); }

int64_t atc_layer_source_us(const atc_layer* layer, int64_t local_us) {
  return layerSource(layerOf(layer), local_us);
}

int64_t atc_layer_absolute_source_us(const atc_layer* layer, int64_t local_us) {
  return layerAbsoluteSource(layerOf(layer), local_us);
}

double atc_layer_rate(const atc_layer* layer, int64_t local_us) {
  return layerRate(layerOf(layer), local_us);
}

void atc_layer_absolute_many(const atc_layer* layer, const int64_t* local_us, int32_t n,
                             int64_t* out) {
  if (!local_us || !out || n <= 0) return;
  const Layer l = layerOf(layer);
  for (int32_t i = 0; i < n; ++i) out[i] = layerAbsoluteSource(l, local_us[i]);
}

int32_t atc_layer_slice(const atc_layer* layer, int64_t from_us, int64_t to_us,
                        int64_t tolerance_us, atc_keyframe* out, int32_t capacity,
                        double* min_value) {
  std::vector<Keyframe> v;
  double minV = 0;
  sliceLayer(layerOf(layer), from_us, to_us, tolerance_us, v, minV);
  const int32_t n = toC(v, minV, out, capacity);
  if (n >= 0 && min_value) *min_value = minV;
  return n;
}

int32_t atc_speed_to_value(const atc_keyframe* speed, int32_t count, double start_value,
                           int64_t tolerance_us, atc_keyframe* out, int32_t capacity) {
  std::vector<Keyframe> v;
  speedToValue(fromC(speed, count), start_value, tolerance_us, v);
  return toC(v, 0, out, capacity);
}

atc_frame_index* atc_frame_index_create(const int64_t* pts_us, int32_t n,
                                        int64_t last_duration_us) {
  return reinterpret_cast<atc_frame_index*>(
      new (std::nothrow) FrameIndex(pts_us, n, last_duration_us));
}

void atc_frame_index_destroy(void* index) { delete static_cast<FrameIndex*>(index); }

int32_t atc_frame_index_count(const atc_frame_index* index) {
  return index ? reinterpret_cast<const FrameIndex*>(index)->count() : 0;
}

int64_t atc_frame_index_pts(const atc_frame_index* index, int32_t i) {
  if (!index) return 0;
  const FrameIndex* f = reinterpret_cast<const FrameIndex*>(index);
  if (i < 0 || i >= f->count()) return 0;
  return f->pts(i);
}

int32_t atc_frame_index_floor(const atc_frame_index* index, int64_t source_us) {
  return index ? reinterpret_cast<const FrameIndex*>(index)->floorIndex(source_us) : -1;
}

int32_t atc_frame_bracket(const atc_frame_index* index, int64_t source_us, int64_t snap_us,
                          double snap_alpha, atc_bracket* out) {
  if (!index || !out) return -1;
  return reinterpret_cast<const FrameIndex*>(index)->bracket(source_us, snap_us, snap_alpha,
                                                             *out)
             ? 0
             : -1;
}

}  // extern "C"
