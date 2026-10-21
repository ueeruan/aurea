// =============================================================================
//  Aurea / tracking / CameraTracker.cpp
// =============================================================================
#include "aurea/tracking/CameraTracker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace aurea::tracking {

namespace {

constexpr f32 kNaN = std::numeric_limits<f32>::quiet_NaN();
constexpr f64 kPi = 3.14159265358979323846;

// -----------------------------------------------------------------------------
// Álgebra pequena (f64)
// -----------------------------------------------------------------------------
struct V3 { f64 x = 0, y = 0, z = 0; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, f64 s) { return {a.x * s, a.y * s, a.z * s}; }
f64 dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
f64 norm(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalized(V3 a) { const f64 n = norm(a); return n > 1e-300 ? a * (1.0 / n) : a; }

struct M3 {
    f64 m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    f64& operator()(int r, int c) { return m[r * 3 + c]; }
    f64 operator()(int r, int c) const { return m[r * 3 + c]; }
};
M3 mul(const M3& a, const M3& b) {
    M3 o;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) o(r, c) = a(r, 0) * b(0, c) + a(r, 1) * b(1, c) + a(r, 2) * b(2, c);
    return o;
}
V3 mul(const M3& a, V3 v) {
    return {a(0, 0) * v.x + a(0, 1) * v.y + a(0, 2) * v.z, a(1, 0) * v.x + a(1, 1) * v.y + a(1, 2) * v.z,
            a(2, 0) * v.x + a(2, 1) * v.y + a(2, 2) * v.z};
}
M3 transpose(const M3& a) {
    M3 o;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) o(r, c) = a(c, r);
    return o;
}
f64 det(const M3& a) {
    return a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) - a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0))
         + a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
}
/// Rotação de um vetor de eixo-ângulo (Rodrigues).
M3 rodrigues(V3 w) {
    const f64 th = norm(w);
    M3 R;
    if (th < 1e-12) {
        R(0, 1) = -w.z; R(0, 2) = w.y; R(1, 0) = w.z; R(1, 2) = -w.x; R(2, 0) = -w.y; R(2, 1) = w.x;
        return R;
    }
    const V3 k = w * (1.0 / th);
    const f64 c = std::cos(th), s = std::sin(th), v = 1.0 - c;
    R(0, 0) = c + k.x * k.x * v;       R(0, 1) = k.x * k.y * v - k.z * s; R(0, 2) = k.x * k.z * v + k.y * s;
    R(1, 0) = k.y * k.x * v + k.z * s; R(1, 1) = c + k.y * k.y * v;       R(1, 2) = k.y * k.z * v - k.x * s;
    R(2, 0) = k.z * k.x * v - k.y * s; R(2, 1) = k.z * k.y * v + k.x * s; R(2, 2) = c + k.z * k.z * v;
    return R;
}

/// Autovalores/vetores de simétrica n×n (Jacobi). A é destruída; V em colunas;
/// w em ordem CRESCENTE (e V reordenada junto).
void jacobi_eigen(int n, std::vector<f64>& A, std::vector<f64>& V, std::vector<f64>& w) {
    V.assign(static_cast<usize>(n * n), 0.0);
    for (int i = 0; i < n; ++i) V[static_cast<usize>(i * n + i)] = 1.0;
    for (int sweep = 0; sweep < 60; ++sweep) {
        f64 off = 0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += A[static_cast<usize>(p * n + q)] * A[static_cast<usize>(p * n + q)];
        if (off < 1e-30) break;
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const f64 apq = A[static_cast<usize>(p * n + q)];
                if (std::fabs(apq) < 1e-300) continue;
                const f64 app = A[static_cast<usize>(p * n + p)], aqq = A[static_cast<usize>(q * n + q)];
                const f64 theta = (aqq - app) / (2.0 * apq);
                const f64 t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const f64 c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const f64 akp = A[static_cast<usize>(k * n + p)], akq = A[static_cast<usize>(k * n + q)];
                    A[static_cast<usize>(k * n + p)] = c * akp - s * akq;
                    A[static_cast<usize>(k * n + q)] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const f64 apk = A[static_cast<usize>(p * n + k)], aqk = A[static_cast<usize>(q * n + k)];
                    A[static_cast<usize>(p * n + k)] = c * apk - s * aqk;
                    A[static_cast<usize>(q * n + k)] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const f64 vkp = V[static_cast<usize>(k * n + p)], vkq = V[static_cast<usize>(k * n + q)];
                    V[static_cast<usize>(k * n + p)] = c * vkp - s * vkq;
                    V[static_cast<usize>(k * n + q)] = s * vkp + c * vkq;
                }
            }
        }
    }
    std::vector<int> idx(static_cast<usize>(n));
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return A[static_cast<usize>(a * n + a)] < A[static_cast<usize>(b * n + b)]; });
    w.resize(static_cast<usize>(n));
    std::vector<f64> V2(V.size());
    for (int j = 0; j < n; ++j) {
        w[static_cast<usize>(j)] = A[static_cast<usize>(idx[static_cast<usize>(j)] * n + idx[static_cast<usize>(j)])];
        for (int k = 0; k < n; ++k) V2[static_cast<usize>(k * n + j)] = V[static_cast<usize>(k * n + idx[static_cast<usize>(j)])];
    }
    V.swap(V2);
}

/// Vetor do menor autovalor de AᵀA (A com `cols` colunas, linhas acumuladas).
std::vector<f64> null_vector(const std::vector<f64>& AtA, int cols) {
    std::vector<f64> A = AtA, V, w;
    jacobi_eigen(cols, A, V, w);
    std::vector<f64> v(static_cast<usize>(cols));
    for (int k = 0; k < cols; ++k) v[static_cast<usize>(k)] = V[static_cast<usize>(k * cols)];
    return v;
}

/// SVD 3×3 (A = U·diag(s)·Vᵀ, s decrescente) pelos autovetores de AᵀA.
void svd3(const M3& A, M3& U, f64 s[3], M3& V) {
    std::vector<f64> AtA(9, 0.0);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) AtA[static_cast<usize>(r * 3 + c)] += A(k, r) * A(k, c);
    std::vector<f64> vv, w;
    jacobi_eigen(3, AtA, vv, w);
    // Decrescente.
    for (int j = 0; j < 3; ++j) {
        const int src = 2 - j;
        s[j] = std::sqrt(std::max(0.0, w[static_cast<usize>(src)]));
        for (int k = 0; k < 3; ++k) V(k, j) = vv[static_cast<usize>(k * 3 + src)];
    }
    V3 u[3];
    for (int j = 0; j < 2; ++j) {
        const V3 vj{V(0, j), V(1, j), V(2, j)};
        const V3 av = mul(A, vj);
        u[j] = s[j] > 1e-12 ? av * (1.0 / s[j]) : V3{j == 0 ? 1.0 : 0.0, j == 1 ? 1.0 : 0.0, 0};
    }
    u[1] = normalized(u[1] - u[0] * dot(u[0], u[1]));
    u[2] = cross(u[0], u[1]);
    if (s[2] > 1e-12) {
        const V3 v2{V(0, 2), V(1, 2), V(2, 2)};
        const V3 av = mul(A, v2) * (1.0 / s[2]);
        if (dot(av, u[2]) < 0) u[2] = u[2] * -1.0;
    }
    for (int j = 0; j < 3; ++j) { U(0, j) = u[j].x; U(1, j) = u[j].y; U(2, j) = u[j].z; }
}

/// Resolve n×n (n ≤ 6) por eliminação com pivô; falso se singular.
bool solve_linear(int n, f64* A, f64* b) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r) if (std::fabs(A[r * n + c]) > std::fabs(A[piv * n + c])) piv = r;
        if (std::fabs(A[piv * n + c]) < 1e-14) return false;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(A[c * n + k], A[piv * n + k]);
            std::swap(b[c], b[piv]);
        }
        for (int r = c + 1; r < n; ++r) {
            const f64 f = A[r * n + c] / A[c * n + c];
            for (int k = c; k < n; ++k) A[r * n + k] -= f * A[c * n + k];
            b[r] -= f * b[c];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        f64 s = b[r];
        for (int k = r + 1; k < n; ++k) s -= A[r * n + k] * b[k];
        b[r] = s / A[r * n + r];
    }
    return true;
}

// -----------------------------------------------------------------------------
// Imagem: pirâmide, amostra bilinear, gradiente
// -----------------------------------------------------------------------------
f32 sample(const Gray& g, f32 x, f32 y) {
    const i32 x0 = static_cast<i32>(std::floor(x)), y0 = static_cast<i32>(std::floor(y));
    const f32 fx = x - static_cast<f32>(x0), fy = y - static_cast<f32>(y0);
    const f32 a = g.at(x0, y0), b = g.at(x0 + 1, y0), c = g.at(x0, y0 + 1), d = g.at(x0 + 1, y0 + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

Gray half(const Gray& g) {
    Gray o;
    o.width = std::max(1u, g.width / 2);
    o.height = std::max(1u, g.height / 2);
    o.px.resize(static_cast<usize>(o.width) * o.height);
    for (u32 y = 0; y < o.height; ++y)
        for (u32 x = 0; x < o.width; ++x) {
            const i32 X = static_cast<i32>(x * 2), Y = static_cast<i32>(y * 2);
            // Binomial low-pass before decimation prevents fine texture from
            // aliasing into a different corner in the coarse LK levels.
            static constexpr f32 kernel[5] = {1, 4, 6, 4, 1};
            f32 sum = 0;
            for (i32 dy = -2; dy <= 2; ++dy)
                for (i32 dx = -2; dx <= 2; ++dx)
                    sum += kernel[dy + 2] * kernel[dx + 2] * g.at(X + dx, Y + dy);
            o.px[static_cast<usize>(y) * o.width + x] = sum / 256.0f;
        }
    return o;
}

std::vector<Gray> pyramid(const Gray& g, int levels) {
    std::vector<Gray> p;
    p.push_back(g);
    for (int i = 1; i < levels; ++i) {
        if (p.back().width < 40 || p.back().height < 40) break;
        p.push_back(half(p.back()));
    }
    return p;
}

/// Lucas-Kanade: coarse 15x15 search, fine 9x9 subpixel localization.
/// `prev`. Falso se perdeu (região lisa ou não convergiu).
bool lk_track(const std::vector<Gray>& prev, const std::vector<Gray>& cur, Vec2 p, Vec2& out) {
    const int levels = static_cast<int>(std::min(prev.size(), cur.size()));
    Vec2 d{0, 0};
    for (int L = levels - 1; L >= 0; --L) {
        const f32 s = 1.0f / static_cast<f32>(1 << L);
        const Gray& A = prev[static_cast<usize>(L)];
        const Gray& B = cur[static_cast<usize>(L)];
        const Vec2 q{p.x * s, p.y * s};
        const int radius = L == 0 ? 4 : 7;
        constexpr int samples = 15 * 15;
        f32 ix[samples], iy[samples], a[samples];
        f64 gxx = 0, gxy = 0, gyy = 0;
        int k = 0;
        for (int wy = -radius; wy <= radius; ++wy)
            for (int wx = -radius; wx <= radius; ++wx, ++k) {
                const f32 x = q.x + static_cast<f32>(wx), y = q.y + static_cast<f32>(wy);
                ix[k] = 0.5f * (sample(A, x + 1, y) - sample(A, x - 1, y));
                iy[k] = 0.5f * (sample(A, x, y + 1) - sample(A, x, y - 1));
                a[k] = sample(A, x, y);
                gxx += ix[k] * ix[k];
                gxy += ix[k] * iy[k];
                gyy += iy[k] * iy[k];
            }
        const f64 det = gxx * gyy - gxy * gxy;
        const f64 tr = gxx + gyy;
        const f64 lmin = 0.5 * (tr - std::sqrt(std::max(0.0, tr * tr - 4 * det)));
        if (lmin < 1e-4 || det < 1e-12) {
            if (L == 0) return false;
            // A real corner can disappear at the coarsest level. Continue at
            // finer resolution instead of throwing away its persistent id.
            d.x *= 2.0f; d.y *= 2.0f;
            continue;
        }
        for (int it = 0; it < 20; ++it) {
            f64 bx = 0, by = 0;
            k = 0;
            for (int wy = -radius; wy <= radius; ++wy)
                for (int wx = -radius; wx <= radius; ++wx, ++k) {
                    const f32 x = q.x + d.x + static_cast<f32>(wx), y = q.y + d.y + static_cast<f32>(wy);
                    const f32 dt = sample(B, x, y) - a[k];
                    bx += ix[k] * dt;
                    by += iy[k] * dt;
                }
            const f64 ux = -(gyy * bx - gxy * by) / det, uy = -(-gxy * bx + gxx * by) / det;
            d.x += static_cast<f32>(ux);
            d.y += static_cast<f32>(uy);
            if (ux * ux + uy * uy < 1e-4) break;
        }
        if (L > 0) { d.x *= 2.0f; d.y *= 2.0f; }
    }
    out = Vec2{p.x + d.x, p.y + d.y};
    const f32 m = 6.0f;
    return out.x >= m && out.y >= m && out.x < static_cast<f32>(cur[0].width) - m && out.y < static_cast<f32>(cur[0].height) - m;
}

} // namespace

// =============================================================================
// FeatureTracker
// =============================================================================
FeatureTracker::FeatureTracker(TrackMode mode) noexcept {
    maxFeatures_ = mode == TrackMode::Fast ? 250 : (mode == TrackMode::High ? 700 : 400);
    minDistance_ = mode == TrackMode::Fast ? 14.0f : (mode == TrackMode::High ? 8.0f : 10.0f);
}

void FeatureTracker::detect(const Gray& g) {
    const u32 w = g.width, h = g.height;
    if (w < 24 || h < 24 || active_.size() >= maxFeatures_) return;
    // Menor autovalor do tensor de estrutura (janela 5×5), por imagens integrais.
    std::vector<f64> sxx((w + 1) * (h + 1), 0.0), sxy(sxx.size(), 0.0), syy(sxx.size(), 0.0);
    for (u32 y = 0; y < h; ++y) {
        f64 rxx = 0, rxy = 0, ryy = 0;
        for (u32 x = 0; x < w; ++x) {
            const f32 gx = 0.5f * (g.at(static_cast<i32>(x) + 1, static_cast<i32>(y)) - g.at(static_cast<i32>(x) - 1, static_cast<i32>(y)));
            const f32 gy = 0.5f * (g.at(static_cast<i32>(x), static_cast<i32>(y) + 1) - g.at(static_cast<i32>(x), static_cast<i32>(y) - 1));
            rxx += gx * gx; rxy += gx * gy; ryy += gy * gy;
            const usize i = (y + 1) * (w + 1) + (x + 1), up = y * (w + 1) + (x + 1);
            sxx[i] = sxx[up] + rxx; sxy[i] = sxy[up] + rxy; syy[i] = syy[up] + ryy;
        }
    }
    auto box = [&](const std::vector<f64>& s, u32 x0, u32 y0, u32 x1, u32 y1) {
        return s[y1 * (w + 1) + x1] - s[y0 * (w + 1) + x1] - s[y1 * (w + 1) + x0] + s[y0 * (w + 1) + x0];
    };
    std::vector<f32> score(static_cast<usize>(w) * h, 0.0f);
    f32 best = 0.0f;
    const u32 border = 10;
    for (u32 y = border; y + border < h; ++y)
        for (u32 x = border; x + border < w; ++x) {
            const f64 a = box(sxx, x - 2, y - 2, x + 3, y + 3), b = box(sxy, x - 2, y - 2, x + 3, y + 3), c = box(syy, x - 2, y - 2, x + 3, y + 3);
            const f64 tr = a + c, dt = a * c - b * b;
            const f32 l = static_cast<f32>(0.5 * (tr - std::sqrt(std::max(0.0, tr * tr - 4 * dt))));
            score[static_cast<usize>(y) * w + x] = l;
            best = std::max(best, l);
        }
    if (best <= 1e-6f) return;
    struct Cand { f32 s; u32 x, y; };
    std::vector<Cand> cands;
    const f32 thr = std::max(best * 0.02f, 2e-4f);
    for (u32 y = border; y + border < h; ++y)
        for (u32 x = border; x + border < w; ++x) {
            const f32 s = score[static_cast<usize>(y) * w + x];
            if (s < thr) continue;
            bool peak = true;
            for (int dy = -1; dy <= 1 && peak; ++dy)
                for (int dx = -1; dx <= 1 && peak; ++dx)
                    if ((dx || dy) && score[static_cast<usize>(static_cast<i32>(y) + dy) * w + static_cast<u32>(static_cast<i32>(x) + dx)] > s) peak = false;
            if (peak) cands.push_back({s, x, y});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.s > b.s; });
    // Grade de ocupação (célula = distância mínima): espalha os pontos.
    const f32 cell = minDistance_;
    const u32 gw = static_cast<u32>(std::ceil(static_cast<f32>(w) / cell)) + 1, gh = static_cast<u32>(std::ceil(static_cast<f32>(h) / cell)) + 1;
    std::vector<std::vector<Vec2>> occ(static_cast<usize>(gw) * gh);
    auto mark = [&](Vec2 p) {
        const u32 cx = static_cast<u32>(p.x / cell), cy = static_cast<u32>(p.y / cell);
        if (cx < gw && cy < gh) occ[static_cast<usize>(cy) * gw + cx].push_back(p);
    };
    auto freeAt = [&](Vec2 p) {
        const i32 cx = static_cast<i32>(p.x / cell), cy = static_cast<i32>(p.y / cell);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const i32 X = cx + dx, Y = cy + dy;
                if (X >= 0 && Y >= 0 && X < static_cast<i32>(gw) && Y < static_cast<i32>(gh))
                    for (Vec2 q : occ[static_cast<usize>(Y) * gw + static_cast<usize>(X)])
                        if ((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) < cell * cell) return false;
            }
        return true;
    };
    for (const Active& a : active_) mark(a.pos);
    const u32 frame = tracks_.frames - 1;
    for (const Cand& c : cands) {
        if (active_.size() >= maxFeatures_) break;
        const Vec2 p{static_cast<f32>(c.x), static_cast<f32>(c.y)};
        if (!freeAt(p)) continue;
        mark(p);
        std::vector<Vec2> row(tracks_.frames, Vec2{kNaN, kNaN});
        row[frame] = p;
        tracks_.pos.push_back(std::move(row));
        active_.push_back({static_cast<u32>(tracks_.pos.size() - 1), p});
    }
}

