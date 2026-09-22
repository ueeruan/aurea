// =============================================================================
//  Aurea / vector / Vector.cpp
//
//  Caminhos bezier, achatamento adaptativo, aparar, tracejado, contorno por
//  peças unidas, avaliação dos valores animados, a malha para a GPU e o ajuste
//  de curva do desenho à mão livre. O recorte mora em Clipper.cpp; o SVG em
//  Svg.cpp.
// =============================================================================
#include "aurea/vector/Vector.hpp"

#include "aurea/animation/Curve.hpp"
#include "aurea/scene3d/Text3D.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::vector {

namespace {
constexpr f32 kKappa = 0.5522847498f;

inline f32 cross2(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
inline Vec2 left_normal(Vec2 d) noexcept { return Vec2{-d.y, d.x}; }
inline Vec2 lerp2(Vec2 a, Vec2 b, f32 t) noexcept { return a + (b - a) * t; }

f32 srgb_to_linear(f32 c) noexcept { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

BezierVertex vert(Vec2 p, Vec2 in = {}, Vec2 out = {}) { return BezierVertex{p, in, out}; }
} // namespace

// =============================================================================
// Afim
// =============================================================================
Affine2 Affine2::rotate(f32 degrees) noexcept {
    const f32 r = degrees * kDeg2Rad;
    const f32 c = std::cos(r), s = std::sin(r);
    return {c, s, -s, c, 0, 0};
}
Affine2 Affine2::inverse() const noexcept {
    const f32 det = a * d - b * c;
    if (std::fabs(det) < 1e-12f) return Affine2{};
    const f32 id = 1.0f / det;
    Affine2 r{d * id, -b * id, -c * id, a * id, 0, 0};
    r.tx = -(r.a * tx + r.c * ty);
    r.ty = -(r.b * tx + r.d * ty);
    return r;
}
f32 Affine2::max_scale() const noexcept {
    return std::max(std::sqrt(a * a + b * b), std::sqrt(c * c + d * d));
}
f32 Affine2::mean_scale() const noexcept { return std::sqrt(std::fabs(a * d - b * c)); }

// =============================================================================
// Caminhos paramétricos
// =============================================================================
BezierPath make_rect(Vec2 c, Vec2 size, f32 roundness) {
    BezierPath p;
    p.closed = true;
    const f32 hw = std::fabs(size.x) * 0.5f, hh = std::fabs(size.y) * 0.5f;
    const f32 r = std::clamp(roundness, 0.0f, std::min(hw, hh));
    const f32 x0 = c.x - hw, x1 = c.x + hw, y0 = c.y - hh, y1 = c.y + hh;
    if (r <= 0.0f) {
        // Sentido horário na tela (y para baixo) = área positiva.
        p.v = {vert({x0, y0}), vert({x1, y0}), vert({x1, y1}), vert({x0, y1})};
        return p;
    }
    const f32 k = r * kKappa;
    p.v = {
        vert({x0 + r, y0}, {-k, 0}, {}), vert({x1 - r, y0}, {}, {k, 0}),
        vert({x1, y0 + r}, {0, -k}, {}), vert({x1, y1 - r}, {}, {0, k}),
        vert({x1 - r, y1}, {k, 0}, {}), vert({x0 + r, y1}, {}, {-k, 0}),
        vert({x0, y1 - r}, {0, k}, {}), vert({x0, y0 + r}, {}, {0, -k}),
    };
    return p;
}

BezierPath make_ellipse(Vec2 c, Vec2 size) {
    const f32 rx = std::fabs(size.x) * 0.5f, ry = std::fabs(size.y) * 0.5f;
    const f32 kx = rx * kKappa, ky = ry * kKappa;
    BezierPath p;
    p.closed = true;
    p.v = {
        vert({c.x, c.y - ry}, {-kx, 0}, {kx, 0}),
        vert({c.x + rx, c.y}, {0, -ky}, {0, ky}),
        vert({c.x, c.y + ry}, {kx, 0}, {-kx, 0}),
        vert({c.x - rx, c.y}, {0, ky}, {0, -ky}),
    };
    return p;
}

BezierPath make_polystar(Vec2 c, f32 points, f32 outerR, f32 innerR, f32 outerRound, f32 innerRound, f32 rotation, bool star) {
    const u32 n = static_cast<u32>(std::clamp(std::lround(points), 3l, 100l));
    const u32 count = star ? n * 2 : n;
    BezierPath p;
    p.closed = true;
    p.v.reserve(count);
    const f32 step = 2.0f * kPi / static_cast<f32>(count);
    for (u32 i = 0; i < count; ++i) {
        const bool outer = !star || (i % 2 == 0);
        const f32 r = outer ? outerR : innerR;
        const f32 ang = -kPi * 0.5f + rotation * kDeg2Rad + step * static_cast<f32>(i);
        const Vec2 dir{std::cos(ang), std::sin(ang)};
        const Vec2 pos = c + dir * r;
        // Arredondar: tangente perpendicular ao raio, no comprimento do arco
        // de círculo entre dois vértices (100% = círculo).
        const f32 round = (outer ? outerRound : innerRound) / 100.0f;
        const f32 L = round * r * (4.0f / 3.0f) * std::tan(step * 0.25f);
        const Vec2 t{-dir.y, dir.x};   // sentido de percurso (horário na tela)
        p.v.push_back(vert(pos, t * -L, t * L));
    }
    return p;
}

BezierPath transform_path(const BezierPath& p, const Affine2& m) {
    BezierPath r;
    r.closed = p.closed;
    r.v.reserve(p.v.size());
    for (const BezierVertex& v : p.v) r.v.push_back(BezierVertex{m.apply(v.p), m.apply_vec(v.in), m.apply_vec(v.out)});
    return r;
}

namespace {
BezierPath reversed_path(const BezierPath& p) {
    BezierPath r;
    r.closed = p.closed;
    r.v.reserve(p.v.size());
    for (usize i = p.v.size(); i-- > 0;) r.v.push_back(BezierVertex{p.v[i].p, p.v[i].out, p.v[i].in});
    return r;
}

/// Divide o segmento i → i+1 no meio (de Casteljau), sem mudar a forma.
void split_segment(BezierPath& p, usize i) {
    const usize n = p.v.size();
    const usize j = (i + 1) % n;
    const Vec2 p0 = p.v[i].p, p1 = p.v[i].p + p.v[i].out, p2 = p.v[j].p + p.v[j].in, p3 = p.v[j].p;
    const Vec2 a = lerp2(p0, p1, 0.5f), b = lerp2(p1, p2, 0.5f), c = lerp2(p2, p3, 0.5f);
    const Vec2 d = lerp2(a, b, 0.5f), e = lerp2(b, c, 0.5f), m = lerp2(d, e, 0.5f);
    p.v[i].out = a - p0;
    p.v[j].in = c - p3;
    p.v.insert(p.v.begin() + static_cast<std::ptrdiff_t>(i + 1), BezierVertex{m, d - m, e - m});
}
} // namespace

BezierPath resample(const BezierPath& src, usize count) {
    BezierPath p = src;
    if (p.v.size() < 2) {
        while (!p.v.empty() && p.v.size() < count) p.v.push_back(p.v.back());
        return p;
    }
    while (p.v.size() < count) {
        const usize n = p.v.size();
        const usize segs = p.closed ? n : n - 1;
        usize best = 0;
        f32 bestLen = -1.0f;
        for (usize i = 0; i < segs; ++i) {
            const usize j = (i + 1) % n;
            const f32 l = (p.v[i].out).length() + (p.v[j].p + p.v[j].in - p.v[i].p - p.v[i].out).length() + (p.v[j].in).length();
            if (l > bestLen) { bestLen = l; best = i; }
        }
        split_segment(p, best);
    }
    return p;
}

BezierPath lerp_path(const BezierPath& a, const BezierPath& b, f32 t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    const usize n = std::max(a.v.size(), b.v.size());
    const BezierPath A = a.v.size() < n ? resample(a, n) : a;
    const BezierPath B = b.v.size() < n ? resample(b, n) : b;
    BezierPath r;
    r.closed = t < 0.5f ? a.closed : b.closed;
    r.v.resize(n);
    for (usize i = 0; i < n; ++i) {
        r.v[i].p = lerp2(A.v[i].p, B.v[i].p, t);
        r.v[i].in = lerp2(A.v[i].in, B.v[i].in, t);
        r.v[i].out = lerp2(A.v[i].out, B.v[i].out, t);
    }
    return r;
}

BezierPath path_at(const VectorPath& p, f64 frame) {
    BezierPath b;
    switch (p.kind) {
        case VectorPathKind::Rect: b = make_rect(p.center, p.size, p.roundness); break;
        case VectorPathKind::Ellipse: b = make_ellipse(p.center, p.size); break;
        case VectorPathKind::Polygon:
            b = make_polystar(p.center, p.points, p.outerRadius, p.outerRadius, p.outerRoundness, p.outerRoundness, p.rotation, false);
            break;
        case VectorPathKind::Star:
            b = make_polystar(p.center, p.points, p.outerRadius, p.innerRadius, p.outerRoundness, p.innerRoundness, p.rotation, true);
            break;
        case VectorPathKind::Free: {
            if (p.keys.empty()) { b = p.path; break; }
            const std::vector<PathKey>& k = p.keys;
            if (k.size() == 1 || frame <= static_cast<f64>(k.front().frame)) { b = k.front().path; break; }
            if (frame >= static_cast<f64>(k.back().frame)) { b = k.back().path; break; }
            usize i = 0;
            while (i + 1 < k.size() && static_cast<f64>(k[i + 1].frame) <= frame) ++i;
            const PathKey& k0 = k[i];
            const PathKey& k1 = k[i + 1];
            const f64 span = static_cast<f64>(k1.frame - k0.frame);
            f32 u = span > 0.0 ? static_cast<f32>((frame - static_cast<f64>(k0.frame)) / span) : 1.0f;
            if (k0.ease == 2) u = 0.0f;
            else if (k0.ease == 1) u = u * u * (3.0f - 2.0f * u);
            b = lerp_path(k0.path, k1.path, u);
            break;
        }
    }
    return p.reversed ? reversed_path(b) : b;
}

void make_editable(VectorPath& p) {
    if (p.kind == VectorPathKind::Free) return;
    const bool rev = p.reversed;
    p.reversed = false;
    p.path = path_at(p, 0.0);
    p.kind = VectorPathKind::Free;
    if (rev) p.path = reversed_path(p.path);
    p.keys.clear();
}

// =============================================================================
// Achatamento
// =============================================================================
namespace {
void flatten_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, f32 tol2, int depth, std::vector<Vec2>& out) {
    // Planura: distância dos pontos de controle até a corda.
    const Vec2 d = p3 - p0;
    const f32 dd = d.length_sq();
    f32 d1, d2;
    if (dd > 1e-12f) {
        const f32 c1 = cross2(p1 - p0, d), c2 = cross2(p2 - p0, d);
        d1 = c1 * c1 / dd;
        d2 = c2 * c2 / dd;
    } else {
        d1 = (p1 - p0).length_sq();
        d2 = (p2 - p0).length_sq();
    }
    // A curva fica a no máximo 3/4 da distância do polígono de controle.
    if (depth >= 18 || std::max(d1, d2) * 0.5625f <= tol2) {
        out.push_back(p3);
        return;
    }
    const Vec2 a = lerp2(p0, p1, 0.5f), b = lerp2(p1, p2, 0.5f), c = lerp2(p2, p3, 0.5f);
    const Vec2 e = lerp2(a, b, 0.5f), f = lerp2(b, c, 0.5f), m = lerp2(e, f, 0.5f);
    flatten_cubic(p0, a, e, m, tol2, depth + 1, out);
    flatten_cubic(m, f, c, p3, tol2, depth + 1, out);
}
} // namespace

void flatten(const BezierPath& p, f32 tolerance, Contour& out) {
    out.pts.clear();
    out.closed = p.closed;
    const usize n = p.v.size();
    if (n == 0) return;
    const f32 tol2 = std::max(1e-4f, tolerance) * std::max(1e-4f, tolerance);
    out.pts.push_back(p.v[0].p);
    const usize segs = p.closed ? n : n - 1;
    for (usize i = 0; i < segs; ++i) {
        const usize j = (i + 1) % n;
        const BezierVertex& a = p.v[i];
        const BezierVertex& b = p.v[j];
        if (a.out == Vec2{} && b.in == Vec2{}) out.pts.push_back(b.p);
        else flatten_cubic(a.p, a.p + a.out, b.p + b.in, b.p, tol2, 0, out.pts);
    }
    // Fechado: o último ponto repetido (volta ao começo) sai — o fecho é implícito.
    if (p.closed && out.pts.size() > 1 && (out.pts.back() - out.pts.front()).length_sq() < 1e-10f) out.pts.pop_back();
    // Pontos repetidos seguidos também.
    std::vector<Vec2> clean;
    clean.reserve(out.pts.size());
    for (Vec2 q : out.pts) if (clean.empty() || (q - clean.back()).length_sq() > 1e-10f) clean.push_back(q);
    out.pts.swap(clean);
}

// =============================================================================
// Medidas
// =============================================================================
f64 length_of(const Contour& c) noexcept {
    const usize n = c.pts.size();
    if (n < 2) return 0.0;
    f64 s = 0.0;
    for (usize i = 1; i < n; ++i) s += static_cast<f64>((c.pts[i] - c.pts[i - 1]).length());
    if (c.closed) s += static_cast<f64>((c.pts.front() - c.pts.back()).length());
    return s;
}

bool sample_at(const Contour& c, f64 s, Vec2& point, Vec2& tangent) noexcept {
    const usize n = c.pts.size();
    if (n < 2) return false;
    const f64 L = length_of(c);
    if (L <= 0.0) return false;
    if (c.closed) { s = std::fmod(s, L); if (s < 0.0) s += L; }
    const usize segs = c.closed ? n : n - 1;
    f64 acc = 0.0;
    for (usize i = 0; i < segs; ++i) {
        const Vec2 a = c.pts[i], b = c.pts[(i + 1) % n];
        const f64 l = static_cast<f64>((b - a).length());
        if (l <= 0.0) continue;
        if (s <= acc + l || i + 1 == segs) {
            // Fora do caminho aberto: estende pela tangente da ponta.
            const f32 t = static_cast<f32>((s - acc) / l);
            point = a + (b - a) * t;
            tangent = (b - a).normalized();
            return true;
        }
        acc += l;
    }
    return false;
}

// =============================================================================
// Aparar e tracejar
// =============================================================================
namespace {
/// Trecho [a, b] (comprimento) de uma polilinha; fechado aceita b > L (volta).
void sub_polyline(const Contour& c, f64 a, f64 b, std::vector<Contour>& out) {
    const usize n = c.pts.size();
    if (n < 2 || b <= a) return;
    const usize segs = c.closed ? n : n - 1;
    Contour r;
    r.closed = false;
    f64 acc = 0.0;
    const int laps = c.closed ? 2 : 1;
    for (int lap = 0; lap < laps; ++lap) {
        for (usize i = 0; i < segs; ++i) {
            const Vec2 p = c.pts[i], q = c.pts[(i + 1) % n];
            const f64 l = static_cast<f64>((q - p).length());
            const f64 s0 = acc, s1 = acc + l;
            acc = s1;
            if (l <= 0.0 || s1 < a || s0 > b) continue;
            const f64 u0 = std::max(0.0, (a - s0) / l), u1 = std::min(1.0, (b - s0) / l);
            const Vec2 A = p + (q - p) * static_cast<f32>(u0), B = p + (q - p) * static_cast<f32>(u1);
            if (r.pts.empty() || (r.pts.back() - A).length_sq() > 1e-10f) r.pts.push_back(A);
            if ((r.pts.back() - B).length_sq() > 1e-10f) r.pts.push_back(B);
        }
    }
    if (r.pts.size() >= 2) out.push_back(std::move(r));
}
} // namespace

void trim(std::vector<Contour>& contours, f32 start, f32 end, f32 offset, bool sequential) {
    f64 s0 = std::clamp(static_cast<f64>(std::min(start, end)), 0.0, 1.0);
    f64 s1 = std::clamp(static_cast<f64>(std::max(start, end)), 0.0, 1.0);
    if (s1 - s0 >= 1.0 - 1e-9) return;   // tudo
    const f64 off = static_cast<f64>(offset) - std::floor(static_cast<f64>(offset));
    std::vector<Contour> out;
    if (s1 - s0 <= 1e-9) { contours.clear(); return; }
    auto window = [&](const Contour& c, f64 a, f64 b, f64 L) {
        // [a, b] em comprimento, já com o deslocamento; aberto quebra na volta.
        if (c.closed) {
            if (b > L) { a -= L; b -= L; }
            if (a < 0.0) { a += L; b += L; }
            sub_polyline(c, a, b, out);
        } else {
            if (b <= L) sub_polyline(c, a, b, out);
            else if (a >= L) sub_polyline(c, a - L, b - L, out);
            else { sub_polyline(c, a, L, out); sub_polyline(c, 0.0, b - L, out); }
        }
    };
    if (!sequential) {
        for (const Contour& c : contours) {
            const f64 L = length_of(c);
            if (L <= 0.0) continue;
            window(c, (s0 + off) * L, (s1 + off) * L, L);
        }
    } else {
        f64 total = 0.0;
        for (const Contour& c : contours) total += length_of(c);
        if (total <= 0.0) return;
        f64 a = (s0 + off) * total, b = (s1 + off) * total;
        if (a >= total) { a -= total; b -= total; }
        // Janela sobre o comprimento concatenado (com volta para o começo).
        auto run = [&](f64 wa, f64 wb) {
            f64 acc = 0.0;
            for (const Contour& c : contours) {
                const f64 L = length_of(c);
                const f64 ca = std::max(wa, acc), cb = std::min(wb, acc + L);
                if (cb > ca) {
                    if (cb - ca >= L - 1e-9 && c.closed) out.push_back(c);
                    else sub_polyline(c, ca - acc, cb - acc, out);
                }
                acc += L;
            }
        };
        if (b <= total) run(a, b);
        else { run(a, total); run(0.0, b - total); }
    }
    contours.swap(out);
}

void dash(std::vector<Contour>& contours, const std::vector<f32>& pattern, f32 offset) {
    f64 period = 0.0;
    std::vector<f64> pat;
    for (f32 v : pattern) { pat.push_back(std::max(0.0, static_cast<f64>(v))); period += pat.back(); }
    if (pat.size() % 2 == 1) { const usize k = pat.size(); for (usize i = 0; i < k; ++i) { pat.push_back(pat[i]); period += pat[i]; } }
    if (pat.empty() || period <= 1e-6) return;
    std::vector<Contour> out;
    for (const Contour& c : contours) {
        const f64 L = length_of(c);
        if (L <= 0.0) continue;
        // Fase: o padrão começa em −offset.
        f64 pos = -std::fmod(static_cast<f64>(offset), period);
        if (pos > 0.0) pos -= period;
        usize k = 0;
        while (pos < L && out.size() < 200000) {
            const f64 len = pat[k];
            if (k % 2 == 0) {
                const f64 a = std::max(0.0, pos), b = std::min(L, pos + len);
                // Fechado: o fecho conta como segmento (a polilinha fechada é recortada).
                if (b > a) sub_polyline(c, a, b, out);
            }
            pos += len;
            k = (k + 1) % pat.size();
        }
    }
    contours.swap(out);
}

// =============================================================================
// Contorno: peças por segmento + juntas + pontas, unidas em não-zero
// =============================================================================
namespace {
void push_piece(std::vector<Contour>& pieces, std::vector<Vec2> pts) {
    if (pts.size() < 3) return;
    const f64 a = signed_area(pts);
    if (std::fabs(a) < 1e-9) return;
    if (a < 0.0) std::reverse(pts.begin(), pts.end());
    pieces.push_back(Contour{std::move(pts), true});
}

void arc_points(Vec2 c, f32 r, f32 a0, f32 a1, f32 tol, std::vector<Vec2>& out) {
    // a0 → a1 (radianos, qualquer sentido), passo pela tolerância.
    const f32 step = 2.0f * std::acos(std::clamp(1.0f - tol / std::max(r, 1e-3f), -1.0f, 1.0f));
    const int n = std::clamp(static_cast<int>(std::ceil(std::fabs(a1 - a0) / std::max(step, 1e-3f))), 1, 256);
    for (int i = 0; i <= n; ++i) {
        const f32 a = a0 + (a1 - a0) * static_cast<f32>(i) / static_cast<f32>(n);
        out.push_back(c + Vec2{std::cos(a), std::sin(a)} * r);
    }
}
} // namespace

void stroke_to_rings(const std::vector<Contour>& contours, f32 width, u8 cap, u8 join, f32 miterLimit, f32 tolerance,
                     std::vector<Contour>& out) {
    out.clear();
    const f32 hw = width * 0.5f;
    if (hw <= 0.0f) return;
    const f32 tol = std::max(0.01f, tolerance);
    std::vector<Contour> pieces;
    for (const Contour& src : contours) {
        std::vector<Vec2> P;
        P.reserve(src.pts.size());
        for (Vec2 q : src.pts) if (P.empty() || (q - P.back()).length_sq() > 1e-8f) P.push_back(q);
        bool closed = src.closed;
        if (closed && P.size() > 2 && (P.back() - P.front()).length_sq() <= 1e-8f) P.pop_back();
        if (closed && P.size() < 3) closed = false;
        if (P.size() == 1) {
            // Ponto isolado: só a ponta (redonda = círculo, quadrada = quadrado).
            if (cap == 1) { std::vector<Vec2> c; arc_points(P[0], hw, 0.0f, 2.0f * kPi, tol, c); c.pop_back(); push_piece(pieces, c); }
            else if (cap == 2) push_piece(pieces, {P[0] + Vec2{-hw, -hw}, P[0] + Vec2{hw, -hw}, P[0] + Vec2{hw, hw}, P[0] + Vec2{-hw, hw}});
            continue;
        }
        if (P.size() < 2) continue;
        const usize n = P.size();
        const usize segs = closed ? n : n - 1;
        for (usize i = 0; i < segs; ++i) {
            const Vec2 a = P[i], b = P[(i + 1) % n];
            const Vec2 d = (b - a).normalized();
            const Vec2 nn = left_normal(d) * hw;
            push_piece(pieces, {a + nn, b + nn, b - nn, a - nn});
        }
        // Juntas.
        const usize first = closed ? 0 : 1, last = closed ? n : n - 1;
        for (usize i = first; i < last; ++i) {
            const Vec2 v = P[i];
            const Vec2 d0 = (v - P[(i + n - 1) % n]).normalized(), d1 = (P[(i + 1) % n] - v).normalized();
            const f32 cr = cross2(d0, d1), dt = d0.dot(d1);
            if (std::fabs(cr) < 1e-6f && dt > 0.0f) continue;   // reto
            // Lado de fora: à direita numa curva para a esquerda e vice-versa.
            const f32 side = cr > 0.0f ? -1.0f : 1.0f;
            const Vec2 n0 = left_normal(d0) * side, n1 = left_normal(d1) * side;
            const Vec2 A = v + n0 * hw, B = v + n1 * hw;
            if (join == 1) {
                std::vector<Vec2> fan{v};
                f32 a0 = std::atan2(n0.y, n0.x), a1 = std::atan2(n1.y, n1.x);
                // O arco curto entre as duas normais.
                while (a1 - a0 > kPi) a1 -= 2.0f * kPi;
                while (a0 - a1 > kPi) a1 += 2.0f * kPi;
                arc_points(v, hw, a0, a1, tol, fan);
                push_piece(pieces, fan);
            } else {
                const Vec2 bis = (n0 + n1).normalized();
                const f32 cosHalf = bis.dot(n0);
                const bool miter = join == 0 && cosHalf > 1e-4f && 1.0f / cosHalf <= std::max(1.0f, miterLimit);
                if (miter) push_piece(pieces, {v, A, v + bis * (hw / cosHalf), B});
                else push_piece(pieces, {v, A, B});
            }
        }
        // Pontas.
        if (!closed && cap != 0) {
            for (int end = 0; end < 2; ++end) {
                const Vec2 p = end == 0 ? P[0] : P[n - 1];
                const Vec2 d = end == 0 ? (P[0] - P[1]).normalized() : (P[n - 1] - P[n - 2]).normalized();   // para fora
                const Vec2 nn = left_normal(d) * hw;
                if (cap == 2) {
                    push_piece(pieces, {p + nn, p + nn + d * hw, p - nn + d * hw, p - nn});
                } else {
                    std::vector<Vec2> fan;
                    const f32 a0 = std::atan2(nn.y, nn.x);
                    arc_points(p, hw, a0, a0 - kPi, tol, fan);
                    push_piece(pieces, fan);
                }
            }
        }
    }
    resolve_fill(pieces, FillRule::NonZero, out);
}

// =============================================================================
// Valores animados
// =============================================================================
f32* param_ref(VectorGroup& g, u32 p) noexcept {
    switch (p) {
        case kVecTrimStart: return &g.trim.start;
        case kVecTrimEnd: return &g.trim.end;
        case kVecTrimOffset: return &g.trim.offset;
        case kVecStrokeWidth: return &g.stroke.width;
        case kVecDashOffset: return &g.stroke.dashOffset;
        case kVecFillOpacity: return &g.fill.paint.opacity;
        case kVecStrokeOpacity: return &g.stroke.paint.opacity;
        case kVecRepCopies: return &g.repeater.copies;
        case kVecRepOffset: return &g.repeater.offset;
        case kVecRepPosX: return &g.repeater.position.x;
        case kVecRepPosY: return &g.repeater.position.y;
        case kVecRepRotation: return &g.repeater.rotation;
        case kVecRepScale: return &g.repeater.scale;
        case kVecRepStartOpacity: return &g.repeater.startOpacity;
        case kVecRepEndOpacity: return &g.repeater.endOpacity;
        case kVecPosX: return &g.position.x;
        case kVecPosY: return &g.position.y;
        case kVecRotation: return &g.rotation;
        case kVecScale: return &g.scale.x;
        case kVecOpacity: return &g.opacity;
        default: return nullptr;
    }
}

f32 clamp_param(u32 p, f32 v) noexcept {
    switch (p) {
        case kVecTrimStart: case kVecTrimEnd: return std::clamp(v, 0.0f, 100.0f);
        case kVecTrimOffset: return std::clamp(v, -100000.0f, 100000.0f);
        case kVecStrokeWidth: return std::clamp(v, 0.0f, 2000.0f);
        case kVecFillOpacity: case kVecStrokeOpacity: case kVecOpacity:
        case kVecRepStartOpacity: case kVecRepEndOpacity: return std::clamp(v, 0.0f, 100.0f);
        case kVecRepCopies: return std::clamp(v, 0.0f, 500.0f);
        case kVecRepScale: case kVecScale: return std::clamp(v, -10000.0f, 10000.0f);
        default: return std::clamp(v, -100000.0f, 100000.0f);
    }
}

VectorGroup evaluate_group(const VectorGroup& g, const TrackSet& tracks, u32 gi, f64 frame) {
    VectorGroup r = g;
    const f64 f = std::floor(frame);
    const f32 k = static_cast<f32>(frame - f);
    for (u32 p = 0; p < kVecParamCount; ++p) {
        const Track* tr = tracks.find(TrackProperty::VectorParam, gi, p);
        if (!tr || tr->keys.empty()) continue;
        f32 v = tr->sample(FrameIndex{static_cast<i64>(f)});
        if (k > 0.0f) v += (tr->sample(FrameIndex{static_cast<i64>(f) + 1}) - v) * k;
        if (f32* ref = param_ref(r, p)) *ref = clamp_param(p, v);
    }
    // Escala do grupo: um valor animável (uniforme) — Y acompanha X.
    if (tracks.find(TrackProperty::VectorParam, gi, kVecScale)) r.scale.y = r.scale.x * (g.scale.x != 0.0f ? g.scale.y / g.scale.x : 1.0f);
    return r;
}

Affine2 group_matrix(const VectorGroup& g) noexcept {
    return Affine2::translate(g.position) * Affine2::rotate(g.rotation) * Affine2::scale(g.scale * 0.01f) * Affine2::translate(-g.anchor);
}

Affine2 repeater_matrix(const VectorRepeater& r, f32 k) noexcept {
    const f32 s = std::pow(std::max(1e-4f, std::fabs(r.scale) * 0.01f), k) * (r.scale < 0.0f && std::fmod(std::fabs(k), 2.0f) >= 1.0f ? -1.0f : 1.0f);
    return Affine2::translate(r.position * k) * Affine2::translate(r.anchor) * Affine2::rotate(r.rotation * k)
         * Affine2::scale(Vec2{s, s}) * Affine2::translate(-r.anchor);
}

// =============================================================================
// Geometria do grupo
// =============================================================================
void group_geometry(const VectorGroup& g, f64 frame, f32 tol, GroupGeometry& out) {
    out.fill.clear();
    out.stroke.clear();
    out.lines.clear();
    std::vector<Contour> lines;
    std::vector<std::vector<Contour>> sets;
    for (const VectorPath& vp : g.paths) {
        const BezierPath b = path_at(vp, frame);
        if (b.v.empty()) continue;
        Contour c;
        flatten(b, tol, c);
        if (c.pts.empty()) continue;
        if (g.merge != 0) {
            if (c.pts.size() >= 3) sets.push_back({c});
        } else {
            lines.push_back(std::move(c));
        }
    }
    const FillRule rule = g.fill.rule == 1 ? FillRule::EvenOdd : FillRule::NonZero;
    if (g.merge != 0) {
        const BoolOp op = static_cast<BoolOp>(std::clamp<u8>(g.merge, 1, 4));
        if (!sets.empty()) boolean_op(sets, rule, op, lines);
    }
    if (g.trim.enabled) trim(lines, g.trim.start * 0.01f, g.trim.end * 0.01f, g.trim.offset * 0.01f, g.trim.mode == 1);
    const bool fill = g.fill.enabled && g.fill.paint.opacity > 0.0f;
    const bool stroke = g.stroke.enabled && g.stroke.width > 0.0f && g.stroke.paint.opacity > 0.0f;
    if (fill) {
        std::vector<Contour> closed;
        for (const Contour& c : lines) if (c.pts.size() >= 3) closed.push_back(c);
        // Depois da mesclagem os anéis já saem limpos; a regra continua valendo.
        resolve_fill(closed, g.merge != 0 ? FillRule::NonZero : rule, out.fill);
    }
    if (stroke) {
        std::vector<Contour> s = lines;
        if (!g.stroke.dashes.empty()) dash(s, g.stroke.dashes, g.stroke.dashOffset);
        stroke_to_rings(s, g.stroke.width, g.stroke.cap, g.stroke.join, g.stroke.miterLimit, tol, out.stroke);
    }
    out.lines = std::move(lines);
}

// =============================================================================
// Malha
// =============================================================================
namespace {
void encode_paint(const VectorPaint& p, std::vector<Vec4>& out) {
    Vec4 block[kPaintVec4]{};
    auto lin = [](Vec4 c) {
        const f32 a = std::clamp(c.w, 0.0f, 1.0f);
        return Vec4{srgb_to_linear(std::clamp(c.x, 0.0f, 1.0f)) * a, srgb_to_linear(std::clamp(c.y, 0.0f, 1.0f)) * a,
                    srgb_to_linear(std::clamp(c.z, 0.0f, 1.0f)) * a, a};
    };
    const f32 opacity = std::clamp(p.opacity * 0.01f, 0.0f, 1.0f);
    if (p.type == 0) {
        block[0] = Vec4{0, 1, opacity, 0};
        block[2] = lin(p.color);
    } else {
        std::vector<VectorStop> st = p.stops;
        if (st.empty()) st = {VectorStop{0.0f, p.color}, VectorStop{1.0f, Vec4{0, 0, 0, 1}}};
        std::stable_sort(st.begin(), st.end(), [](const VectorStop& a, const VectorStop& b) { return a.pos < b.pos; });
        if (st.size() > kMaxStops) {
            // Mais paradas do que o shader lê: amostra 8 posições uniformes.
            std::vector<VectorStop> s8;
            for (u32 i = 0; i < kMaxStops; ++i) {
                const f32 t = st.front().pos + (st.back().pos - st.front().pos) * static_cast<f32>(i) / static_cast<f32>(kMaxStops - 1);
                usize j = 0;
                while (j + 1 < st.size() && st[j + 1].pos < t) ++j;
                const VectorStop& a = st[j];
                const VectorStop& b = st[std::min(j + 1, st.size() - 1)];
                const f32 u = b.pos > a.pos ? std::clamp((t - a.pos) / (b.pos - a.pos), 0.0f, 1.0f) : 0.0f;
                s8.push_back(VectorStop{t, Vec4{a.color.x + (b.color.x - a.color.x) * u, a.color.y + (b.color.y - a.color.y) * u,
                                                  a.color.z + (b.color.z - a.color.z) * u, a.color.w + (b.color.w - a.color.w) * u}});
            }
            st.swap(s8);
        }
        block[0] = Vec4{static_cast<f32>(p.type), static_cast<f32>(st.size()), opacity, 0};
        block[1] = Vec4{p.start.x, p.start.y, p.end.x, p.end.y};
        f32 pos[kMaxStops]{};
        for (usize i = 0; i < st.size(); ++i) {
            block[2 + i] = lin(st[i].color);
            pos[i] = std::clamp(st[i].pos, 0.0f, 1.0f);
        }
        block[10] = Vec4{pos[0], pos[1], pos[2], pos[3]};
        block[11] = Vec4{pos[4], pos[5], pos[6], pos[7]};
    }
    out.insert(out.end(), block, block + kPaintVec4);
}

struct Emitter {
    VectorMesh& mesh;
    bool any = false;
    void vtx(Vec2 p, Vec2 g, f32 d, f32 paint, f32 alpha) {
        mesh.verts.push_back(Vec4{p.x, p.y, g.x, g.y});
        mesh.verts.push_back(Vec4{d, paint, alpha, 0});
        if (!any) { mesh.min = mesh.max = p; any = true; }
        mesh.min = Vec2{std::min(mesh.min.x, p.x), std::min(mesh.min.y, p.y)};
        mesh.max = Vec2{std::max(mesh.max.x, p.x), std::max(mesh.max.y, p.y)};
    }
};

/// Preenchimento por faixas horizontais (reserva quando a triangulação falha):
/// cada faixa entre dois y de vértice vira trapézios pelo enrolamento.
void trapezoids(const std::vector<std::vector<Vec2>>& rings, std::vector<Vec2>& tris) {
    struct E { Vec2 a, b; int dir; };
    std::vector<E> edges;
    std::vector<f32> ys;
    for (const auto& r : rings) {
        for (usize i = 0, n = r.size(); i < n; ++i) {
            const Vec2 a = r[i], b = r[(i + 1) % n];
            ys.push_back(a.y);
            if (a.y == b.y) continue;
            edges.push_back(a.y < b.y ? E{a, b, 1} : E{b, a, -1});
        }
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    struct X { f32 x0, x1, xm; int dir; };
    std::vector<X> xs;
    for (usize k = 0; k + 1 < ys.size(); ++k) {
        const f32 y0 = ys[k], y1 = ys[k + 1], ym = (y0 + y1) * 0.5f;
        xs.clear();
        for (const E& e : edges) {
            if (e.a.y > ym || e.b.y < ym) continue;
            auto at = [&](f32 y) { return e.a.x + (e.b.x - e.a.x) * (y - e.a.y) / (e.b.y - e.a.y); };
            xs.push_back(X{at(y0), at(y1), at(ym), e.dir});
        }
        std::sort(xs.begin(), xs.end(), [](const X& a, const X& b) { return a.xm < b.xm; });
        int w = 0;
        for (usize i = 0; i + 1 < xs.size(); ++i) {
            w += xs[i].dir;
            if (w == 0) continue;
            const X& l = xs[i];
            const X& r = xs[i + 1];
            tris.insert(tris.end(), {Vec2{l.x0, y0}, Vec2{r.x0, y0}, Vec2{r.x1, y1}, Vec2{l.x0, y0}, Vec2{r.x1, y1}, Vec2{l.x1, y1}});
        }
    }
}

/// Anéis limpos (espaço do grupo) → triângulos na camada: interior recuado
/// meia largura de AA e a franja com a distância até a borda.
void emit_rings(Emitter& em, const std::vector<Contour>& rings, const Affine2& M, f32 paint, f32 alpha, f32 aa) {
    if (rings.empty() || alpha <= 0.0f) return;
    const Affine2 inv = M.inverse();
    const bool flip = (M.a * M.d - M.b * M.c) < 0.0f;   // espelhado: o "dentro" troca de lado
    std::vector<std::vector<Vec2>> L(rings.size());
    std::vector<f64> area(rings.size());
    for (usize r = 0; r < rings.size(); ++r) {
        L[r].reserve(rings[r].pts.size());
        for (Vec2 p : rings[r].pts) L[r].push_back(M.apply(p));
        area[r] = signed_area(L[r]) * (flip ? -1.0 : 1.0);
    }
    // Recuo por vértice (miter das normais internas, limitado).
    std::vector<std::vector<Vec2>> inner(rings.size()), outer(rings.size());
    for (usize r = 0; r < L.size(); ++r) {
        const std::vector<Vec2>& P = L[r];
        const usize n = P.size();
        inner[r].resize(n);
        outer[r].resize(n);
        for (usize i = 0; i < n; ++i) {
            const Vec2 a = P[(i + n - 1) % n], v = P[i], b = P[(i + 1) % n];
            Vec2 n0 = left_normal((v - a).normalized()), n1 = left_normal((b - v).normalized());
            if (flip) { n0 = -n0; n1 = -n1; }
            Vec2 m = (n0 + n1);
            const f32 ml = m.length();
            m = ml > 1e-6f ? m / ml : n0;
            const f32 c = std::max(m.dot(n0), 0.25f);
            const Vec2 off = m * (aa / c);
            inner[r][i] = v + off;
            outer[r][i] = v - off;
        }
    }
    // Interior: externos com os seus furos, triangulados; vértices recuados.
    std::vector<i32> owner(rings.size(), -1);
    for (usize h = 0; h < rings.size(); ++h) {
        if (area[h] >= 0.0 || L[h].size() < 2) continue;
        // Ponto logo à esquerda (lado preenchido) da primeira aresta do furo.
        const Vec2 a = L[h][0], b = L[h][1];
        Vec2 nrm = left_normal((b - a).normalized());
        if (flip) nrm = -nrm;
        const Vec2 q = (a + b) * 0.5f + nrm * 1e-3f;
        f64 best = 0.0;
        for (usize o = 0; o < rings.size(); ++o) {
            if (area[o] <= 0.0) continue;
            // Enrolamento de q no externo o.
            const std::vector<Vec2>& P = L[o];
            int w = 0;
            for (usize i = 0, n = P.size(); i < n; ++i) {
                const Vec2 u = P[i], v = P[(i + 1) % n];
                const f32 c = (v.x - u.x) * (q.y - u.y) - (v.y - u.y) * (q.x - u.x);
                if (u.y <= q.y && q.y < v.y && c > 0) ++w;
                else if (v.y <= q.y && q.y < u.y && c < 0) --w;
            }
            if (w != 0 && (owner[h] < 0 || area[o] < best)) { owner[h] = static_cast<i32>(o); best = area[o]; }
        }
    }
    const f32 kInterior = 1.0e4f;
    for (usize o = 0; o < rings.size(); ++o) {
        if (area[o] <= 0.0) continue;
        std::vector<usize> members{o};
        for (usize h = 0; h < rings.size(); ++h) if (owner[h] == static_cast<i32>(o)) members.push_back(h);
        std::vector<std::vector<Vec2>> rr;
        std::vector<std::vector<Vec2>> moved;
        for (usize m : members) { rr.push_back(L[m]); moved.push_back(inner[m]); }
        std::vector<u32> idx;
        if (scene3d::triangulate_polygon(rr, idx) && !idx.empty()) {
            std::vector<Vec2> flat;
            for (const auto& r : moved) flat.insert(flat.end(), r.begin(), r.end());
            for (u32 i : idx) em.vtx(flat[i], inv.apply(flat[i]), kInterior, paint, alpha);
        } else {
            std::vector<Vec2> tris;
            trapezoids(rr, tris);
            for (Vec2 p : tris) em.vtx(p, inv.apply(p), kInterior, paint, alpha);
        }
    }
    // Furos sem dono (numérico): o interior deles não existe, só a franja.
    // Franja: da borda recuada (+aa) à expandida (−aa) em cada aresta.
    for (usize r = 0; r < L.size(); ++r) {
        const usize n = L[r].size();
        for (usize i = 0; i < n; ++i) {
            const usize j = (i + 1) % n;
            const Vec2 i0 = inner[r][i], i1 = inner[r][j], o0 = outer[r][i], o1 = outer[r][j];
            em.vtx(i0, inv.apply(i0), aa, paint, alpha);
            em.vtx(o0, inv.apply(o0), -aa, paint, alpha);
            em.vtx(o1, inv.apply(o1), -aa, paint, alpha);
            em.vtx(i0, inv.apply(i0), aa, paint, alpha);
            em.vtx(o1, inv.apply(o1), -aa, paint, alpha);
            em.vtx(i1, inv.apply(i1), aa, paint, alpha);
        }
    }
}

u32 copy_count(const VectorRepeater& r, f32& lastAlpha) {
    const f32 c = std::clamp(r.copies, 0.0f, 500.0f);
    const u32 n = static_cast<u32>(std::ceil(c - 1e-4f));
    const f32 frac = c - std::floor(c);
    lastAlpha = frac > 1e-4f ? frac : 1.0f;
    return n;
}
} // namespace

void build_mesh(const std::vector<VectorGroup>& groups, f64 frame, f32 density, VectorMesh& out) {
    out.verts.clear();
    out.paints.clear();
    out.min = out.max = Vec2{0, 0};
    Emitter em{out};
    const f32 dens = std::max(density, 1e-3f);
    const f32 aa = 1.0f / dens;   // 1 texel de cada lado da borda
    for (const VectorGroup& g : groups) {
        if (!g.visible || g.opacity <= 0.0f) continue;
        const Affine2 G = group_matrix(g);
        f32 repScale = 1.0f;
        f32 lastAlpha = 1.0f;
        u32 copies = 1;
        if (g.repeater.enabled) {
            copies = copy_count(g.repeater, lastAlpha);
            if (copies == 0) continue;
            const f32 s = std::fabs(g.repeater.scale) * 0.01f;
            repScale = std::max(1.0f, std::pow(std::max(s, 1e-3f), std::max(0.0f, static_cast<f32>(copies) - 1.0f + std::fabs(g.repeater.offset))));
            repScale = std::min(repScale, 64.0f);
        }
        // Tolerância: 0,2 texel na maior escala que o grupo chega a ter.
        const f32 tol = 0.2f / (dens * std::max(1e-3f, G.max_scale() * repScale));
        GroupGeometry geo;
        group_geometry(g, frame, tol, geo);
        if (geo.fill.empty() && geo.stroke.empty()) continue;
        const f32 fillPaint = static_cast<f32>(out.paints.size() / kPaintVec4);
        encode_paint(g.fill.paint, out.paints);
        const f32 strokePaint = static_cast<f32>(out.paints.size() / kPaintVec4);
        encode_paint(g.stroke.paint, out.paints);
        const f32 gop = std::clamp(g.opacity * 0.01f, 0.0f, 1.0f);
        auto draw_copy = [&](const Affine2& M, f32 alpha) {
            emit_rings(em, geo.fill, M, fillPaint, alpha, aa);
            emit_rings(em, geo.stroke, M, strokePaint, alpha, aa);
        };
        if (!g.repeater.enabled) {
            draw_copy(G, gop);
            continue;
        }
        const VectorRepeater& R = g.repeater;
        auto copy = [&](u32 k) {
            const f32 t = copies > 1 ? static_cast<f32>(k) / static_cast<f32>(copies - 1) : 0.0f;
            f32 a = std::clamp((R.startOpacity + (R.endOpacity - R.startOpacity) * t) * 0.01f, 0.0f, 1.0f) * gop;
            if (k + 1 == copies) a *= lastAlpha;
            draw_copy(G * repeater_matrix(R, static_cast<f32>(k) + R.offset), a);
        };
        // "Embaixo": a cópia 0 fica por cima (desenhada por último).
        if (R.above) for (u32 k = 0; k < copies; ++k) copy(k);
        else for (u32 k = copies; k-- > 0;) copy(k);
    }
}

bool bounds_of(const std::vector<VectorGroup>& groups, f64 frame, Vec2& mn, Vec2& mx) {
    bool any = false;
    auto add = [&](Vec2 p) {
        if (!any) { mn = mx = p; any = true; return; }
        mn = Vec2{std::min(mn.x, p.x), std::min(mn.y, p.y)};
        mx = Vec2{std::max(mx.x, p.x), std::max(mx.y, p.y)};
    };
    for (const VectorGroup& g : groups) {
        if (!g.visible) continue;
        const Affine2 G = group_matrix(g);
        f32 lastAlpha = 1.0f;
        const u32 copies = g.repeater.enabled ? copy_count(g.repeater, lastAlpha) : 1u;
        const f32 grow = g.stroke.enabled ? g.stroke.width * 0.5f * (g.stroke.join == 0 ? std::max(1.0f, g.stroke.miterLimit) : 1.5f) : 0.0f;
        for (const VectorPath& vp : g.paths) {
            const BezierPath b = path_at(vp, frame);
            for (u32 k = 0; k < copies; ++k) {
                const Affine2 M = g.repeater.enabled ? G * repeater_matrix(g.repeater, static_cast<f32>(k) + g.repeater.offset) : G;
                const f32 gs = grow * M.max_scale();
                for (const BezierVertex& v : b.v) {
                    for (Vec2 q : {v.p, v.p + v.in, v.p + v.out}) {
                        const Vec2 t = M.apply(q);
                        add(t - Vec2{gs, gs});
                        add(t + Vec2{gs, gs});
                    }
                }
            }
        }
    }
    return any;
}

namespace {
struct Hasher {
    u64 h = 1469598103934665603ull;
    void bytes(const void* p, usize n) noexcept {
        const auto* b = static_cast<const u8*>(p);
        for (usize i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    }
    void f(f32 v) noexcept { bytes(&v, sizeof(v)); }
    void v2(Vec2 v) noexcept { f(v.x); f(v.y); }
    void v4(Vec4 v) noexcept { f(v.x); f(v.y); f(v.z); f(v.w); }
    void u(u64 v) noexcept { bytes(&v, sizeof(v)); }
    void path(const BezierPath& p) noexcept {
        u(p.v.size());
        u(p.closed);
        for (const BezierVertex& v : p.v) { v2(v.p); v2(v.in); v2(v.out); }
    }
    void paint(const VectorPaint& p) noexcept {
        u(p.type); v4(p.color); v2(p.start); v2(p.end); f(p.opacity);
        u(p.stops.size());
        for (const VectorStop& s : p.stops) { f(s.pos); v4(s.color); }
    }
};
} // namespace

u64 content_hash(const std::vector<VectorGroup>& groups, f64 frame) noexcept {
    Hasher H;
    bool morph = false;
    H.u(groups.size());
    for (const VectorGroup& g : groups) {
        H.u(g.visible); H.u(g.merge);
        H.u(g.paths.size());
        for (const VectorPath& p : g.paths) {
            H.u(static_cast<u64>(p.kind)); H.u(p.reversed);
            H.v2(p.center); H.v2(p.size); H.f(p.roundness); H.f(p.points); H.f(p.outerRadius); H.f(p.innerRadius);
            H.f(p.outerRoundness); H.f(p.innerRoundness); H.f(p.rotation);
            H.path(p.path);
            H.u(p.keys.size());
            for (const PathKey& k : p.keys) { H.u(static_cast<u64>(k.frame)); H.u(k.ease); H.path(k.path); }
            morph = morph || p.keys.size() > 1;
        }
        H.u(g.fill.enabled); H.u(g.fill.rule); H.paint(g.fill.paint);
        const VectorStroke& s = g.stroke;
        H.u(s.enabled); H.paint(s.paint); H.f(s.width); H.u(s.cap); H.u(s.join); H.f(s.miterLimit); H.f(s.dashOffset);
        H.u(s.dashes.size());
        for (f32 d : s.dashes) H.f(d);
        const VectorTrim& t = g.trim;
        H.u(t.enabled); H.f(t.start); H.f(t.end); H.f(t.offset); H.u(t.mode);
        const VectorRepeater& r = g.repeater;
        H.u(r.enabled); H.f(r.copies); H.f(r.offset); H.v2(r.anchor); H.v2(r.position); H.f(r.scale); H.f(r.rotation);
        H.f(r.startOpacity); H.f(r.endOpacity); H.u(r.above);
        H.v2(g.position); H.v2(g.anchor); H.v2(g.scale); H.f(g.rotation); H.f(g.opacity);
    }
    if (morph) H.bytes(&frame, sizeof(frame));
    return H.h;
}

bool place_on_path(const Contour& path, f32 offset, bool reverse, bool perpendicular, f32 x, f32 dy, Vec2& pos, f32& angleDeg) noexcept {
    const f64 L = length_of(path);
    if (L <= 0.0) return false;
    f64 s = static_cast<f64>(offset) + static_cast<f64>(x);
    if (reverse) s = L - s;
    Vec2 p, t;
    if (!sample_at(path, s, p, t)) return false;
    // Tangente pela corda de ±δ: a do segmento da polilinha salta de um
    // segmento para o outro (letras "tremendo" numa curva achatada).
    const f64 delta = std::min(2.0, L * 0.01);
    Vec2 pa, pb, ta, tb;
    const bool closedOrInside = path.closed || (s - delta >= 0.0 && s + delta <= L);
    if (closedOrInside && sample_at(path, s - delta, pa, ta) && sample_at(path, s + delta, pb, tb) && (pb - pa).length_sq() > 1e-12f)
        t = (pb - pa).normalized();
    if (reverse) t = -t;
    const Vec2 n{-t.y, t.x};   // "para baixo" da linha de base (tela com y para baixo)
    pos = p + n * dy;
    angleDeg = perpendicular ? std::atan2(t.y, t.x) * kRad2Deg : 0.0f;
    return true;
}

bool guide_contour(const VectorData& data, const TrackSet& tracks, f64 frame, Contour& out) {
    for (u32 gi = 0; gi < data.groups.size(); ++gi) {
        const VectorGroup& g0 = data.groups[gi];
        if (!g0.visible || g0.paths.empty()) continue;
        const VectorGroup g = evaluate_group(g0, tracks, gi, frame);
        const BezierPath b = transform_path(path_at(g.paths[0], frame), group_matrix(g));
        if (b.v.size() < 2) continue;
        flatten(b, 0.1f, out);
        return out.pts.size() >= 2;
    }
    return false;
}

// =============================================================================
// Ajuste de curva (Schneider, "An Algorithm for Automatically Fitting
// Digitized Curves", Graphics Gems 1990)
// =============================================================================
namespace {
struct Cubic { Vec2 p0, p1, p2, p3; };

Vec2 bez(const Cubic& c, f32 t) {
    const f32 u = 1.0f - t;
    return c.p0 * (u * u * u) + c.p1 * (3 * u * u * t) + c.p2 * (3 * u * t * t) + c.p3 * (t * t * t);
}
Vec2 bez1(const Cubic& c, f32 t) {
    const f32 u = 1.0f - t;
    return (c.p1 - c.p0) * (3 * u * u) + (c.p2 - c.p1) * (6 * u * t) + (c.p3 - c.p2) * (3 * t * t);
}
Vec2 bez2(const Cubic& c, f32 t) {
    return (c.p2 - c.p1 * 2.0f + c.p0) * (6 * (1 - t)) + (c.p3 - c.p2 * 2.0f + c.p1) * (6 * t);
}

Cubic generate(const std::vector<Vec2>& d, usize first, usize last, const std::vector<f32>& u, Vec2 t1, Vec2 t2) {
    const usize n = last - first + 1;
    f32 C[2][2]{{0, 0}, {0, 0}}, X[2]{0, 0};
    const Vec2 p0 = d[first], p3 = d[last];
    for (usize i = 0; i < n; ++i) {
        const f32 t = u[i], s = 1.0f - t;
        const Vec2 a1 = t1 * (3 * s * s * t), a2 = t2 * (3 * s * t * t);
        C[0][0] += a1.dot(a1);
        C[0][1] += a1.dot(a2);
        C[1][1] += a2.dot(a2);
        const Vec2 tmp = d[first + i] - (p0 * (s * s * s) + p0 * (3 * s * s * t) + p3 * (3 * s * t * t) + p3 * (t * t * t));
        X[0] += a1.dot(tmp);
        X[1] += a2.dot(tmp);
    }
    C[1][0] = C[0][1];
    const f32 det = C[0][0] * C[1][1] - C[1][0] * C[0][1];
    f32 al = 0, ar = 0;
    if (std::fabs(det) > 1e-12f) {
        al = (X[0] * C[1][1] - X[1] * C[0][1]) / det;
        ar = (C[0][0] * X[1] - C[1][0] * X[0]) / det;
    }
    const f32 seg = (p3 - p0).length();
    const f32 eps = 1e-6f * seg;
    if (al < eps || ar < eps) {
        // Heurística do artigo: terços da corda.
        const f32 dist = seg / 3.0f;
        return Cubic{p0, p0 + t1 * dist, p3 + t2 * dist, p3};
    }
    return Cubic{p0, p0 + t1 * al, p3 + t2 * ar, p3};
}

f32 max_error(const std::vector<Vec2>& d, usize first, usize last, const Cubic& c, const std::vector<f32>& u, usize& split) {
    f32 m = 0.0f;
    split = (first + last) / 2;
    for (usize i = first + 1; i < last; ++i) {
        const f32 e = (bez(c, u[i - first]) - d[i]).length_sq();
        if (e >= m) { m = e; split = i; }
    }
    return m;
}

void fit(const std::vector<Vec2>& d, usize first, usize last, Vec2 t1, Vec2 t2, f32 err, std::vector<Cubic>& out, int depth) {
    const usize n = last - first + 1;
    if (n == 2) {
        const f32 dist = (d[last] - d[first]).length() / 3.0f;
        out.push_back(Cubic{d[first], d[first] + t1 * dist, d[last] + t2 * dist, d[last]});
        return;
    }
    // Parametrização pelo comprimento da corda.
    std::vector<f32> u(n);
    u[0] = 0;
    for (usize i = 1; i < n; ++i) u[i] = u[i - 1] + (d[first + i] - d[first + i - 1]).length();
    for (usize i = 1; i < n; ++i) u[i] = u[n - 1] > 0 ? u[i] / u[n - 1] : 0;
    Cubic c = generate(d, first, last, u, t1, t2);
    usize split = 0;
    f32 e = max_error(d, first, last, c, u, split);
    const f32 err2 = err * err;
    if (e < err2) { out.push_back(c); return; }
    if (e < err2 * 16.0f) {
        // Reparametrização de Newton-Raphson (até 4 voltas).
        for (int it = 0; it < 4; ++it) {
            for (usize i = 0; i < n; ++i) {
                const Vec2 q = bez(c, u[i]) - d[first + i], q1 = bez1(c, u[i]), q2 = bez2(c, u[i]);
                const f32 num = q.dot(q1), den = q1.dot(q1) + q.dot(q2);
                if (std::fabs(den) > 1e-12f) u[i] = std::clamp(u[i] - num / den, 0.0f, 1.0f);
            }
            c = generate(d, first, last, u, t1, t2);
            e = max_error(d, first, last, c, u, split);
            if (e < err2) { out.push_back(c); return; }
        }
    }
    if (depth > 24) { out.push_back(c); return; }
    // Divide no ponto de maior erro, com a tangente central.
    split = std::clamp<usize>(split, first + 1, last - 1);
    Vec2 tc = (d[split - 1] - d[split + 1]).normalized();
    if (tc.length_sq() < 1e-12f) tc = left_normal((d[split] - d[split - 1]).normalized());
    fit(d, first, split, t1, tc, err, out, depth + 1);
    fit(d, split, last, -tc, t2, err, out, depth + 1);
}
} // namespace

BezierPath fit_curve(const std::vector<Vec2>& raw, f32 error, bool closeIfNear) {
    BezierPath out;
    std::vector<Vec2> d;
    const f32 minStep = std::max(0.5f, error * 0.25f);
    for (Vec2 p : raw) if (d.empty() || (p - d.back()).length() >= minStep) d.push_back(p);
    if (d.size() < 2) {
        if (!d.empty()) out.v.push_back(BezierVertex{d[0], {}, {}});
        out.closed = false;
        return out;
    }
    bool closed = false;
    if (closeIfNear && d.size() > 3) {
        f32 len = 0;
        for (usize i = 1; i < d.size(); ++i) len += (d[i] - d[i - 1]).length();
        if ((d.back() - d.front()).length() < std::max(error * 3.0f, std::min(24.0f, len * 0.08f))) {
            closed = true;
            d.back() = d.front();
        }
    }
    const usize n = d.size();
    Vec2 t1 = (d[1] - d[0]).normalized(), t2 = (d[n - 2] - d[n - 1]).normalized();
    if (closed) {
        // Tangente única no ponto de fecho (curva suave na emenda).
        const Vec2 t = (d[1] - d[n - 2]).normalized();
        t1 = t;
        t2 = -t;
    }
    std::vector<Cubic> cubics;
    fit(d, 0, n - 1, t1, t2, std::max(0.1f, error), cubics, 0);
    out.closed = closed;
    for (usize i = 0; i < cubics.size(); ++i) {
        const Cubic& c = cubics[i];
        if (i == 0) out.v.push_back(BezierVertex{c.p0, {}, c.p1 - c.p0});
        else out.v.back().out = c.p1 - c.p0;
        out.v.push_back(BezierVertex{c.p3, c.p2 - c.p3, {}});
    }
    if (closed && out.v.size() > 1) {
        // O último vértice é o primeiro: funde as tangentes.
        out.v.front().in = out.v.back().in;
        out.v.pop_back();
    }
    return out;
}

} // namespace aurea::vector
