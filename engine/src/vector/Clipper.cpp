// =============================================================================
//  Aurea / vector / Clipper.cpp
//
//  Recorte de polígonos pelo ARRANJO PLANAR — um algoritmo só para regra de
//  preenchimento, união do contorno e as quatro booleanas:
//
//   1. Vértices numa grade inteira (1/256 px). Orientação por produto
//      vetorial EXATO em i64: cruzamento próprio, toque em T e colinear são
//      decididos sem epsilon.
//   2. Arredondamento na grade (snap rounding): pontas e cruzamentos
//      (varredura por x) viram pixels quentes; toda aresta que atravessa um
//      pixel quente é dividida no centro dele — as arestas passam a
//      compartilhar os vértices e nenhum cruzamento novo nasce do
//      arredondamento. Repete até nenhuma divisão sobrar.
//   3. Arestas iguais se fundem (contagem assinada por conjunto).
//   4. Enrolamento de cada conjunto logo à esquerda de cada aresta, por raio
//      horizontal (faixas em y aceleram); o da direita = esquerda − contagem.
//   5. Fica a aresta cujo "dentro" (a operação sobre os enrolamentos) muda
//      de um lado para o outro, orientada com o dentro à esquerda.
//   6. As arestas viram anéis: em cada vértice, a saída que mais vira à
//      esquerda (separa figuras que só se tocam num ponto).
//
//  Resultado: anéis sem auto-interseção, preenchido à esquerda — externos com
//  área positiva, furos com área negativa.
// =============================================================================
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <limits>

