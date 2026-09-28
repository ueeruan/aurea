#include "aurea/tracking/PointTracker.hpp"
#include "aurea/media/VideoTypes.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::tracking {

Gray to_gray(const u8* rgba, u32 width, u32 height) {
    Gray g;
    g.width = width;
    g.height = height;
    g.px.resize(static_cast<usize>(width) * height);
    for (usize i = 0; i < g.px.size(); ++i) {
        const u8* p = rgba + i * 4;
        g.px[i] = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
    }
    return g;
}

bool frame_to_gray(const DecodedFrame& f, u32 height, Gray& out) {
    if (!f.planes[0] || !f.width || !f.height || !height) return false;
    const bool rgba = f.format == PixelFormat::RGBA8;
    const bool tenBit = f.format == PixelFormat::P010;
    if (!rgba && !tenBit && f.format != PixelFormat::NV12 && f.format != PixelFormat::NV21 && f.format != PixelFormat::YUV420P)
        return false;
    const u32 vw = std::min(f.visibleWidth ? f.visibleWidth : f.width, f.width - std::min(f.cropLeft, f.width - 1));
    const u32 vh = std::min(f.visibleHeight ? f.visibleHeight : f.height, f.height - std::min(f.cropTop, f.height - 1));
    const u32 stride = f.strides[0] ? f.strides[0] : f.width * (rgba ? 4u : tenBit ? 2u : 1u);
    const bool sideways = f.rotation == 90 || f.rotation == 270;
    const u32 dispW = sideways ? vh : vw, dispH = sideways ? vw : vh;
    const u32 oh = std::min(height, dispH);
    const u32 ow = std::max<u32>(1, static_cast<u32>(std::lround(static_cast<f64>(oh) * dispW / dispH)));
    // Média da área de cada pixel de saída no quadro CODIFICADO, depois a
    // rotação da imagem pequena (troca de índices, sem reamostrar). Em 4K a
    // área tem 6×6 pixels: amostra no máximo ~3×3 dela, centrada (custo
    // limitado por quadro no aparelho; o serrilhado continua de fora).
    const u32 cw = sideways ? oh : ow, ch = sideways ? ow : oh;
    std::vector<u32> x0(cw + 1), y0(ch + 1);
    for (u32 i = 0; i <= cw; ++i) x0[i] = static_cast<u32>(static_cast<u64>(i) * vw / cw);
    for (u32 i = 0; i <= ch; ++i) y0[i] = static_cast<u32>(static_cast<u64>(i) * vh / ch);
    const u32 stepX = std::max(1u, (vw / cw + 2) / 3), stepY = std::max(1u, (vh / ch + 2) / 3);
    std::vector<f32> small(static_cast<usize>(cw) * ch, 0.0f);
    auto luma = [&](const u8* line, u32 x) -> f64 {
        if (rgba) { const u8* p = line + static_cast<usize>(x) * 4; return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; }
        if (tenBit) { const u8* p = line + static_cast<usize>(x) * 2; return static_cast<f64>(static_cast<u16>(p[0] | (p[1] << 8)) >> 6) * (255.0 / 1023.0); }
        return line[x];
    };
    // Primeira amostra e passo dentro de [a, b): as amostras ficam centradas na área.
    auto first = [](u32 a, u32 b, u32 step) { const u32 n = (b - a + step - 1) / step; return a + ((b - a - 1) - (n - 1) * step) / 2; };
    for (u32 oy = 0; oy < ch; ++oy) {
        const u32 ya = std::min(y0[oy], vh - 1), yb = std::min(std::max(y0[oy + 1], ya + 1), vh);
        const u32 yStart = first(ya, yb, stepY);
        for (u32 ox = 0; ox < cw; ++ox) {
            const u32 xa = std::min(x0[ox], vw - 1), xb = std::min(std::max(x0[ox + 1], xa + 1), vw);
            const u32 xStart = first(xa, xb, stepX);
            f64 sum = 0;
            u32 count = 0;
            for (u32 y = yStart; y < yb; y += stepY) {
                const u8* line = f.planes[0] + static_cast<usize>(f.cropTop + y) * stride;
                for (u32 x = xStart; x < xb; x += stepX) { sum += luma(line, f.cropLeft + x); ++count; }
            }
            small[static_cast<usize>(oy) * cw + ox] = count ? static_cast<f32>(sum / count / 255.0) : 0.0f;
        }
    }
    out.width = ow;
    out.height = oh;
    out.px.resize(static_cast<usize>(ow) * oh);
    for (u32 y = 0; y < oh; ++y)
        for (u32 x = 0; x < ow; ++x) {
            // Exibição (x, y) ← codificado (a mesma convenção do shader de vídeo).
            u32 sx = x, sy = y;
            switch (f.rotation) {
                case 90:  sx = y;          sy = ch - 1 - x; break;
                case 180: sx = cw - 1 - x; sy = ch - 1 - y; break;
                case 270: sx = cw - 1 - y; sy = x;          break;
                default: break;
            }
            out.px[static_cast<usize>(y) * ow + x] = small[static_cast<usize>(sy) * cw + sx];
        }
    return true;
}

