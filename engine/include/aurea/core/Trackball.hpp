#pragma once
// Trackball (arcball) do gizmo 3D de girar.
//
// A esfera fica no pivô da camada com tamanho fixo na tela; os anéis vermelho,
// verde e azul são os grandes círculos perpendiculares aos eixos LOCAIS X, Y e
// Z, e o anel cinza de fora gira em volta do eixo da vista. Toda a conta vive
// aqui (Android e iOS só desenham e repassam o dedo):
//
// - Espaço da VISTA: x para a direita, y para BAIXO, z para dentro da tela
//   (os mesmos sentidos do mundo da composição). Ponto da esfera virado para a
//   câmera tem z < 0.
// - Matriz 3×3 em array de 9 floats, coluna-maior (`m[c * 3 + r]`), como Mat4.
// - F (frame): leva vetores do espaço do PAI da camada para a vista.
//   A = F·R(euler): coluna i = eixo local i visto da câmera.
// - Euler na convenção da camada (Layer.hpp / Renderer.cpp): graus,
//   R = Rz·Ry·Rx (Quat::from_euler_zyx). O resultado do arrasto volta como o
//   equivalente mais PERTO do valor anterior: nada de salto em ±180°, e voltas
//   inteiras se acumulam (720° continua 720°).
//
// O arrasto é absoluto desde o toque: a rotação acumulada na vista (Q) vem e
// volta pelo array de argumentos, e o resultado é F⁻¹·Q·F·R(início). Canal que
// não mudou volta EXATAMENTE igual ao início (anel X mexe só na Rotação X).
#include "aurea/core/Math.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::trackball {

inline constexpr i32 kNone = -1;
inline constexpr i32 kRingX = 0;      ///< anel vermelho: gira só em volta do X local
inline constexpr i32 kRingY = 1;      ///< anel verde: Y local
inline constexpr i32 kRingZ = 2;      ///< anel azul: Z local
inline constexpr i32 kRingView = 3;   ///< anel cinza de fora: em volta do eixo da vista
inline constexpr i32 kFree = 4;       ///< superfície da esfera: arcball livre
/// Raio do anel cinza = raio da esfera × isto.
inline constexpr f32 kViewRingScale = 1.25f;
/// Floats do `Engine::query_trackball`: origem (2), A (9), F (9), Euler (3).
inline constexpr u32 kQueryFloats = 23;
/// Argumentos do `drag`: F (9), Euler do início (3), Euler anterior (3),
/// Q acumulado (4: x, y, z, w — identidade no toque), parte (1), ponto do
/// toque (2), ponto anterior (2), ponto atual (2), raio (1). Pontos em px da
/// tela relativos ao centro da esfera, y para baixo.
inline constexpr u32 kDragArgs = 27;
/// Saída do `drag`: Euler (3) e o novo Q acumulado (4).
inline constexpr u32 kDragOut = 7;

