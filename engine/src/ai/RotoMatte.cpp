#include "aurea/ai/RotoMatte.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace aurea::ai {
namespace {

constexpr f32 kInf = 1e30f;
constexpr f32 kMagic = -7777.0f;
// Custos geodésicos: atravessar a grade inteira em cor lisa custa 0,5; uma
// aresta de 100/255 custa ~6 — o traço "escorre" pela região parecida e
// para na borda.
constexpr f32 kSpatial = 0.5f;
constexpr f32 kColor = 40.0f;
constexpr f32 kStrokeSeed = 0.0f;
constexpr f32 kPriorSeed = 0.05f;
constexpr f32 kModelSeed = 0.6f;
// The border is only an inferred background seed, but its cost must remain
// comparable to the spatial path across the image (at most about 0.7). A cost
// of 1 let a single foreground stroke flood low-confidence background regions.
constexpr f32 kBorderSeed = 0.05f;
constexpr f32 kDecision = 0.04f;
constexpr u32 kErode = 3;

u64 fnv(u64 h, u64 v) noexcept {
    for (u32 i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 0x100000001B3ull; }
    return h;
}
u64 fbits(f32 f) noexcept { return std::bit_cast<u32>(f); }

u64 stroke_hash(u64 h, const RotoStroke& s) noexcept {
    h = fnv(h, static_cast<u64>(s.frame));
    h = fnv(h, s.background ? 2 : 1);
    h = fnv(h, fbits(s.radius));
    for (const Vec2& p : s.points) h = fnv(h, (fbits(p.x) << 32) | fbits(p.y));
    return h;
}

f32 cdiff(const u8* a, const u8* b) noexcept {
    const f32 r = (int(a[0]) - int(b[0])) * (1.0f / 255.0f);
    const f32 g = (int(a[1]) - int(b[1])) * (1.0f / 255.0f);
    const f32 c = (int(a[2]) - int(b[2])) * (1.0f / 255.0f);
    return (r * r + g * g + c * c) * (1.0f / 3.0f);
}

/// Guia suavizada 3×3: o ruído do sensor não vira parede geodésica.
void box3(const u8* rgb, u32 n, std::vector<u8>& out) {
    out.resize(static_cast<usize>(n) * n * 3);
    for (u32 y = 0; y < n; ++y) for (u32 x = 0; x < n; ++x) {
        u32 sum[3]{}, cnt = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int sx = int(x) + dx, sy = int(y) + dy;
            if (sx < 0 || sy < 0 || sx >= int(n) || sy >= int(n)) continue;
            const u8* p = rgb + (static_cast<usize>(sy) * n + u32(sx)) * 3;
            sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; ++cnt;
        }
        u8* o = out.data() + (static_cast<usize>(y) * n + x) * 3;
        for (u32 c = 0; c < 3; ++c) o[c] = static_cast<u8>(sum[c] / cnt);
    }
}

/// Distância geodésica aproximada (varreduras raster de ida e volta, 8 vizinhos).
void geodesic(std::vector<f32>& d, const std::vector<u8>& rgb, u32 n) {
    const f32 ws = kSpatial / static_cast<f32>(n), wd = ws * 1.41421356f;
    for (int it = 0; it < 3; ++it) {
        for (u32 y = 0; y < n; ++y) for (u32 x = 0; x < n; ++x) {
            const usize i = static_cast<usize>(y) * n + x;
            const u8* c = &rgb[i * 3];
            f32 best = d[i];
            auto relax = [&](usize j, f32 step) {
                if (d[j] >= kInf) return;
                const f32 v = d[j] + step + kColor * cdiff(c, &rgb[j * 3]);
                if (v < best) best = v;
            };
            if (x) relax(i - 1, ws);
            if (y) { relax(i - n, ws); if (x) relax(i - n - 1, wd); if (x + 1 < n) relax(i - n + 1, wd); }
            d[i] = best;
        }
        for (u32 y = n; y-- > 0;) for (u32 x = n; x-- > 0;) {
            const usize i = static_cast<usize>(y) * n + x;
            const u8* c = &rgb[i * 3];
            f32 best = d[i];
            auto relax = [&](usize j, f32 step) {
                if (d[j] >= kInf) return;
                const f32 v = d[j] + step + kColor * cdiff(c, &rgb[j * 3]);
                if (v < best) best = v;
            };
            if (x + 1 < n) relax(i + 1, ws);
            if (y + 1 < n) { relax(i + n, ws); if (x + 1 < n) relax(i + n + 1, wd); if (x) relax(i + n - 1, wd); }
            d[i] = best;
        }
    }
}