namespace {

/// Bloco bilinear em (cx, cy) — o ponto anterior é subpixel.
std::vector<f32> patch(const Gray& g, Vec2 c, i32 half) {
    const i32 n = 2 * half + 1;
    std::vector<f32> out(static_cast<usize>(n * n));
    const f32 fx = c.x - std::floor(c.x), fy = c.y - std::floor(c.y);
    const i32 ix = static_cast<i32>(std::floor(c.x)), iy = static_cast<i32>(std::floor(c.y));
    for (i32 y = -half; y <= half; ++y) {
        for (i32 x = -half; x <= half; ++x) {
            const f32 a = g.at(ix + x, iy + y), b = g.at(ix + x + 1, iy + y);
            const f32 cc = g.at(ix + x, iy + y + 1), d = g.at(ix + x + 1, iy + y + 1);
            out[static_cast<usize>((y + half) * n + (x + half))] =
                (a * (1 - fx) + b * fx) * (1 - fy) + (cc * (1 - fx) + d * fx) * fy;
        }
    }
    return out;
}

} // namespace

TrackStep track_step(const Gray& prev, const Gray& cur, Vec2 from, i32 half, i32 radius, Vec2 searchCenter, bool centerWeighted) {
    TrackStep best;
    best.pos = from;
    best.score = -2.0f;
    if (!prev.width || !prev.height || !cur.width || !cur.height ||
        prev.px.size()!=static_cast<usize>(prev.width)*prev.height || cur.px.size()!=static_cast<usize>(cur.width)*cur.height ||
        !std::isfinite(from.x) || !std::isfinite(from.y)) return best;
    half=std::clamp(half,2,32);radius=std::clamp(radius,1,96);
    const i32 n = 2 * half + 1;
    const std::vector<f32> t = patch(prev, from, half);
    std::vector<f64> wt(t.size(), 1.0);
    if (centerWeighted) {
        const f64 sigma = std::max(1.0, 0.5 * half);
        for (i32 y = -half; y <= half; ++y)
            for (i32 x = -half; x <= half; ++x)
                wt[static_cast<usize>((y + half) * n + (x + half))] = std::exp(-(x * x + y * y) / (2 * sigma * sigma));
    }
    f64 wsum = 0, tm = 0;
    for (usize i = 0; i < t.size(); ++i) { tm += wt[i] * t[i]; wsum += wt[i]; }
    tm /= wsum;
    f64 tv = 0;
    for (usize i = 0; i < t.size(); ++i) tv += wt[i] * (t[i] - tm) * (t[i] - tm);
    if (tv < 1e-8 * wsum / static_cast<f64>(t.size())) return best;   // bloco liso: nada para seguir
    if(!std::isfinite(searchCenter.x)||!std::isfinite(searchCenter.y))searchCenter=from;
    const i32 cx = static_cast<i32>(std::lround(std::clamp(searchCenter.x,0.f,static_cast<f32>(cur.width-1))));
    const i32 cy = static_cast<i32>(std::lround(std::clamp(searchCenter.y,0.f,static_cast<f32>(cur.height-1))));
    const i32 w = 2 * radius + 1;
    std::vector<f32> score(static_cast<usize>(w * w), -2.0f);
    i32 bx = 0, by = 0;
    for (i32 dy = -radius; dy <= radius; ++dy) {
        for (i32 dx = -radius; dx <= radius; ++dx) {
            if(cx+dx-half<0 || cy+dy-half<0 || cx+dx+half>=static_cast<i32>(cur.width) || cy+dy+half>=static_cast<i32>(cur.height))continue;
            f64 sm = 0;
            for (i32 y = -half; y <= half; ++y)
                for (i32 x = -half; x <= half; ++x) sm += wt[static_cast<usize>((y + half) * n + (x + half))] * cur.at(cx + dx + x, cy + dy + y);
            sm /= wsum;
            f64 cross = 0, sv = 0;
            for (i32 y = -half; y <= half; ++y) {
                for (i32 x = -half; x <= half; ++x) {
                    const usize k = static_cast<usize>((y + half) * n + (x + half));
                    const f64 a = t[k] - tm;
                    const f64 b = cur.at(cx + dx + x, cy + dy + y) - sm;
                    cross += wt[k] * a * b;
                    sv += wt[k] * b * b;
                }
            }
            const f32 s = sv > 1e-10 ? static_cast<f32>(cross / std::sqrt(tv * sv)) : -1.0f;
            score[static_cast<usize>((dy + radius) * w + (dx + radius))] = s;
            if (s > best.score) { best.score = s; bx = dx; by = dy; }
        }
    }
    // Refino subpixel: parábola pelos vizinhos do pico.
    auto sc = [&](i32 dx, i32 dy) {
        dx = std::clamp(dx, -radius, radius);
        dy = std::clamp(dy, -radius, radius);
        return score[static_cast<usize>((dy + radius) * w + (dx + radius))];
    };
    auto refine = [](f32 l, f32 c, f32 r) {
        const f32 den = l - 2.0f * c + r;
        return std::fabs(den) > 1e-6f ? std::clamp(0.5f * (l - r) / den, -0.5f, 0.5f) : 0.0f;
    };
    const f32 ox = (bx > -radius && bx < radius) ? refine(sc(bx - 1, by), sc(bx, by), sc(bx + 1, by)) : 0.0f;
    const f32 oy = (by > -radius && by < radius) ? refine(sc(bx, by - 1), sc(bx, by), sc(bx, by + 1)) : 0.0f;
    // O bloco foi tirado CENTRADO em `from` (bilinear): o centro do melhor
    // bloco do quadro atual é onde o ponto está agora.
    best.pos = Vec2{static_cast<f32>(cx + bx) + ox, static_cast<f32>(cy + by) + oy};
    return best;
}


