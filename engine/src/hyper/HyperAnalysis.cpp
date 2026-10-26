// =============================================================================
//  Aurea / hyper / HyperAnalysis.cpp — pente (entrelaçado) e estabilização.
//  Código próprio (nenhum filtro do FFmpeg nem de outro aplicativo).
// =============================================================================
#include "HyperAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace aurea::hyper {

CombScore comb_score(const u8* luma, u32 width, u32 height, u32 stride) noexcept {
    CombScore s;
    if (!luma || width < 4 || height < 8) return s;
    f64 inter = 0.0, intra = 0.0;
    u64 n = 0;
    // Amostra a cada 2 colunas: a métrica é de linhas, não de detalhe fino.
    for (u32 y = 2; y + 2 < height; ++y) {
        const u8* m2 = luma + static_cast<usize>(y - 2) * stride;
        const u8* m1 = luma + static_cast<usize>(y - 1) * stride;
        const u8* c0 = luma + static_cast<usize>(y) * stride;
        const u8* p1 = luma + static_cast<usize>(y + 1) * stride;
        const u8* p2 = luma + static_cast<usize>(y + 2) * stride;
        for (u32 x = 0; x < width; x += 2) {
            inter += std::fabs(static_cast<f64>(c0[x]) - 0.5 * (static_cast<f64>(m1[x]) + p1[x]));
            intra += std::fabs(static_cast<f64>(c0[x]) - 0.5 * (static_cast<f64>(m2[x]) + p2[x]));
            ++n;
        }
    }
    if (!n) return s;
    s.inter = inter / static_cast<f64>(n);
    s.intra = intra / static_cast<f64>(n);
    return s;
}

namespace {

/// Resposta de canto (menor autovalor do tensor de estrutura 5×5, passo 1).
f32 corner_response(const tracking::Gray& g, i32 cx, i32 cy) noexcept {
    f32 xx = 0, yy = 0, xy = 0;
    for (i32 y = cy - 2; y <= cy + 2; ++y)
        for (i32 x = cx - 2; x <= cx + 2; ++x) {
            const f32 ix = 0.5f * (g.at(x + 1, y) - g.at(x - 1, y));
            const f32 iy = 0.5f * (g.at(x, y + 1) - g.at(x, y - 1));
            xx += ix * ix;
            yy += iy * iy;
            xy += ix * iy;
        }
    const f32 tr = 0.5f * (xx + yy);
    const f32 det = std::sqrt(std::max(0.0f, 0.25f * (xx - yy) * (xx - yy) + xy * xy));
    return tr - det;
}

struct Pair { f64 x, y, u, v; };   ///< ponto em prev (x,y) → cur (u,v), centrados

/// Semelhança por mínimos quadrados (fechada, coordenadas centradas).
bool fit_similarity(const std::vector<Pair>& pts, const std::vector<u8>& use, f64& a, f64& b, f64& tx, f64& ty) {
    f64 mx = 0, my = 0, mu = 0, mv = 0;
    u32 n = 0;
    for (usize i = 0; i < pts.size(); ++i) {
        if (!use[i]) continue;
        mx += pts[i].x; my += pts[i].y; mu += pts[i].u; mv += pts[i].v;
        ++n;
    }
    if (n < 3) return false;
    mx /= n; my /= n; mu /= n; mv /= n;
    f64 sqq = 0, sa = 0, sb = 0;
    for (usize i = 0; i < pts.size(); ++i) {
        if (!use[i]) continue;
        const f64 qx = pts[i].x - mx, qy = pts[i].y - my, rx = pts[i].u - mu, ry = pts[i].v - mv;
        sqq += qx * qx + qy * qy;
        sa += qx * rx + qy * ry;
        sb += qx * ry - qy * rx;
    }
    if (sqq < 1e-9) {
        a = 1.0; b = 0.0;
    } else {
        a = sa / sqq;
        b = sb / sqq;
    }
    // t = média(q') − M·média(q)
    tx = mu - (a * mx - b * my);
    ty = mv - (b * mx + a * my);
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(tx) && std::isfinite(ty);
}

} // namespace