/// Erosão binária quadrada (raio r), separável por somas de prefixo.
void erode(const std::vector<u8>& in, u32 n, u32 r, std::vector<u8>& out) {
    std::vector<u8> tmp(in.size());
    std::vector<u32> pre(n + 1);
    for (u32 y = 0; y < n; ++y) {
        pre[0] = 0;
        for (u32 x = 0; x < n; ++x) pre[x + 1] = pre[x] + in[static_cast<usize>(y) * n + x];
        for (u32 x = 0; x < n; ++x) {
            const u32 a = x >= r ? x - r : 0, b = std::min(n, x + r + 1);
            tmp[static_cast<usize>(y) * n + x] = (pre[b] - pre[a]) == (b - a) ? 1 : 0;
        }
    }
    out.assign(in.size(), 0);
    for (u32 x = 0; x < n; ++x) {
        pre[0] = 0;
        for (u32 y = 0; y < n; ++y) pre[y + 1] = pre[y] + tmp[static_cast<usize>(y) * n + x];
        for (u32 y = 0; y < n; ++y) {
            const u32 a = y >= r ? y - r : 0, b = std::min(n, y + r + 1);
            out[static_cast<usize>(y) * n + x] = (pre[b] - pre[a]) == (b - a) ? 1 : 0;
        }
    }
}

struct Gray { u32 n = 0; std::vector<f32> v; f32 at(int x, int y) const noexcept {
    x = std::clamp(x, 0, int(n) - 1); y = std::clamp(y, 0, int(n) - 1); return v[static_cast<usize>(y) * n + u32(x)]; } };

/// Um canal (as cores separadas: vermelho e verde de mesma luma são bordas).
Gray gray_of(const u8* rgb, u32 n, u32 channel) {
    Gray g; g.n = n; g.v.resize(static_cast<usize>(n) * n);
    for (usize i = 0; i < g.v.size(); ++i) g.v[i] = rgb[i * 3 + channel];
    return g;
}
Gray half_of(const Gray& g) {
    Gray h; h.n = std::max(1u, g.n / 2); h.v.resize(static_cast<usize>(h.n) * h.n);
    for (u32 y = 0; y < h.n; ++y) for (u32 x = 0; x < h.n; ++x)
        h.v[static_cast<usize>(y) * h.n + x] = 0.25f * (g.at(int(2 * x), int(2 * y)) + g.at(int(2 * x + 1), int(2 * y))
                                                       + g.at(int(2 * x), int(2 * y + 1)) + g.at(int(2 * x + 1), int(2 * y + 1)));
    return h;
}

f32 bilinear(const std::vector<f32>& m, u32 n, f32 x, f32 y) noexcept {
    x = std::clamp(x, 0.0f, static_cast<f32>(n - 1)); y = std::clamp(y, 0.0f, static_cast<f32>(n - 1));
    const u32 x0 = static_cast<u32>(x), y0 = static_cast<u32>(y);
    const u32 x1 = std::min(n - 1, x0 + 1), y1 = std::min(n - 1, y0 + 1);
    const f32 fx = x - x0, fy = y - y0;
    const f32 a = m[static_cast<usize>(y0) * n + x0], b = m[static_cast<usize>(y0) * n + x1];
    const f32 c = m[static_cast<usize>(y1) * n + x0], d = m[static_cast<usize>(y1) * n + x1];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

} // namespace