namespace {

constexpr f64 kPiT = 3.14159265358979323846;

/// Metade da resolução (média 2×2); o pixel (x, y) cobre (2x..2x+1, 2y..2y+1).
Gray half_res(const Gray& g) {
    Gray o;
    o.width = std::max(1u, g.width / 2);
    o.height = std::max(1u, g.height / 2);
    o.px.resize(static_cast<usize>(o.width) * o.height);
    for (u32 y = 0; y < o.height; ++y)
        for (u32 x = 0; x < o.width; ++x) {
            const i32 X = static_cast<i32>(2 * x), Y = static_cast<i32>(2 * y);
            o.px[static_cast<usize>(y) * o.width + x] = 0.25f * (g.at(X, Y) + g.at(X + 1, Y) + g.at(X, Y + 1) + g.at(X + 1, Y + 1));
        }
    return o;
}

/// NCC ponderada do bloco `t` (pesos `w`) na posição inteira (px, py) de `g`.
/// Com `resid`, devolve também o resíduo normalizado por pixel.
f32 weighted_ncc(const std::vector<f32>& t, const std::vector<f32>& w, i32 half, const Gray& g, i32 px, i32 py,
                 std::vector<f32>* resid = nullptr) {
    const i32 n = 2 * half + 1;
    f64 ws = 0, mt = 0, mp = 0;
    for (i32 y = -half; y <= half; ++y)
        for (i32 x = -half; x <= half; ++x) {
            const usize k = static_cast<usize>((y + half) * n + (x + half));
            ws += w[k];
            mt += w[k] * t[k];
            mp += w[k] * g.at(px + x, py + y);
        }
    if (ws <= 0) return -1.0f;
    mt /= ws;
    mp /= ws;
    f64 vt = 0, vp = 0, cr = 0;
    for (i32 y = -half; y <= half; ++y)
        for (i32 x = -half; x <= half; ++x) {
            const usize k = static_cast<usize>((y + half) * n + (x + half));
            const f64 a = t[k] - mt, b = g.at(px + x, py + y) - mp;
            vt += w[k] * a * a;
            vp += w[k] * b * b;
            cr += w[k] * a * b;
        }
    if (vt <= 1e-10 * ws || vp <= 1e-10 * ws) return -1.0f;
    if (resid) {
        const f64 st = std::sqrt(vt / ws), sp = std::sqrt(vp / ws);
        resid->resize(t.size());
        for (i32 y = -half; y <= half; ++y)
            for (i32 x = -half; x <= half; ++x) {
                const usize k = static_cast<usize>((y + half) * n + (x + half));
                (*resid)[k] = static_cast<f32>((t[k] - mt) / st - (g.at(px + x, py + y) - mp) / sp);
            }
    }
    return static_cast<f32>(cr / std::sqrt(vt * vp));
}

/// Melhor posição inteira numa janela ±radius em volta de `center` e o
/// refino subpixel pela parábola dos vizinhos.
TrackStep weighted_search(const std::vector<f32>& t, const std::vector<f32>& w, i32 half, const Gray& g, Vec2 center, i32 radius) {
    TrackStep best;
    best.score = -2.0f;
    const i32 cx = static_cast<i32>(std::lround(std::clamp(center.x, 0.f, static_cast<f32>(g.width - 1))));
    const i32 cy = static_cast<i32>(std::lround(std::clamp(center.y, 0.f, static_cast<f32>(g.height - 1))));
    const i32 side = 2 * radius + 1;
    std::vector<f32> score(static_cast<usize>(side) * side, -2.0f);
    i32 bx = 0, by = 0;
    for (i32 dy = -radius; dy <= radius; ++dy)
        for (i32 dx = -radius; dx <= radius; ++dx) {
            const f32 s = weighted_ncc(t, w, half, g, cx + dx, cy + dy);
            score[static_cast<usize>((dy + radius) * side + (dx + radius))] = s;
            if (s > best.score) { best.score = s; bx = dx; by = dy; }
        }
    auto sc = [&](i32 dx, i32 dy) { return score[static_cast<usize>((dy + radius) * side + (dx + radius))]; };
    auto refine = [](f32 l, f32 c, f32 r) {
        const f32 den = l - 2.0f * c + r;
        return std::fabs(den) > 1e-6f ? std::clamp(0.5f * (l - r) / den, -0.5f, 0.5f) : 0.0f;
    };
    const f32 ox = bx > -radius && bx < radius ? refine(sc(bx - 1, by), sc(bx, by), sc(bx + 1, by)) : 0.0f;
    const f32 oy = by > -radius && by < radius ? refine(sc(bx, by - 1), sc(bx, by), sc(bx, by + 1)) : 0.0f;
    best.pos = Vec2{static_cast<f32>(cx + bx) + ox, static_cast<f32>(cy + by) + oy};
    return best;
}

} // namespace