Motion2D estimate_motion(const tracking::Gray& prev, const tracking::Gray& cur, u32 points) {
    Motion2D m;
    if (prev.width < 32 || prev.height < 32 || cur.width != prev.width || cur.height != prev.height) return m;
    const i32 W = static_cast<i32>(prev.width), H = static_cast<i32>(prev.height);
    const f64 aspect = static_cast<f64>(W) / H;
    const i32 gx = std::max(4, static_cast<i32>(std::lround(std::sqrt(points * aspect))));
    const i32 gy = std::max(3, static_cast<i32>(std::lround(static_cast<f64>(points) / gx)));
    constexpr i32 kHalf = 6, kRadius = 12, kMargin = 8;
    const f64 cx = 0.5 * (W - 1), cy = 0.5 * (H - 1);
    std::vector<Pair> pts;
    pts.reserve(static_cast<usize>(gx * gy));
    for (i32 j = 0; j < gy; ++j)
        for (i32 i = 0; i < gx; ++i) {
            const i32 x0 = kMargin + (W - 2 * kMargin) * i / gx, x1 = kMargin + (W - 2 * kMargin) * (i + 1) / gx;
            const i32 y0 = kMargin + (H - 2 * kMargin) * j / gy, y1 = kMargin + (H - 2 * kMargin) * (j + 1) / gy;
            f32 best = 0.0f;
            i32 bx = -1, by = -1;
            for (i32 y = y0; y < y1; y += 2)
                for (i32 x = x0; x < x1; x += 2) {
                    const f32 r = corner_response(prev, x, y);
                    if (r > best) { best = r; bx = x; by = y; }
                }
            if (bx < 0 || best < 2e-4f) continue;   // célula lisa: nada a rastrear
            const tracking::TrackStep st = tracking::track_step(prev, cur, Vec2{static_cast<f32>(bx), static_cast<f32>(by)},
                                                                kHalf, kRadius);
            if (!(st.score >= 0.75f) || !std::isfinite(st.pos.x) || !std::isfinite(st.pos.y)) continue;
            pts.push_back(Pair{bx - cx, by - cy, st.pos.x - cx, st.pos.y - cy});
        }
    if (pts.size() < 6) return m;
    std::vector<u8> use(pts.size(), 1);
    f64 a = 1, b = 0, tx = 0, ty = 0;
    std::vector<f64> res(pts.size());
    for (int iter = 0; iter < 4; ++iter) {
        if (!fit_similarity(pts, use, a, b, tx, ty)) return m;
        for (usize i = 0; i < pts.size(); ++i) {
            const f64 px = a * pts[i].x - b * pts[i].y + tx, py = b * pts[i].x + a * pts[i].y + ty;
            res[i] = std::hypot(px - pts[i].u, py - pts[i].v);
        }
        std::vector<f64> sorted = res;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const f64 limit = std::max(0.75, 2.5 * sorted[sorted.size() / 2]);
        u32 kept = 0;
        for (usize i = 0; i < pts.size(); ++i) {
            use[i] = res[i] <= limit ? 1 : 0;
            kept += use[i];
        }
        if (kept < 6) return m;
    }
    m.dx = tx;
    m.dy = ty;
    m.angle = std::atan2(b, a);
    m.scale = std::hypot(a, b);
    m.inliers = static_cast<u32>(std::count(use.begin(), use.end(), u8{1}));
    m.valid = m.scale > 0.8 && m.scale < 1.25 && std::fabs(m.angle) < 0.35;
    return m;
}

namespace {

struct Path4 { std::vector<f64> x, y, a, s; };

std::vector<f64> gaussian_smooth(const std::vector<f64>& v, f64 sigma) {
    const i64 n = static_cast<i64>(v.size());
    std::vector<f64> out(v.size(), 0.0);
    if (n == 0) return out;
    const i64 r = std::max<i64>(1, static_cast<i64>(std::ceil(3.0 * sigma)));
    std::vector<f64> w(static_cast<usize>(2 * r + 1));
    for (i64 k = -r; k <= r; ++k) w[static_cast<usize>(k + r)] = std::exp(-0.5 * (k * k) / (sigma * sigma));
    // Extensão ÍMPAR nas pontas (2·v0 − v[−k]): uma panorâmica que chega na
    // borda continua reta (a ponta não "freia").
    auto at = [&](i64 i) {
        if (i < 0) return 2.0 * v[0] - v[static_cast<usize>(std::min<i64>(-i, n - 1))];
        if (i >= n) return 2.0 * v[static_cast<usize>(n - 1)] - v[static_cast<usize>(std::max<i64>(0, 2 * (n - 1) - i))];
        return v[static_cast<usize>(i)];
    };
    for (i64 i = 0; i < n; ++i) {
        f64 acc = 0.0, ws = 0.0;
        for (i64 k = -r; k <= r; ++k) {
            const f64 wk = w[static_cast<usize>(k + r)];
            acc += wk * at(i + k);
            ws += wk;
        }
        out[static_cast<usize>(i)] = acc / ws;
    }
    return out;
}

/// Zoom que esconde as bordas de UMA correção (quadro W×H, centro na origem).
f64 zoom_for(const StabCorrection& c, f64 W, f64 H) noexcept {
    const f64 th = -c.degrees * 3.14159265358979323846 / 180.0;
    const f64 cs = std::cos(th), sn = std::sin(th);
    const f64 hx = W * 0.5 - std::fabs(c.dx), hy = H * 0.5 - std::fabs(c.dy);
    if (hx <= 1.0 || hy <= 1.0) return 1e9;
    f64 z = 1.0;
    for (const f64 qx : {-W * 0.5, W * 0.5})
        for (const f64 qy : {-H * 0.5, H * 0.5}) {
            const f64 rx = (cs * qx - sn * qy) / c.scale, ry = (sn * qx + cs * qy) / c.scale;
            z = std::max({z, std::fabs(rx) / hx, std::fabs(ry) / hy});
        }
    return z;
}

} // namespace