// ---------------------------------------------------------------- traços ----

bool roto_decode(const CurveData& curve, RotoStrokes& out) {
    out.clear();
    const auto& marker = curve.channel[3];
    if (marker.size() != 1 || marker[0].x != kMagic) return false;
    const auto& heads = curve.channel[0];
    const auto& counts = curve.channel[1];
    const auto& pts = curve.channel[2];
    if (heads.size() != counts.size()) return false;
    usize at = 0;
    for (usize i = 0; i < heads.size(); ++i) {
        const f32 c = counts[i].x;
        if (!std::isfinite(c) || c < 1 || !std::isfinite(heads[i].x) || !std::isfinite(heads[i].y)) return false;
        const usize count = static_cast<usize>(c);
        if (at + count > pts.size()) return false;
        RotoStroke s;
        s.frame = static_cast<i64>(std::llround(heads[i].x));
        s.background = heads[i].y < 0;
        s.radius = std::clamp(std::fabs(heads[i].y), 1e-4f, 1.0f);
        s.points.reserve(count);
        for (usize k = 0; k < count; ++k, ++at) {
            const auto& p = pts[at];
            s.points.push_back(Vec2{std::isfinite(p.x) ? p.x : 0.0f, std::isfinite(p.y) ? p.y : 0.0f});
        }
        out.push_back(std::move(s));
    }
    return true;
}

void roto_encode(const RotoStrokes& strokes, CurveData& curve) {
    for (auto& ch : curve.channel) ch.clear();
    for (const RotoStroke& s : strokes) {
        if (s.points.empty()) continue;
        curve.channel[0].push_back({static_cast<f32>(s.frame), s.background ? -s.radius : s.radius});
        curve.channel[1].push_back({static_cast<f32>(s.points.size()), 0.0f});
        for (const Vec2& p : s.points) curve.channel[2].push_back({p.x, p.y});
    }
    curve.channel[3].push_back({kMagic, 1.0f});
}

bool roto_instance_strokes(const EffectInstance& instance, RotoStrokes& out) {
    out.clear();
    if (instance.params.size() <= kRotoStrokesParam) return false;
    const u64 ref = instance.params[kRotoStrokesParam].constant.ref;
    if (ref >= instance.curves.size()) return false;
    return roto_decode(instance.curves[ref], out) && !out.empty();
}

std::vector<i64> roto_bases(const RotoStrokes& strokes) {
    std::vector<i64> b;
    for (const RotoStroke& s : strokes) b.push_back(s.frame);
    std::sort(b.begin(), b.end());
    b.erase(std::unique(b.begin(), b.end()), b.end());
    return b;
}

u64 roto_dependency(const RotoStrokes& strokes, i64 frame) {
    if (strokes.empty()) return 0;
    i64 first = strokes.front().frame;
    for (const RotoStroke& s : strokes) first = std::min(first, s.frame);
    u64 h = 0xCBF29CE484222325ull;
    if (frame >= first) {
        // Para a frente: toda base até este quadro decide (na ordem do traço).
        for (const RotoStroke& s : strokes) if (s.frame <= frame) h = stroke_hash(h, s);
    } else {
        // Antes da primeira base: só ela, levada para trás.
        h = fnv(h, 0xBAC);
        for (const RotoStroke& s : strokes) if (s.frame == first) h = stroke_hash(h, s);
    }
    return h ? h : 1;
}