void FeatureTracker::add_frame(const Gray& g) {
    std::vector<Gray> cur = pyramid(g, 4);
    ++tracks_.frames;
    tracks_.width = g.width;
    tracks_.height = g.height;
    for (auto& row : tracks_.pos) row.push_back(Vec2{kNaN, kNaN});
    const u32 frame = tracks_.frames - 1;
    if (!prev_.empty()) {
        std::vector<Active> kept;
        kept.reserve(active_.size());
        for (const Active& a : active_) {
            Vec2 fwd, back;
            // Ida e volta: o ponto tem de voltar para onde estava.
            if (!lk_track(prev_, cur, a.pos, fwd) || !lk_track(cur, prev_, fwd, back)) continue;
            if (std::hypot(back.x - a.pos.x, back.y - a.pos.y) > 0.5f) continue;
            tracks_.pos[a.track][frame] = fwd;
            kept.push_back({a.track, fwd});
        }
        active_.swap(kept);
    }
    detect(g);
    prev_ = std::move(cur);
}

// =============================================================================
// Solve
// =============================================================================
Vec3 CameraPose::center() const noexcept {
    return Vec3{static_cast<f32>(-(R[0] * t[0] + R[3] * t[1] + R[6] * t[2])),
                static_cast<f32>(-(R[1] * t[0] + R[4] * t[1] + R[7] * t[2])),
                static_cast<f32>(-(R[2] * t[0] + R[5] * t[1] + R[8] * t[2]))};
}

namespace {

struct Pose { M3 R; V3 t; bool valid = false; };

struct Obs { u32 track; V3 ray; };   // ray = (x, y, 1) normalizado

/// Projeção normalizada; falso se atrás da câmera.
bool project(const Pose& P, V3 X, f64& u, f64& v) {
    const V3 c = mul(P.R, X) + P.t;
    if (c.z <= 1e-9) return false;
    u = c.x / c.z;
    v = c.y / c.z;
    return true;
}

/// Triangulação DLT com várias vistas (raios normalizados).
bool triangulate(const std::vector<const Pose*>& poses, const std::vector<V3>& rays, V3& X) {
    std::vector<f64> AtA(16, 0.0);
    for (usize i = 0; i < poses.size(); ++i) {
        const Pose& P = *poses[i];
        const f64 Pm[12] = {P.R(0, 0), P.R(0, 1), P.R(0, 2), P.t.x, P.R(1, 0), P.R(1, 1), P.R(1, 2), P.t.y,
                            P.R(2, 0), P.R(2, 1), P.R(2, 2), P.t.z};
        f64 r0[4], r1[4];
        for (int k = 0; k < 4; ++k) {
            r0[k] = rays[i].x * Pm[8 + k] - Pm[k];
            r1[k] = rays[i].y * Pm[8 + k] - Pm[4 + k];
        }
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) AtA[static_cast<usize>(a * 4 + b)] += r0[a] * r0[b] + r1[a] * r1[b];
    }
    const std::vector<f64> v = null_vector(AtA, 4);
    if (std::fabs(v[3]) < 1e-12) return false;
    X = V3{v[0] / v[3], v[1] / v[3], v[2] / v[3]};
    return true;
}

/// Ângulo (radianos) entre os raios de dois centros até X.
f64 parallax(const Pose& a, const Pose& b, V3 X) {
    const V3 ca = mul(transpose(a.R), a.t) * -1.0, cb = mul(transpose(b.R), b.t) * -1.0;
    const V3 ra = normalized(X - ca), rb = normalized(X - cb);
    return std::acos(std::clamp(dot(ra, rb), -1.0, 1.0));
}

/// Essencial pelo método de 8 pontos (x2ᵀ·E·x1 = 0), projetada nas essenciais.
bool essential8(const std::vector<V3>& x1, const std::vector<V3>& x2, const std::vector<u32>& idx, M3& E) {
    if (idx.size() < 8) return false;
    // Hartley normalization is still needed after camera normalization: on a
    // phone most rays occupy a small image region, making A^T A ill-conditioned.
    V3 c1{}, c2{};
    for (u32 i : idx) { c1 = c1 + x1[i]; c2 = c2 + x2[i]; }
    c1 = c1 * (1.0 / idx.size()); c2 = c2 * (1.0 / idx.size());
    f64 d1 = 0, d2 = 0;
    for (u32 i : idx) { d1 += std::hypot(x1[i].x - c1.x, x1[i].y - c1.y); d2 += std::hypot(x2[i].x - c2.x, x2[i].y - c2.y); }
    if (d1 < 1e-9 || d2 < 1e-9) return false;
    const f64 z1 = std::sqrt(2.0) * idx.size() / d1, z2 = std::sqrt(2.0) * idx.size() / d2;
    M3 T1, T2;
    T1(0, 0) = T1(1, 1) = z1; T1(0, 2) = -c1.x * z1; T1(1, 2) = -c1.y * z1;
    T2(0, 0) = T2(1, 1) = z2; T2(0, 2) = -c2.x * z2; T2(1, 2) = -c2.y * z2;
    std::vector<f64> AtA(81, 0.0);
    for (u32 i : idx) {
        const V3 a = mul(T1, x1[i]), b = mul(T2, x2[i]);
        const f64 r[9] = {b.x * a.x, b.x * a.y, b.x, b.y * a.x, b.y * a.y, b.y, a.x, a.y, 1.0};
        for (int p = 0; p < 9; ++p)
            for (int q = 0; q < 9; ++q) AtA[static_cast<usize>(p * 9 + q)] += r[p] * r[q];
    }
    const std::vector<f64> e = null_vector(AtA, 9);
    M3 F;
    for (int k = 0; k < 9; ++k) F.m[k] = e[static_cast<usize>(k)];
    F = mul(mul(transpose(T2), F), T1);
    M3 U, V;
    f64 s[3];
    svd3(F, U, s, V);
    const f64 sm = 0.5 * (s[0] + s[1]);
    if (sm < 1e-12) return false;
    M3 D;
    D(0, 0) = 1; D(1, 1) = 1; D(2, 2) = 0;
    E = mul(mul(U, D), transpose(V));
    return true;
}

f64 sampson(const M3& E, V3 a, V3 b) {
    const V3 Ea = mul(E, a), Etb = mul(transpose(E), b);
    const f64 e = dot(b, Ea);
    const f64 d = Ea.x * Ea.x + Ea.y * Ea.y + Etb.x * Etb.x + Etb.y * Etb.y;
    return d > 1e-300 ? e * e / d : 1e30;
}

/// Pose de 2 relativa à 1 (identidade) pela essencial: a das 4 que põe mais
/// pontos na frente das duas câmeras.
bool decompose(const M3& E, const std::vector<V3>& x1, const std::vector<V3>& x2, const std::vector<u32>& inl, Pose& out) {
    M3 U, V;
    f64 s[3];
    svd3(E, U, s, V);
    if (det(U) < 0) for (int r = 0; r < 3; ++r) U(r, 2) = -U(r, 2);
    if (det(V) < 0) for (int r = 0; r < 3; ++r) V(r, 2) = -V(r, 2);
    M3 W;
    W(0, 0) = 0; W(0, 1) = -1; W(1, 0) = 1; W(1, 1) = 0; W(2, 2) = 1;
    const M3 Ra = mul(mul(U, W), transpose(V)), Rb = mul(mul(U, transpose(W)), transpose(V));
    const V3 u3{U(0, 2), U(1, 2), U(2, 2)};
    const Pose I{M3{}, V3{}, true};
    int best = -1;
    Pose cand[4] = {{Ra, u3, true}, {Ra, u3 * -1.0, true}, {Rb, u3, true}, {Rb, u3 * -1.0, true}};
    for (int c = 0; c < 4; ++c) {
        int front = 0;
        for (u32 i : inl) {
            V3 X;
            if (!triangulate({&I, &cand[c]}, {x1[i], x2[i]}, X)) continue;
            f64 u, v;
            if (X.z > 0 && project(cand[c], X, u, v)) ++front;
        }
        if (best < 0 || front > best) { best = front; out = cand[c]; }
    }
    return best > static_cast<int>(inl.size() / 2);
}

