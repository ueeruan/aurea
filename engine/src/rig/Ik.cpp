// IK de dois ossos e contas de rotação (ver rig/Ik.hpp).
#include "aurea/rig/Ik.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::rig {

Quat rotation_between(Vec3 from, Vec3 to) noexcept {
    const Vec3 f = from.normalized(), t = to.normalized();
    if (f.length_sq() < 0.5f || t.length_sq() < 0.5f) return Quat::identity();
    const f32 d = std::clamp(f.dot(t), -1.0f, 1.0f);
    if (d > 0.999999f) return Quat::identity();
    if (d < -0.999999f) {
        // Oposto: qualquer eixo perpendicular serve.
        Vec3 axis = Vec3{1, 0, 0}.cross(f);
        if (axis.length_sq() < 1e-6f) axis = Vec3{0, 1, 0}.cross(f);
        return Quat::from_axis_angle(axis.normalized(), 3.14159265358979323846f);
    }
    const Vec3 c = f.cross(t);
    return Quat{c.x, c.y, c.z, 1.0f + d}.normalized();
}

Quat matrix_rotation(const Mat4& m) noexcept {
    Vec3 x{m.col[0].x, m.col[0].y, m.col[0].z};
    Vec3 y{m.col[1].x, m.col[1].y, m.col[1].z};
    Vec3 z{m.col[2].x, m.col[2].y, m.col[2].z};
    x = x.normalized();
    // Gram-Schmidt: tira cisalhamento e escala não uniforme pequenos.
    y = (y - x * x.dot(y)).normalized();
    z = x.cross(y);   // sempre rotação própria (espelho fica na escala)
    const f32 tr = x.x + y.y + z.z;
    Quat q;
    if (tr > 0.0f) {
        const f32 s = std::sqrt(tr + 1.0f) * 2.0f;
        q = Quat{(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
    } else if (x.x > y.y && x.x > z.z) {
        const f32 s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
        q = Quat{0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const f32 s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
        q = Quat{(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const f32 s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
        q = Quat{(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
    }
    const Quat n = q.normalized();
    return std::isfinite(n.x) && std::isfinite(n.w) ? n : Quat::identity();
}

bool inverse(const Mat4& mat, Mat4& out) noexcept {
    const f32* m = &mat.col[0].x;
    f32 inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    out = Mat4::identity();
    if (!std::isfinite(det) || std::fabs(det) < 1e-20f) return false;
    const f32 k = 1.0f / det;
    f32* o = &out.col[0].x;
    for (int i = 0; i < 16; ++i) o[i] = inv[i] * k;
    return true;
}

TwoBoneIk solve_two_bone(Vec3 a, Vec3 b, Vec3 c, Vec3 target, Vec3 pole, f32 weight) noexcept {
    TwoBoneIk r;
    r.end = c;
    const f32 w = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
    const f32 lab = (b - a).length(), lbc = (c - b).length();
    if (w <= 0.0f || lab < 1e-6f || lbc < 1e-6f) return r;
    Vec3 toT = target - a;
    f32 dist = toT.length();
    if (!std::isfinite(dist)) return r;
    const f32 reach = lab + lbc;
    const f32 minReach = std::fabs(lab - lbc);
    r.reached = dist <= reach * 0.9999f && dist >= minReach;
    const f32 lat = std::clamp(dist, minReach + 1e-5f * reach, reach * 0.9999f);
    Vec3 dir = dist > 1e-6f ? toT / dist : (c - a).normalized();
    if (dir.length_sq() < 0.5f) return r;
    // Direção da dobra: o polo projetado no plano perpendicular a `dir`; sem
    // polo útil, a dobra de agora (o meio fora da reta raiz→ponta).
    auto perpendicular = [&](Vec3 v) { return v - dir * dir.dot(v); };
    Vec3 bend = perpendicular(pole - a);
    if (bend.length_sq() < 1e-10f) bend = perpendicular(b - a);
    if (bend.length_sq() < 1e-10f) {
        bend = dir.cross(Vec3{0, 1, 0});
        if (bend.length_sq() < 1e-8f) bend = dir.cross(Vec3{1, 0, 0});
    }
    bend = bend.normalized();
    // Lei dos cossenos: quanto do braço vai ao longo de `dir` até o cotovelo.
    const f32 x = (lab * lab - lbc * lbc + lat * lat) / (2.0f * lat);
    const f32 h = std::sqrt(std::max(0.0f, lab * lab - x * x));
    const Vec3 b2 = a + dir * x + bend * h;
    const Vec3 c2 = a + dir * lat;
    // Raiz: leva o meio até b2 (menor arco: guarda o giro do osso).
    Quat rootQ = rotation_between(b - a, b2 - a);
    // Meio: depois da raiz, leva a ponta até c2.
    const Vec3 c1 = a + rotate(rootQ, c - a);
    Quat midQ = rotation_between(c1 - b2, c2 - b2);
    if (w < 1.0f) {
        rootQ = Quat::slerp(Quat::identity(), rootQ, w).normalized();
        midQ = Quat::slerp(Quat::identity(), midQ, w).normalized();
    }
    r.rootDelta = rootQ;
    r.midDelta = midQ;
    const Vec3 bNew = a + rotate(rootQ, b - a);
    r.end = bNew + rotate(midQ, rotate(rootQ, c - b));
    return r;
}

} // namespace aurea::rig