void roto_rasterize(const RotoStrokes& strokes, i64 frame, u32 size, f32 layerW, f32 layerH,
                    std::vector<u8>& labels) {
    labels.assign(static_cast<usize>(size) * size, 0);
    const f32 w = std::max(1.0f, layerW), h = std::max(1.0f, layerH), m = std::min(w, h);
    for (const RotoStroke& s : strokes) {
        if (s.frame != frame || s.points.empty()) continue;
        const f32 rx = std::max(0.75f, s.radius * m / w * size), ry = std::max(0.75f, s.radius * m / h * size);
        const u8 v = s.background ? 2 : 1;
        auto stamp = [&](f32 cx, f32 cy) {
            const int x0 = std::max(0, int(std::floor(cx - rx))), x1 = std::min(int(size) - 1, int(std::ceil(cx + rx)));
            const int y0 = std::max(0, int(std::floor(cy - ry))), y1 = std::min(int(size) - 1, int(std::ceil(cy + ry)));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const f32 dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
                if (dx * dx + dy * dy <= 1.0f) labels[static_cast<usize>(y) * size + u32(x)] = v;
            }
        };
        Vec2 prev{s.points[0].x * size, s.points[0].y * size};
        stamp(prev.x, prev.y);
        const f32 spacing = std::max(0.5f, 0.5f * std::min(rx, ry));
        for (usize i = 1; i < s.points.size(); ++i) {
            const Vec2 p{s.points[i].x * size, s.points[i].y * size};
            const f32 len = std::hypot(p.x - prev.x, p.y - prev.y);
            const u32 steps = std::min<u32>(4096, static_cast<u32>(std::ceil(len / spacing)));
            for (u32 k = 1; k <= steps; ++k) {
                const f32 t = static_cast<f32>(k) / static_cast<f32>(steps);
                stamp(prev.x + (p.x - prev.x) * t, prev.y + (p.y - prev.y) * t);
            }
            prev = p;
        }
    }
}

// ---------------------------------------------------------- segmentação ----

void roto_segment(const f32* prob, const u8* rgb, const u8* labels, const f32* prior, u32 size,
                  std::vector<f32>& out) {
    const usize count = static_cast<usize>(size) * size;
    out.assign(count, 0.0f);
    if (!rgb || !size) return;
    std::vector<u8> guide;
    box3(rgb, size, guide);
    std::vector<f32> dF(count, kInf), dB(count, kInf);
    std::vector<u8> band(count, 1);
    if (prior) {
        std::vector<u8> in(count), outside(count), ein, eout;
        for (usize i = 0; i < count; ++i) { in[i] = prior[i] >= 0.5f; outside[i] = !in[i]; }
        erode(in, size, kErode, ein);
        erode(outside, size, kErode, eout);
        for (usize i = 0; i < count; ++i) {
            if (ein[i]) { dF[i] = kPriorSeed; band[i] = 0; }
            else if (eout[i]) { dB[i] = kPriorSeed; band[i] = 0; }
        }
    }
    const bool paintedObject = labels && std::any_of(labels, labels + count, [](u8 value) { return value == 1; });
    if (prob) for (usize i = 0; i < count; ++i) {
        if (!band[i]) continue;
        // A green stroke selects the object. Automatic foreground seeds on
        // unrelated people/objects must not override that selection. During
        // propagation the selected prior already supplies foreground seeds.
        if (prob[i] > 0.8f && !paintedObject && !prior) dF[i] = std::min(dF[i], kModelSeed);
        else if (prob[i] < 0.2f) dB[i] = std::min(dB[i], kModelSeed);
    }
    if (labels) for (usize i = 0; i < count; ++i) {
        if (labels[i] == 1) { dF[i] = kStrokeSeed; dB[i] = kInf; }
        else if (labels[i] == 2) { dB[i] = kStrokeSeed; dF[i] = kInf; }
    }
    bool anyF = false, anyB = false;
    for (usize i = 0; i < count; ++i) { anyF |= dF[i] < kInf; anyB |= dB[i] < kInf; }
    if (!anyF) {
        // Nada diz "objeto": a rede como está, menos o que foi pintado de fundo.
        for (usize i = 0; i < count; ++i) out[i] = (labels && labels[i] == 2) ? 0.0f : (prob ? prob[i] : 0.0f);
        return;
    }
    if (!anyB) {
        // Só objeto: a moldura do quadro é fundo fraco (o traço não engole tudo).
        for (u32 i = 0; i < size; ++i) {
            for (const usize j : {static_cast<usize>(i), static_cast<usize>(size - 1) * size + i,
                                  static_cast<usize>(i) * size, static_cast<usize>(i) * size + size - 1})
                if (dF[j] > 0.0f) dB[j] = std::min(dB[j], kBorderSeed);
        }
    }
    geodesic(dF, guide, size);
    geodesic(dB, guide, size);
    for (usize i = 0; i < count; ++i) {
        const f32 t = std::clamp((dB[i] - dF[i]) / (2.0f * kDecision) + 0.5f, 0.0f, 1.0f);
        out[i] = t * t * (3.0f - 2.0f * t);
        if (labels && labels[i] == 1) out[i] = 1.0f;
        else if (labels && labels[i] == 2) out[i] = 0.0f;
    }
}