/// Pose por Gauss-Newton robusto (Huber) a partir de `P`, com pontos fixos.
/// `f` converte o erro para px (o limiar de Huber é em px).
bool refine_pose(Pose& P, const std::vector<V3>& X, const std::vector<V3>& rays, f64 f, std::vector<u8>* inlierOut = nullptr) {
    if (X.size() < 6) return false;
    const f64 huber = 1.5 / f;
    for (int it = 0; it < 12; ++it) {
        f64 H[36] = {0}, g[6] = {0};
        for (usize i = 0; i < X.size(); ++i) {
            const V3 RX = mul(P.R, X[i]);
            const V3 c = RX + P.t;
            if (c.z <= 1e-9) continue;
            const f64 iz = 1.0 / c.z;
            const f64 u = c.x * iz, v = c.y * iz;
            const f64 ru = u - rays[i].x, rv = v - rays[i].y;
            const f64 e = std::sqrt(ru * ru + rv * rv);
            const f64 w = e <= huber ? 1.0 : huber / e;
            // d(u,v)/dc
            const f64 Ju[3] = {iz, 0, -c.x * iz * iz}, Jv[3] = {0, iz, -c.y * iz * iz};
            // dc/dω = −[RX]×, dc/dt = I
            const f64 S[9] = {0, RX.z, -RX.y, -RX.z, 0, RX.x, RX.y, -RX.x, 0};   // −[RX]× linha a linha
            f64 Ju6[6], Jv6[6];
            for (int k = 0; k < 3; ++k) {
                Ju6[k] = Ju[0] * S[0 * 3 + k] + Ju[1] * S[1 * 3 + k] + Ju[2] * S[2 * 3 + k];
                Jv6[k] = Jv[0] * S[0 * 3 + k] + Jv[1] * S[1 * 3 + k] + Jv[2] * S[2 * 3 + k];
                Ju6[3 + k] = Ju[k];
                Jv6[3 + k] = Jv[k];
            }
            for (int a = 0; a < 6; ++a) {
                g[a] += w * (Ju6[a] * ru + Jv6[a] * rv);
                for (int b = 0; b < 6; ++b) H[a * 6 + b] += w * (Ju6[a] * Ju6[b] + Jv6[a] * Jv6[b]);
            }
        }
        for (int a = 0; a < 6; ++a) { H[a * 6 + a] *= 1.0 + 1e-6; H[a * 6 + a] += 1e-12; g[a] = -g[a]; }
        if (!solve_linear(6, H, g)) return false;
        P.R = mul(rodrigues(V3{g[0], g[1], g[2]}), P.R);
        P.t = P.t + V3{g[3], g[4], g[5]};
        if (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] + g[3] * g[3] + g[4] * g[4] + g[5] * g[5] < 1e-16) break;
    }
    if (inlierOut) {
        inlierOut->assign(X.size(), 0);
        for (usize i = 0; i < X.size(); ++i) {
            f64 u, v;
            if (project(P, X[i], u, v) && std::hypot(u - rays[i].x, v - rays[i].y) * f < 3.0) (*inlierOut)[i] = 1;
        }
    }
    return true;
}

/// Ponto por Gauss-Newton com as poses fixas.
void refine_point(V3& X, const std::vector<const Pose*>& poses, const std::vector<V3>& rays, f64 f) {
    const f64 huber = 1.5 / f;
    for (int it = 0; it < 6; ++it) {
        f64 H[9] = {0}, g[3] = {0};
        for (usize i = 0; i < poses.size(); ++i) {
            const Pose& P = *poses[i];
            const V3 c = mul(P.R, X) + P.t;
            if (c.z <= 1e-9) continue;
            const f64 iz = 1.0 / c.z;
            const f64 ru = c.x * iz - rays[i].x, rv = c.y * iz - rays[i].y;
            const f64 e = std::sqrt(ru * ru + rv * rv);
            const f64 w = e <= huber ? 1.0 : huber / e;
            const f64 Ju[3] = {iz, 0, -c.x * iz * iz}, Jv[3] = {0, iz, -c.y * iz * iz};
            f64 JuX[3], JvX[3];
            for (int k = 0; k < 3; ++k) {
                JuX[k] = Ju[0] * P.R(0, k) + Ju[1] * P.R(1, k) + Ju[2] * P.R(2, k);
                JvX[k] = Jv[0] * P.R(0, k) + Jv[1] * P.R(1, k) + Jv[2] * P.R(2, k);
            }
            for (int a = 0; a < 3; ++a) {
                g[a] -= w * (JuX[a] * ru + JvX[a] * rv);
                for (int b = 0; b < 3; ++b) H[a * 3 + b] += w * (JuX[a] * JuX[b] + JvX[a] * JvX[b]);
            }
        }
        for (int a = 0; a < 3; ++a) H[a * 3 + a] += 1e-12;
        if (!solve_linear(3, H, g)) return;
        X = X + V3{g[0], g[1], g[2]};
        if (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] < 1e-18) break;
    }
}

/// Pose e focal por Gauss-Newton robusto (Huber, px) com prior na focal:
/// o zoom variável quadro a quadro. `px` = observações centradas (u − cx, v − cy).
bool refine_pose_focal(Pose& P, f64& f, const std::vector<V3>& X, const std::vector<V3>& px, f64 fPrior, f64 priorPx) {
    if (X.size() < 8 || !(f > 1.0) || !(fPrior > 1.0)) return false;
    const f64 huber = 1.5;
    for (int it = 0; it < 12; ++it) {
        f64 H[49] = {0}, g[7] = {0};
        for (usize i = 0; i < X.size(); ++i) {
            const V3 RX = mul(P.R, X[i]);
            const V3 c = RX + P.t;
            if (c.z <= 1e-9) continue;
            const f64 iz = 1.0 / c.z;
            const f64 ru = f * c.x * iz - px[i].x, rv = f * c.y * iz - px[i].y;
            const f64 e = std::sqrt(ru * ru + rv * rv);
            const f64 w = e <= huber ? 1.0 : huber / e;
            const f64 Ju[3] = {f * iz, 0, -f * c.x * iz * iz}, Jv[3] = {0, f * iz, -f * c.y * iz * iz};
            const f64 S[9] = {0, RX.z, -RX.y, -RX.z, 0, RX.x, RX.y, -RX.x, 0};
            f64 Ju7[7], Jv7[7];
            for (int k = 0; k < 3; ++k) {
                Ju7[k] = Ju[0] * S[k] + Ju[1] * S[3 + k] + Ju[2] * S[6 + k];
                Jv7[k] = Jv[0] * S[k] + Jv[1] * S[3 + k] + Jv[2] * S[6 + k];
                Ju7[3 + k] = Ju[k];
                Jv7[3 + k] = Jv[k];
            }
            Ju7[6] = c.x * iz;
            Jv7[6] = c.y * iz;
            for (int a = 0; a < 7; ++a) {
                g[a] += w * (Ju7[a] * ru + Jv7[a] * rv);
                for (int b = 0; b < 7; ++b) H[a * 7 + b] += w * (Ju7[a] * Ju7[b] + Jv7[a] * Jv7[b]);
            }
        }
        // Prior: um desvio de 1/priorPx da focal anterior custa 1 px.
        const f64 jp = priorPx / fPrior, rp = jp * (f - fPrior);
        H[48] += jp * jp;
        g[6] += jp * rp;
        for (int a = 0; a < 7; ++a) { H[a * 7 + a] *= 1.0 + 1e-6; H[a * 7 + a] += 1e-12; g[a] = -g[a]; }
        if (!solve_linear(7, H, g)) return false;
        P.R = mul(rodrigues(V3{g[0], g[1], g[2]}), P.R);
        P.t = P.t + V3{g[3], g[4], g[5]};
        f = std::max(1.0, f + g[6]);
        if (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] + g[3] * g[3] + g[4] * g[4] + g[5] * g[5] < 1e-16 && g[6] * g[6] < 1e-8) break;
    }
    return true;
}

/// Homografia x2 ~ H·x1 (DLT normalizado, raios normalizados).
bool homography4(const std::vector<V3>& x1, const std::vector<V3>& x2, const std::vector<u32>& idx, M3& H) {
    if (idx.size() < 4) return false;
    V3 c1{}, c2{};
    for (u32 i : idx) { c1 = c1 + x1[i]; c2 = c2 + x2[i]; }
    c1 = c1 * (1.0 / idx.size()); c2 = c2 * (1.0 / idx.size());
    f64 d1 = 0, d2 = 0;
    for (u32 i : idx) { d1 += std::hypot(x1[i].x - c1.x, x1[i].y - c1.y); d2 += std::hypot(x2[i].x - c2.x, x2[i].y - c2.y); }
    if (d1 < 1e-12 || d2 < 1e-12) return false;
    const f64 z1 = std::sqrt(2.0) * idx.size() / d1, z2 = std::sqrt(2.0) * idx.size() / d2;
    M3 T1, T2;
    T1(0, 0) = T1(1, 1) = z1; T1(0, 2) = -c1.x * z1; T1(1, 2) = -c1.y * z1;
    T2(0, 0) = T2(1, 1) = z2; T2(0, 2) = -c2.x * z2; T2(1, 2) = -c2.y * z2;
    std::vector<f64> AtA(81, 0.0);
    for (u32 i : idx) {
        const V3 a = mul(T1, x1[i]), b = mul(T2, x2[i]);
        const f64 r0[9] = {0, 0, 0, -a.x, -a.y, -1, b.y * a.x, b.y * a.y, b.y};
        const f64 r1[9] = {a.x, a.y, 1, 0, 0, 0, -b.x * a.x, -b.x * a.y, -b.x};
        for (int p = 0; p < 9; ++p)
            for (int q = 0; q < 9; ++q) AtA[static_cast<usize>(p * 9 + q)] += r0[p] * r0[q] + r1[p] * r1[q];
    }
    const std::vector<f64> h = null_vector(AtA, 9);
    M3 Hn;
    for (int k = 0; k < 9; ++k) Hn.m[k] = h[static_cast<usize>(k)];
    // H = T2⁻¹·Hn·T1.
    M3 T2i;
    T2i(0, 0) = T2i(1, 1) = 1.0 / z2; T2i(0, 2) = c2.x; T2i(1, 2) = c2.y;
    H = mul(mul(T2i, Hn), T1);
    return std::isfinite(H.m[0]) && std::fabs(det(H)) > 1e-18;
}

/// Erro de transferência (raio normalizado) de x1 por H até x2, ao quadrado.
f64 transfer2(const M3& H, V3 a, V3 b) {
    const V3 p = mul(H, a);
    if (std::fabs(p.z) < 1e-12) return 1e30;
    const f64 dx = p.x / p.z - b.x, dy = p.y / p.z - b.y;
    return dx * dx + dy * dy;
}

/// As soluções fisicamente possíveis de H = R + T·Nᵀ (plano Nᵀ·X = 1 na
/// câmera 1), Ma, Soatto, Košecká e Sastry, §5.3.3. Até duas (as outras
/// duas põem o plano atrás da câmera); T normalizado (escala livre).
std::vector<Pose> decompose_homography(M3 H, const std::vector<V3>& x1, const std::vector<V3>& x2, const std::vector<u32>& inl) {
    std::vector<Pose> out;
    // Sinal: x2ᵀ·H·x1 > 0 (profundidades positivas nos dois quadros).
    f64 sign = 0;
    for (u32 i : inl) sign += dot(x2[i], mul(H, x1[i])) > 0 ? 1.0 : -1.0;
    if (sign < 0) for (f64& v : H.m) v = -v;
    std::vector<f64> HtH(9, 0.0), V, w;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) HtH[static_cast<usize>(r * 3 + c)] += H(k, r) * H(k, c);
    jacobi_eigen(3, HtH, V, w);   // crescente: σ3², σ2², σ1²
    if (!(w[1] > 1e-18)) return out;
    const f64 s2 = std::sqrt(w[1]);
    for (f64& v : H.m) v /= s2;   // σ2 = 1
    const f64 l1 = w[2] / w[1], l3 = w[0] / w[1];
    const V3 v1{V[2], V[5], V[8]}, v2{V[1], V[4], V[7]}, v3{V[0], V[3], V[6]};
    if (l1 - l3 < 1e-9) return out;   // só rotação: sem translação, sem plano
    const f64 a = std::sqrt(std::max(0.0, 1.0 - l3)), b = std::sqrt(std::max(0.0, l1 - 1.0)), n = std::sqrt(l1 - l3);
    const V3 u1 = (v1 * a + v3 * b) * (1.0 / n), u2 = (v1 * a - v3 * b) * (1.0 / n);
    auto solution = [&](V3 u, f64 s) {
        const V3 Hv2 = mul(H, v2), Hu = mul(H, u);
        M3 U, W;
        const V3 cu = cross(v2, u), cw = cross(Hv2, Hu);
        for (int r = 0; r < 3; ++r) {
            U(r, 0) = (&v2.x)[r]; U(r, 1) = (&u.x)[r]; U(r, 2) = (&cu.x)[r];
            W(r, 0) = (&Hv2.x)[r]; W(r, 1) = (&Hu.x)[r]; W(r, 2) = (&cw.x)[r];
        }
        const M3 R = mul(W, transpose(U));
        V3 N = cu * s;
        M3 HR;
        for (int k = 0; k < 9; ++k) HR.m[k] = H.m[k] - R.m[k];
        V3 T = mul(HR, cu) * s;
        // O plano na frente: Nᵀ·e3 > 0.
        if (N.z <= 0) return;
        const f64 tn = norm(T);
        if (tn < 1e-9 || det(R) < 0.5) return;
        out.push_back(Pose{R, T * (1.0 / tn), true});
    };
    solution(u1, 1.0); solution(u1, -1.0); solution(u2, 1.0); solution(u2, -1.0);
    return out;
}

struct SolveState {
    std::vector<Pose> poses;
    std::vector<V3> X;
    std::vector<u8> hasX;
    std::vector<u8> rotationTracks;
    std::vector<f64> focal;   // px da análise, por quadro (FOV fixa: todos iguais)
    f64 rms = 1e30;
    u32 solved = 0, inliers = 0;
    u32 fixedFrame = 0;
    bool rotationOnly = false;
    bool homography = false;  // o par inicial veio da homografia (cena plana)
    std::string failure;
};

struct SolveCfg {
    bool zoom = false;   // focal por quadro
    u32 init = 0;        // par inicial: 0 essencial, 1 homografia
};

// Zoom variável: prior da focal no PnP quadro a quadro (1/kZoomPrior da
// focal custa 1 px) e na 2ª diferença entre quadros-chave do BA.
constexpr f64 kZoomPrior = 40.0;
constexpr f64 kZoomSmooth = 400.0;