namespace detail {
struct D3 { f64 x = 0, y = 0, z = 0; };
struct DQ { f64 x = 0, y = 0, z = 0, w = 1; };
inline constexpr f64 kRad = 3.14159265358979323846 / 180.0;

inline D3 add(D3 a, D3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline D3 sub(D3 a, D3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline D3 mul(D3 a, f64 s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
inline f64 dot(D3 a, D3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3 cross(D3 a, D3 b) noexcept { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline D3 unit(D3 a) noexcept {
    const f64 l = std::sqrt(dot(a, a));
    return l > 1e-300 ? mul(a, 1.0 / l) : D3{};
}
inline DQ qmul(DQ a, DQ b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline DQ qconj(DQ q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }
inline DQ qunit(DQ q) noexcept {
    const f64 l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 1e-300 ? DQ{q.x / l, q.y / l, q.z / l, q.w / l} : DQ{};
}
inline DQ axis_angle(D3 axis, f64 radians) noexcept {
    const D3 a = unit(axis);
    if (dot(a, a) == 0.0 || !std::isfinite(radians)) return {};
    const f64 s = std::sin(radians * 0.5);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5)};
}
/// Mesma fórmula de Quat::from_euler_zyx (q = qz·qy·qx), em double e graus.
inline DQ from_euler_deg(f64 dx, f64 dy, f64 dz) noexcept {
    const f64 cx = std::cos(dx * kRad * 0.5), sx = std::sin(dx * kRad * 0.5);
    const f64 cy = std::cos(dy * kRad * 0.5), sy = std::sin(dy * kRad * 0.5);
    const f64 cz = std::cos(dz * kRad * 0.5), sz = std::sin(dz * kRad * 0.5);
    return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz};
}
/// Linha-maior `m[r * 3 + c]` (só aqui dentro), como Mat4::from_quat.
inline void to_rows(DQ q, f64 m[9]) noexcept {
    const f64 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const f64 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m[0] = 1 - 2 * (yy + zz); m[1] = 2 * (xy - wz);     m[2] = 2 * (xz + wy);
    m[3] = 2 * (xy + wz);     m[4] = 1 - 2 * (xx + zz); m[5] = 2 * (yz - wx);
    m[6] = 2 * (xz - wy);     m[7] = 2 * (yz + wx);     m[8] = 1 - 2 * (xx + yy);
}
/// Quaternion de uma rotação (linha-maior), Shepperd.
inline DQ from_rows(const f64 m[9]) noexcept {
    const f64 tr = m[0] + m[4] + m[8];
    DQ q;
    if (tr > 0) {
        const f64 s = std::sqrt(tr + 1.0) * 2;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25 * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const f64 s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
        q = {0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        const f64 s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        const f64 s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s, (m[3] - m[1]) / s};
    }
    return qunit(q);
}
/// Coluna-maior (API) → rotação ortonormal (Gram-Schmidt): F que chega em
/// float não é exatamente ortogonal, e F⁻¹ = Fᵀ só vale se for.
inline DQ frame_quat(const f32* f) noexcept {
    const D3 c0 = unit(D3{f[0], f[1], f[2]});
    const D3 c1 = unit(sub(D3{f[3], f[4], f[5]}, mul(c0, dot(c0, D3{f[3], f[4], f[5]}))));
    const D3 c2 = cross(c0, c1);
    if (dot(c0, c0) == 0.0 || dot(c1, c1) == 0.0) return {};
    const f64 rows[9]{c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z};
    return from_rows(rows);
}
inline f64 near_turn(f64 v, f64 reference) noexcept { return v + 360.0 * std::round((reference - v) / 360.0); }
inline D3 rotate(DQ q, D3 v) noexcept {
    f64 m[9];
    to_rows(q, m);
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z};
}
/// Euler ZYX (graus) de uma rotação, o equivalente mais perto de `ref`.
inline D3 euler_near(DQ q, D3 ref) noexcept {
    f64 m[9];
    to_rows(qunit(q), m);
    // R = Rz·Ry·Rx: r20 = −sen y, r21 = sen x cos y, r22 = cos x cos y,
    // r10 = cos y sen z, r00 = cos y cos z.
    const f64 sy = std::clamp(-m[6], -1.0, 1.0);
    D3 a, b;
    if (std::fabs(sy) < 0.9999999) {
        const f64 x = std::atan2(m[7], m[8]) / kRad, y = std::asin(sy) / kRad, z = std::atan2(m[3], m[0]) / kRad;
        a = {x, y, z};
        b = {x + 180.0, 180.0 - y, z + 180.0};   // a outra leitura da mesma rotação
    } else {
        // Trava do cardã (Y = ±90°): só X−Z (ou X+Z) é definido. Mantém o X de
        // antes e põe a diferença no Z — sem pulo nos dois.
        const f64 x = ref.x * kRad;
        f64 z;
        if (sy > 0) z = x - std::atan2(m[1], m[2]);
        else z = std::atan2(-m[1], -m[2]) - x;
        a = b = {ref.x, sy > 0 ? 90.0 : -90.0, z / kRad};
    }
    auto wrap = [&](D3 e) { return D3{near_turn(e.x, ref.x), near_turn(e.y, ref.y), near_turn(e.z, ref.z)}; };
    a = wrap(a);
    b = wrap(b);
    auto cost = [&](D3 e) { return std::fabs(e.x - ref.x) + std::fabs(e.y - ref.y) + std::fabs(e.z - ref.z); };
    return cost(b) < cost(a) ? b : a;
}
/// Ponto da esfera virtual (raio 1) sob (x, y) px relativos ao centro: dentro
/// do disco, a calota virada para a câmera; fora, a borda (gira na vista).
inline D3 sphere(f64 x, f64 y, f64 radius) noexcept {
    const f64 u = x / radius, v = y / radius, d2 = u * u + v * v;
    if (d2 <= 1.0) return {u, v, -std::sqrt(1.0 - d2)};
    return unit(D3{u, v, 0.0});
}
inline D3 column(const f32* m, u32 c) noexcept { return {m[c * 3], m[c * 3 + 1], m[c * 3 + 2]}; }
/// Ponto do anel `ring` (eixo = coluna `ring` de A) mais perto do toque e a
/// direção, NA TELA, para onde ele anda quando o giro em volta do eixo cresce.
inline bool ring_tangent(const f32* a, i32 ring, f64 gx, f64 gy, f64 radius, f64& tx, f64& ty) noexcept {
    const D3 axis = unit(column(a, static_cast<u32>(ring)));
    const D3 u = unit(column(a, static_cast<u32>((ring + 1) % 3)));
    const D3 v = unit(column(a, static_cast<u32>((ring + 2) % 3)));
    f64 best = 1e300;
    D3 grab{};
    for (u32 i = 0; i < 144; ++i) {
        const f64 t = static_cast<f64>(i) * (2.0 * 3.14159265358979323846 / 144.0);
        const D3 p = mul(add(mul(u, std::cos(t)), mul(v, std::sin(t))), radius);
        // O lado de trás do anel (z > 0) só ganha se nada da frente estiver perto.
        const f64 d = std::hypot(p.x - gx, p.y - gy) + (p.z > 0.05 * radius ? radius * 0.5 : 0.0);
        if (d < best) { best = d; grab = p; }
    }
    const D3 t = cross(axis, grab);
    const f64 l = std::hypot(t.x, t.y);
    if (!(l > 1e-6 * radius)) return false;
    tx = t.x / l;
    ty = t.y / l;
    return true;
}
} // namespace detail

/// Euler ZYX em graus (como a camada guarda) → quaternion, igual ao Renderer.
[[nodiscard]] inline Quat quat_from_euler_deg(Vec3 deg) noexcept {
    const detail::DQ q = detail::from_euler_deg(deg.x, deg.y, deg.z);
    return Quat{static_cast<f32>(q.x), static_cast<f32>(q.y), static_cast<f32>(q.z), static_cast<f32>(q.w)};
}

/// Quaternion → Euler ZYX em graus: o equivalente mais perto de `reference`
/// (sem salto em ±180°; voltas inteiras preservadas).
[[nodiscard]] inline Vec3 euler_deg_from_quat(Quat q, Vec3 reference) noexcept {
    const detail::D3 e = detail::euler_near(detail::DQ{q.x, q.y, q.z, q.w}, detail::D3{reference.x, reference.y, reference.z});
    return Vec3{static_cast<f32>(e.x), static_cast<f32>(e.y), static_cast<f32>(e.z)};
}

/// O mesmo ângulo, levado para a volta mais perto de `reference` (por canal).
[[nodiscard]] inline Vec3 unwrap_near(Vec3 deg, Vec3 reference) noexcept {
    return Vec3{static_cast<f32>(detail::near_turn(deg.x, reference.x)), static_cast<f32>(detail::near_turn(deg.y, reference.y)),
                static_cast<f32>(detail::near_turn(deg.z, reference.z))};
}

/// Ponto da esfera virtual sob o dedo (px relativos ao centro, y para baixo).
[[nodiscard]] inline Vec3 sphere_point(f32 x, f32 y, f32 radius) noexcept {
    if (!(radius > 0.0f)) return Vec3{0, 0, -1};
    const detail::D3 p = detail::sphere(x, y, radius);
    return Vec3{static_cast<f32>(p.x), static_cast<f32>(p.y), static_cast<f32>(p.z)};
}

/// F a partir da vista no pivô (`screenX`/`screenY`: vetor do mundo que anda
/// 1 px na tela para a direita / para baixo) e das colunas lineares do pai
/// (escala e espelho saem: fica só a rotação).
inline bool frame_from(Vec3 screenX, Vec3 screenY, const Vec3 parent[3], f32 outF[9]) noexcept {
    using namespace detail;
    if (!outF || !parent) return false;
    const D3 cx = unit(D3{screenX.x, screenX.y, screenX.z});
    const D3 sy{screenY.x, screenY.y, screenY.z};
    const D3 cy = unit(sub(sy, mul(cx, dot(cx, sy))));
    const D3 cz = cross(cx, cy);
    const D3 p0 = unit(D3{parent[0].x, parent[0].y, parent[0].z});
    const D3 q1{parent[1].x, parent[1].y, parent[1].z};
    const D3 p1 = unit(sub(q1, mul(p0, dot(p0, q1))));
    const D3 p2 = cross(p0, p1);
    if (dot(cx, cx) == 0.0 || dot(cy, cy) == 0.0 || dot(p0, p0) == 0.0 || dot(p1, p1) == 0.0) return false;
    const D3 c[3]{cx, cy, cz}, p[3]{p0, p1, p2};
    for (u32 col = 0; col < 3; ++col)
        for (u32 row = 0; row < 3; ++row) outF[col * 3 + row] = static_cast<f32>(dot(c[row], p[col]));
    return true;
}

/// A = F·R(euler): coluna i = eixo local i na vista (desenho e toque dos anéis).
inline void axes(const f32 F[9], Vec3 eulerDeg, f32 outA[9]) noexcept {
    using namespace detail;
    const DQ q = qmul(frame_quat(F), from_euler_deg(eulerDeg.x, eulerDeg.y, eulerDeg.z));
    f64 m[9];
    to_rows(q, m);
    for (u32 col = 0; col < 3; ++col)
        for (u32 row = 0; row < 3; ++row) outA[col * 3 + row] = static_cast<f32>(m[row * 3 + col]);
}

/// Parte sob o dedo (px relativos ao centro): anéis coloridos primeiro (o mais
/// perto dentro de `tolerance`, só a metade da frente), depois o anel cinza,
/// depois a superfície da esfera; fora de tudo, kNone.
[[nodiscard]] inline i32 hit_test(const f32 A[9], f32 x, f32 y, f32 radius, f32 tolerance) noexcept {
    using namespace detail;
    if (!A || !(radius > 0.0f) || !std::isfinite(x) || !std::isfinite(y)) return kNone;
    i32 part = kNone;
    f64 best = tolerance;
    for (i32 ring = 0; ring < 3; ++ring) {
        const D3 u = unit(column(A, static_cast<u32>((ring + 1) % 3)));
        const D3 v = unit(column(A, static_cast<u32>((ring + 2) % 3)));
        D3 prev{};
        bool prevFront = false;
        for (u32 i = 0; i <= 96; ++i) {
            const f64 t = static_cast<f64>(i) * (2.0 * 3.14159265358979323846 / 96.0);
            const D3 p = mul(add(mul(u, std::cos(t)), mul(v, std::sin(t))), radius);
            const bool front = p.z <= 0.02 * radius;
            if (i > 0 && front && prevFront) {
                // Distância do dedo ao segmento prev→p na tela.
                const f64 ex = p.x - prev.x, ey = p.y - prev.y, l2 = ex * ex + ey * ey;
                const f64 s = l2 > 0 ? std::clamp(((x - prev.x) * ex + (y - prev.y) * ey) / l2, 0.0, 1.0) : 0.0;
                const f64 d = std::hypot(x - (prev.x + ex * s), y - (prev.y + ey * s));
                if (d < best) { best = d; part = ring; }
            }
            prev = p;
            prevFront = front;
        }
    }
    const f64 dist = std::hypot(static_cast<f64>(x), static_cast<f64>(y));
    const f64 view = std::fabs(dist - static_cast<f64>(radius) * kViewRingScale);
    if (view < best) { best = view; part = kRingView; }
    if (part != kNone) return part;
    return dist < radius ? kFree : kNone;
}

/// Um passo do arrasto (layout em kDragArgs / kDragOut). Falso = argumentos
/// inválidos (nada a gravar).
inline bool drag(const f32* in, f32* out) noexcept {
    using namespace detail;
    if (!in || !out) return false;
    for (u32 i = 0; i < kDragArgs; ++i) if (!std::isfinite(in[i])) return false;
    const f32* F = in;
    const D3 start{in[9], in[10], in[11]}, prev{in[12], in[13], in[14]};
    DQ acc = qunit(DQ{in[15], in[16], in[17], in[18]});
    const i32 part = static_cast<i32>(std::lround(in[19]));
    const f64 gx = in[20], gy = in[21], px = in[22], py = in[23], x = in[24], y = in[25], radius = in[26];
    if (!(radius > 0.0) || part < kRingX || part > kFree) return false;
    const DQ frame = frame_quat(F);
    const DQ qStart = from_euler_deg(start.x, start.y, start.z);
    DQ step{};
    if (part == kFree) {
        const D3 a = sphere(px, py, radius), b = sphere(x, y, radius);
        const D3 axis = cross(a, b);
        const f64 s = std::sqrt(dot(axis, axis));
        if (s > 1e-12) step = axis_angle(axis, std::atan2(s, dot(a, b)));
    } else if (part == kRingView) {
        // Ângulo varrido em volta do centro (y para baixo: horário na tela = giro
        // positivo em volta do z que entra na tela).
        const f64 c = px * y - py * x, d = px * x + py * y;
        if (std::hypot(px, py) > 1e-6 && std::hypot(x, y) > 1e-6) step = axis_angle(D3{0, 0, 1}, std::atan2(c, d));
    } else {
        // Anel colorido: o eixo local no INÍCIO (girar em volta dele não o
        // muda) e a tangente do anel no ponto tocado; o dedo andando ao longo
        // dela, dividido pelo raio, é o ângulo.
        f32 a0[9];
        f64 m[9];
        to_rows(qmul(frame, qStart), m);
        for (u32 col = 0; col < 3; ++col)
            for (u32 row = 0; row < 3; ++row) a0[col * 3 + row] = static_cast<f32>(m[row * 3 + col]);
        const u32 p = static_cast<u32>(part);
        const D3 axis{m[p], m[3 + p], m[6 + p]};
        f64 tx = 0, ty = 0;
        if (ring_tangent(a0, part, gx, gy, radius, tx, ty)) {
            // Absoluto desde o toque (a tangente não muda): nada acumula erro.
            acc = axis_angle(axis, ((x - gx) * tx + (y - gy) * ty) / radius);
            step = DQ{};
        } else {
            // Tangente some na tela (anel de perfil na ponta): ângulo varrido.
            const f64 c = px * y - py * x, d = px * x + py * y;
            step = axis_angle(axis, std::atan2(c, d) * (axis.z >= 0 ? 1.0 : -1.0));
        }
    }
    acc = qunit(qmul(step, acc));
    // R' = F⁻¹·Q·F·R(início): a rotação da vista volta para o espaço do pai.
    const DQ result = qmul(qmul(qmul(qconj(frame), acc), frame), qStart);
    D3 e = euler_near(result, prev);
    // Canal que não mudou fica IGUAL ao início (sem chave espúria nos outros).
    if (std::fabs(e.x - start.x) < 1e-4) e.x = start.x;
    if (std::fabs(e.y - start.y) < 1e-4) e.y = start.y;
    if (std::fabs(e.z - start.z) < 1e-4) e.z = start.z;
    out[0] = static_cast<f32>(e.x);
    out[1] = static_cast<f32>(e.y);
    out[2] = static_cast<f32>(e.z);
    out[3] = static_cast<f32>(acc.x);
    out[4] = static_cast<f32>(acc.y);
    out[5] = static_cast<f32>(acc.z);
    out[6] = static_cast<f32>(acc.w);
    for (u32 i = 0; i < kDragOut; ++i) if (!std::isfinite(out[i])) return false;
    return true;
}

} // namespace aurea::trackball