// --------------------------------------------------------------- fluxo ----

void roto_flow(const u8* prevRgb, const u8* curRgb, u32 size, std::vector<Vec2>& flow) {
    flow.assign(static_cast<usize>(size) * size, Vec2{});
    if (!prevRgb || !curRgb || size < 16) return;
    // Pirâmide por canal (R, G, B): nível 0 = 320, 1 = 160, 2 = 80.
    Gray ps[3][3], cs[3][3];
    for (u32 ch = 0; ch < 3; ++ch) {
        ps[0][ch] = gray_of(prevRgb, size, ch); cs[0][ch] = gray_of(curRgb, size, ch);
        for (u32 l = 1; l < 3; ++l) { ps[l][ch] = half_of(ps[l - 1][ch]); cs[l][ch] = half_of(cs[l - 1][ch]); }
    }
    constexpr u32 B = 8;
    std::vector<Vec2> coarse;
    u32 coarseW = 0;
    for (int level = 2; level >= 0; --level) {
        const Gray* P = ps[level];
        const Gray* C = cs[level];
        const u32 gw = (C[0].n + B - 1) / B;
        std::vector<Vec2> vec(static_cast<usize>(gw) * gw);
        const int R = level == 2 ? 4 : 2;
        for (u32 by = 0; by < gw; ++by) for (u32 bx = 0; bx < gw; ++bx) {
            Vec2 pred{};
            if (!coarse.empty()) {
                const u32 cx = std::min(coarseW - 1, bx / 2), cy = std::min(coarseW - 1, by / 2);
                pred = coarse[static_cast<usize>(cy) * coarseW + cx];
                pred.x *= 2; pred.y *= 2;
            }
            const int px = int(std::lround(pred.x)), py = int(std::lround(pred.y));
            f32 best = kInf; Vec2 bv{static_cast<f32>(px), static_cast<f32>(py)};
            for (int dy = -R; dy <= R; ++dy) for (int dx = -R; dx <= R; ++dx) {
                const int ox = px + dx, oy = py + dy;
                f32 sad = 0;
                for (u32 y = 0; y < B; ++y) for (u32 x = 0; x < B; ++x) {
                    const int sx = int(bx * B + x), sy = int(by * B + y);
                    for (u32 ch = 0; ch < 3; ++ch) sad += std::fabs(C[ch].at(sx, sy) - P[ch].at(sx + ox, sy + oy));
                }
                // Empate em região lisa: o menor movimento vence.
                sad += 0.5f * static_cast<f32>(std::abs(ox) + std::abs(oy));
                if (sad < best) { best = sad; bv = Vec2{static_cast<f32>(ox), static_cast<f32>(oy)}; }
            }
            vec[static_cast<usize>(by) * gw + bx] = bv;
        }
        coarse = std::move(vec);
        coarseW = gw;
    }
    // Sem mediana: numa região lisa só os blocos da BORDA veem o movimento,
    // e a mediana com os vizinhos parados o apagava (o recorte ficava para trás).
    const std::vector<Vec2>& med = coarse;
    for (u32 y = 0; y < size; ++y) for (u32 x = 0; x < size; ++x) {
        const f32 gx = std::clamp((x + 0.5f) / B - 0.5f, 0.0f, static_cast<f32>(coarseW - 1));
        const f32 gy = std::clamp((y + 0.5f) / B - 0.5f, 0.0f, static_cast<f32>(coarseW - 1));
        const u32 x0 = static_cast<u32>(gx), y0 = static_cast<u32>(gy);
        const u32 x1 = std::min(coarseW - 1, x0 + 1), y1 = std::min(coarseW - 1, y0 + 1);
        const f32 fx = gx - x0, fy = gy - y0;
        auto at = [&](u32 a, u32 b) { return med[static_cast<usize>(b) * coarseW + a]; };
        const Vec2 a = at(x0, y0), b = at(x1, y0), c = at(x0, y1), d = at(x1, y1);
        flow[static_cast<usize>(y) * size + x] = Vec2{
            (a.x * (1 - fx) + b.x * fx) * (1 - fy) + (c.x * (1 - fx) + d.x * fx) * fy,
            (a.y * (1 - fx) + b.y * fx) * (1 - fy) + (c.y * (1 - fx) + d.y * fx) * fy};
    }
}

