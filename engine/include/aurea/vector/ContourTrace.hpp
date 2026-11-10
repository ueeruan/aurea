// =============================================================================
//  Aurea / vector / ContourTrace.hpp
//
//  Do campo para o contorno: marching squares do nível zero, ajuste em bézier
//  preservando cantos e, em cima dos dois, o contorno do canal alfa de uma
//  imagem (Imagem 3D).
// =============================================================================
#pragma once

#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace aurea::vector {

/// Uma volta fechada do nível zero (px da caixa, centro na origem).
using Loop = std::vector<Vec2>;

inline f32 loop_area(const Loop& p) noexcept {
    f64 a = 0.0;
    for (usize i = 0, n = p.size(); i < n; ++i) {
        const Vec2 u = p[i], v = p[(i + 1) % n];
        a += static_cast<f64>(u.x) * v.y - static_cast<f64>(v.x) * u.y;
    }
    return static_cast<f32>(a * 0.5);
}

/// Marching squares do nível zero de `sd` na grade (cs px por célula) que
/// cobre [−ext, ext]. Devolve as voltas fechadas (sem as de área desprezível).
template <typename Sd>
std::vector<Loop> trace_zero(Sd&& sd, Vec2 ext, f32 cs) {
    const u32 nx = static_cast<u32>(std::ceil(2.0f * ext.x / cs)) + 1;
    const u32 ny = static_cast<u32>(std::ceil(2.0f * ext.y / cs)) + 1;
    const f32 x0 = -0.5f * static_cast<f32>(nx) * cs, y0 = -0.5f * static_cast<f32>(ny) * cs;
    const u32 sx = nx + 1;   // nós por linha
    std::vector<f32> v(static_cast<usize>(sx) * (ny + 1));
    for (u32 j = 0; j <= ny; ++j)
        for (u32 i = 0; i <= nx; ++i) {
            f32 d = sd(Vec2{x0 + static_cast<f32>(i) * cs, y0 + static_cast<f32>(j) * cs});
            if (!std::isfinite(d)) d = 1.0f;
            if (d == 0.0f) d = 1e-6f;   // nunca exatamente no nível: fora
            v[static_cast<usize>(j) * sx + i] = d;
        }
    auto at = [&](u32 i, u32 j) { return v[static_cast<usize>(j) * sx + i]; };
    auto node = [&](u32 i, u32 j) { return Vec2{x0 + static_cast<f32>(i) * cs, y0 + static_cast<f32>(j) * cs}; };
    // Ids das arestas da grade: horizontais (i, j)→(i+1, j) e verticais (i, j)→(i, j+1).
    const u64 vBase = static_cast<u64>(sx) * (ny + 1);
    auto hId = [&](u32 i, u32 j) { return static_cast<u64>(j) * sx + i; };
    auto vId = [&](u32 i, u32 j) { return vBase + static_cast<u64>(j) * sx + i; };
    std::unordered_map<u64, Vec2> point;
    std::unordered_map<u64, std::vector<u64>> link;
    auto cross = [&](u64 id, Vec2 pa, f32 da, Vec2 pb, f32 db) {
        if (point.find(id) != point.end()) return;
        const f32 t = std::clamp(da / (da - db), 0.0f, 1.0f);
        point.emplace(id, Vec2{pa.x + (pb.x - pa.x) * t, pa.y + (pb.y - pa.y) * t});
    };
    auto segment = [&](u64 a, u64 b) {
        link[a].push_back(b);
        link[b].push_back(a);
    };
    for (u32 j = 0; j < ny; ++j)
        for (u32 i = 0; i < nx; ++i) {
            const f32 a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            const u32 k = (a < 0.0f ? 1u : 0u) | (b < 0.0f ? 2u : 0u) | (c < 0.0f ? 4u : 0u) | (d < 0.0f ? 8u : 0u);
            if (k == 0u || k == 15u) continue;
            const u64 T = hId(i, j), B = hId(i, j + 1), L = vId(i, j), R = vId(i + 1, j);
            if ((a < 0.0f) != (b < 0.0f)) cross(T, node(i, j), a, node(i + 1, j), b);
            if ((d < 0.0f) != (c < 0.0f)) cross(B, node(i, j + 1), d, node(i + 1, j + 1), c);
            if ((a < 0.0f) != (d < 0.0f)) cross(L, node(i, j), a, node(i, j + 1), d);
            if ((b < 0.0f) != (c < 0.0f)) cross(R, node(i + 1, j), b, node(i + 1, j + 1), c);
            const bool centre = (a + b + c + d) * 0.25f < 0.0f;
            switch (k) {
                case 1: case 14: segment(T, L); break;
                case 2: case 13: segment(T, R); break;
                case 3: case 12: segment(L, R); break;
                case 4: case 11: segment(R, B); break;
                case 6: case 9: segment(T, B); break;
                case 7: case 8: segment(L, B); break;
                case 5:   // a e c dentro
                    if (centre) { segment(T, R); segment(B, L); } else { segment(T, L); segment(R, B); }
                    break;
                case 10:  // b e d dentro
                    if (centre) { segment(T, L); segment(R, B); } else { segment(T, R); segment(B, L); }
                    break;
                default: break;
            }
        }
    // Cada ponto de cruzamento tem exatamente dois vizinhos (a borda da grade
    // fica fora da forma): andar de vizinho em vizinho fecha a volta.
    std::vector<Loop> loops;
    std::unordered_map<u64, bool> seen;
    seen.reserve(link.size());
    constexpr u64 kNone = ~0ull;
    for (const auto& entry : link) {
        const u64 start = entry.first;
        if (seen.count(start) != 0 || entry.second.size() != 2) continue;
        Loop loop;
        u64 prev = kNone, cur = start;
        bool closed = false;
        while (loop.size() <= link.size()) {
            seen[cur] = true;
            loop.push_back(point.at(cur));
            const std::vector<u64>& nb = link.at(cur);
            if (nb.size() != 2) break;
            const u64 next = nb[0] != prev ? nb[0] : nb[1];
            if (next == start) { closed = true; break; }
            if (seen.count(next) != 0) break;
            prev = cur;
            cur = next;
        }
        if (closed && loop.size() >= 3 && std::fabs(loop_area(loop)) > cs * cs * 2.0f) loops.push_back(std::move(loop));
    }
    return loops;
}

/// Limites do contorno de uma imagem: acima deles, sobra ruído que só encarece a malha.
struct AlphaTraceOptions {
    f32 threshold = 0.5f;     ///< alfa da silhueta (0,05..0,95)
    u32 maxPoints = 2500;     ///< total nos contornos; a tolerância sobe até caber
    u32 maxRings = 96;        ///< ilhas + furos; ficam os maiores
    u32 stride = 4;           ///< bytes por pixel do buffer
    u32 alphaOffset = 3;      ///< onde está o alfa no pixel
};

/// Contornos fechados do alfa de `pixels` (w×h), em pixels da imagem (origem no
/// canto superior esquerdo). Imagem opaca devolve a própria moldura. Falso se
/// não houver nada acima do limiar.
[[nodiscard]] bool trace_alpha(const u8* pixels, u32 w, u32 h, const AlphaTraceOptions& options, std::vector<Contour>& out);

/// Bézier fechada de uma volta: corta nos cantos vivos e ajusta cada trecho.
[[nodiscard]] BezierPath fit_loop(const Loop& raw, f32 win, f32 error);

} // namespace aurea::vector
