#include "aurea/render/MaskRaster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::mask {

bool active(const Mask& m) noexcept {
    if (m.operation == MaskOperation::None || !m.closed) return false;
    if (!m.pathKeys.empty()) {
        for (const MaskPathKey& k : m.pathKeys) if (k.points.size() >= 2) return true;
        return false;
    }
    return m.points.size() >= 2;
}

void evaluate_path(const Mask& m, f64 localFrame, std::vector<MaskPoint>& out) {
    const std::vector<MaskPathKey>& keys = m.pathKeys;
    if (keys.empty()) { out = m.points; return; }
    if (localFrame <= static_cast<f64>(keys.front().frame)) { out = keys.front().points; return; }
    if (localFrame >= static_cast<f64>(keys.back().frame)) { out = keys.back().points; return; }
    usize i = 0;
    while (i + 1 < keys.size() && static_cast<f64>(keys[i + 1].frame) <= localFrame) ++i;
    const MaskPathKey& a = keys[i];
    const MaskPathKey& b = keys[i + 1];
    // Número de pontos diferente (ponto acrescentado no meio) ou key de
    // segurar: a forma de `a` vale até `b`.
    if (a.interp == 0 || a.points.size() != b.points.size() || b.frame <= a.frame) { out = a.points; return; }
    f32 t = static_cast<f32>((localFrame - static_cast<f64>(a.frame)) / static_cast<f64>(b.frame - a.frame));
    if (a.interp == 2) t = t * t * (3.0f - 2.0f * t);
    auto lerp = [t](Vec2 x, Vec2 y) { return Vec2{x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t}; };
    out.resize(a.points.size());
    for (usize k = 0; k < out.size(); ++k) {
        out[k].position = lerp(a.points[k].position, b.points[k].position);
        out[k].inTangent = lerp(a.points[k].inTangent, b.points[k].inTangent);
        out[k].outTangent = lerp(a.points[k].outTangent, b.points[k].outTangent);
    }
}

void flatten(const std::vector<MaskPoint>& pts, bool closed, f32 tolerance, Vec2 offset, std::vector<Vec4>& edges) {
    const usize n = pts.size();
    if (n < 2) return;
    const f32 tol = std::max(1e-3f, tolerance);
    const usize segs = closed ? n : n - 1;
    for (usize i = 0; i < segs; ++i) {
        const MaskPoint& A = pts[i];
        const MaskPoint& B = pts[(i + 1) % n];
        const Vec2 p0{A.position.x + offset.x, A.position.y + offset.y};
        const Vec2 p1{p0.x + A.outTangent.x, p0.y + A.outTangent.y};
        const Vec2 p3{B.position.x + offset.x, B.position.y + offset.y};
        const Vec2 p2{p3.x + B.inTangent.x, p3.y + B.inTangent.y};
        // Wang: n ≥ √(¾·max|Δ²P| / tol) segmentos garantem a corda a ≤ tol.
        const f32 d1x = p0.x - 2 * p1.x + p2.x, d1y = p0.y - 2 * p1.y + p2.y;
        const f32 d2x = p1.x - 2 * p2.x + p3.x, d2y = p1.y - 2 * p2.y + p3.y;
        const f32 dd = std::sqrt(std::max(d1x * d1x + d1y * d1y, d2x * d2x + d2y * d2y));
        const u32 k = std::clamp<u32>(static_cast<u32>(std::ceil(std::sqrt(0.75f * dd / tol))), 1u, 96u);
        Vec2 prev = p0;
        for (u32 s = 1; s <= k; ++s) {
            const f32 t = static_cast<f32>(s) / static_cast<f32>(k);
            const f32 u = 1.0f - t;
            const f32 b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
            const Vec2 q = s == k ? p3 : Vec2{b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                                              b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y};
            if (q.x != prev.x || q.y != prev.y) edges.push_back(Vec4{prev.x, prev.y, q.x, q.y});
            prev = q;
        }
    }
}

