#pragma once
#include "aurea/core/Math.hpp"
#include <algorithm>
#include <cmath>

namespace aurea {
/// Captured once on pointer-count changes. Screen deltas use composition pixels.
/// Keeping the basis fixed prevents queued transforms from feeding back into drag.
[[nodiscard]] inline Vec3 preview_gesture_value(const f32* basis, f32 dx, f32 dy, bool rotate) noexcept {
    if (!basis || !std::isfinite(dx) || !std::isfinite(dy)) return {};
    if (rotate) return Vec3{basis[9] - dy * basis[12], basis[10] + dx * basis[12], basis[11]};
    return Vec3{basis[0] + dx * basis[3] + dy * basis[6],
                basis[1] + dx * basis[4] + dy * basis[7],
                basis[2] + dx * basis[5] + dy * basis[8]};
}
/// Uniform pinch scaling preserves signs, aspect ratio and (for 3D) depth.
/// Existing scales outside the gesture range must not jump on first contact:
/// include factor 1 in the interval, and only allow motion toward the range.
[[nodiscard]] inline f32 clamp_pinch_factor(f32 desired, f32 x, f32 y, f32 z, bool threeD) noexcept {
    if (!std::isfinite(desired) || desired <= 0) return 1.f;
    const f32 axes[]{x, y, z};
    f64 low = 0.0, high = 1e6;
    bool nonzero = false;
    for (u32 i = 0; i < (threeD ? 3u : 2u); ++i) {
        if (!std::isfinite(axes[i])) return 1.f;
        const f64 magnitude = std::fabs(static_cast<f64>(axes[i]));
        if (magnitude == 0) continue; // A flat/zero axis stays zero, never divides by zero.
        nonzero = true;
        low = std::max(low, std::min(1.0, .001 / magnitude));
        high = std::min(high, std::max(1.0, 100.0 / magnitude));
    }
    return nonzero ? static_cast<f32>(std::clamp(static_cast<f64>(desired), low, high)) : 1.f;
}

/// Conteúdo 3D (tudo que não é câmera/luz) GRAVA a escala Z relativa à X: o
/// mundo usa z × x (Renderer.cpp, layer_matrix_3d_frac). Câmera e luz gravam Z
/// absoluto.
[[nodiscard]] inline bool scale_z_follows_x(LayerKind kind) noexcept {
    return kind != LayerKind::Camera && kind != LayerKind::Light;
}

/// Modos de `gesture_scale_3d`.
inline constexpr i32 kGestureScaleUniform = 3;   ///< pinça / gizmo uniforme: tudo × fator
inline constexpr i32 kGestureScaleFit = 4;       ///< ajustar/preencher: |fator| em X/Y (sinal mantido)

/// Escala 3D de um gesto já no formato GRAVADO, para o volume nunca esticar:
///  - eixo 0..2: só aquele eixo × `factor`. Em conteúdo, mexer em X deixaria
///    a profundidade ir junto (z × x), então Z gravado é dividido pelo mesmo
///    fator — a profundidade efetiva fica onde estava;
///  - kGestureScaleUniform: X, Y e a profundidade EFETIVA × `factor` (em
///    conteúdo o Z gravado fica: ele já acompanha X). Antes as UIs também
///    multiplicavam Z, e a profundidade crescia com fator² — "o zoom estica";
///  - kGestureScaleFit: X/Y = ±|factor|; a profundidade efetiva segue X.
/// Fator negativo espelha (régua de escala que passa do zero); zero ou NaN
/// devolve a escala de partida.
[[nodiscard]] inline Vec3 gesture_scale_3d(Vec3 start, i32 axis, f32 factor, bool depthFollowsWidth) noexcept {
    if (!std::isfinite(factor) || factor == 0.0f) return start;
    auto sgn = [](f32 v) { return v < 0.0f ? -1.0f : 1.0f; };
    Vec3 s = start;
    switch (axis) {
        case 0:
            s.x = start.x * factor;
            if (depthFollowsWidth) s.z = start.z / factor;
            break;
        case 1: s.y = start.y * factor; break;
        case 2: s.z = start.z * factor; break;
        case kGestureScaleUniform:
            s.x = start.x * factor;
            s.y = start.y * factor;
            if (!depthFollowsWidth) s.z = start.z * factor;
            break;
        case kGestureScaleFit:
            s.x = sgn(start.x) * std::fabs(factor);
            s.y = sgn(start.y) * std::fabs(factor);
            if (!depthFollowsWidth) s.z = sgn(start.z) * std::fabs(factor);
            break;
        default: break;
    }
    return s;
}

/// A mesma pinça, com o limite certo: em conteúdo 3D o Z gravado não muda,
/// então só X/Y (e o Z de câmera/luz) entram no limite de `clamp_pinch_factor`.
[[nodiscard]] inline f32 clamp_pinch_factor_3d(f32 desired, Vec3 start, bool depthFollowsWidth) noexcept {
    return clamp_pinch_factor(desired, start.x, start.y, start.z, !depthFollowsWidth);
}
}