namespace aurea::vector {
namespace {

constexpr f64 kGrid = 256.0;

struct IP {
    i64 x = 0, y = 0;
    friend bool operator==(IP a, IP b) noexcept { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(IP a, IP b) noexcept { return !(a == b); }
    friend bool operator<(IP a, IP b) noexcept { return a.x < b.x || (a.x == b.x && a.y < b.y); }
};
struct IPHash {
    usize operator()(IP p) const noexcept {
        return static_cast<usize>(static_cast<u64>(p.x) * 0x9E3779B97F4A7C15ull ^ (static_cast<u64>(p.y) + 0x632BE59BD9B4E019ull));
    }
};

inline i64 cross3(IP o, IP a, IP b) noexcept { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

inline IP quant(Vec2 p) noexcept {
    return IP{static_cast<i64>(std::llround(static_cast<f64>(p.x) * kGrid)), static_cast<i64>(std::llround(static_cast<f64>(p.y) * kGrid))};
}

struct Edge {
    IP a, b;
    u32 set = 0;
};

/// O segmento ab passa pelo pixel quente de centro c (quadrado de lado 1 da
/// grade)? Coordenadas dobradas: os cantos ficam inteiros, conta exata.
inline bool crosses_hot(IP a, IP b, IP c) noexcept {
    if (std::max(a.x, b.x) * 2 < c.x * 2 - 1 || std::min(a.x, b.x) * 2 > c.x * 2 + 1) return false;
    if (std::max(a.y, b.y) * 2 < c.y * 2 - 1 || std::min(a.y, b.y) * 2 > c.y * 2 + 1) return false;
    const IP A{a.x * 2, a.y * 2}, B{b.x * 2, b.y * 2};
    int pos = 0, neg = 0;
    for (int k = 0; k < 4; ++k) {
        const IP q{c.x * 2 + ((k & 1) ? 1 : -1), c.y * 2 + ((k & 2) ? 1 : -1)};
        const i64 s = cross3(A, B, q);
        if (s > 0) ++pos; else if (s < 0) ++neg; else { ++pos; ++neg; }
    }
    return pos > 0 && neg > 0;
}

/// Uma passada de ARREDONDAMENTO NA GRADE (snap rounding, Hobby): pixels
/// quentes = pontas + cruzamentos arredondados; toda aresta que atravessa um
/// pixel quente passa pelo centro dele. Arredondar só o ponto de cruzamento
/// (sem os pixels quentes) cria cruzamento novo com arestas quase paralelas a
/// cada passada — o contorno de uma curva nunca convergia. Devolve quantas
/// divisões houve.
usize split_pass(std::vector<Edge>& edges) {
    const usize n = edges.size();
    std::vector<IP> hot;
    hot.reserve(n * 2);
    for (const Edge& e : edges) { hot.push_back(e.a); hot.push_back(e.b); }
    std::vector<u32> order(n);
    for (u32 i = 0; i < n; ++i) order[i] = i;
    auto minx = [&](u32 i) { return std::min(edges[i].a.x, edges[i].b.x); };
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return minx(a) < minx(b); });
    for (usize oi = 0; oi < n; ++oi) {
        const u32 i = order[oi];
        const Edge& E = edges[i];
        const i64 maxx = std::max(E.a.x, E.b.x);
        const i64 ey0 = std::min(E.a.y, E.b.y), ey1 = std::max(E.a.y, E.b.y);
        for (usize oj = oi + 1; oj < n; ++oj) {
            const u32 j = order[oj];
            const Edge& F = edges[j];
            if (minx(j) > maxx) break;
            if (std::max(F.a.y, F.b.y) < ey0 || std::min(F.a.y, F.b.y) > ey1) continue;
            const i64 d1 = cross3(F.a, F.b, E.a), d2 = cross3(F.a, F.b, E.b);
            const i64 d3 = cross3(E.a, E.b, F.a), d4 = cross3(E.a, E.b, F.b);
            if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
                const f64 t = static_cast<f64>(d1) / static_cast<f64>(d1 - d2);
                hot.push_back(IP{static_cast<i64>(std::llround(static_cast<f64>(E.a.x) + t * static_cast<f64>(E.b.x - E.a.x))),
                                 static_cast<i64>(std::llround(static_cast<f64>(E.a.y) + t * static_cast<f64>(E.b.y - E.a.y)))});
            }
        }
    }
    std::sort(hot.begin(), hot.end());
    hot.erase(std::unique(hot.begin(), hot.end()), hot.end());
    std::vector<std::vector<IP>> splits(n);
    usize count = 0;
    for (usize i = 0; i < n; ++i) {
        const Edge& E = edges[i];
        const i64 x0 = std::min(E.a.x, E.b.x) - 1, x1 = std::max(E.a.x, E.b.x) + 1;
        auto it = std::lower_bound(hot.begin(), hot.end(), IP{x0, std::numeric_limits<i64>::min()});
        for (; it != hot.end() && it->x <= x1; ++it) {
            const IP c = *it;
            if (c == E.a || c == E.b) continue;
            if (!crosses_hot(E.a, E.b, c)) continue;
            splits[i].push_back(c);
            ++count;
        }
    }
    if (count == 0) return 0;
    std::vector<Edge> out;
    out.reserve(n + count);
    for (usize i = 0; i < n; ++i) {
        const Edge& E = edges[i];
        if (splits[i].empty()) { out.push_back(E); continue; }
        std::vector<IP>& s = splits[i];
        const f64 dx = static_cast<f64>(E.b.x - E.a.x), dy = static_cast<f64>(E.b.y - E.a.y);
        std::sort(s.begin(), s.end(), [&](IP p, IP q) {
            return static_cast<f64>(p.x - E.a.x) * dx + static_cast<f64>(p.y - E.a.y) * dy
                 < static_cast<f64>(q.x - E.a.x) * dx + static_cast<f64>(q.y - E.a.y) * dy;
        });
        IP prev = E.a;
        for (IP p : s) {
            if (p == prev) continue;
            out.push_back(Edge{prev, p, E.set});
            prev = p;
        }
        if (prev != E.b) out.push_back(Edge{prev, E.b, E.set});
    }
    edges.swap(out);
    return count;
}

struct UEdge {
    IP lo, hi;   ///< lo < hi
};

/// Recorte genérico: `inside(w)` decide pelo vetor de enrolamentos (um por conjunto).
template <class Inside>
void arrange(std::vector<Edge>& edges, u32 sets, Inside inside, std::vector<Contour>& out) {
    out.clear();
    if (edges.empty()) return;
    for (int pass = 0; pass < 8; ++pass) {
        if (split_pass(edges) == 0) break;
    }
    // Fusão das arestas iguais (contagem assinada na direção lo → hi).
    struct KeyHash {
        usize operator()(const std::pair<IP, IP>& k) const noexcept { return IPHash{}(k.first) * 31u ^ IPHash{}(k.second); }
    };
    std::unordered_map<std::pair<IP, IP>, u32, KeyHash> index;
    index.reserve(edges.size() * 2);
    std::vector<UEdge> ue;
    std::vector<i32> cnt;   // ue.size() × sets
    for (const Edge& e : edges) {
        if (e.a == e.b) continue;
        const bool fwd = e.a < e.b;
        const IP lo = fwd ? e.a : e.b, hi = fwd ? e.b : e.a;
        auto [it, fresh] = index.try_emplace(std::make_pair(lo, hi), static_cast<u32>(ue.size()));
        if (fresh) {
            ue.push_back(UEdge{lo, hi});
            cnt.resize(cnt.size() + sets, 0);
        }
        cnt[static_cast<usize>(it->second) * sets + e.set] += fwd ? 1 : -1;
    }
    const usize m = ue.size();
    // Arestas com contagem zero em todos os conjuntos não contam (ida e volta).
    std::vector<u8> live(m, 0);
    for (usize i = 0; i < m; ++i)
        for (u32 s = 0; s < sets; ++s) if (cnt[i * sets + s] != 0) { live[i] = 1; break; }

    // Faixas em y para o raio horizontal.
    i64 ymin = ue[0].lo.y, ymax = ue[0].lo.y;
    for (const UEdge& e : ue) { ymin = std::min({ymin, e.lo.y, e.hi.y}); ymax = std::max({ymax, e.lo.y, e.hi.y}); }
    const u32 bands = std::clamp<u32>(static_cast<u32>(std::sqrt(static_cast<f64>(m))) + 1u, 1u, 4096u);
    const f64 bandH = std::max(1.0, static_cast<f64>(ymax - ymin + 1) / static_cast<f64>(bands));
    std::vector<std::vector<u32>> band(bands);
    for (u32 i = 0; i < m; ++i) {
        if (!live[i]) continue;
        const i64 y0 = std::min(ue[i].lo.y, ue[i].hi.y), y1 = std::max(ue[i].lo.y, ue[i].hi.y);
        if (y0 == y1) continue;   // horizontal não cruza raio horizontal
        const u32 b0 = std::min<u32>(bands - 1, static_cast<u32>(static_cast<f64>(y0 - ymin) / bandH));
        const u32 b1 = std::min<u32>(bands - 1, static_cast<u32>(static_cast<f64>(y1 - ymin) / bandH));
        for (u32 b = b0; b <= b1; ++b) band[b].push_back(i);
    }
    std::vector<i32> wl(sets), wr(sets);
    auto winding_at = [&](f64 px, f64 py, std::vector<i32>& w) {
        std::fill(w.begin(), w.end(), 0);
        const f64 bf = (py - static_cast<f64>(ymin)) / bandH;
        if (bf < 0.0 || bf >= static_cast<f64>(bands)) return;
        for (u32 i : band[static_cast<u32>(bf)]) {
            const IP u = ue[i].lo, v = ue[i].hi;
            const f64 uy = static_cast<f64>(u.y), vy = static_cast<f64>(v.y);
            const f64 c = (static_cast<f64>(v.x - u.x)) * (py - uy) - (vy - uy) * (px - static_cast<f64>(u.x));
            i32 dir = 0;
            if (uy <= py && py < vy && c > 0.0) dir = 1;
            else if (vy <= py && py < uy && c < 0.0) dir = -1;
            if (dir == 0) continue;
            for (u32 s = 0; s < sets; ++s) w[s] += dir * cnt[static_cast<usize>(i) * sets + s];
        }
    };
    struct DEdge { IP a, b; };
    std::vector<DEdge> kept;
    for (u32 i = 0; i < m; ++i) {
        if (!live[i]) continue;
        const IP lo = ue[i].lo, hi = ue[i].hi;
        const f64 dx = static_cast<f64>(hi.x - lo.x), dy = static_cast<f64>(hi.y - lo.y);
        const f64 len = std::sqrt(dx * dx + dy * dy);
        const f64 eps = 1e-3;
        const f64 px = (static_cast<f64>(lo.x) + static_cast<f64>(hi.x)) * 0.5 - dy / len * eps;
        const f64 py = (static_cast<f64>(lo.y) + static_cast<f64>(hi.y)) * 0.5 + dx / len * eps;
        winding_at(px, py, wl);
        for (u32 s = 0; s < sets; ++s) wr[s] = wl[s] - cnt[static_cast<usize>(i) * sets + s];
        const bool il = inside(wl), ir = inside(wr);
        if (il == ir) continue;
        kept.push_back(il ? DEdge{lo, hi} : DEdge{hi, lo});
    }
    // Encadeamento em anéis.
    std::unordered_map<IP, std::vector<u32>, IPHash> outgoing;
    outgoing.reserve(kept.size() * 2);
    for (u32 i = 0; i < kept.size(); ++i) outgoing[kept[i].a].push_back(i);
    std::vector<u8> used(kept.size(), 0);
    std::vector<IP> ring;
    for (u32 s0 = 0; s0 < kept.size(); ++s0) {
        if (used[s0]) continue;
        ring.clear();
        u32 e = s0;
        used[e] = 1;
        const IP start = kept[e].a;
        ring.push_back(start);
        bool closed = false;
        for (usize guard = 0; guard < kept.size() + 2; ++guard) {
            const IP a = kept[e].a, b = kept[e].b;
            if (b == start) { closed = true; break; }
            ring.push_back(b);
            auto it = outgoing.find(b);
            if (it == outgoing.end()) break;
            const f64 ix = static_cast<f64>(b.x - a.x), iy = static_cast<f64>(b.y - a.y);
            u32 best = kInvalidIndex;
            f64 bestAng = -10.0;
            for (u32 cand : it->second) {
                if (used[cand]) continue;
                const f64 ox = static_cast<f64>(kept[cand].b.x - b.x), oy = static_cast<f64>(kept[cand].b.y - b.y);
                const f64 cr = ix * oy - iy * ox, dt = ix * ox + iy * oy;
                f64 ang = std::atan2(cr, dt);
                if (cr == 0.0 && dt < 0.0) ang = -4.0;   // volta exata: último recurso
                if (ang > bestAng) { bestAng = ang; best = cand; }
            }
            if (best == kInvalidIndex) break;
            used[best] = 1;
            e = best;
        }
        if (!closed || ring.size() < 3) continue;
        // Tira pontos colineares (o arranjo divide arestas retas em pedaços).
        std::vector<IP> clean;
        clean.reserve(ring.size());
        const usize rn = ring.size();
        for (usize k = 0; k < rn; ++k) {
            const IP p = ring[(k + rn - 1) % rn], c = ring[k], q = ring[(k + 1) % rn];
            const i64 cr = cross3(p, c, q);
            const i64 dt = (c.x - p.x) * (q.x - c.x) + (c.y - p.y) * (q.y - c.y);
            if (cr == 0 && dt >= 0) continue;
            clean.push_back(c);
        }
        if (clean.size() < 3) continue;
        Contour C;
        C.closed = true;
        C.pts.reserve(clean.size());
        for (IP p : clean) C.pts.push_back(Vec2{static_cast<f32>(static_cast<f64>(p.x) / kGrid), static_cast<f32>(static_cast<f64>(p.y) / kGrid)});
        if (std::fabs(signed_area(C.pts)) < 1e-6) continue;
        out.push_back(std::move(C));
    }
}

void append_edges(const std::vector<Contour>& cs, u32 set, std::vector<Edge>& edges) {
    for (const Contour& c : cs) {
        const usize n = c.pts.size();
        if (n < 2) continue;
        IP prev = quant(c.pts[n - 1]);   // aberto também fecha (preenchimento implícito)
        for (usize i = 0; i < n; ++i) {
            const IP p = quant(c.pts[i]);
            if (p != prev) edges.push_back(Edge{prev, p, set});
            prev = p;
        }
    }
}

inline bool rule_in(i32 w, FillRule r) noexcept { return r == FillRule::EvenOdd ? (w & 1) != 0 : w != 0; }

} // namespace

