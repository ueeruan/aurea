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
            o.px[static_cast<usize>(y) * o.width + x] = 0.25f * (g.at(X, Y) + g.at(X + 1, Y) + g.at(X, Y + 1) + g.at(X + 1, Y + 1));
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

/// Lucas-Kanade em pirâmide (janela 9×9): posição em `cur` do ponto `p` de
/// `prev`. Falso se perdeu (região lisa ou não convergiu).
bool lk_track(const std::vector<Gray>& prev, const std::vector<Gray>& cur, Vec2 p, Vec2& out) {
    const int levels = static_cast<int>(std::min(prev.size(), cur.size()));
    Vec2 d{0, 0};
    for (int L = levels - 1; L >= 0; --L) {
        const f32 s = 1.0f / static_cast<f32>(1 << L);
        const Gray& A = prev[static_cast<usize>(L)];
        const Gray& B = cur[static_cast<usize>(L)];
        const Vec2 q{p.x * s, p.y * s};
        f32 ix[81], iy[81], a[81];
        f64 gxx = 0, gxy = 0, gyy = 0;
        int k = 0;
        for (int wy = -4; wy <= 4; ++wy)
            for (int wx = -4; wx <= 4; ++wx, ++k) {
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
        if (lmin < 1e-4 || det < 1e-12) return false;
        for (int it = 0; it < 20; ++it) {
            f64 bx = 0, by = 0;
            k = 0;
            for (int wy = -4; wy <= 4; ++wy)
                for (int wx = -4; wx <= 4; ++wx, ++k) {
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
    std::vector<u8> occ(static_cast<usize>(gw) * gh, 0);
    auto mark = [&](Vec2 p) {
        const u32 cx = static_cast<u32>(p.x / cell), cy = static_cast<u32>(p.y / cell);
        if (cx < gw && cy < gh) occ[static_cast<usize>(cy) * gw + cx] = 1;
    };
    auto freeAt = [&](Vec2 p) {
        const i32 cx = static_cast<i32>(p.x / cell), cy = static_cast<i32>(p.y / cell);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const i32 X = cx + dx, Y = cy + dy;
                if (X >= 0 && Y >= 0 && X < static_cast<i32>(gw) && Y < static_cast<i32>(gh) && occ[static_cast<usize>(Y) * gw + static_cast<usize>(X)]) return false;
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
    std::vector<f64> AtA(81, 0.0);
    for (u32 i : idx) {
        const V3 a = x1[i], b = x2[i];
        const f64 r[9] = {b.x * a.x, b.x * a.y, b.x, b.y * a.x, b.y * a.y, b.y, a.x, a.y, 1.0};
        for (int p = 0; p < 9; ++p)
            for (int q = 0; q < 9; ++q) AtA[static_cast<usize>(p * 9 + q)] += r[p] * r[q];
    }
    const std::vector<f64> e = null_vector(AtA, 9);
    M3 F;
    for (int k = 0; k < 9; ++k) F.m[k] = e[static_cast<usize>(k)];
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

struct SolveState {
    std::vector<Pose> poses;
    std::vector<V3> X;
    std::vector<u8> hasX;
    f64 rms = 1e30;
    u32 solved = 0, inliers = 0;
    u32 fixedFrame = 0;
    bool rotationOnly = false;
    std::string failure;
};

/// Erro final (px) e inliers: ponto com erro > 4 px em algum quadro sai.
void evaluate(SolveState& S, const Tracks2D& T, f64 f) {
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

/// Bundle adjustment (Levenberg-Marquardt, complemento de Schur) sobre os
/// quadros-chave: poses, pontos e a distância focal ao mesmo tempo, erro em
/// px com peso de Huber. O quadro fixo prende o referencial. Depois, os
/// quadros fora da chave ganham pose pelos pontos refinados.
void bundle_adjust(SolveState& S, const Tracks2D& T, f64& f, bool freeFocal, u32 maxKeys, int iterations,
                   const std::atomic<bool>* cancel) {
    const u32 N = T.frames, M = static_cast<u32>(T.pos.size());
    const f64 cx = static_cast<f64>(T.width) * 0.5, cy = static_cast<f64>(T.height) * 0.5;
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
    const int nc = nb * 6 + (freeFocal ? 1 : 0);
    const int fi = nc - 1;   // índice da focal (se livre)
    struct O { u32 key; f64 u, v; };
    std::vector<u32> pts;
    std::vector<std::vector<O>> obs;
    for (u32 t = 0; t < M; ++t) {
        if (!S.hasX[t]) continue;
        std::vector<O> o;
        for (u32 k = 0; k < K; ++k) {
            const Vec2 p = T.pos[t][keys[k]];
            if (Tracks2D::present(p)) o.push_back({k, static_cast<f64>(p.x) - cx, static_cast<f64>(p.y) - cy});
        }
        if (o.size() >= 2) { pts.push_back(t); obs.push_back(std::move(o)); }
    }
    if (pts.size() < 20) return;
    const f64 huber = 2.0;
    auto cost = [&](const std::vector<Pose>& P, const std::vector<V3>& X, f64 ff) {
        f64 c = 0;
        for (usize i = 0; i < pts.size(); ++i)
            for (const O& o : obs[i]) {
                const V3 q = mul(P[o.key].R, X[i]) + P[o.key].t;
                if (q.z <= 1e-9) { c += huber * huber * 4; continue; }
                const f64 e = std::hypot(ff * q.x / q.z - o.u, ff * q.y / q.z - o.v);
                c += e <= huber ? e * e : 2 * huber * e - huber * huber;
            }
        return c;
    };
    std::vector<Pose> P(K);
    for (u32 k = 0; k < K; ++k) P[k] = S.poses[keys[k]];
    std::vector<V3> X(pts.size());
    for (usize i = 0; i < pts.size(); ++i) X[i] = S.X[pts[i]];
    f64 lambda = 1e-3;
    f64 c0 = cost(P, X, f);
    std::vector<f64> Sm(static_cast<usize>(nc) * nc), rhs(static_cast<usize>(nc));
    for (int it = 0; it < iterations; ++it) {
        if (cancel && cancel->load()) return;
        std::fill(Sm.begin(), Sm.end(), 0.0);
        std::fill(rhs.begin(), rhs.end(), 0.0);
        // Por ponto: V (3×3), g_p, e os blocos W por observação.
        struct PtSys { f64 V[9]; f64 g[3]; std::vector<std::array<f64, 21>> W; std::vector<u32> keys; };
        std::vector<PtSys> sys(pts.size());
        for (usize i = 0; i < pts.size(); ++i) {
            PtSys& ps = sys[i];
            std::fill(std::begin(ps.V), std::end(ps.V), 0.0);
            std::fill(std::begin(ps.g), std::end(ps.g), 0.0);
            for (const O& o : obs[i]) {
                const Pose& Pk = P[o.key];
                const V3 RX = mul(Pk.R, X[i]);
                const V3 q = RX + Pk.t;
                if (q.z <= 1e-9) continue;
                const f64 iz = 1.0 / q.z;
                const f64 ru = f * q.x * iz - o.u, rv = f * q.y * iz - o.v;
                const f64 e = std::sqrt(ru * ru + rv * rv);
                const f64 w = e <= huber ? 1.0 : huber / e;
                const f64 Ju[3] = {f * iz, 0, -f * q.x * iz * iz}, Jv[3] = {0, f * iz, -f * q.y * iz * iz};
                const f64 Sk[9] = {0, RX.z, -RX.y, -RX.z, 0, RX.x, RX.y, -RX.x, 0};
                f64 Jc_u[7], Jc_v[7];   // 6 da pose + focal
                for (int k = 0; k < 3; ++k) {
                    Jc_u[k] = Ju[0] * Sk[k] + Ju[1] * Sk[3 + k] + Ju[2] * Sk[6 + k];
                    Jc_v[k] = Jv[0] * Sk[k] + Jv[1] * Sk[3 + k] + Jv[2] * Sk[6 + k];
                    Jc_u[3 + k] = Ju[k];
                    Jc_v[3 + k] = Jv[k];
                }
                Jc_u[6] = q.x * iz;
                Jc_v[6] = q.y * iz;
                f64 Jp_u[3], Jp_v[3];
                for (int k = 0; k < 3; ++k) {
                    Jp_u[k] = Ju[0] * Pk.R(0, k) + Ju[1] * Pk.R(1, k) + Ju[2] * Pk.R(2, k);
                    Jp_v[k] = Jv[0] * Pk.R(0, k) + Jv[1] * Pk.R(1, k) + Jv[2] * Pk.R(2, k);
                }
                // Índices dos 7 parâmetros de câmera desta observação (−1 = fixo).
                int idx[7];
                const i32 b = block[o.key];
                for (int k = 0; k < 6; ++k) idx[k] = b >= 0 ? b * 6 + k : -1;
                idx[6] = freeFocal ? fi : -1;
                for (int a = 0; a < 7; ++a) {
                    if (idx[a] < 0) continue;
                    rhs[static_cast<usize>(idx[a])] -= w * (Jc_u[a] * ru + Jc_v[a] * rv);
                    for (int c = 0; c < 7; ++c) {
                        if (idx[c] < 0) continue;
                        Sm[static_cast<usize>(idx[a]) * nc + static_cast<usize>(idx[c])] += w * (Jc_u[a] * Jc_u[c] + Jc_v[a] * Jc_v[c]);
                    }
                }
                for (int a = 0; a < 3; ++a) {
                    ps.g[a] -= w * (Jp_u[a] * ru + Jp_v[a] * rv);
                    for (int c = 0; c < 3; ++c) ps.V[a * 3 + c] += w * (Jp_u[a] * Jp_u[c] + Jp_v[a] * Jp_v[c]);
                }
                std::array<f64, 21> Wo{};
                for (int a = 0; a < 7; ++a)
                    for (int c = 0; c < 3; ++c) Wo[static_cast<usize>(a * 3 + c)] = w * (Jc_u[a] * Jp_u[c] + Jc_v[a] * Jp_v[c]);
                ps.W.push_back(Wo);
                ps.keys.push_back(o.key);
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
            const f64 d = det(Vm);
            if (std::fabs(d) < 1e-30) { okV = false; break; }
            std::array<f64, 9> inv{};
            inv[0] = (V[4] * V[8] - V[5] * V[7]) / d; inv[1] = (V[2] * V[7] - V[1] * V[8]) / d; inv[2] = (V[1] * V[5] - V[2] * V[4]) / d;
            inv[3] = (V[5] * V[6] - V[3] * V[8]) / d; inv[4] = (V[0] * V[8] - V[2] * V[6]) / d; inv[5] = (V[2] * V[3] - V[0] * V[5]) / d;
            inv[6] = (V[3] * V[7] - V[4] * V[6]) / d; inv[7] = (V[1] * V[6] - V[0] * V[7]) / d; inv[8] = (V[0] * V[4] - V[1] * V[3]) / d;
            Vinv[i] = inv;
            const usize no = ps.W.size();
            std::vector<std::array<f64, 21>> Y(no);   // W·V⁻¹
            for (usize a = 0; a < no; ++a)
                for (int r = 0; r < 7; ++r)
                    for (int c = 0; c < 3; ++c)
                        Y[a][static_cast<usize>(r * 3 + c)] = ps.W[a][static_cast<usize>(r * 3)] * inv[c] + ps.W[a][static_cast<usize>(r * 3 + 1)] * inv[3 + c]
                                                            + ps.W[a][static_cast<usize>(r * 3 + 2)] * inv[6 + c];
            for (usize a = 0; a < no; ++a) {
                int ia[7];
                const i32 ba = block[ps.keys[a]];
                for (int k = 0; k < 6; ++k) ia[k] = ba >= 0 ? ba * 6 + k : -1;
                ia[6] = freeFocal ? fi : -1;
                for (int r = 0; r < 7; ++r) {
                    if (ia[r] < 0) continue;
                    rhs[static_cast<usize>(ia[r])] -= Y[a][static_cast<usize>(r * 3)] * ps.g[0] + Y[a][static_cast<usize>(r * 3 + 1)] * ps.g[1] + Y[a][static_cast<usize>(r * 3 + 2)] * ps.g[2];
                }
                for (usize bIdx = 0; bIdx < no; ++bIdx) {
                    int ib[7];
                    const i32 bb = block[ps.keys[bIdx]];
                    for (int k = 0; k < 6; ++k) ib[k] = bb >= 0 ? bb * 6 + k : -1;
                    ib[6] = freeFocal ? fi : -1;
                    for (int r = 0; r < 7; ++r) {
                        if (ia[r] < 0) continue;
                        for (int c = 0; c < 7; ++c) {
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
        f64 f2 = freeFocal ? f + dc[static_cast<usize>(fi)] : f;
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
                const i32 ba = block[ps.keys[a]];
                for (int row = 0; row < 7; ++row) {
                    const int id = row < 6 ? (ba >= 0 ? ba * 6 + row : -1) : (freeFocal ? fi : -1);
                    if (id < 0) continue;
                    for (int c = 0; c < 3; ++c) r[c] -= ps.W[a][static_cast<usize>(row * 3 + c)] * dc[static_cast<usize>(id)];
                }
            }
            const auto& vi = Vinv[i];
            X2[i] = X[i] + V3{vi[0] * r[0] + vi[1] * r[1] + vi[2] * r[2], vi[3] * r[0] + vi[4] * r[1] + vi[5] * r[2], vi[6] * r[0] + vi[7] * r[1] + vi[8] * r[2]};
        }
        const f64 c1 = cost(P2, X2, f2);
        if (c1 < c0 && f2 > 1.0) {
            const bool small = (c0 - c1) < 1e-7 * c0;
            P.swap(P2);
            X.swap(X2);
            f = f2;
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
    // Quadros fora da chave e pontos fora do BA, com a focal nova.
    const f64 fcx = cx, fcy = cy;
    auto ray = [&](u32 track, u32 frame) {
        const Vec2 q = T.pos[track][frame];
        return V3{(static_cast<f64>(q.x) - fcx) / f, (static_cast<f64>(q.y) - fcy) / f, 1.0};
    };
    for (u32 fr = 0; fr < N; ++fr) {
        if (!S.poses[fr].valid || std::binary_search(keys.begin(), keys.end(), fr)) continue;
        std::vector<V3> Xs, rs;
        for (u32 t = 0; t < M; ++t) if (S.hasX[t] && Tracks2D::present(T.pos[t][fr])) { Xs.push_back(S.X[t]); rs.push_back(ray(t, fr)); }
        if (Xs.size() >= 8) refine_pose(S.poses[fr], Xs, rs, f);
    }
    for (u32 t = 0; t < M; ++t) {
        if (!S.hasX[t]) continue;
        std::vector<const Pose*> ps;
        std::vector<V3> rs;
        for (u32 fr = 0; fr < N; ++fr) if (Tracks2D::present(T.pos[t][fr]) && S.poses[fr].valid) { ps.push_back(&S.poses[fr]); rs.push_back(ray(t, fr)); }
        if (ps.size() >= 2) refine_point(S.X[t], ps, rs, f);
    }
}

/// Um solve completo com a distância focal `f` (px da análise).
SolveState solve_with_focal(const Tracks2D& T, f64 f, TrackMode mode, const std::atomic<bool>* cancel) {
    SolveState S;
    const u32 N = T.frames, M = static_cast<u32>(T.pos.size());
    const f64 cx = static_cast<f64>(T.width) * 0.5, cy = static_cast<f64>(T.height) * 0.5;
    auto ray = [&](u32 track, u32 frame) {
        const Vec2 p = T.pos[track][frame];
        return V3{(static_cast<f64>(p.x) - cx) / f, (static_cast<f64>(p.y) - cy) / f, 1.0};
    };
    auto has = [&](u32 track, u32 frame) { return Tracks2D::present(T.pos[track][frame]); };
    S.poses.assign(N, Pose{});
    S.X.assign(M, V3{});
    S.hasX.assign(M, 0);
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
        if (x1.size() < 30) break;
        std::vector<u32> bestInl;
        M3 bestE;
        std::uniform_int_distribution<u32> pick(0, static_cast<u32>(x1.size() - 1));
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
        Pose P;
        if (!decompose(E, x1, x2, bestInl, P)) continue;
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
            if (parallax(S.poses[first], S.poses[frame], X) < 2.0 * kPi / 180.0) continue;
            bool good = true;
            for (usize i = 0; i < ps.size() && good; ++i) {
                f64 u, v;
                good = project(*ps[i], X, u, v) && std::hypot(u - rs[i].x, v - rs[i].y) * f < 2.0;
            }
            if (good) { S.X[t] = X; S.hasX[t] = 1; }
        }
    };
    for (u32 fr = a + 1; fr < N; ++fr) {
        if (cancel && cancel->load()) return S;
        std::vector<V3> Xs, rs;
        std::vector<u32> ids;
        for (u32 t = 0; t < M; ++t) if (S.hasX[t] && has(t, fr)) { Xs.push_back(S.X[t]); rs.push_back(ray(t, fr)); ids.push_back(t); }
        if (Xs.size() < 12) break;   // perdeu a cena: para aqui
        Pose P = S.poses[fr].valid && fr == k ? S.poses[fr] : S.poses[fr - 1];
        std::vector<u8> inl;
        if (!refine_pose(P, Xs, rs, f, &inl)) break;
        // Segunda passada só com os inliers (o ponto errado não puxa a pose).
        std::vector<V3> X2, r2;
        for (usize i = 0; i < inl.size(); ++i) if (inl[i]) { X2.push_back(Xs[i]); r2.push_back(rs[i]); }
        if (X2.size() < 10) break;
        refine_pose(P, X2, r2, f);
        P.valid = true;
        S.poses[fr] = P;
        triangulate_new(fr);
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
            if (Xs.size() >= 8) refine_pose(S.poses[fr], Xs, rs, f);
        }
        // Referencial: a base a→k volta a valer 1 (o refinamento pode encolher tudo).
        const f64 base = norm(S.poses[k].t);
        if (base > 1e-9 && std::fabs(base - 1.0) > 1e-6) {
            for (auto& P : S.poses) if (P.valid) P.t = P.t * (1.0 / base);
            for (u32 t = 0; t < M; ++t) if (S.hasX[t]) S.X[t] = S.X[t] * (1.0 / base);
        }
    }

    S.fixedFrame = a;
    evaluate(S, T, f);
    return S;
}

/// Tripé: rotação entre quadros por Kabsch (3 raios + RANSAC), acumulada.
SolveState solve_rotation(const Tracks2D& T, f64 f) {
    SolveState S;
    S.rotationOnly = true;
    const u32 N = T.frames, M = static_cast<u32>(T.pos.size());
    const f64 cx = static_cast<f64>(T.width) * 0.5, cy = static_cast<f64>(T.height) * 0.5;
    auto dir = [&](u32 track, u32 frame) {
        const Vec2 p = T.pos[track][frame];
        return normalized(V3{(static_cast<f64>(p.x) - cx) / f, (static_cast<f64>(p.y) - cy) / f, 1.0});
    };
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
    for (u32 fr = 1; fr < N; ++fr) {
        std::vector<V3> A, B;
        for (u32 t = 0; t < M; ++t) if (has(t, fr - 1) && has(t, fr)) { A.push_back(dir(t, fr - 1)); B.push_back(dir(t, fr)); }
        if (A.size() < 8) break;
        std::uniform_int_distribution<u32> pick(0, static_cast<u32>(A.size() - 1));
        std::vector<u32> best;
        M3 bestR;
        const f64 thr = 1.5 / f;
        for (int it = 0; it < 150; ++it) {
            std::vector<u32> smp;
            while (smp.size() < 3) { const u32 r = pick(rng); if (std::find(smp.begin(), smp.end(), r) == smp.end()) smp.push_back(r); }
            const M3 R = kabsch(A, B, smp);
            std::vector<u32> inl;
            for (u32 i = 0; i < A.size(); ++i) if (norm(mul(R, A[i]) - B[i]) < thr) inl.push_back(i);
            if (inl.size() > best.size()) { best = std::move(inl); bestR = R; }
        }
        if (best.size() < 8) break;
        const M3 R = kabsch(A, B, best);
        for (u32 i : best) {
            const V3 p = mul(R, A[i]);
            const f64 e = std::hypot(p.x / p.z - B[i].x / B[i].z, p.y / p.z - B[i].y / B[i].z) * f;
            sum += e * e;
            ++n;
        }
        S.poses[fr] = Pose{mul(R, S.poses[fr - 1].R), V3{}, true};
        S.inliers = std::max<u32>(S.inliers, static_cast<u32>(best.size()));
    }
    S.solved = static_cast<u32>(std::count_if(S.poses.begin(), S.poses.end(), [](const Pose& p) { return p.valid; }));
    S.rms = n ? std::sqrt(sum / static_cast<f64>(n)) : 1e30;
    return S;
}

} // namespace

CameraSolution solve_camera(const Tracks2D& tracks, const SolveOptions& opt, const std::atomic<bool>* cancel,
                            std::atomic<f32>* progress) {
    CameraSolution out;
    const u32 N = tracks.frames;
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
        return s.rms * (1.0 + 4.0 * missing);
    };
    auto run = [&](f64 fovDeg, bool rot) { return rot ? solve_rotation(tracks, focalOf(fovDeg)) : solve_with_focal(tracks, focalOf(fovDeg), opt.mode, cancel); };

    const bool known = opt.knownFovDeg > 0.0f;
    const f64 lo = known ? opt.knownFovDeg : std::max(5.0f, opt.fovMinDeg);
    const f64 hi = known ? opt.knownFovDeg : std::min(150.0f, opt.fovMaxDeg);
    // Tripé? A FOV do meio decide o modo.
    SolveState probe = run(0.5 * (lo + hi), false);
    const bool rot = probe.rotationOnly;
    if (progress) progress->store(0.15f);
    f64 bestFov = 0.5 * (lo + hi);
    SolveState best = rot ? run(bestFov, true) : std::move(probe);
    if (!known) {
        // Busca grossa + seção áurea em volta do melhor.
        const int coarse = opt.mode == TrackMode::Fast ? 5 : 7;
        f64 bestScore = score(best);
        for (int i = 0; i < coarse; ++i) {
            if (cancel && cancel->load()) break;
            const f64 fov = lo + (hi - lo) * (static_cast<f64>(i) + 0.5) / static_cast<f64>(coarse);
            SolveState s = run(fov, rot);
            const f64 sc = score(s);
            if (sc < bestScore) { bestScore = sc; best = std::move(s); bestFov = fov; }
            if (progress) progress->store(0.15f + 0.45f * static_cast<f32>(i + 1) / static_cast<f32>(coarse));
        }
    }
    if (!rot && best.solved >= 3 && !(cancel && cancel->load())) {
        // Bundle adjustment com a focal livre (ou presa, se conhecida).
        f64 f = focalOf(bestFov);
        const u32 keysMax = opt.mode == TrackMode::Fast ? 30 : (opt.mode == TrackMode::High ? 90 : 60);
        const int iters = opt.mode == TrackMode::Fast ? 12 : (opt.mode == TrackMode::High ? 40 : 25);
        for (int pass = 0; pass < 2; ++pass) {
            bundle_adjust(best, tracks, f, !known, keysMax, iters, cancel);
            evaluate(best, tracks, f);
        }
        bestFov = 2.0 * std::atan(0.5 * h / f) * 180.0 / kPi;
        if (progress) progress->store(1.0f);
    }
    if (cancel && cancel->load()) { out.failure = "cancelado"; return out; }
    if (best.solved < std::max<u32>(10, N / 2) || best.rms > 3.0) {
        out.failure = best.solved < 2 ? "nao deu para resolver a camera (pouca paralaxe ou cena sem textura)"
                                      : "solve fraco: erro alto ou poucos quadros resolvidos";
        out.rmsError = static_cast<f32>(std::min(best.rms, 999.0));
        out.framesSolved = best.solved;
        return out;
    }
    out.ok = true;
    out.rotationOnly = rot;
    out.fovY = static_cast<f32>(bestFov * kPi / 180.0);
    out.poses.resize(N);
    for (u32 i = 0; i < N; ++i) {
        const Pose& p = best.poses[i];
        out.poses[i].valid = p.valid;
        for (int k = 0; k < 9; ++k) out.poses[i].R[k] = p.R.m[k];
        out.poses[i].t[0] = p.t.x; out.poses[i].t[1] = p.t.y; out.poses[i].t[2] = p.t.z;
    }
    out.trackSolved.assign(tracks.pos.size(), rot ? 1 : 0);
    for (usize t = 0; t < best.X.size(); ++t) {
        if (!best.hasX[t]) continue;
        out.points.push_back(Vec3{static_cast<f32>(best.X[t].x), static_cast<f32>(best.X[t].y), static_cast<f32>(best.X[t].z)});
        out.trackSolved[t] = 1;
    }
    out.inliers = rot ? best.inliers : static_cast<u32>(out.points.size());
    out.framesSolved = best.solved;
    out.rmsError = static_cast<f32>(best.rms);
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
    if (pts.size() < 10) return false;
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
    Vec3 sum{0, 0, 0};
    usize cnt = 0;
    for (const Vec3& p : pts) if (std::fabs(bn.dot(p) - bd) < tolerance) { sum = sum + p; ++cnt; }
    centroid = sum * (1.0f / static_cast<f32>(cnt));
    normal = bn;
    return true;
}

} // namespace aurea::tracking
