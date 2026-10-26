// =============================================================================
//  Aurea / hyper / HyperTurbo.cpp — FSR 1 (EASU + RCAS) na CPU para o grafo.
//  O mesmo algoritmo de shaders/hyper/fsr_easu.frag e fsr_rcas.frag (portado
//  de ffx_fsr1.h, AMD, MIT — engine/assets/licenses/THIRD_PARTY_HYPER.md).
// =============================================================================
#include "aurea/hyper/HyperTurbo.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace aurea::hyper {

namespace {

struct Rgb { f32 r, g, b; };

inline Rgb load(const u8* src, u32 w, u32 h, i32 x, i32 y) noexcept {
    x = std::clamp(x, 0, static_cast<i32>(w) - 1);
    y = std::clamp(y, 0, static_cast<i32>(h) - 1);
    const u8* p = src + (static_cast<usize>(y) * w + static_cast<usize>(x)) * 3;
    constexpr f32 k = 1.0f / 255.0f;
    return Rgb{p[0] * k, p[1] * k, p[2] * k};
}
inline f32 luma2(const Rgb& c) noexcept { return c.b * 0.5f + (c.r * 0.5f + c.g); }
inline u8 to8(f32 v) noexcept { return static_cast<u8>(std::clamp(std::lround(v * 255.0f), 0l, 255l)); }

inline void easu_set(f32& dirX, f32& dirY, f32& len, f32 w, f32 lA, f32 lB, f32 lC, f32 lD, f32 lE) noexcept {
    const f32 dc = lD - lC, cb = lC - lB;
    f32 lenX = 1.0f / std::max(std::max(std::fabs(dc), std::fabs(cb)), 1.0f / 65536.0f);
    const f32 dx = lD - lB;
    dirX += dx * w;
    lenX = std::clamp(std::fabs(dx) * lenX, 0.0f, 1.0f);
    len += lenX * lenX * w;
    const f32 ec = lE - lC, ca = lC - lA;
    f32 lenY = 1.0f / std::max(std::max(std::fabs(ec), std::fabs(ca)), 1.0f / 65536.0f);
    const f32 dy = lE - lA;
    dirY += dy * w;
    lenY = std::clamp(std::fabs(dy) * lenY, 0.0f, 1.0f);
    len += lenY * lenY * w;
}

inline void easu_tap(Rgb& acc, f32& aw, f32 ox, f32 oy, f32 dx, f32 dy, f32 lx, f32 ly, f32 lob, f32 clp,
                     const Rgb& c) noexcept {
    f32 vx = ox * dx + oy * dy;
    f32 vy = ox * -dy + oy * dx;
    vx *= lx;
    vy *= ly;
    const f32 d2 = std::min(vx * vx + vy * vy, clp);
    f32 wB = 0.4f * d2 - 1.0f, wA = lob * d2 - 1.0f;
    wB *= wB;
    wA *= wA;
    wB = 1.5625f * wB - 0.5625f;
    const f32 wt = wB * wA;
    acc.r += c.r * wt; acc.g += c.g * wt; acc.b += c.b * wt;
    aw += wt;
}

void easu_rows(const u8* src, u32 w, u32 h, u8* dst, u32 dw, u32 dh, u32 y0, u32 y1) noexcept {
    const f32 sx = static_cast<f32>(w) / static_cast<f32>(dw), sy = static_cast<f32>(h) / static_cast<f32>(dh);
    for (u32 oy = y0; oy < y1; ++oy) {
        const f32 ipy = (static_cast<f32>(oy) + 0.5f) * sy - 0.5f;
        const f32 fpy = std::floor(ipy), ppy = ipy - fpy;
        const i32 by = static_cast<i32>(fpy);
        for (u32 ox = 0; ox < dw; ++ox) {
            const f32 ipx = (static_cast<f32>(ox) + 0.5f) * sx - 0.5f;
            const f32 fpx = std::floor(ipx), ppx = ipx - fpx;
            const i32 bx = static_cast<i32>(fpx);
            const Rgb b = load(src, w, h, bx, by - 1), c = load(src, w, h, bx + 1, by - 1);
            const Rgb e = load(src, w, h, bx - 1, by), f = load(src, w, h, bx, by);
            const Rgb g = load(src, w, h, bx + 1, by), hh = load(src, w, h, bx + 2, by);
            const Rgb i = load(src, w, h, bx - 1, by + 1), j = load(src, w, h, bx, by + 1);
            const Rgb k = load(src, w, h, bx + 1, by + 1), l = load(src, w, h, bx + 2, by + 1);
            const Rgb n = load(src, w, h, bx, by + 2), o = load(src, w, h, bx + 1, by + 2);
            const f32 bL = luma2(b), cL = luma2(c), eL = luma2(e), fL = luma2(f), gL = luma2(g), hL = luma2(hh);
            const f32 iL = luma2(i), jL = luma2(j), kL = luma2(k), lL = luma2(l), nL = luma2(n), oL = luma2(o);
            f32 dX = 0, dY = 0, len = 0;
            easu_set(dX, dY, len, (1 - ppx) * (1 - ppy), bL, eL, fL, gL, jL);
            easu_set(dX, dY, len, ppx * (1 - ppy), cL, fL, gL, hL, kL);
            easu_set(dX, dY, len, (1 - ppx) * ppy, fL, iL, jL, kL, nL);
            easu_set(dX, dY, len, ppx * ppy, gL, jL, kL, lL, oL);
            f32 dirR = dX * dX + dY * dY;
            const bool zro = dirR < 1.0f / 32768.0f;
            dirR = zro ? 1.0f : 1.0f / std::sqrt(dirR);
            if (zro) dX = 1.0f;
            dX *= dirR;
            dY *= dirR;
            len = len * 0.5f;
            len *= len;
            const f32 stretch = (dX * dX + dY * dY) / std::max(std::max(std::fabs(dX), std::fabs(dY)), 1e-6f);
            const f32 lx = 1.0f + (stretch - 1.0f) * len, ly = 1.0f - 0.5f * len;
            const f32 lob = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
            const f32 clp = 1.0f / lob;
            Rgb acc{0, 0, 0};
            f32 aw = 0;
            easu_tap(acc, aw, 0 - ppx, -1 - ppy, dX, dY, lx, ly, lob, clp, b);
            easu_tap(acc, aw, 1 - ppx, -1 - ppy, dX, dY, lx, ly, lob, clp, c);
            easu_tap(acc, aw, -1 - ppx, 1 - ppy, dX, dY, lx, ly, lob, clp, i);
            easu_tap(acc, aw, 0 - ppx, 1 - ppy, dX, dY, lx, ly, lob, clp, j);
            easu_tap(acc, aw, 0 - ppx, 0 - ppy, dX, dY, lx, ly, lob, clp, f);
            easu_tap(acc, aw, -1 - ppx, 0 - ppy, dX, dY, lx, ly, lob, clp, e);
            easu_tap(acc, aw, 1 - ppx, 1 - ppy, dX, dY, lx, ly, lob, clp, k);
            easu_tap(acc, aw, 2 - ppx, 1 - ppy, dX, dY, lx, ly, lob, clp, l);
            easu_tap(acc, aw, 2 - ppx, 0 - ppy, dX, dY, lx, ly, lob, clp, hh);
            easu_tap(acc, aw, 1 - ppx, 0 - ppy, dX, dY, lx, ly, lob, clp, g);
            easu_tap(acc, aw, 1 - ppx, 2 - ppy, dX, dY, lx, ly, lob, clp, o);
            easu_tap(acc, aw, 0 - ppx, 2 - ppy, dX, dY, lx, ly, lob, clp, n);
            const f32 inv = 1.0f / (std::fabs(aw) > 1e-6f ? aw : 1e-6f);
            auto fit = [&](f32 v, f32 a0, f32 a1, f32 a2, f32 a3) {
                const f32 mn = std::min(std::min(a0, a1), std::min(a2, a3)), mx = std::max(std::max(a0, a1), std::max(a2, a3));
                return std::min(mx, std::max(mn, v * inv));
            };
            u8* q = dst + (static_cast<usize>(oy) * dw + ox) * 3;
            q[0] = to8(fit(acc.r, f.r, g.r, j.r, k.r));
            q[1] = to8(fit(acc.g, f.g, g.g, j.g, k.g));
            q[2] = to8(fit(acc.b, f.b, g.b, j.b, k.b));
        }
    }
}

void rcas_rows(const u8* src, u32 w, u32 h, u8* dst, f32 con, u32 y0, u32 y1) noexcept {
    constexpr f32 kLimit = 0.25f - 1.0f / 16.0f;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = 0; x < w; ++x) {
            const i32 xi = static_cast<i32>(x), yi = static_cast<i32>(y);
            const Rgb b = load(src, w, h, xi, yi - 1), d = load(src, w, h, xi - 1, yi), e = load(src, w, h, xi, yi);
            const Rgb f = load(src, w, h, xi + 1, yi), hh = load(src, w, h, xi, yi + 1);
            auto lobe_of = [&](f32 bb, f32 dd, f32 ee, f32 ff, f32 hv) {
                const f32 mn = std::min(std::min(bb, dd), std::min(ff, hv)), mx = std::max(std::max(bb, dd), std::max(ff, hv));
                const f32 hitMin = std::min(mn, ee) / std::max(4.0f * mx, 1.0f / 65536.0f);
                const f32 hitMax = (1.0f - std::max(mx, ee)) / std::min(4.0f * mn - 4.0f, -1.0f / 65536.0f);
                return std::max(-hitMin, hitMax);
            };
            const f32 lr = lobe_of(b.r, d.r, e.r, f.r, hh.r), lg = lobe_of(b.g, d.g, e.g, f.g, hh.g);
            const f32 lb = lobe_of(b.b, d.b, e.b, f.b, hh.b);
            // Ruído: o lobo cai onde o centro destoa do anel.
            const f32 bL = luma2(b), dL = luma2(d), eL = luma2(e), fL = luma2(f), hL = luma2(hh);
            const f32 range = std::max({bL, dL, eL, fL, hL}) - std::min({bL, dL, eL, fL, hL});
            f32 nz = std::clamp(std::fabs(0.25f * (bL + dL + fL + hL) - eL) / std::max(range, 1.0f / 4096.0f), 0.0f, 1.0f);
            nz = -0.5f * nz + 1.0f;
            const f32 lobe = std::max(-kLimit, std::min(std::max({lr, lg, lb}), 0.0f)) * con * nz;
            const f32 rcp = 1.0f / (4.0f * lobe + 1.0f);
            u8* q = dst + (static_cast<usize>(y) * w + x) * 3;
            q[0] = to8((lobe * (b.r + d.r + hh.r + f.r) + e.r) * rcp);
            q[1] = to8((lobe * (b.g + d.g + hh.g + f.g) + e.g) * rcp);
            q[2] = to8((lobe * (b.b + d.b + hh.b + f.b) + e.b) * rcp);
        }
}