/// Erro final (px) e inliers: ponto com erro > 4 px em algum quadro sai.
void evaluate(SolveState& S, const Tracks2D& T) {
    const u32 N = T.frames, M = static_cast<u32>(T.pos.size());
    const f64 cx = static_cast<f64>(T.width) * 0.5, cy = static_cast<f64>(T.height) * 0.5;
    f64 sum = 0;
    u64 n = 0;
    u32 used = 0;
    for (u32 t = 0; t < M; ++t) {
        if (!S.hasX[t]) continue;
        f64 st = 0;
        u32 nt = 0;
        bool bad = false;
        for (u32 fr = 0; fr < N; ++fr) {
            const Vec2 o = T.pos[t][fr];
            if (!Tracks2D::present(o) || !S.poses[fr].valid) continue;
            f64 u, v;
            if (!project(S.poses[fr], S.X[t], u, v)) { bad = true; break; }
            const f64 f = S.focal[fr];
            const f64 e = std::hypot(u * f + cx - o.x, v * f + cy - o.y);
            if (e > 4.0) { bad = true; break; }
            st += e * e;
            ++nt;
        }
        if (bad || nt < 2) { S.hasX[t] = 0; continue; }
        sum += st;
        n += nt;
        ++used;
    }
    S.inliers = used;
    S.solved = static_cast<u32>(std::count_if(S.poses.begin(), S.poses.end(), [](const Pose& q) { return q.valid; }));
    S.rms = n ? std::sqrt(sum / static_cast<f64>(n)) : 1e30;
}

/// Cholesky denso (A simétrica positiva, n×n) resolvendo A·x = b em b.
bool cholesky_solve(int n, std::vector<f64>& A, std::vector<f64>& b) {
    for (int j = 0; j < n; ++j) {
        f64 d = A[static_cast<usize>(j * n + j)];
        for (int k = 0; k < j; ++k) d -= A[static_cast<usize>(j * n + k)] * A[static_cast<usize>(j * n + k)];
        if (d <= 1e-18) return false;
        d = std::sqrt(d);
        A[static_cast<usize>(j * n + j)] = d;
        for (int i = j + 1; i < n; ++i) {
            f64 v = A[static_cast<usize>(i * n + j)];
            for (int k = 0; k < j; ++k) v -= A[static_cast<usize>(i * n + k)] * A[static_cast<usize>(j * n + k)];
            A[static_cast<usize>(i * n + j)] = v / d;
        }
    }
    for (int i = 0; i < n; ++i) {
        f64 v = b[static_cast<usize>(i)];
        for (int k = 0; k < i; ++k) v -= A[static_cast<usize>(i * n + k)] * b[static_cast<usize>(k)];
        b[static_cast<usize>(i)] = v / A[static_cast<usize>(i * n + i)];
    }
    for (int i = n - 1; i >= 0; --i) {
        f64 v = b[static_cast<usize>(i)];
        for (int k = i + 1; k < n; ++k) v -= A[static_cast<usize>(k * n + i)] * b[static_cast<usize>(k)];
        b[static_cast<usize>(i)] = v / A[static_cast<usize>(i * n + i)];
    }
    return true;
}

/// O que o bundle adjustment libera além das poses e dos pontos.
struct BAParams {
    bool freeFocal = false;   // focal livre
    bool perKey = false;      // zoom variável: uma focal por quadro-chave (+ prior de suavidade)
    u32 lens = 0;             // 0: k1/k2 presos; 1: k1 livre; 2: k1 e k2 livres
    f64 k1 = 0, k2 = 0;       // meia altura = 1
};