StabPlan smooth_trajectory(const std::vector<Motion2D>& steps, f32 strength, u32 width, u32 height,
                           f64 analysisScale, bool crop) {
    StabPlan plan;
    plan.width = width;
    plan.height = height;
    const usize n = steps.size();
    if (n == 0 || !width || !height) return plan;
    Path4 raw;
    raw.x.resize(n); raw.y.resize(n); raw.a.resize(n); raw.s.resize(n);
    f64 X = 0, Y = 0, A = 0, S = 0;
    for (usize k = 0; k < n; ++k) {
        if (k > 0 && steps[k].valid) {
            X += steps[k].dx * analysisScale;
            Y += steps[k].dy * analysisScale;
            A += steps[k].angle;
            S += std::log(std::max(1e-3, steps[k].scale));
        }
        raw.x[k] = X; raw.y[k] = Y; raw.a[k] = A; raw.s[k] = S;
    }
    const f64 sigma = 3.0 + 42.0 * std::clamp(static_cast<f64>(strength), 0.0, 1.0);
    const auto sx = gaussian_smooth(raw.x, sigma), sy = gaussian_smooth(raw.y, sigma);
    const auto sa = gaussian_smooth(raw.a, sigma), ss = gaussian_smooth(raw.s, sigma);
    std::vector<StabCorrection> full(n);
    for (usize k = 0; k < n; ++k) {
        full[k].dx = static_cast<f32>(sx[k] - raw.x[k]);
        full[k].dy = static_cast<f32>(sy[k] - raw.y[k]);
        full[k].degrees = static_cast<f32>((sa[k] - raw.a[k]) * 180.0 / 3.14159265358979323846);
        full[k].scale = static_cast<f32>(std::exp(ss[k] - raw.s[k]));
    }
    auto scaled = [&](f64 f) {
        std::vector<StabCorrection> c(n);
        for (usize k = 0; k < n; ++k) {
            c[k].dx = static_cast<f32>(full[k].dx * f);
            c[k].dy = static_cast<f32>(full[k].dy * f);
            c[k].degrees = static_cast<f32>(full[k].degrees * f);
            c[k].scale = static_cast<f32>(std::exp(std::log(full[k].scale) * f));
        }
        return c;
    };
    auto zoom_of = [&](const std::vector<StabCorrection>& c) {
        f64 z = 1.0;
        for (const auto& k : c) z = std::max(z, zoom_for(k, width, height));
        return z;
    };
    constexpr f64 kMaxZoom = 1.3;
    plan.frames = full;
    if (!crop) return plan;
    f64 z = zoom_of(plan.frames);
    if (z > kMaxZoom) {
        // Correção grande demais para o teto: reduz até o recorte caber.
        f64 lo = 0.0, hi = 1.0;
        for (int it = 0; it < 24; ++it) {
            const f64 mid = 0.5 * (lo + hi);
            if (zoom_of(scaled(mid)) <= kMaxZoom) lo = mid; else hi = mid;
        }
        plan.frames = scaled(lo);
        z = zoom_of(plan.frames);
    }
    plan.zoom = static_cast<f32>(std::min(z, kMaxZoom));
    return plan;
}

} // namespace aurea::hyper
