#pragma once
// =============================================================================
//  Aurea / timeline / SpatialPath.hpp
//
//  0.4.6: BÉZIER ESPACIAL da posição (o caminho de movimento curvo, como as
//  alças da trajetória no After Effects). Cada keyframe de posição pode ter
//  tangentes de ENTRADA e SAÍDA (relativas ao ponto, px do espaço do pai).
//  Entre dois keyframes, a camada anda pela cúbica
//      P0, P0 + saída0, P1 + entrada1, P1
//  com o MESMO tempo (easing) do trecho e velocidade constante ao longo da
//  curva (parametrização por comprimento de arco). Sem tangentes, a posição é
//  a de sempre (X, Y e Z independentes) — projeto antigo abre igual.
// =============================================================================
#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/animation/Curve.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aurea {

/// Tangentes do keyframe de posição no instante `time` (quadro LOCAL da camada).
struct SpatialTangent {
    i64  time = 0;
    Vec3 in{0.0f, 0.0f, 0.0f};
    Vec3 out{0.0f, 0.0f, 0.0f};
};

[[nodiscard]] inline bool spatial_tangent_zero(const SpatialTangent& t) noexcept {
    return t.in.x == 0.0f && t.in.y == 0.0f && t.in.z == 0.0f && t.out.x == 0.0f && t.out.y == 0.0f && t.out.z == 0.0f;
}

[[nodiscard]] inline const SpatialTangent* find_spatial_tangent(const std::vector<SpatialTangent>& list, i64 time) noexcept {
    for (const SpatialTangent& t : list) if (t.time == time) return &t;
    return nullptr;
}

[[nodiscard]] inline Vec3 spatial_bezier(Vec3 p0, Vec3 c0, Vec3 c1, Vec3 p1, f32 u) noexcept {
    const f32 v = 1.0f - u;
    return p0 * (v * v * v) + c0 * (3.0f * v * v * u) + c1 * (3.0f * v * u * u) + p1 * (u * u * u);
}

/// Parâmetro da cúbica no comprimento de arco `s` (0..1): tabela de 32 trechos.
[[nodiscard]] inline f32 spatial_arc_param(Vec3 p0, Vec3 c0, Vec3 c1, Vec3 p1, f32 s) noexcept {
    constexpr int kN = 32;
    f32 len[kN + 1];
    len[0] = 0.0f;
    Vec3 prev = p0;
    for (int i = 1; i <= kN; ++i) {
        const Vec3 q = spatial_bezier(p0, c0, c1, p1, static_cast<f32>(i) / kN);
        len[i] = len[i - 1] + (q - prev).length();
        prev = q;
    }
    if (!(len[kN] > 1e-6f)) return s;
    const f32 target = std::clamp(s, 0.0f, 1.0f) * len[kN];
    int i = 1;
    while (i < kN && len[i] < target) ++i;
    const f32 a = len[i - 1], b = len[i];
    const f32 f = b > a ? (target - a) / (b - a) : 0.0f;
    return (static_cast<f32>(i - 1) + f) / kN;
}

/// A posição no quadro local `t` pela bézier espacial. Falso = sem curva aqui
/// (sem tangentes no trecho, fora dos keyframes, expressão, Segurar…): use a
/// posição de sempre.
[[nodiscard]] inline bool spatial_position(const TrackSet& tracks, const std::vector<SpatialTangent>& tangents,
                                           Vec3 fallback, f64 t, Vec3& out) noexcept {
    if (tangents.empty()) return false;
    const Track* tx = tracks.find(TrackProperty::PositionX);
    const Track* ty = tracks.find(TrackProperty::PositionY);
    if (!tx || tx->keys.size() < 2 || tx->has_expression() || (ty && ty->has_expression())) return false;
    const auto& keys = tx->keys;
    if (t <= static_cast<f64>(keys.front().time.value) || t >= static_cast<f64>(keys.back().time.value)) return false;
    usize i = 0;
    while (i + 1 < keys.size() && static_cast<f64>(keys[i + 1].time.value) <= t) ++i;
    if (i + 1 >= keys.size()) return false;
    const Keyframe& k0 = keys[i];
    const Keyframe& k1 = keys[i + 1];
    if (k0.interp == Interpolation::Hold) return false;
    const SpatialTangent* a = find_spatial_tangent(tangents, k0.time.value);
    const SpatialTangent* b = find_spatial_tangent(tangents, k1.time.value);
    const Vec3 out0 = a ? a->out : Vec3{0, 0, 0};
    const Vec3 in1 = b ? b->in : Vec3{0, 0, 0};
    if (out0.x == 0.0f && out0.y == 0.0f && out0.z == 0.0f && in1.x == 0.0f && in1.y == 0.0f && in1.z == 0.0f) return false;
    const Track* tz = tracks.find(TrackProperty::PositionZ);
    auto at = [&](FrameIndex f) {
        return Vec3{k0.time == f ? k0.value : k1.value,
                    ty ? ty->value_or(f, fallback.y) : fallback.y,
                    tz ? tz->value_or(f, fallback.z) : fallback.z};
    };
    const Vec3 p0 = at(k0.time), p1 = at(k1.time);
    const f32 span = static_cast<f32>(k1.time.value - k0.time.value);
    const f32 lin = span > 0.0f ? static_cast<f32>((t - static_cast<f64>(k0.time.value)) / span) : 0.0f;
    const f32 s = keyframe_ease(k0, std::clamp(lin, 0.0f, 1.0f));
    const Vec3 c0 = p0 + out0, c1 = p1 + in1;
    out = spatial_bezier(p0, c0, c1, p1, spatial_arc_param(p0, c0, c1, p1, s));
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

} // namespace aurea