/// Bundle adjustment (Levenberg-Marquardt, complemento de Schur) sobre os
/// quadros-chave: poses, pontos, a(s) focal(is) e a distorção ao mesmo tempo,
/// erro em px nas observações CRUAS (com distorção), peso de Huber. O
/// quadro fixo prende o referencial. Depois, os rastros sem distorção
/// (`und`) e os quadros fora da chave ganham pose pelos pontos refinados.
void bundle_adjust(SolveState& S, const Tracks2D& raw, const Tracks2D& undIn, BAParams& bp, u32 maxKeys, int iterations,
                   const std::atomic<bool>* cancel, Tracks2D* undOut = nullptr) {
    const u32 N = raw.frames, M = static_cast<u32>(raw.pos.size());
    const f64 cx = static_cast<f64>(raw.width) * 0.5, cy = static_cast<f64>(raw.height) * 0.5;
    const f64 halfH = std::max(1.0, static_cast<f64>(raw.height) * 0.5), ih2 = 1.0 / (halfH * halfH);
    std::vector<u32> valid;
    for (u32 i = 0; i < N; ++i) if (S.poses[i].valid) valid.push_back(i);
    if (valid.size() < 3) return;
    std::vector<u32> keys;
    const f64 step = std::max(1.0, static_cast<f64>(valid.size() - 1) / static_cast<f64>(std::max<u32>(2, maxKeys) - 1));
    for (f64 x = 0; x < static_cast<f64>(valid.size()) - 0.5; x += step) keys.push_back(valid[static_cast<usize>(std::lround(x))]);
    if (keys.back() != valid.back()) keys.push_back(valid.back());
    if (std::find(keys.begin(), keys.end(), S.fixedFrame) == keys.end()) keys.insert(keys.begin(), S.fixedFrame);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const u32 K = static_cast<u32>(keys.size());
    std::vector<i32> block(K, -1);
    i32 nb = 0;
    for (u32 k = 0; k < K; ++k) if (keys[k] != S.fixedFrame) block[k] = nb++;
    // Parâmetros de câmera: [poses 6·nb][focal(is)][k1][k2].
    const bool perKey = bp.perKey && bp.freeFocal;
    const int nf = bp.freeFocal ? (perKey ? static_cast<int>(K) : 1) : 0;
    const int f0 = nb * 6, k1i = bp.lens >= 1 ? f0 + nf : -1, k2i = bp.lens >= 2 ? f0 + nf + 1 : -1;
    const int nc = nb * 6 + nf + (bp.lens >= 1 ? 1 : 0) + (bp.lens >= 2 ? 1 : 0);
    auto focal_index = [&](u32 key) { return !bp.freeFocal ? -1 : (perKey ? f0 + static_cast<int>(key) : f0); };
    struct O { u32 key; f64 u, v; };
    std::vector<u32> pts;
    std::vector<std::vector<O>> obs;
    for (u32 t = 0; t < M; ++t) {
        if (!S.hasX[t]) continue;
        std::vector<O> o;
        for (u32 k = 0; k < K; ++k) {
            const Vec2 p = raw.pos[t][keys[k]];
            if (Tracks2D::present(p)) o.push_back({k, static_cast<f64>(p.x) - cx, static_cast<f64>(p.y) - cy});
        }
        if (o.size() >= 2) { pts.push_back(t); obs.push_back(std::move(o)); }
    }
    if (pts.size() < 20) return;
    const f64 huber = 2.0;
    std::vector<f64> F(K);
    for (u32 k = 0; k < K; ++k) F[k] = S.focal[keys[k]];
    f64 fmean = 0;
    for (f64 v : F) fmean += v;
    fmean /= static_cast<f64>(K);
    const f64 wz = kZoomSmooth / std::max(1.0, fmean);
    // Previsão em px (centrada, com distorção) de q = f·(x/z, y/z).
    auto distort = [&](f64 qx, f64 qy, f64 k1, f64 k2, f64& px, f64& py) {
        const f64 s = (qx * qx + qy * qy) * ih2, d = 1.0 + k1 * s + k2 * s * s;
        px = qx * d; py = qy * d;
    };
    auto cost = [&](const std::vector<Pose>& P, const std::vector<V3>& X, const std::vector<f64>& Fk, f64 k1, f64 k2) {
        f64 c = 0;
        for (usize i = 0; i < pts.size(); ++i)
            for (const O& o : obs[i]) {
                const V3 q = mul(P[o.key].R, X[i]) + P[o.key].t;
                if (q.z <= 1e-9) { c += huber * huber * 4; continue; }
                f64 pu, pv;
                distort(Fk[o.key] * q.x / q.z, Fk[o.key] * q.y / q.z, k1, k2, pu, pv);
                const f64 e = std::hypot(pu - o.u, pv - o.v);
                c += e <= huber ? e * e : 2 * huber * e - huber * huber;
            }
        if (perKey) for (u32 k = 1; k + 1 < K; ++k) { const f64 r = wz * (Fk[k - 1] - 2 * Fk[k] + Fk[k + 1]); c += r * r; }
        return c;
    };
    std::vector<Pose> P(K);
    for (u32 k = 0; k < K; ++k) P[k] = S.poses[keys[k]];
    std::vector<V3> X(pts.size());
    for (usize i = 0; i < pts.size(); ++i) X[i] = S.X[pts[i]];
    f64 k1 = bp.k1, k2 = bp.k2;
    f64 lambda = 1e-3;
    f64 c0 = cost(P, X, F, k1, k2);
    constexpr int kC = 9;   // 6 da pose + focal + k1 + k2
    std::vector<f64> Sm(static_cast<usize>(nc) * nc), rhs(static_cast<usize>(nc));
    for (int it = 0; it < iterations; ++it) {
        if (cancel && cancel->load()) return;
        std::fill(Sm.begin(), Sm.end(), 0.0);
        std::fill(rhs.begin(), rhs.end(), 0.0);
        // Por ponto: V (3×3), g_p, e os blocos W por observação.
        struct PtSys { f64 V[9]; f64 g[3]; std::vector<std::array<f64, kC * 3>> W; std::vector<u32> keys; };
        std::vector<PtSys> sys(pts.size());
        auto indices = [&](u32 key, int idx[kC]) {
            const i32 b = block[key];
            for (int k = 0; k < 6; ++k) idx[k] = b >= 0 ? b * 6 + k : -1;
            idx[6] = focal_index(key);
            idx[7] = k1i;
            idx[8] = k2i;
        };
        for (usize i = 0; i < pts.size(); ++i) {
            PtSys& ps = sys[i];
            std::fill(std::begin(ps.V), std::end(ps.V), 0.0);
            std::fill(std::begin(ps.g), std::end(ps.g), 0.0);
            for (const O& o : obs[i]) {
                const Pose& Pk = P[o.key];
                const f64 f = F[o.key];
                const V3 RX = mul(Pk.R, X[i]);
                const V3 q = RX + Pk.t;
                if (q.z <= 1e-9) continue;
                const f64 iz = 1.0 / q.z;
                const f64 qx = f * q.x * iz, qy = f * q.y * iz;
                const f64 s = (qx * qx + qy * qy) * ih2, d = 1.0 + k1 * s + k2 * s * s;
                const f64 ru = qx * d - o.u, rv = qy * d - o.v;
                const f64 e = std::sqrt(ru * ru + rv * rv);
                const f64 w = e <= huber ? 1.0 : huber / e;
                // ∂pred/∂q = d·I + 2(k1 + 2k2·s)/h²·q·qᵀ.
                const f64 dd = 2.0 * (k1 + 2.0 * k2 * s) * ih2;
                const f64 A00 = d + dd * qx * qx, A01 = dd * qx * qy, A11 = d + dd * qy * qy;
                const f64 Ju[3] = {f * iz, 0, -f * q.x * iz * iz}, Jv[3] = {0, f * iz, -f * q.y * iz * iz};
                const f64 Sk[9] = {0, RX.z, -RX.y, -RX.z, 0, RX.x, RX.y, -RX.x, 0};
                f64 Qu[kC], Qv[kC];   // ∂q/∂(câmera)
                for (int k = 0; k < 3; ++k) {
                    Qu[k] = Ju[0] * Sk[k] + Ju[1] * Sk[3 + k] + Ju[2] * Sk[6 + k];
                    Qv[k] = Jv[0] * Sk[k] + Jv[1] * Sk[3 + k] + Jv[2] * Sk[6 + k];
                    Qu[3 + k] = Ju[k];
                    Qv[3 + k] = Jv[k];
                }
                Qu[6] = q.x * iz; Qv[6] = q.y * iz;
                f64 Jc_u[kC], Jc_v[kC];
                for (int k = 0; k < 7; ++k) { Jc_u[k] = A00 * Qu[k] + A01 * Qv[k]; Jc_v[k] = A01 * Qu[k] + A11 * Qv[k]; }
                Jc_u[7] = qx * s; Jc_v[7] = qy * s;
                Jc_u[8] = qx * s * s; Jc_v[8] = qy * s * s;
                f64 Jp_u[3], Jp_v[3];
                for (int k = 0; k < 3; ++k) {
                    const f64 a = Ju[0] * Pk.R(0, k) + Ju[1] * Pk.R(1, k) + Ju[2] * Pk.R(2, k);
                    const f64 b = Jv[0] * Pk.R(0, k) + Jv[1] * Pk.R(1, k) + Jv[2] * Pk.R(2, k);
                    Jp_u[k] = A00 * a + A01 * b;
                    Jp_v[k] = A01 * a + A11 * b;
                }
                int idx[kC];
                indices(o.key, idx);
                for (int a = 0; a < kC; ++a) {
                    if (idx[a] < 0) continue;
                    rhs[static_cast<usize>(idx[a])] -= w * (Jc_u[a] * ru + Jc_v[a] * rv);
                    for (int c = 0; c < kC; ++c) {
                        if (idx[c] < 0) continue;
                        Sm[static_cast<usize>(idx[a]) * nc + static_cast<usize>(idx[c])] += w * (Jc_u[a] * Jc_u[c] + Jc_v[a] * Jc_v[c]);
                    }
                }
                for (int a = 0; a < 3; ++a) {
                    ps.g[a] -= w * (Jp_u[a] * ru + Jp_v[a] * rv);
                    for (int c = 0; c < 3; ++c) ps.V[a * 3 + c] += w * (Jp_u[a] * Jp_u[c] + Jp_v[a] * Jp_v[c]);
                }
                std::array<f64, kC * 3> Wo{};
                for (int a = 0; a < kC; ++a)
                    for (int c = 0; c < 3; ++c) Wo[static_cast<usize>(a * 3 + c)] = w * (Jc_u[a] * Jp_u[c] + Jc_v[a] * Jp_v[c]);
                ps.W.push_back(Wo);
                ps.keys.push_back(o.key);
            }
        }
        // Zoom variável: prior na 2ª diferença das focais dos quadros-chave.
        if (perKey) for (u32 k = 1; k + 1 < K; ++k) {
            const int ia[3] = {f0 + static_cast<int>(k) - 1, f0 + static_cast<int>(k), f0 + static_cast<int>(k) + 1};
            const f64 J[3] = {wz, -2.0 * wz, wz};
            const f64 r = wz * (F[k - 1] - 2 * F[k] + F[k + 1]);
            for (int a = 0; a < 3; ++a) {
                rhs[static_cast<usize>(ia[a])] -= J[a] * r;
                for (int c = 0; c < 3; ++c) Sm[static_cast<usize>(ia[a]) * nc + static_cast<usize>(ia[c])] += J[a] * J[c];
            }
        }
        // Amortecimento (LM) na diagonal das câmeras e dos pontos.
        for (int a = 0; a < nc; ++a) Sm[static_cast<usize>(a) * nc + a] *= 1.0 + lambda, Sm[static_cast<usize>(a) * nc + a] += 1e-9;
        // Schur: S −= W·V⁻¹·Wᵀ; rhs −= W·V⁻¹·g_p.
        std::vector<std::array<f64, 9>> Vinv(pts.size());
        bool okV = true;
        for (usize i = 0; i < pts.size() && okV; ++i) {
            PtSys& ps = sys[i];
            f64 V[9];
            for (int a = 0; a < 9; ++a) V[a] = ps.V[a];
            for (int a = 0; a < 3; ++a) V[a * 3 + a] = V[a * 3 + a] * (1.0 + lambda) + 1e-9;
            M3 Vm;
            for (int a = 0; a < 9; ++a) Vm.m[a] = V[a];
            const f64 dV = det(Vm);
            if (std::fabs(dV) < 1e-30) { okV = false; break; }
            std::array<f64, 9> inv{};
            inv[0] = (V[4] * V[8] - V[5] * V[7]) / dV; inv[1] = (V[2] * V[7] - V[1] * V[8]) / dV; inv[2] = (V[1] * V[5] - V[2] * V[4]) / dV;
            inv[3] = (V[5] * V[6] - V[3] * V[8]) / dV; inv[4] = (V[0] * V[8] - V[2] * V[6]) / dV; inv[5] = (V[2] * V[3] - V[0] * V[5]) / dV;
            inv[6] = (V[3] * V[7] - V[4] * V[6]) / dV; inv[7] = (V[1] * V[6] - V[0] * V[7]) / dV; inv[8] = (V[0] * V[4] - V[1] * V[3]) / dV;
            Vinv[i] = inv;
            const usize no = ps.W.size();
            std::vector<std::array<f64, kC * 3>> Y(no);   // W·V⁻¹
            for (usize a = 0; a < no; ++a)
                for (int r = 0; r < kC; ++r)
                    for (int c = 0; c < 3; ++c)
                        Y[a][static_cast<usize>(r * 3 + c)] = ps.W[a][static_cast<usize>(r * 3)] * inv[c] + ps.W[a][static_cast<usize>(r * 3 + 1)] * inv[3 + c]
                                                            + ps.W[a][static_cast<usize>(r * 3 + 2)] * inv[6 + c];
            for (usize a = 0; a < no; ++a) {
                int ia[kC];
                indices(ps.keys[a], ia);
                for (int r = 0; r < kC; ++r) {
                    if (ia[r] < 0) continue;
                    rhs[static_cast<usize>(ia[r])] -= Y[a][static_cast<usize>(r * 3)] * ps.g[0] + Y[a][static_cast<usize>(r * 3 + 1)] * ps.g[1] + Y[a][static_cast<usize>(r * 3 + 2)] * ps.g[2];
                }
                for (usize bIdx = 0; bIdx < no; ++bIdx) {
                    int ib[kC];
                    indices(ps.keys[bIdx], ib);
                    for (int r = 0; r < kC; ++r) {
                        if (ia[r] < 0) continue;
                        for (int c = 0; c < kC; ++c) {
                            if (ib[c] < 0) continue;
                            Sm[static_cast<usize>(ia[r]) * nc + static_cast<usize>(ib[c])] -=
                                Y[a][static_cast<usize>(r * 3)] * ps.W[bIdx][static_cast<usize>(c * 3)] + Y[a][static_cast<usize>(r * 3 + 1)] * ps.W[bIdx][static_cast<usize>(c * 3 + 1)]
                                + Y[a][static_cast<usize>(r * 3 + 2)] * ps.W[bIdx][static_cast<usize>(c * 3 + 2)];
                        }
                    }
                }
            }
        }
        std::vector<f64> dc = rhs;
        std::vector<f64> A = Sm;
        if (!okV || !cholesky_solve(nc, A, dc)) { lambda *= 10.0; if (lambda > 1e8) break; continue; }
        // Passo candidato.
        std::vector<Pose> P2 = P;
        std::vector<V3> X2 = X;
        std::vector<f64> F2 = F;
        if (bp.freeFocal) for (u32 k = 0; k < K; ++k) F2[k] = F[k] + dc[static_cast<usize>(focal_index(k))];
        const f64 k1n = k1i >= 0 ? k1 + dc[static_cast<usize>(k1i)] : k1, k2n = k2i >= 0 ? k2 + dc[static_cast<usize>(k2i)] : k2;
        for (u32 k = 0; k < K; ++k) {
            if (block[k] < 0) continue;
            const f64* d = &dc[static_cast<usize>(block[k]) * 6];
            P2[k].R = mul(rodrigues(V3{d[0], d[1], d[2]}), P[k].R);
            P2[k].t = P[k].t + V3{d[3], d[4], d[5]};
        }
        for (usize i = 0; i < pts.size(); ++i) {
            const PtSys& ps = sys[i];
            f64 r[3] = {ps.g[0], ps.g[1], ps.g[2]};
            for (usize a = 0; a < ps.W.size(); ++a) {
                int ia[kC];
                indices(ps.keys[a], ia);
                for (int row = 0; row < kC; ++row) {
                    if (ia[row] < 0) continue;
                    for (int c = 0; c < 3; ++c) r[c] -= ps.W[a][static_cast<usize>(row * 3 + c)] * dc[static_cast<usize>(ia[row])];
                }
            }
            const auto& vi = Vinv[i];
            X2[i] = X[i] + V3{vi[0] * r[0] + vi[1] * r[1] + vi[2] * r[2], vi[3] * r[0] + vi[4] * r[1] + vi[5] * r[2], vi[6] * r[0] + vi[7] * r[1] + vi[8] * r[2]};
        }
        const bool sane = std::all_of(F2.begin(), F2.end(), [](f64 v) { return v > 1.0; })
                       && std::fabs(k1n) < 2.0 && std::fabs(k2n) < 2.0 && std::isfinite(k1n) && std::isfinite(k2n);
        const f64 c1 = sane ? cost(P2, X2, F2, k1n, k2n) : 1e300;
        if (c1 < c0) {
            const bool small = (c0 - c1) < 1e-7 * c0;
            P.swap(P2);
            X.swap(X2);
            F.swap(F2);
            k1 = k1n; k2 = k2n;
            c0 = c1;
            lambda = std::max(1e-7, lambda / 3.0);
            if (small) break;
        } else {
            lambda *= 5.0;
            if (lambda > 1e8) break;
        }
    }
    for (u32 k = 0; k < K; ++k) S.poses[keys[k]] = P[k];
    for (usize i = 0; i < pts.size(); ++i) S.X[pts[i]] = X[i];
    // Focal dos quadros: a dos quadros-chave; entre eles, interpolação no log.
    // Focal presa: a de cada quadro fica como estava.
    if (bp.freeFocal && perKey) {
        for (u32 fr = 0; fr < N; ++fr) {
            const auto it = std::lower_bound(keys.begin(), keys.end(), fr);
            if (it == keys.end()) { S.focal[fr] = F[K - 1]; continue; }
            const u32 hi = static_cast<u32>(it - keys.begin());
            if (keys[hi] == fr || hi == 0) { S.focal[fr] = F[hi]; continue; }
            const f64 t = static_cast<f64>(fr - keys[hi - 1]) / static_cast<f64>(keys[hi] - keys[hi - 1]);
            S.focal[fr] = std::exp(std::log(F[hi - 1]) * (1 - t) + std::log(F[hi]) * t);
        }
    } else if (bp.freeFocal) {
        std::fill(S.focal.begin(), S.focal.end(), F[0]);
    }
    // A distorção nova muda os raios: os rastros sem distorção são refeitos.
    Tracks2D fresh;
    const Tracks2D* undp = &undIn;
    if (k1 != bp.k1 || k2 != bp.k2 || undIn.pos.size() != raw.pos.size()) {
        fresh = undistort_tracks(raw, static_cast<f32>(k1), static_cast<f32>(k2));
        undp = &fresh;
    }
    const Tracks2D& und = *undp;
    bp.k1 = k1; bp.k2 = k2;
    // Quadros fora da chave e pontos fora do BA, com a focal (e a lente) novas.
    auto ray = [&](u32 track, u32 frame) {
        const Vec2 q = und.pos[track][frame];
        return V3{(static_cast<f64>(q.x) - cx) / S.focal[frame], (static_cast<f64>(q.y) - cy) / S.focal[frame], 1.0};
    };
    for (u32 fr = 0; fr < N; ++fr) {
        if (!S.poses[fr].valid || std::binary_search(keys.begin(), keys.end(), fr)) continue;
        std::vector<V3> Xs, rs;
        for (u32 t = 0; t < M; ++t) if (S.hasX[t] && Tracks2D::present(und.pos[t][fr])) { Xs.push_back(S.X[t]); rs.push_back(ray(t, fr)); }
        if (Xs.size() < 8) continue;
        if (perKey) {
            // Pose e focal do quadro, presa à interpolação dos vizinhos.
            std::vector<V3> px(rs.size());
            for (usize i = 0; i < rs.size(); ++i) px[i] = V3{rs[i].x * S.focal[fr], rs[i].y * S.focal[fr], 0};
            f64 f = S.focal[fr];
            if (refine_pose_focal(S.poses[fr], f, Xs, px, S.focal[fr], kZoomPrior * 4)) S.focal[fr] = f;
        } else {
            refine_pose(S.poses[fr], Xs, rs, S.focal[fr]);
        }
    }
    for (u32 t = 0; t < M; ++t) {
        if (!S.hasX[t]) continue;
        std::vector<const Pose*> ps;
        std::vector<V3> rs;
        for (u32 fr = 0; fr < N; ++fr) if (Tracks2D::present(und.pos[t][fr]) && S.poses[fr].valid) { ps.push_back(&S.poses[fr]); rs.push_back(ray(t, fr)); }
        if (ps.size() >= 2) refine_point(S.X[t], ps, rs, S.focal[0]);
    }
    if (undOut && undp == &fresh) *undOut = std::move(fresh);
}

