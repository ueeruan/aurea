// =============================================================================
//  Aurea / vector / PathEdit.cpp
//
//  Omega conteúdo (área 06, P1-8): regras de edição de ponto (ver PathEdit.hpp).
//  As contas são as que o palco do Android e o do iOS faziam cada um à sua
//  maneira (inserir por De Casteljau, canto ↔ curva pelos vizinhos, alça
//  espelhada), agora com as travas de ângulo e de comprimento da referência.
// =============================================================================
#include "aurea/vector/PathEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace aurea::vector::pathedit {
namespace {
Vec2 lerp(Vec2 a, Vec2 b, f32 t) noexcept { return a + (b - a) * t; }

Vec2 cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, f32 t) noexcept {
    const f32 u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}
} // namespace

u32 segment_count(const BezierPath& p) noexcept {
    const u32 n = static_cast<u32>(p.v.size());
    if (n < 2) return 0;
    return p.closed ? n : n - 1;
}

bool point_on_segment(const BezierPath& p, u32 segment, f32 t, Vec2& out) noexcept {
    if (segment >= segment_count(p)) return false;
    const usize n = p.v.size();
    const BezierVertex& a = p.v[segment];
    const BezierVertex& b = p.v[(segment + 1) % n];
    out = cubic(a.p, a.p + a.out, b.p + b.in, b.p, std::clamp(t, 0.0f, 1.0f));
    return true;
}

f32 segment_length(const BezierPath& p, u32 segment) noexcept {
    if (segment >= segment_count(p)) return 0.0f;
    constexpr int kSteps = 24;
    Vec2 prev{};
    (void)point_on_segment(p, segment, 0.0f, prev);
    f32 len = 0.0f;
    for (int k = 1; k <= kSteps; ++k) {
        Vec2 q{};
        (void)point_on_segment(p, segment, static_cast<f32>(k) / kSteps, q);
        len += (q - prev).length();
        prev = q;
    }
    return len;
}

i32 insert_at(BezierPath& p, u32 segment, f32 t) noexcept {
    if (segment >= segment_count(p) || !std::isfinite(t)) return -1;
    t = std::clamp(t, 0.001f, 0.999f);
    const usize n = p.v.size();
    const usize i = segment, j = (segment + 1) % n;
    BezierVertex& a = p.v[i];
    BezierVertex& b = p.v[j];
    const Vec2 p0 = a.p, p1 = a.p + a.out, p2 = b.p + b.in, p3 = b.p;
    const Vec2 q0 = lerp(p0, p1, t), q1 = lerp(p1, p2, t), q2 = lerp(p2, p3, t);
    const Vec2 r0 = lerp(q0, q1, t), r1 = lerp(q1, q2, t);
    const Vec2 s = lerp(r0, r1, t);
    a.out = q0 - p0;
    b.in = q2 - p3;
    const BezierVertex nv{s, r0 - s, r1 - s};
    // O último trecho de um caminho fechado volta ao 0: o ponto novo vai no fim.
    const usize at = j == 0 ? n : i + 1;
    p.v.insert(p.v.begin() + static_cast<std::ptrdiff_t>(at), nv);
    return static_cast<i32>(at);
}

bool remove_point(BezierPath& p, u32 i) noexcept {
    if (i >= p.v.size()) return false;
    p.v.erase(p.v.begin() + i);
    if (p.v.size() < 3) p.closed = false;
    return true;
}

bool point_is_curve(const BezierPath& p, u32 i) noexcept {
    if (i >= p.v.size()) return false;
    return p.v[i].in.length_sq() > 1e-6f || p.v[i].out.length_sq() > 1e-6f;
}

bool set_point_type(BezierPath& p, u32 i, bool curve) noexcept {
    if (i >= p.v.size()) return false;
    BezierVertex& v = p.v[i];
    if (!curve) {
        if (!point_is_curve(p, i)) return false;
        v.in = Vec2{};
        v.out = Vec2{};
        return true;
    }
    const usize n = p.v.size();
    const BezierVertex& prev = p.v[i > 0 ? i - 1 : (p.closed ? n - 1 : i)];
    const BezierVertex& next = p.v[i + 1 < n ? i + 1 : (p.closed ? 0 : i)];
    const Vec2 dir = (next.p - prev.p).normalized();
    if (dir.length_sq() < 1e-8f) return false;
    const f32 lin = (v.p - prev.p).length() / 3.0f;
    const f32 lout = (next.p - v.p).length() / 3.0f;
    v.in = -dir * lin;
    v.out = dir * lout;
    return true;
}

void move_points(BezierPath& p, const u32* indices, usize count, Vec2 delta) noexcept {
    if (!indices || !std::isfinite(delta.x) || !std::isfinite(delta.y)) return;
    std::vector<u32> seen;
    seen.reserve(count);
    for (usize k = 0; k < count; ++k) {
        const u32 i = indices[k];
        if (i >= p.v.size() || std::find(seen.begin(), seen.end(), i) != seen.end()) continue;
        seen.push_back(i);
        p.v[i].p += delta;
    }
}

bool move_handle(BezierPath& p, u32 i, bool out, Vec2 delta, bool lockAngle, bool lockLength) noexcept {
    if (i >= p.v.size() || !std::isfinite(delta.x) || !std::isfinite(delta.y)) return false;
    BezierVertex& v = p.v[i];
    Vec2& moved = out ? v.out : v.in;
    Vec2& other = out ? v.in : v.out;
    moved += delta;
    if (!lockAngle) return true;
    const f32 len = moved.length();
    if (len < 1e-5f) return true;
    const f32 keep = lockLength ? len : other.length();
    other = -(moved / len) * keep;
    return true;
}

u32 append_point(BezierPath& p, Vec2 at, bool atStart) noexcept {
    const BezierVertex nv{at, {}, {}};
    if (atStart) {
        p.v.insert(p.v.begin(), nv);
        return 0;
    }
    p.v.push_back(nv);
    return static_cast<u32>(p.v.size() - 1);
}

bool set_closed(BezierPath& p, bool closed) noexcept {
    if (closed && p.v.size() < 2) return false;
    if (p.closed == closed) return false;
    p.closed = closed;
    return true;
}

} // namespace aurea::vector::pathedit