f64 signed_area(const std::vector<Vec2>& ring) noexcept {
    const usize n = ring.size();
    if (n < 3) return 0.0;
    f64 s = 0.0;
    for (usize i = 0, j = n - 1; i < n; j = i++) {
        s += static_cast<f64>(ring[j].x) * static_cast<f64>(ring[i].y) - static_cast<f64>(ring[i].x) * static_cast<f64>(ring[j].y);
    }
    return s * 0.5;
}

f64 area_of(const std::vector<Contour>& rings) noexcept {
    f64 s = 0.0;
    for (const Contour& c : rings) s += signed_area(c.pts);
    return s;
}

void resolve_fill(const std::vector<Contour>& in, FillRule rule, std::vector<Contour>& out) {
    std::vector<Edge> edges;
    append_edges(in, 0, edges);
    arrange(edges, 1, [rule](const std::vector<i32>& w) { return rule_in(w[0], rule); }, out);
}

void boolean_op(const std::vector<std::vector<Contour>>& sets, FillRule rule, BoolOp op, std::vector<Contour>& out) {
    out.clear();
    const u32 k = static_cast<u32>(sets.size());
    if (k == 0) return;
    std::vector<Edge> edges;
    for (u32 s = 0; s < k; ++s) append_edges(sets[s], s, edges);
    arrange(edges, k, [&](const std::vector<i32>& w) {
        switch (op) {
            case BoolOp::Union: {
                for (u32 s = 0; s < k; ++s) if (rule_in(w[s], rule)) return true;
                return false;
            }
            case BoolOp::Subtract: {
                if (!rule_in(w[0], rule)) return false;
                for (u32 s = 1; s < k; ++s) if (rule_in(w[s], rule)) return false;
                return true;
            }
            case BoolOp::Intersect: {
                for (u32 s = 0; s < k; ++s) if (!rule_in(w[s], rule)) return false;
                return true;
            }
            case BoolOp::Exclude: {
                u32 c = 0;
                for (u32 s = 0; s < k; ++s) c += rule_in(w[s], rule) ? 1u : 0u;
                return (c & 1u) != 0;
            }
        }
        return false;
    }, out);
}

} // namespace aurea::vector