/// Um solve completo com a distância focal inicial `f` (px da análise), nos
/// rastros SEM distorção. Zoom variável: a focal acompanha quadro a quadro.
SolveState solve_with_focal(const Tracks2D& T, f64 f, TrackMode mode, const std::atomic<bool>* cancel, const SolveCfg& cfg = {}) {
    SolveState S;
    const u32 N = T.frames, M = static_cast<u32>(T.pos.size());
    const f64 cx = static_cast<f64>(T.width) * 0.5, cy = static_cast<f64>(T.height) * 0.5;
    S.focal.assign(N, f);
    auto ray = [&](u32 track, u32 frame) {
        const Vec2 p = T.pos[track][frame];
        return V3{(static_cast<f64>(p.x) - cx) / S.focal[frame], (static_cast<f64>(p.y) - cy) / S.focal[frame], 1.0};
    };
    auto has = [&](u32 track, u32 frame) { return Tracks2D::present(T.pos[track][frame]); };
    S.poses.assign(N, Pose{});
    S.X.assign(M, V3{});
    S.hasX.assign(M, 0);
    S.homography = cfg.init == 1;
    std::mt19937 rng(1234u);
    const int ransacIters = mode == TrackMode::Fast ? 200 : (mode == TrackMode::High ? 800 : 400);
    const f64 thr2 = (1.5 / f) * (1.5 / f);

    // --- Par inicial: primeiro quadro × o primeiro quadro adiante com paralaxe.
    u32 a = 0;
    u32 k = 0;
    Pose Pk;
    std::vector<u32> initTracks;
    for (u32 cand = 8; cand < N && k == 0; cand += std::max<u32>(1, N / 60)) {
        if (cancel && cancel->load()) return S;
        std::vector<V3> x1, x2;
        std::vector<u32> ids;
        for (u32 t = 0; t < M; ++t) if (has(t, a) && has(t, cand)) { x1.push_back(ray(t, a)); x2.push_back(ray(t, cand)); ids.push_back(t); }
        if (x1.size() < 30) continue;
        std::vector<u32> bestInl;
        Pose P;
        std::uniform_int_distribution<u32> pick(0, static_cast<u32>(x1.size() - 1));
        if (cfg.init == 1) {
            // Cena plana: homografia (4 pontos) com RANSAC, decomposta; a
            // ambiguidade das duas soluções sai num terceiro quadro (PnP).
            M3 bestH;
            for (int it = 0; it < ransacIters; ++it) {
                std::vector<u32> smp;
                while (smp.size() < 4) { const u32 r = pick(rng); if (std::find(smp.begin(), smp.end(), r) == smp.end()) smp.push_back(r); }
                M3 Hc;
                if (!homography4(x1, x2, smp, Hc)) continue;
                std::vector<u32> inl;
                for (u32 i = 0; i < x1.size(); ++i) if (transfer2(Hc, x1[i], x2[i]) < 2.0 * thr2) inl.push_back(i);
                if (inl.size() > bestInl.size()) { bestInl = std::move(inl); bestH = Hc; }
            }
            if (bestInl.size() < 30) continue;
            M3 H;
            if (!homography4(x1, x2, bestInl, H)) continue;
            const std::vector<Pose> sols = decompose_homography(H, x1, x2, bestInl);
            if (sols.empty()) continue;
            const Pose I{M3{}, V3{}, true};
            const u32 third = std::min(N - 1, cand + std::max<u32>(4, cand / 2));
            f64 bestScore = 1e300;
            for (const Pose& sol : sols) {
                // Pontos na frente das duas câmeras e o erro do PnP no terceiro quadro.
                std::vector<V3> Xs, rs;
                u32 front = 0;
                for (u32 i : bestInl) {
                    V3 X;
                    if (!triangulate({&I, &sol}, {x1[i], x2[i]}, X)) continue;
                    f64 u, v;
                    if (X.z <= 0 || !project(sol, X, u, v)) continue;
                    ++front;
                    if (third != cand && has(ids[i], third)) { Xs.push_back(X); rs.push_back(ray(ids[i], third)); }
                }
                f64 score = 1e6 * (static_cast<f64>(bestInl.size()) - front);
                if (Xs.size() >= 8) {
                    Pose P3 = sol;
                    if (refine_pose(P3, Xs, rs, f)) {
                        f64 e = 0;
                        for (usize i = 0; i < Xs.size(); ++i) { f64 u, v; e += project(P3, Xs[i], u, v) ? std::hypot(u - rs[i].x, v - rs[i].y) : 1.0; }
                        score += e / static_cast<f64>(Xs.size());
                    }
                }
                if (score < bestScore) { bestScore = score; P = sol; }
            }
        } else {
            M3 bestE;
            for (int it = 0; it < ransacIters; ++it) {
                std::vector<u32> smp;
                while (smp.size() < 8) { const u32 r = pick(rng); if (std::find(smp.begin(), smp.end(), r) == smp.end()) smp.push_back(r); }
                M3 E;
                if (!essential8(x1, x2, smp, E)) continue;
                std::vector<u32> inl;
                for (u32 i = 0; i < x1.size(); ++i) if (sampson(E, x1[i], x2[i]) < thr2) inl.push_back(i);
                if (inl.size() > bestInl.size()) { bestInl = std::move(inl); bestE = E; }
            }
            if (bestInl.size() < 30) continue;
            M3 E;
            if (!essential8(x1, x2, bestInl, E)) continue;
            if (!decompose(E, x1, x2, bestInl, P)) continue;
        }
        // Paralaxe mediana dos inliers triangulados.
        const Pose I{M3{}, V3{}, true};
        std::vector<f64> ang;
        for (u32 i : bestInl) {
            V3 X;
            if (triangulate({&I, &P}, {x1[i], x2[i]}, X) && X.z > 0) ang.push_back(parallax(I, P, X));
        }
        if (ang.size() < 30) continue;
        std::nth_element(ang.begin(), ang.begin() + static_cast<long>(ang.size() / 2), ang.end());
        if (ang[ang.size() / 2] < 1.5 * kPi / 180.0) continue;   // pouca paralaxe: tentar mais adiante
        k = cand;
        Pk = P;
        for (u32 i : bestInl) initTracks.push_back(ids[i]);
    }
    if (k == 0) {
        S.rotationOnly = true;
        return S;
    }
    S.poses[a] = Pose{M3{}, V3{}, true};
    S.poses[k] = Pk;
    for (u32 t : initTracks) {
        V3 X;
        if (!triangulate({&S.poses[a], &S.poses[k]}, {ray(t, a), ray(t, k)}, X)) continue;
        f64 u, v;
        if (!project(S.poses[a], X, u, v) || !project(S.poses[k], X, u, v)) continue;
        if (parallax(S.poses[a], S.poses[k], X) < 1.0 * kPi / 180.0) continue;
        S.X[t] = X;
        S.hasX[t] = 1;
    }

    // --- Pose de cada quadro (para a frente a partir de a) + pontos novos.
    auto triangulate_new = [&](u32 frame) {
        for (u32 t = 0; t < M; ++t) {
            if (S.hasX[t] || !has(t, frame)) continue;
            std::vector<const Pose*> ps;
            std::vector<V3> rs;
            u32 first = N;
            for (u32 fr = 0; fr <= frame; ++fr) {
                if (!has(t, fr) || !S.poses[fr].valid) continue;
                if (first == N) first = fr;
                ps.push_back(&S.poses[fr]);
                rs.push_back(ray(t, fr));
            }
            if (ps.size() < 2 || first == frame) continue;
            V3 X;
            if (!triangulate(ps, rs, X)) continue;
            if (parallax(S.poses[first], S.poses[frame], X) < 1.0 * kPi / 180.0) continue;
            refine_point(X, ps, rs, S.focal[frame]);
            bool good = true;
            for (usize i = 0; i < ps.size() && good; ++i) {
                f64 u, v;
                good = project(*ps[i], X, u, v) && std::hypot(u - rs[i].x, v - rs[i].y) * S.focal[frame] < 2.0;
            }
            if (good) { S.X[t] = X; S.hasX[t] = 1; }
        }
    };
    for (u32 fr = a + 1; fr < N; ++fr) {
        if (cancel && cancel->load()) return S;
        // Zoom: o quadro começa com a focal do anterior (o PnP a corrige).
        if (cfg.zoom && fr != k) S.focal[fr] = S.focal[fr - 1];
        const f64 ff = S.focal[fr];
        std::vector<V3> Xs, rs;
        std::vector<u32> ids;
        for (u32 t = 0; t < M; ++t) if (S.hasX[t] && has(t, fr)) { Xs.push_back(S.X[t]); rs.push_back(ray(t, fr)); ids.push_back(t); }
        if (Xs.size() < 8) break;   // perdeu a cena: para aqui
        Pose P = S.poses[fr].valid && fr == k ? S.poses[fr] : S.poses[fr - 1];
        std::vector<u8> inl;
        if (!refine_pose(P, Xs, rs, ff, &inl)) break;
        // Segunda passada só com os inliers (o ponto errado não puxa a pose).
        std::vector<V3> X2, r2;
        for (usize i = 0; i < inl.size(); ++i) if (inl[i]) { X2.push_back(Xs[i]); r2.push_back(rs[i]); }
        if (X2.size() < std::max<usize>(8, Xs.size() / 2)) {
            // Robust PnP around the last known pose. A handful of bad/new
            // landmarks must not pull every observation into a wrong minimum.
            std::uniform_int_distribution<usize> choose(0, Xs.size() - 1);
            for (int attempt = 0; attempt < 64; ++attempt) {
                if (cancel && cancel->load()) return S;
                std::vector<usize> indices;
                while (indices.size() < 6) { const usize id = choose(rng); if (std::find(indices.begin(), indices.end(), id) == indices.end()) indices.push_back(id); }
                std::vector<V3> xx, rr;
                for (usize id : indices) { xx.push_back(Xs[id]); rr.push_back(rs[id]); }
                Pose candidate = S.poses[fr - 1];
                if (!refine_pose(candidate, xx, rr, ff)) continue;
                xx.clear(); rr.clear();
                for (usize id = 0; id < Xs.size(); ++id) {
                    f64 u, v;
                    if (project(candidate, Xs[id], u, v) && std::hypot(u - rs[id].x, v - rs[id].y) * ff < 2.5) { xx.push_back(Xs[id]); rr.push_back(rs[id]); }
                }
                if (xx.size() > X2.size()) { P = candidate; X2 = std::move(xx); r2 = std::move(rr); }
            }
        }
        if (X2.size() < 8) break;
        refine_pose(P, X2, r2, ff);
        if (cfg.zoom && fr != k) {
            // A focal do quadro com a pose (prior: a do quadro anterior).
            std::vector<V3> px(r2.size());
            for (usize i = 0; i < r2.size(); ++i) px[i] = V3{r2[i].x * ff, r2[i].y * ff, 0};
            Pose Pz = P;
            f64 fz = ff;
            if (refine_pose_focal(Pz, fz, X2, px, S.focal[fr - 1], kZoomPrior) && fz > 0.2 * ff && fz < 5.0 * ff) {
                P = Pz;
                S.focal[fr] = fz;
            }
        }
        P.valid = true;
        S.poses[fr] = P;
        triangulate_new(fr);
        // Keep the growing map conditioned before older features leave the
        // image. Waiting until the end caused new points to inherit drift and
        // made the pose solver lose otherwise textured handheld sequences.
        if (fr > k && fr % 20 == 0) {
            S.fixedFrame = a;
            BAParams bp;   // focal e lente presas: só poses e pontos
            bundle_adjust(S, T, T, bp, 12, 5, cancel);
            triangulate_new(fr);
        }
    }

    // --- Refinamento alternado: pontos com poses fixas, poses com pontos fixos.
    const int rounds = mode == TrackMode::Fast ? 2 : (mode == TrackMode::High ? 4 : 3);
    for (int r = 0; r < rounds; ++r) {
        if (cancel && cancel->load()) return S;
        for (u32 t = 0; t < M; ++t) {
            if (!S.hasX[t]) continue;
            std::vector<const Pose*> ps;
            std::vector<V3> rs;
            for (u32 fr = 0; fr < N; ++fr) if (has(t, fr) && S.poses[fr].valid) { ps.push_back(&S.poses[fr]); rs.push_back(ray(t, fr)); }
            if (ps.size() >= 2) refine_point(S.X[t], ps, rs, f);
        }
        for (u32 fr = 0; fr < N; ++fr) {
            if (!S.poses[fr].valid || fr == a) continue;   // a fica na origem (fixa o referencial)
            std::vector<V3> Xs, rs;
            for (u32 t = 0; t < M; ++t) if (S.hasX[t] && has(t, fr)) { Xs.push_back(S.X[t]); rs.push_back(ray(t, fr)); }
            if (Xs.size() >= 8) refine_pose(S.poses[fr], Xs, rs, S.focal[fr]);
        }
        // Referencial: a base a→k volta a valer 1 (o refinamento pode encolher tudo).
        const f64 base = norm(S.poses[k].t);
        if (base > 1e-9 && std::fabs(base - 1.0) > 1e-6) {
            for (auto& P : S.poses) if (P.valid) P.t = P.t * (1.0 / base);
            for (u32 t = 0; t < M; ++t) if (S.hasX[t]) S.X[t] = S.X[t] * (1.0 / base);
        }
    }

    S.fixedFrame = a;
    evaluate(S, T);
    return S;
}

