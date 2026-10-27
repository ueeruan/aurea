// =============================================================================
//  Aurea / rig / Ik.hpp
//
//  IK de dois ossos (braço: ombro→cotovelo→punho; perna: quadril→joelho→
//  tornozelo) com alvo e polo, e as contas de rotação que o rig usa.
//  Analítico (lei dos cossenos), sem estado: o mesmo alvo dá a mesma pose
//  na prévia e no export.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"

namespace aurea::rig {

[[nodiscard]] inline Quat conj(Quat q) noexcept { return Quat{-q.x, -q.y, -q.z, q.w}; }

[[nodiscard]] inline Vec3 rotate(Quat q, Vec3 v) noexcept {
    // v' = q v q*  (forma de Rodrigues, sem matriz)
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = u.cross(v) * 2.0f;
    return v + t * q.w + u.cross(t);
}

/// Menor rotação que leva a direção `from` a `to`.
[[nodiscard]] Quat rotation_between(Vec3 from, Vec3 to) noexcept;

/// Rotação (sem escala) da parte linear de uma matriz afim.
[[nodiscard]] Quat matrix_rotation(const Mat4& m) noexcept;

/// Inversa geral 4×4 (false = singular; `out` fica identidade).
bool inverse(const Mat4& m, Mat4& out) noexcept;

/// Translação (coluna 3) de uma matriz afim.
[[nodiscard]] inline Vec3 matrix_position(const Mat4& m) noexcept { return Vec3{m.col[3].x, m.col[3].y, m.col[3].z}; }

struct TwoBoneIk {
    Quat rootDelta = Quat::identity();   ///< rotação de MUNDO aplicada na raiz (em volta de a)
    Quat midDelta = Quat::identity();    ///< rotação de MUNDO aplicada no meio, DEPOIS da raiz
    Vec3 end{0, 0, 0};                   ///< onde a ponta fica
    bool reached = false;                ///< o alvo estava ao alcance
};

/// a = raiz, b = meio, c = ponta, no mesmo espaço do alvo e do polo.
/// `weight` 0..1 mistura com a pose de entrada (0 = nada muda).
[[nodiscard]] TwoBoneIk solve_two_bone(Vec3 a, Vec3 b, Vec3 c, Vec3 target, Vec3 pole, f32 weight) noexcept;

} // namespace aurea::rig