i32 feature_half_at(const Gray& g, Vec2 c, i32 minHalf, i32 maxHalf) noexcept {
    minHalf = std::max(2, minHalf);
    maxHalf = std::max(minHalf, maxHalf);
    if (!g.width || !g.height || !std::isfinite(c.x) || !std::isfinite(c.y)) return minHalf;
    const i32 cx = static_cast<i32>(std::lround(c.x)), cy = static_cast<i32>(std::lround(c.y));
    f64 bestSigma = minHalf / 3.0, bestValue = -1;
    for (f64 sigma = std::max(1.0, minHalf / 3.0); sigma <= maxHalf / 3.0 + 1e-9; sigma *= 1.15) {
        const i32 reach = static_cast<i32>(std::ceil(3.5 * sigma));
        f64 acc = 0;
        for (i32 y = -reach; y <= reach; ++y)
            for (i32 x = -reach; x <= reach; ++x) {
                const f64 r2 = static_cast<f64>(x) * x + static_cast<f64>(y) * y, s2 = sigma * sigma;
                acc += (r2 / s2 - 2.0) / s2 * std::exp(-r2 / (2 * s2)) / (2 * kPiT * s2) * g.at(cx + x, cy + y);
            }
        const f64 value = std::abs(acc) * sigma * sigma;
        if (value > bestValue) { bestValue = value; bestSigma = sigma; }
    }
    return std::clamp(static_cast<i32>(std::lround(3.0 * bestSigma)), minHalf, maxHalf);
}