/// Tripé: rotação entre quadros por Kabsch (3 raios + RANSAC), acumulada.
SolveState solve_rotation(const Tracks2D& T, f64 f, const std::atomic<bool>* cancel, bool zoom = false) {
    SolveState S;
    S.rotationOnly = true;
    const u32 N = T.frames, M = static_cast<u32>(T.pos.size());
    const f64 cx = static_cast<f64>(T.width) * 0.5, cy = static_cast<f64>(T.height) * 0.5;
    S.focal.assign(N, f);
    auto dir_f = [&](u32 track, u32 frame, f64 ff) {
        const Vec2 p = T.pos[track][frame];
        return normalized(V3{(static_cast<f64>(p.x) - cx) / ff, (static_cast<f64>(p.y) - cy) / ff, 1.0});
    };
    auto dir = [&](u32 track, u32 frame) { return dir_f(track, frame, S.focal[frame]); };
    auto has = [&](u32 track, u32 frame) { return Tracks2D::present(T.pos[track][frame]); };
    auto kabsch = [](const std::vector<V3>& A, const std::vector<V3>& B, const std::vector<u32>& idx) {
        // R tal que B ≈ R·A.
        M3 H;
        for (int i = 0; i < 9; ++i) H.m[i] = 0;
        for (u32 i : idx)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) H(r, c) += (&B[i].x)[r] * (&A[i].x)[c];
        M3 U, V;
        f64 s[3];
        svd3(H, U, s, V);
        M3 R = mul(U, transpose(V));
        if (det(R) < 0) {
            for (int r = 0; r < 3; ++r) U(r, 2) = -U(r, 2);
            R = mul(U, transpose(V));
        }
        return R;
    };
    S.poses.assign(N, Pose{});
    S.poses[0] = Pose{M3{}, V3{}, true};
    std::mt19937 rng(99u);
    f64 sum = 0;
    u64 n = 0;
    std::vector<u32> accepted(M, 0), observed(M, 0);
    for (u32 fr = 1; fr < N; ++fr) {
        if (cancel && cancel->load()) return S;
        // Zoom: o quadro começa com a focal do anterior.
        if (zoom) S.focal[fr] = S.focal[fr - 1];
        std::vector<V3> A, B;
        std::vector<u32> ids;
        for (u32 t = 0; t < M; ++t) if (has(t, fr - 1) && has(t, fr)) { A.push_back(dir(t, fr - 1)); B.push_back(dir(t, fr)); ids.push_back(t); }
        if (A.size() < 8) break;
        std::uniform_int_distribution<u32> pick(0, static_cast<u32>(A.size() - 1));
        std::vector<u32> best;
        M3 bestR;
        const f64 thr = 1.5 / f;
        for (int it = 0; it < 150; ++it) {
            if (cancel && cancel->load()) return S;
            std::vector<u32> smp;
            while (smp.size() < 3) { const u32 r = pick(rng); if (std::find(smp.begin(), smp.end(), r) == smp.end()) smp.push_back(r); }
            const M3 R = kabsch(A, B, smp);
            std::vector<u32> inl;
            for (u32 i = 0; i < A.size(); ++i) if (norm(mul(R, A[i]) - B[i]) < thr) inl.push_back(i);
            if (inl.size() > best.size()) { best = std::move(inl); bestR = R; }
        }
        if (best.size() < 8) break;
        if (zoom) {
            // Tripé com zoom: a focal deste quadro pela seção áurea (±15% da
            // anterior) no erro dos inliers; o giro de cada focal por Kabsch.
            auto residual = [&](f64 ff) {
                std::vector<V3> Bf(B.size());
                for (u32 i : best) Bf[i] = dir_f(ids[i], fr, ff);
                const M3 Rf = kabsch(A, Bf, best);
                f64 e2 = 0;
                for (u32 i : best) {
                    const V3 q = mul(Rf, A[i]);
                    if (q.z <= 1e-9) { e2 += 1e6; continue; }
                    const f64 du = ff * (q.x / q.z - Bf[i].x / Bf[i].z), dv = ff * (q.y / q.z - Bf[i].y / Bf[i].z);
                    e2 += std::min(du * du + dv * dv, 9.0);   // Huber leve: um erro grande não decide
                }
                return e2;
            };
            f64 lo = S.focal[fr] * 0.85, hi = S.focal[fr] * 1.15;
            for (int it = 0; it < 24; ++it) {
                const f64 a = lo + (hi - lo) * 0.381966, b = lo + (hi - lo) * 0.618034;
                if (residual(a) < residual(b)) hi = b; else lo = a;
            }
            S.focal[fr] = 0.5 * (lo + hi);
            for (u32 i = 0; i < ids.size(); ++i) B[i] = dir(ids[i], fr);
        }
        const M3 R = kabsch(A, B, best);
        for (u32 id : ids) ++observed[id];
        for (u32 i : best) {
            const V3 p = mul(R, A[i]);
            const f64 e = std::hypot(p.x / p.z - B[i].x / B[i].z, p.y / p.z - B[i].y / B[i].z) * S.focal[fr];
            sum += e * e;
            ++n;
            if (std::isfinite(e) && e < 1.5) ++accepted[ids[i]];
        }
        S.poses[fr] = Pose{mul(R, S.poses[fr - 1].R), V3{}, true};
    }
    S.rotationTracks.resize(M, 0);
    // Validate the accumulated rotation over each entire track. Small adjacent
    // displacements can fit rotation even on a travelling camera; their drift
    // over the shot must not be mistaken for a tripod reconstruction.
    sum = 0;
    n = 0;
    for (u32 t = 0; t < M; ++t) {
        // A track's overlay is solved only if it actually agrees with the camera.
        // A single lucky match must not mark a moving foreground object as solved.
        if (observed[t] == 0 || accepted[t] < observed[t] - observed[t] / 5) continue;
        u32 first = N, count = 0, good = 0;
        f64 trackSum = 0;
        V3 world{};
        for (u32 fr = 0; fr < N; ++fr) {
            if (!S.poses[fr].valid || !has(t, fr)) continue;
            if (first == N) { first = fr; world = mul(transpose(S.poses[fr].R), dir(t, fr)); continue; }
            ++count;
            const V3 predicted = mul(S.poses[fr].R, world), actual = dir(t, fr);
            if (predicted.z <= 1e-9) continue;
            const f64 e = std::hypot(predicted.x / predicted.z - actual.x / actual.z,
                                      predicted.y / predicted.z - actual.y / actual.z) * S.focal[fr];
            if (std::isfinite(e) && e < 1.5) { ++good; trackSum += e * e; }
        }
        if (count == 0 || good < count - count / 5) continue;
        S.rotationTracks[t] = 1;
        ++S.inliers;
        sum += trackSum;
        n += good;
    }
    S.solved = static_cast<u32>(std::count_if(S.poses.begin(), S.poses.end(), [](const Pose& p) { return p.valid; }));
    S.rms = n ? std::sqrt(sum / static_cast<f64>(n)) : 1e30;
    return S;
}

} // namespace

Vec2 lens_distort(Vec2 p, f32 k1, f32 k2) noexcept {
    const f32 r2 = p.x * p.x + p.y * p.y;
    const f32 d = 1.0f + k1 * r2 + k2 * r2 * r2;
    return Vec2{p.x * d, p.y * d};
}

Vec2 lens_undistort(Vec2 pd, f32 k1, f32 k2) noexcept {
    if (k1 == 0.0f && k2 == 0.0f) return pd;
    Vec2 p = pd;
    for (int i = 0; i < 30; ++i) {
        const f32 r2 = p.x * p.x + p.y * p.y;
        const f32 d = 1.0f + k1 * r2 + k2 * r2 * r2;
        if (!(d > 0.1f)) break;   // fora do domínio do modelo: devolve a última estimativa
        const Vec2 n{pd.x / d, pd.y / d};
        const f32 change = std::fabs(n.x - p.x) + std::fabs(n.y - p.y);
        p = n;
        if (change < 1e-7f) break;
    }
    return p;
}

Tracks2D undistort_tracks(const Tracks2D& tracks, f32 k1, f32 k2) {
    Tracks2D out = tracks;
    if (k1 == 0.0f && k2 == 0.0f) return out;
    const f32 cx = static_cast<f32>(tracks.width) * 0.5f, cy = static_cast<f32>(tracks.height) * 0.5f;
    const f32 h = std::max(1.0f, static_cast<f32>(tracks.height) * 0.5f);
    for (auto& row : out.pos)
        for (Vec2& p : row) {
            if (!Tracks2D::present(p)) continue;
            const Vec2 q = lens_undistort(Vec2{(p.x - cx) / h, (p.y - cy) / h}, k1, k2);
            p = Vec2{cx + q.x * h, cy + q.y * h};
        }
    return out;
}