template <typename Fn>
void parallel_rows(u32 rows, u32 threads, Fn fn) noexcept {
    threads = std::clamp<u32>(threads, 1, 8);
    if (threads == 1 || rows < 32) { fn(0u, rows); return; }
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    const u32 step = (rows + threads - 1) / threads;
    for (u32 t = 1; t < threads; ++t) {
        const u32 a = t * step, b = std::min(rows, a + step);
        if (a >= b) break;
        pool.emplace_back([=] { fn(a, b); });
    }
    fn(0u, std::min(rows, step));
    for (auto& th : pool) th.join();
}

} // namespace

void fsr_upscale_rgb(const u8* src, u32 w, u32 h, u8* dst, u32 dw, u32 dh, f32 sharpness, u32 threads) noexcept {
    if (!src || !dst || !w || !h || !dw || !dh) return;
    sharpness = std::clamp(std::isfinite(sharpness) ? sharpness : 0.5f, 0.0f, 1.0f);
    if (sharpness <= 0.0f) {
        parallel_rows(dh, threads, [&](u32 a, u32 b) { easu_rows(src, w, h, dst, dw, dh, a, b); });
        return;
    }
    thread_local std::vector<u8> mid;
    mid.resize(static_cast<usize>(dw) * dh * 3);
    u8* m = mid.data();
    parallel_rows(dh, threads, [&](u32 a, u32 b) { easu_rows(src, w, h, m, dw, dh, a, b); });
    const f32 con = std::exp2(-2.0f * (1.0f - sharpness));
    parallel_rows(dh, threads, [&](u32 a, u32 b) { rcas_rows(m, dw, dh, dst, con, a, b); });
}

} // namespace aurea::hyper