void TemplateTracker::build(const Gray& g, Vec2 seed) {
    const i32 n = 2 * half_ + 1;
    l0_.half = half_;
    l0_.t = patch(g, seed, half_);
    const Gray g1 = half_res(g);
    l1_.half = std::max(2, half_ / 2);
    l1_.t = patch(g1, Vec2{(seed.x + 0.5f) * 0.5f - 0.5f, (seed.y + 0.5f) * 0.5f - 0.5f}, l1_.half);
    prior_.assign(static_cast<usize>(n) * n, 0.0f);
    const f64 sigma = std::max(1.0, static_cast<f64>(half_));
    for (i32 y = -half_; y <= half_; ++y)
        for (i32 x = -half_; x <= half_; ++x)
            prior_[static_cast<usize>((y + half_) * n + (x + half_))] = static_cast<f32>(std::exp(-(x * x + y * y) / (2 * sigma * sigma)));
    rho_.assign(prior_.size(), 0.0f);
    refresh_weights();
}

void TemplateTracker::refresh_weights() {
    const i32 n0 = 2 * l0_.half + 1, n1 = 2 * l1_.half + 1;
    l0_.w.resize(prior_.size());
    for (usize k = 0; k < prior_.size(); ++k) l0_.w[k] = prior_[k] / (1.0f + rho_[k]);   // Cauchy, c = 1 desvio
    l1_.w.assign(static_cast<usize>(n1) * n1, 0.0f);
    for (i32 y = -l1_.half; y <= l1_.half; ++y)
        for (i32 x = -l1_.half; x <= l1_.half; ++x) {
            const i32 X = std::clamp(2 * x, -l0_.half, l0_.half), Y = std::clamp(2 * y, -l0_.half, l0_.half);
            l1_.w[static_cast<usize>((y + l1_.half) * n1 + (x + l1_.half))] = l0_.w[static_cast<usize>((Y + l0_.half) * n0 + (X + l0_.half))];
        }
}

void TemplateTracker::start(const Gray& g, Vec2 seed, i32 half) {
    half_ = std::clamp(half, 2, 48);
    build(g, seed);
    last_ = g;
    lastPos_ = seed;
}

TrackStep TemplateTracker::track(const Gray& cur, Vec2 predicted, i32 radius, f32 accept) {
    radius = std::clamp(radius, 2, 192);
    // 1. Janela inteira na metade da resolução; 2. refino ±2 px; 3. pesos
    //    robustos (resíduo de cada pixel) e um novo refino ±1 px.
    const Gray c1 = half_res(cur);
    const Vec2 p1{(predicted.x + 0.5f) * 0.5f - 0.5f, (predicted.y + 0.5f) * 0.5f - 0.5f};
    const TrackStep coarse = weighted_search(l1_.t, l1_.w, l1_.half, c1, p1, std::max(2, (radius + 1) / 2));
    TrackStep step = weighted_search(l0_.t, l0_.w, half_, cur, Vec2{coarse.pos.x * 2 + 0.5f, coarse.pos.y * 2 + 0.5f}, 2);
    // A aceitação usa os pesos JÁ aprendidos: o reajuste dos pesos no próprio
    // quadro não pode "explicar" um objeto que sumiu (oclusão) pelo fundo.
    if (step.score >= accept) {
        std::vector<f32> resid;
        (void)weighted_ncc(l0_.t, l0_.w, half_, cur, static_cast<i32>(std::lround(step.pos.x)), static_cast<i32>(std::lround(step.pos.y)), &resid);
        for (usize k = 0; k < rho_.size(); ++k) rho_[k] = 0.7f * rho_[k] + 0.3f * resid[k] * resid[k];
        refresh_weights();
        const TrackStep again = weighted_search(l0_.t, l0_.w, half_, cur, step.pos, 1);
        if (again.score >= accept) step.pos = again.pos;
        last_ = cur;
        lastPos_ = step.pos;
        return step;
    }
    // Aparência mudou (giro, escala, luz local): o passo a passo desde o
    // último quadro aceito; se ele casar bem, o bloco é renovado ali.
    const TrackStep chained = track_step(last_, cur, lastPos_, half_, std::min(radius, 96), predicted);
    if (chained.score >= std::max(accept, 0.8f)) {
        build(cur, chained.pos);
        last_ = cur;
        lastPos_ = chained.pos;
        return chained;
    }
    return step.score >= chained.score ? step : chained;
}

} // namespace aurea::tracking