CameraSolution solve_camera(const Tracks2D& tracks, const SolveOptions& opt, const std::atomic<bool>* cancel,
                            std::atomic<f32>* progress) {
    CameraSolution out;
    if (opt.motion > 3 || opt.shotType > 2 || opt.lens > 2 || !std::isfinite(opt.knownFovDeg) || opt.knownFovDeg < 0 ||
        opt.knownFovDeg > 150 || !std::isfinite(opt.sourceScale) || opt.sourceScale <= 0) {
        out.failure = "invalid camera constraints"; return out;
    }
    if (opt.shotType == 2 && !(opt.knownFovDeg > 0.0f)) { out.failure = "informe o campo de visao (FOV) da tomada"; return out; }
    const u32 N = tracks.frames;
    if (cancel && cancel->load()) { out.failure = "cancelado"; return out; }
    if (tracks.width == 0 || tracks.height == 0 ||
        std::any_of(tracks.pos.begin(), tracks.pos.end(), [N](const auto& row) { return row.size() != N; })) {
        out.failure = "rastros de camera invalidos";
        return out;
    }
    for (const auto& row : tracks.pos) {
        u32 c = 0;
        for (Vec2 p : row) c += Tracks2D::present(p) ? 1u : 0u;
        if (c >= 2) ++out.tracks;
    }
    if (N < 10 || out.tracks < 30) {
        out.failure = N < 10 ? "video curto demais para rastrear a camera" : "poucos pontos com textura para seguir";
        return out;
    }
    const f64 h = static_cast<f64>(tracks.height);
    auto focalOf = [&](f64 fovDeg) { return 0.5 * h / std::tan(0.5 * fovDeg * kPi / 180.0); };
    // Pontuação de um solve: erro, com castigo para quadros sem pose.
    auto score = [&](const SolveState& s) {
        if (s.solved < 2) return 1e30;
        const f64 missing = static_cast<f64>(N - s.solved) / static_cast<f64>(N);
        return s.rms + 100.0 * missing;
    };
    const bool known = opt.knownFovDeg > 0.0f;
    // FOV informada vence o zoom (a focal do quadro não é estimada).
    const bool zoom = opt.shotType == 1 && !known;
    SolveCfg cfg;
    cfg.zoom = zoom;
    cfg.init = opt.motion == 3 ? 1u : 0u;
    auto run = [&](f64 fovDeg, bool rot) {
        return rot ? solve_rotation(tracks, focalOf(fovDeg), cancel, zoom) : solve_with_focal(tracks, focalOf(fovDeg), opt.mode, cancel, cfg);
    };

    const f64 lo = known ? opt.knownFovDeg : std::max(5.0f, opt.fovMinDeg);
    const f64 hi = known ? opt.knownFovDeg : std::min(150.0f, opt.fovMaxDeg);
    // Tripé? A FOV do meio decide o modo.
    SolveState probe = run(0.5 * (lo + hi), false);
    if (opt.motion == 0 && !(cancel && cancel->load())) {
        // Automático: num plano a essencial é degenerada (o 8 pontos tem
        // solução demais) e a cena inicial sai errada — às vezes com erro
        // baixo (medido: 0,73 px numa cena plana, câmera 96% fora do caminho).
        // A homografia também tenta; ela só fica com uma vitória clara (10%).
        SolveCfg flat = cfg;
        flat.init = 1;
        SolveState fp = solve_with_focal(tracks, focalOf(0.5 * (lo + hi)), opt.mode, cancel, flat);
        if (score(fp) < 0.9 * score(probe)) { probe = std::move(fp); cfg = flat; }
    }
    SolveState rotationProbe = run(0.5 * (lo + hi), true);
    // Essential matrices are degenerate under pure rotation. Foreground motion
    // can invent an apparently valid translation, so test the competing model
    // against whole background tracks instead of trusting decomposition alone.
    auto rotationFits = [&](const SolveState& s) {
        return s.solved >= std::max<u32>(10, N / 2) && s.inliers >= 30 &&
               static_cast<u64>(s.inliers) * 3 >= static_cast<u64>(out.tracks) * 2 && s.rms < 1.5;
    };
    f64 rotationFov = 0.5 * (lo + hi);
    if (!known && !rotationFits(rotationProbe)) {
        const int coarse = opt.mode == TrackMode::Fast ? 5 : 7;
        for (int i = 0; i < coarse; ++i) {
            if (cancel && cancel->load()) break;
            const f64 fov = lo + (hi - lo) * (static_cast<f64>(i) + 0.5) / coarse;
            SolveState candidate = run(fov, true);
            if (candidate.inliers > rotationProbe.inliers ||
                (candidate.inliers == rotationProbe.inliers && candidate.rms < rotationProbe.rms)) {
                rotationProbe = std::move(candidate);
                rotationFov = fov;
            }
        }
    }
    // Failure to initialize translation does not prove a tripod shot. Require
    // whole-track background consensus for the rotation-only model as well.
    const bool rot = opt.motion == 2 || (opt.motion == 0 && rotationFits(rotationProbe));
    if (progress) progress->store(0.15f);
    f64 bestFov = rot ? rotationFov : 0.5 * (lo + hi);
    SolveState best = rot ? std::move(rotationProbe) : std::move(probe);
    if (!known) {
        // Busca grossa + seção áurea em volta do melhor.
        const int coarse = opt.mode == TrackMode::Fast ? 5 : 7;
        f64 bestScore = score(best);
        for (int i = 0; i < coarse; ++i) {
            if (cancel && cancel->load()) break;
            const f64 fov = lo + (hi - lo) * (static_cast<f64>(i) + 0.5) / static_cast<f64>(coarse);
            SolveState s = run(fov, rot);
            if (rot && !rotationFits(s)) continue;
            const f64 sc = score(s);
            if (sc < bestScore) { bestScore = sc; best = std::move(s); bestFov = fov; }
            if (progress) progress->store(0.15f + 0.45f * static_cast<f32>(i + 1) / static_cast<f32>(coarse));
        }
    }
    if (rot && !known && !(cancel && cancel->load())) {
        // Coarse FOV bins leave systematic pan drift at image edges. Refine the
        // same global rotation residual instead of dropping those valid tracks.
        const int coarse = opt.mode == TrackMode::Fast ? 5 : 7;
        const f64 span = (hi - lo) / coarse;
        f64 left = std::max(lo, bestFov - span), right = std::min(hi, bestFov + span);
        auto objective = [&](const SolveState& s) {
            return s.solved < std::max<u32>(10, N / 2) || s.inliers < 30 ? 1e30
                : s.rms + 3.0 * (1.0 - static_cast<f64>(s.inliers) / std::max<u32>(1, out.tracks));
        };
        f64 bestValue = objective(best);
        for (int iteration = 0; iteration < (opt.mode == TrackMode::Fast ? 8 : 12); ++iteration) {
            if (cancel && cancel->load()) break;
            const f64 a = left + (right - left) / 3.0, b = right - (right - left) / 3.0;
            SolveState sa = run(a, true), sb = run(b, true);
            const f64 va = objective(sa), vb = objective(sb);
            if (va < bestValue) { bestValue = va; best = std::move(sa); bestFov = a; }
            if (vb < bestValue) { bestValue = vb; best = std::move(sb); bestFov = b; }
            if (va < vb) right = b; else left = a;
        }
    }
    // Os rastros sem distorção (iguais aos crus enquanto a lente não é resolvida).
    Tracks2D undStore;
    const Tracks2D* und = &tracks;
    BAParams bp;
    bool k2Solved = false;
    if (!rot && best.solved >= 3 && !(cancel && cancel->load())) {
        // Bundle adjustment com a focal livre (ou presa, se conhecida); zoom:
        // uma focal por quadro-chave, suave.
        const u32 keysMax = opt.mode == TrackMode::Fast ? 30 : (opt.mode == TrackMode::High ? 90 : 60);
        const int iters = opt.mode == TrackMode::Fast ? 12 : (opt.mode == TrackMode::High ? 40 : 25);
        bp.freeFocal = !known;
        bp.perKey = zoom;
        auto adjust = [&](SolveState& s, BAParams& p, int passes) {
            for (int pass = 0; pass < passes; ++pass) {
                Tracks2D fresh;
                bundle_adjust(s, tracks, *und, p, keysMax, iters, cancel, &fresh);
                if (fresh.frames) { undStore = std::move(fresh); und = &undStore; }
                evaluate(s, *und);
            }
        };
        auto median_focal = [](const SolveState& s) {
            std::vector<f64> v;
            for (usize i = 0; i < s.focal.size(); ++i) if (s.poses[i].valid) v.push_back(s.focal[i]);
            if (v.empty()) return s.focal.empty() ? 1.0 : s.focal[0];
            std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
            return v[v.size() / 2];
        };
        // A coarse focal seed can lose PnP support before global BA recovers
        // the lens. Reuse the tracked images with that refined focal instead
        // of accepting the short prefix or decoding the footage again.
        auto recover = [&] {
            if (known || best.solved >= N || (cancel && cancel->load())) return;
            SolveState recovered = solve_with_focal(*und, median_focal(best), opt.mode, cancel, cfg);
            if (recovered.solved > best.solved) {
                best = std::move(recovered);
                adjust(best, bp, 1);
            }
        };
        adjust(best, bp, 2);
        recover();
        if (opt.lens >= 1 && !(cancel && cancel->load())) {
            // Distorção: k1 livre junto com tudo (nas observações cruas). `u` =
            // os rastros sem a distorção atual (cópia: as tentativas não sujam).
            auto adjust_into = [&](SolveState& s, BAParams& p, int passes, Tracks2D& u) {
                for (int pass = 0; pass < passes; ++pass) {
                    Tracks2D fresh;
                    bundle_adjust(s, tracks, u, p, keysMax, iters, cancel, &fresh);
                    if (fresh.frames) u = std::move(fresh);
                    evaluate(s, u);
                }
            };
            Tracks2D u = *und;
            bp.lens = 1;
            adjust_into(best, bp, 1, u);
            if (std::fabs(bp.k1) > 0.003 && !(cancel && cancel->load())) {
                // A busca da FOV e o par inicial viram a distorção como erro: o
                // solve de novo nos rastros sem ela (medido: k1 −0,06 → FOV 34,8°
                // sem lente, 60,2° com; verdade 60°). Fica o de menor erro.
                SolveState again = solve_with_focal(u, median_focal(best), opt.mode, cancel, cfg);
                if (again.solved >= 3) {
                    BAParams pa = bp;
                    Tracks2D ua = u;
                    adjust_into(again, pa, 2, ua);
                    if (score(again) < score(best)) { best = std::move(again); bp = pa; u = std::move(ua); }
                }
            }
            // k2 só quando estável: as mesmas iterações com e sem ele; fica com
            // melhora clara do erro e valor plausível.
            SolveState s1 = best, s2 = best;
            BAParams b1 = bp, b2 = bp;
            b2.lens = 2;
            Tracks2D u1 = u, u2 = u;
            adjust_into(s1, b1, 1, u1);
            adjust_into(s2, b2, 1, u2);
            if (s2.solved >= s1.solved && s2.rms < 0.97 * s1.rms && std::fabs(b2.k2) < 0.5 && std::fabs(b2.k1) < 1.0) {
                best = std::move(s2); bp = b2; u = std::move(u2);
                k2Solved = true;
            } else {
                best = std::move(s1); bp = b1; u = std::move(u1);
                bp.k2 = 0;
            }
            bp.lens = 0;
            undStore = std::move(u);
            und = &undStore;
            recover();
        }
        const u32 first = static_cast<u32>(std::find_if(best.poses.begin(), best.poses.end(), [](const Pose& p) { return p.valid; }) - best.poses.begin());
        bestFov = 2.0 * std::atan(0.5 * h / best.focal[std::min(first, N - 1)]) * 180.0 / kPi;
        if (progress) progress->store(1.0f);
    }
    if (cancel && cancel->load()) { out.failure = "cancelado"; return out; }
    out.method = rot ? static_cast<u8>(SolveMethod::Tripod) : (best.homography ? static_cast<u8>(SolveMethod::FlatScene)
                                                                                 : static_cast<u8>(SolveMethod::Typical));
    out.shotType = known ? static_cast<u8>(ShotType::SpecifyFov) : (zoom ? static_cast<u8>(ShotType::VariableZoom)
                                                                           : static_cast<u8>(ShotType::FixedFov));
    if (best.solved != N || best.rms > 3.0) {
        out.failure = best.solved < 2 ? "nao deu para resolver a camera (pouca paralaxe ou cena sem textura)"
                                      : "solve fraco: erro alto ou poucos quadros resolvidos";
        if (best.solved >= 2 && !rot && zoom) out.failure += " (zoom variavel: zoom e movimento para a frente se confundem)";
        out.rmsError = static_cast<f32>(std::min(best.rms, 999.0));
        out.framesSolved = best.solved;
        return out;
    }
    out.ok = true;
    out.rotationOnly = rot;
    out.fovY = static_cast<f32>(bestFov * kPi / 180.0);
    if (zoom) {
        out.frameFovY.resize(N);
        for (u32 i = 0; i < N; ++i) out.frameFovY[i] = static_cast<f32>(2.0 * std::atan(0.5 * h / best.focal[i]));
    }
    out.k1 = static_cast<f32>(bp.k1);
    out.k2 = static_cast<f32>(bp.k2);
    out.lensSolved = !rot && opt.lens >= 1;
    out.k2Solved = out.lensSolved && k2Solved;
    out.poses.resize(N);
    for (u32 i = 0; i < N; ++i) {
        const Pose& p = best.poses[i];
        out.poses[i].valid = p.valid;
        for (int k = 0; k < 9; ++k) out.poses[i].R[k] = p.R.m[k];
        out.poses[i].t[0] = p.t.x; out.poses[i].t[1] = p.t.y; out.poses[i].t[2] = p.t.z;
    }
    out.trackSolved = rot ? best.rotationTracks : std::vector<u8>(tracks.pos.size(), 0);
    for (usize t = 0; t < best.X.size(); ++t) {
        if (!best.hasX[t]) continue;
        out.points.push_back(Vec3{static_cast<f32>(best.X[t].x), static_cast<f32>(best.X[t].y), static_cast<f32>(best.X[t].z)});
        out.trackSolved[t] = 1;
    }
    out.inliers = rot ? best.inliers : static_cast<u32>(out.points.size());
    out.framesSolved = best.solved;
    out.rmsError = static_cast<f32>(best.rms);
    // "Average Error": média de |reprojeção| nas observações CRUAS (com a
    // distorção), em px da fonte; e por quadro.
    {
        const f64 cx = static_cast<f64>(tracks.width) * 0.5, cy = static_cast<f64>(tracks.height) * 0.5;
        const f64 halfH = std::max(1.0, h * 0.5);
        std::vector<f64> sum(N, 0.0);
        std::vector<u32> cnt(N, 0);
        auto add = [&](u32 fr, f64 qx, f64 qy, Vec2 o) {
            const Vec2 d = lens_distort(Vec2{static_cast<f32>(qx / halfH), static_cast<f32>(qy / halfH)}, out.k1, out.k2);
            const f64 e = std::hypot(d.x * halfH + cx - o.x, d.y * halfH + cy - o.y);
            if (!std::isfinite(e)) return;
            sum[fr] += e;
            ++cnt[fr];
        };
        for (usize t = 0; t < tracks.pos.size(); ++t) {
            if (rot) {
                if (t >= best.rotationTracks.size() || !best.rotationTracks[t]) continue;
                // Tripé: a direção no mundo vem da primeira observação.
                V3 world{};
                bool have = false;
                for (u32 fr = 0; fr < N; ++fr) {
                    const Vec2 o = tracks.pos[t][fr];
                    if (!best.poses[fr].valid || !Tracks2D::present(o)) continue;
                    const f64 f = best.focal.empty() ? focalOf(bestFov) : best.focal[fr];
                    if (!have) { world = mul(transpose(best.poses[fr].R), normalized(V3{(o.x - cx) / f, (o.y - cy) / f, 1.0})); have = true; continue; }
                    const V3 c = mul(best.poses[fr].R, world);
                    if (c.z <= 1e-9) continue;
                    add(fr, f * c.x / c.z, f * c.y / c.z, o);
                }
                continue;
            }
            if (!best.hasX[t]) continue;
            for (u32 fr = 0; fr < N; ++fr) {
                const Vec2 o = tracks.pos[t][fr];
                if (!best.poses[fr].valid || !Tracks2D::present(o)) continue;
                f64 u, v;
                if (!project(best.poses[fr], best.X[t], u, v)) continue;
                add(fr, u * best.focal[fr], v * best.focal[fr], o);
            }
        }
        f64 total = 0;
        u64 n = 0;
        out.frameErrorPx.assign(N, -1.0f);
        for (u32 fr = 0; fr < N; ++fr) {
            total += sum[fr];
            n += cnt[fr];
            if (cnt[fr]) out.frameErrorPx[fr] = static_cast<f32>(sum[fr] / cnt[fr] * opt.sourceScale);
        }
        out.averageErrorPx = n ? static_cast<f32>(total / static_cast<f64>(n) * opt.sourceScale) : 0.0f;
    }
    const f32 share = static_cast<f32>(out.inliers) / static_cast<f32>(std::max<u32>(1, out.tracks));
    out.confidence = std::clamp((1.0f - out.rmsError / 2.0f) * std::min(1.0f, share * 2.0f)
                                * static_cast<f32>(out.framesSolved) / static_cast<f32>(N), 0.0f, 1.0f);
    return out;
}

Vec3 euler_zyx_from_matrix(const f64 m[9]) noexcept {
    // R = Rz·Ry·Rx: R20 = −sin(ry), R21 = cos(ry)·sin(rx), R22 = cos(ry)·cos(rx),
    // R10 = cos(ry)·sin(rz), R00 = cos(ry)·cos(rz).
    const f64 ry = std::asin(std::clamp(-m[6], -1.0, 1.0));
    f64 rx, rz;
    if (std::fabs(m[6]) < 0.999999) {
        rx = std::atan2(m[7], m[8]);
        rz = std::atan2(m[3], m[0]);
    } else {
        rx = 0.0;
        rz = std::atan2(-m[1], m[4]);
    }
    return Vec3{static_cast<f32>(rx), static_cast<f32>(ry), static_cast<f32>(rz)};
}

bool dominant_plane(const std::vector<Vec3>& pts, f32 tolerance, f32 minShare, Vec3& centroid, Vec3& normal) {
    if (pts.size() < 3 || !std::isfinite(tolerance) || tolerance <= 0 ||
        !std::isfinite(minShare) || minShare <= 0 || minShare > 1) return false;
    if (std::any_of(pts.begin(), pts.end(), [](Vec3 p) { return !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z); })) return false;
    std::mt19937 rng(7u);
    std::uniform_int_distribution<usize> pick(0, pts.size() - 1);
    usize bestCount = 0;
    Vec3 bn{0, -1, 0};
    f32 bd = 0;
    for (int it = 0; it < 400; ++it) {
        const Vec3 a = pts[pick(rng)], b = pts[pick(rng)], c = pts[pick(rng)];
        const Vec3 n = (b - a).cross(c - a);
        const f32 len = n.length();
        if (len < 1e-9f) continue;
        const Vec3 u = n * (1.0f / len);
        const f32 d = u.dot(a);
        usize cnt = 0;
        for (const Vec3& p : pts) if (std::fabs(u.dot(p) - d) < tolerance) ++cnt;
        if (cnt > bestCount) { bestCount = cnt; bn = u; bd = d; }
    }
    if (static_cast<f32>(bestCount) < minShare * static_cast<f32>(pts.size())) return false;
    // RANSAC chooses membership, not the final plane. Fit its normal using every
    // inlier; a random three-point normal visibly tilted the tracked scene floor.
    Vec3 center{};
    for (int pass = 0; pass < 3; ++pass) {
        V3 sum{};
        usize cnt = 0;
        for (const Vec3& p : pts) if (std::fabs(bn.dot(p) - bd) < tolerance) { sum = sum + V3{p.x, p.y, p.z}; ++cnt; }
        if (cnt < 3 || static_cast<f32>(cnt) < minShare * static_cast<f32>(pts.size())) return false;
        sum = sum * (1.0 / static_cast<f64>(cnt));
        std::vector<f64> covariance(9, 0.0), vectors, values;
        for (const Vec3& p : pts) if (std::fabs(bn.dot(p) - bd) < tolerance) {
            const f64 d[3] = {p.x - sum.x, p.y - sum.y, p.z - sum.z};
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) covariance[r * 3 + c] += d[r] * d[c];
        }
        jacobi_eigen(3, covariance, vectors, values);
        // Collinear/coincident features cannot establish a floor orientation.
        if (values[2] <= 0 || values[1] <= values[2] * 1e-8) return false;
        Vec3 fitted{static_cast<f32>(vectors[0]), static_cast<f32>(vectors[3]), static_cast<f32>(vectors[6])};
        if (fitted.dot(bn) < 0) fitted = fitted * -1.0f;
        center = Vec3{static_cast<f32>(sum.x), static_cast<f32>(sum.y), static_cast<f32>(sum.z)};
        bn = fitted;
        bd = bn.dot(center);
    }
    const usize finalCount = static_cast<usize>(std::count_if(pts.begin(), pts.end(), [&](Vec3 p) { return std::fabs(bn.dot(p) - bd) < tolerance; }));
    if (static_cast<f32>(finalCount) < minShare * static_cast<f32>(pts.size())) return false;
    centroid = center;
    normal = bn;
    return true;
}

} // namespace aurea::tracking