void roto_warp(const std::vector<f32>& prev, const std::vector<Vec2>& flow, u32 size, std::vector<f32>& out) {
    const usize count = static_cast<usize>(size) * size;
    out.assign(count, 0.0f);
    if (prev.size() != count || flow.size() != count) { out = prev; return; }
    for (u32 y = 0; y < size; ++y) for (u32 x = 0; x < size; ++x) {
        const usize i = static_cast<usize>(y) * size + x;
        out[i] = bilinear(prev, size, static_cast<f32>(x) + flow[i].x, static_cast<f32>(y) + flow[i].y);
    }
}

void roto_propagate(const std::vector<f32>& prevMatte, const u8* prevRgb, const u8* curRgb, const f32* prob,
                    const u8* labels, u32 size, std::vector<f32>& out) {
    std::vector<Vec2> flow;
    roto_flow(prevRgb, curRgb, size, flow);
    std::vector<f32> warped;
    roto_warp(prevMatte, flow, size, warped);
    roto_segment(prob, curRgb, labels, warped.data(), size, out);
}

void roto_reduce_chatter(const std::vector<f32>& cur, const std::vector<f32>* prev, const std::vector<f32>* next,
                         f32 amount, std::vector<f32>& out) {
    out = cur;
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return;
    const bool hp = prev && prev->size() == cur.size(), hn = next && next->size() == cur.size();
    if (!hp && !hn) return;
    for (usize i = 0; i < cur.size(); ++i) {
        f32 sum = cur[i]; f32 n = 1;
        if (hp) { sum += (*prev)[i]; ++n; }
        if (hn) { sum += (*next)[i]; ++n; }
        out[i] = cur[i] + amount * (sum / n - cur[i]);
    }
}

// ----------------------------------------------------------------- RLE ----

void roto_pack(const std::vector<f32>& matte, std::vector<u8>& out) {
    out.clear();
    usize i = 0;
    while (i < matte.size()) {
        const u8 v = static_cast<u8>(std::lround(std::clamp(matte[i], 0.0f, 1.0f) * 255.0f));
        u32 run = 1;
        while (i + run < matte.size() && run < 255
               && static_cast<u8>(std::lround(std::clamp(matte[i + run], 0.0f, 1.0f) * 255.0f)) == v) ++run;
        out.push_back(v); out.push_back(static_cast<u8>(run));
        i += run;
    }
}

bool roto_unpack(const std::vector<u8>& packed, usize count, std::vector<f32>& out) {
    out.clear(); out.reserve(count);
    for (usize i = 0; i + 1 < packed.size(); i += 2) {
        const f32 v = packed[i] * (1.0f / 255.0f);
        for (u32 k = 0; k < packed[i + 1]; ++k) out.push_back(v);
    }
    if (out.size() != count) { out.clear(); return false; }
    return true;
}

} // namespace aurea::ai