u32 build_block(const Layer& l, f64 localFrame, Vec2 offset, f32 tolerance, std::vector<Vec4>& out, f32& start,
                u64& key) {
    start = 0.0f;
    key = 0;
    const usize first = out.size();
    u32 count = 0;
    for (const Mask& m : l.masks) if (active(m)) ++count;
    if (count == 0) return 0;
    out.resize(first + static_cast<usize>(count) * kHeaderVec4);
    std::vector<MaskPoint> pts;
    std::vector<Vec4> edges;
    u32 mi = 0;
    bool firstActive = true;
    for (const Mask& m : l.masks) {
        if (!active(m)) continue;
        if (firstActive) {
            start = (m.operation == MaskOperation::Subtract || m.operation == MaskOperation::Intersect) ? 1.0f : 0.0f;
            firstActive = false;
        }
        evaluate_path(m, localFrame, pts);
        edges.clear();
        flatten(pts, true, tolerance, offset, edges);
        Vec4 box{1e30f, 1e30f, -1e30f, -1e30f};
        for (const Vec4& e : edges) {
            box.x = std::min({box.x, e.x, e.z});
            box.y = std::min({box.y, e.y, e.w});
            box.z = std::max({box.z, e.x, e.z});
            box.w = std::max({box.w, e.y, e.w});
        }
        if (edges.empty()) box = Vec4{0, 0, 0, 0};
        Vec4* h = &out[first + static_cast<usize>(mi) * kHeaderVec4];
        h[0] = Vec4{static_cast<f32>(static_cast<u8>(m.operation)), m.inverted ? 1.0f : 0.0f,
                    std::clamp(m.opacity, 0.0f, 1.0f), std::max(0.0f, m.feather) * 0.25f};
        h[1] = Vec4{m.expansion, static_cast<f32>(out.size() - first), static_cast<f32>(edges.size()), 0.0f};
        h[2] = box;
        out.insert(out.end(), edges.begin(), edges.end());
        ++mi;
    }
    // FNV-1a do bloco (offsets relativos: a posição no buffer do quadro não conta).
    u64 hsh = 1469598103934665603ull;
    const u8* bytes = reinterpret_cast<const u8*>(out.data() + first);
    const usize nb = (out.size() - first) * sizeof(Vec4);
    for (usize i = 0; i < nb; ++i) { hsh ^= bytes[i]; hsh *= 1099511628211ull; }
    u32 sb;
    std::memcpy(&sb, &start, 4);
    hsh ^= sb;
    hsh *= 1099511628211ull;
    key = hsh | 1ull;
    return count;
}

namespace {

f32 erf_approx(f32 x) noexcept {
    // Abramowitz–Stegun 7.1.26 (erro < 1,5e-7) — a mesma do shader.
    const f32 s = x < 0.0f ? -1.0f : 1.0f;
    x = std::fabs(x);
    const f32 t = 1.0f / (1.0f + 0.3275911f * x);
    const f32 y = 1.0f - (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t
                         * std::exp(-x * x);
    return s * y;
}

} // namespace

f32 coverage_at(const Vec4* block, u32 count, f32 start, Vec2 p, f32 k) noexcept {
    f32 acc = start;
    for (u32 m = 0; m < count; ++m) {
        const Vec4 h0 = block[m * kHeaderVec4 + 0];
        const Vec4 h1 = block[m * kHeaderVec4 + 1];
        const Vec4 h2 = block[m * kHeaderVec4 + 2];
        const u32 op = static_cast<u32>(h0.x + 0.5f);
        const f32 sigma = h0.w, expand = h1.x;
        const u32 e0 = static_cast<u32>(h1.y + 0.5f), ne = static_cast<u32>(h1.z + 0.5f);
        const f32 margin = std::fabs(expand) + 4.0f * sigma + 2.0f / k;
        f32 cov = 0.0f;
        if (!(p.x < h2.x - margin || p.y < h2.y - margin || p.x > h2.z + margin || p.y > h2.w + margin)) {
            f32 d2 = 1e30f;
            i32 wind = 0;
            for (u32 i = 0; i < ne; ++i) {
                const Vec4 e = block[e0 + i];
                const f32 abx = e.z - e.x, aby = e.w - e.y, apx = p.x - e.x, apy = p.y - e.y;
                const f32 t = std::clamp((apx * abx + apy * aby) / std::max(abx * abx + aby * aby, 1e-12f), 0.0f, 1.0f);
                const f32 dx = apx - abx * t, dy = apy - aby * t;
                d2 = std::min(d2, dx * dx + dy * dy);
                const f32 cr = abx * apy - aby * apx;
                if (e.y <= p.y) { if (e.w > p.y && cr > 0.0f) ++wind; }
                else if (e.w <= p.y && cr < 0.0f) --wind;
            }
            const f32 d = std::sqrt(d2);
            const f32 sd = (wind != 0 ? d : -d) + expand;
            if (sigma > 0.0f) {
                const f32 s = std::sqrt(sigma * sigma + 1.0f / (12.0f * k * k));
                cov = 0.5f * (1.0f + erf_approx(sd / (s * 1.41421356f)));
            } else {
                cov = std::clamp(sd * k + 0.5f, 0.0f, 1.0f);
            }
        }
        if (h0.y > 0.5f) cov = 1.0f - cov;
        cov *= h0.z;
        switch (op) {
            case 0: acc = acc + cov * (1.0f - acc); break;           // Add (união)
            case 1: acc = acc * (1.0f - cov); break;                 // Subtract
            case 2: acc = acc * cov; break;                          // Intersect
            case 3: acc = acc + cov - 2.0f * acc * cov; break;       // Difference
            default: break;
        }
    }
    return std::clamp(acc, 0.0f, 1.0f);
}

} // namespace aurea::mask
