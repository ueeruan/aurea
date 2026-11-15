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
//      (achados por uma grade de células) viram pixels quentes; toda aresta
//      que atravessa um pixel quente é dividida no centro dele — as arestas
//      passam a compartilhar os vértices e nenhum cruzamento novo nasce do
//      arredondamento. Repete até nenhuma divisão sobrar (da segunda passada
//      em diante, só em volta das arestas que mudaram).
//   3. Arestas iguais se fundem (contagem assinada por conjunto).
//   4. Enrolamento de cada conjunto logo à esquerda de cada aresta: um raio
//      horizontal por fileira da grade e, de cada aresta até a referência da
//      fileira, só as arestas da célula; o da direita = esquerda − contagem.
//   5. Fica a aresta cujo "dentro" (a operação sobre os enrolamentos) muda
//      de um lado para o outro, orientada com o dentro à esquerda.
//   6. As arestas viram anéis: em cada vértice, a saída que mais vira à
//      esquerda (separa figuras que só se tocam num ponto).
//
//  Resultado: anéis sem auto-interseção, preenchido à esquerda — externos com
//  área positiva, furos com área negativa.
//
//  ORÇAMENTO: o arranjo exato tem um teto de CUSTO estimado (arestas e
//  cruzamentos próprios, cada um contado uma vez, com os coeficientes medidos
//  no host: kUsPerEdge, kUsPerCrossing, kExactBudgetUs). Uma entrada que passa
//  disso — o contorno largo de uma linha de milhares de pontos toda ondulada
//  chega a centenas de milhares de cruzamentos — sai pela amostragem
//  (raster_arrange): cobertura numa grade no passo da tolerância de quem
//  chama, contorno de nível e um arranjo exato pequeno no fim. Tempo limitado;
//  nenhuma geometria vetorial prende um quadro. O motor avisa uma vez no log e
//  conta os dois caminhos (clip_stats).
// =============================================================================
#include "aurea/vector/Vector.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
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

/// Grade uniforme de vizinhança (células de `cs` unidades a partir de `o`):
/// a aresta cai em toda célula por onde passa (cobertura conservadora, coluna
/// por coluna, com folga de uma unidade) e o pixel quente nas células em volta
/// do quadrado dele. Só se testa o que divide uma célula: antes, a busca de
/// cruzamentos e a de pixels quentes varriam por x só, e as arestas de um lado
/// vertical (todas na mesma coluna) se testavam todas contra todas.
struct CellGrid {
    i64 ox = 0, oy = 0, cs = 1;
    /// Fileira na parte alta: a lista ordenada fica fileira por fileira.
    [[nodiscard]] u64 key(i64 cx, i64 cy) const noexcept {
        return (static_cast<u64>(static_cast<u32>(cy)) << 32) | static_cast<u64>(static_cast<u32>(cx));
    }
    [[nodiscard]] static i64 row_of(u64 k) noexcept { return static_cast<i64>(k >> 32); }
    [[nodiscard]] i64 col(i64 x) const noexcept { return (x - ox) / cs; }
    [[nodiscard]] i64 row(i64 y) const noexcept { return (y - oy) / cs; }
    template <class F>
    void cover(IP a, IP b, F&& f) const {
        const i64 x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
        const i64 c0 = col(x0 - 1), c1 = col(x1 + 1);
        if (a.x == b.x || c0 == c1) {
            const i64 r0 = row(std::min(a.y, b.y) - 1), r1 = row(std::max(a.y, b.y) + 1);
            for (i64 c = c0; c <= c1; ++c)
                for (i64 r = r0; r <= r1; ++r) f(key(c, r));
            return;
        }
        const f64 slope = static_cast<f64>(b.y - a.y) / static_cast<f64>(b.x - a.x);
        for (i64 c = c0; c <= c1; ++c) {
            const i64 xa = std::clamp(ox + c * cs, x0, x1), xb = std::clamp(ox + (c + 1) * cs, x0, x1);
            const f64 ya = static_cast<f64>(a.y) + static_cast<f64>(xa - a.x) * slope;
            const f64 yb = static_cast<f64>(a.y) + static_cast<f64>(xb - a.x) * slope;
            const i64 r0 = row(static_cast<i64>(std::floor(std::min(ya, yb))) - 1);
            const i64 r1 = row(static_cast<i64>(std::ceil(std::max(ya, yb))) + 1);
            for (i64 r = r0; r <= r1; ++r) f(key(c, r));
        }
    }
};

/// Célula do tamanho de duas arestas típicas (mediana, que não se deixa levar
/// por meia dúzia de arestas longas), grande o bastante para a cobertura
/// inteira ficar em ~12 células por aresta.
CellGrid make_grid(const std::vector<Edge>& edges) {
    CellGrid G;
    const usize n = edges.size();
    i64 minx = edges[0].a.x, miny = edges[0].a.y, maxx = minx, maxy = miny;
    std::vector<i64> ext(n);
    f64 sumL = 0.0;
    for (usize i = 0; i < n; ++i) {
        const Edge& e = edges[i];
        minx = std::min({minx, e.a.x, e.b.x}); maxx = std::max({maxx, e.a.x, e.b.x});
        miny = std::min({miny, e.a.y, e.b.y}); maxy = std::max({maxy, e.a.y, e.b.y});
        const i64 dx = std::llabs(e.b.x - e.a.x), dy = std::llabs(e.b.y - e.a.y);
        ext[i] = std::max(dx, dy);
        sumL += static_cast<f64>(dx + dy);
    }
    std::nth_element(ext.begin(), ext.begin() + static_cast<std::ptrdiff_t>(n / 2), ext.end());
    const f64 span = static_cast<f64>(std::max(maxx - minx, maxy - miny)) + 16.0;
    f64 cs = std::max({16.0, 2.0 * static_cast<f64>(ext[n / 2]), sumL / (12.0 * static_cast<f64>(n)), span / 1.0e9});
    G.cs = static_cast<i64>(std::ceil(cs));
    G.ox = minx - 4;
    G.oy = miny - 4;
    return G;
}

using CellItems = std::vector<std::pair<u64, u32>>;   ///< (célula, índice), ordenado por célula

/// Uma passada de ARREDONDAMENTO NA GRADE (snap rounding, Hobby): pixels
/// quentes = pontas + cruzamentos arredondados; toda aresta que atravessa um
/// pixel quente passa pelo centro dele. Arredondar só o ponto de cruzamento
/// (sem os pixels quentes) cria cruzamento novo com arestas quase paralelas a
/// cada passada — o contorno de uma curva nunca convergia. Devolve quantas
/// divisões houve, ou kOverBudget quando os cruzamentos próprios passam de
/// `budget` (a passada para no meio, sem mexer nas arestas).
///
/// `dirty` (uma marca por aresta; vazio = todas): as arestas que a passada
/// anterior dividiu. Só elas saíram do lugar (as filhas ligam centros de
/// pixel), então a passada seguinte só olha o que encosta nelas: os pares com
/// pelo menos uma aresta nova, as novas contra os vértices em volta e as
/// velhas contra os cruzamentos novos. Na volta, `dirty` marca as filhas
/// desta passada. Antes cada passada refazia tudo — e as últimas, que mexem em
/// meia dúzia de arestas, custavam o mesmo que a primeira.
constexpr usize kOverBudget = std::numeric_limits<usize>::max();
usize split_pass(std::vector<Edge>& edges, std::vector<u8>& dirty, usize budget, usize& crossings) {
    const usize n = edges.size();
    if (n == 0) return 0;
    usize nd = 0;
    for (u8 d : dirty) nd += d;
    const bool full = dirty.size() != n || nd * 4 > n;
    if (!full && nd == 0) return 0;
    const CellGrid G = make_grid(edges);
    std::vector<IP> hot;     // pixels quentes para as arestas testadas contra tudo
    std::vector<IP> fresh;   // cruzamentos próprios achados nesta passada
    bool over = false;
    // O mesmo par aparece em toda célula que as duas arestas dividem: o ponto
    // entra na lista todas as vezes (sai sem repetição adiante), mas o
    // cruzamento só CONTA na célula onde ele cai — o orçamento conta cada
    // cruzamento próprio uma vez. (E é sempre a aresta de índice menor do
    // par, ou a velha contra a nova: a conta do ponto é a mesma em toda célula.)
    auto cross_pair = [&](const Edge& E, const Edge& F, u64 cell) {
        if (std::max(F.a.x, F.b.x) < std::min(E.a.x, E.b.x) || std::min(F.a.x, F.b.x) > std::max(E.a.x, E.b.x)) return;
        if (std::max(F.a.y, F.b.y) < std::min(E.a.y, E.b.y) || std::min(F.a.y, F.b.y) > std::max(E.a.y, E.b.y)) return;
        const i64 d1 = cross3(F.a, F.b, E.a), d2 = cross3(F.a, F.b, E.b);
        if (!((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0))) return;
        const i64 d3 = cross3(E.a, E.b, F.a), d4 = cross3(E.a, E.b, F.b);
        if (!((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return;
        const f64 tt = static_cast<f64>(d1) / static_cast<f64>(d1 - d2);
        const f64 px = static_cast<f64>(E.a.x) + tt * static_cast<f64>(E.b.x - E.a.x);
        const f64 py = static_cast<f64>(E.a.y) + tt * static_cast<f64>(E.b.y - E.a.y);
        fresh.push_back(IP{static_cast<i64>(std::llround(px)), static_cast<i64>(std::llround(py))});
        if (G.key(G.col(static_cast<i64>(std::floor(px))), G.row(static_cast<i64>(std::floor(py)))) != cell) return;
        if (++crossings > budget) over = true;
    };
    // As células das arestas (todas, ou só as novas).
    CellItems cells;
    cells.reserve((full ? n : nd) * 4);
    for (u32 i = 0; i < n; ++i) {
        if (full || dirty[i]) G.cover(edges[i].a, edges[i].b, [&](u64 k) { cells.emplace_back(k, i); });
    }
    std::sort(cells.begin(), cells.end());
    auto cell_begin = [](const CellItems& list, u64 k) { return std::lower_bound(list.begin(), list.end(), std::make_pair(k, u32{0})); };
    if (full) {
        // Cruzamentos próprios entre as arestas de cada célula.
        for (usize s = 0; s < cells.size() && !over;) {
            usize t = s + 1;
            while (t < cells.size() && cells[t].first == cells[s].first) ++t;
            for (usize p = s; p < t && !over; ++p)
                for (usize q = p + 1; q < t && !over; ++q) cross_pair(edges[cells[p].second], edges[cells[q].second], cells[s].first);
            s = t;
        }
        if (over) return kOverBudget;
        hot.reserve(n * 2);
        for (const Edge& e : edges) { hot.push_back(e.a); hot.push_back(e.b); }
    } else {
        for (u32 i = 0; i < n && !over; ++i) {
            const Edge& E = edges[i];
            // Pares com as novas (novo com novo, uma vez só).
            G.cover(E.a, E.b, [&](u64 k) {
                for (auto it = cell_begin(cells, k); it != cells.end() && it->first == k && !over; ++it) {
                    const u32 j = it->second;
                    if (j == i || (dirty[i] && j < i)) continue;
                    cross_pair(E, edges[j], k);
                }
            });
            // Vértices em volta das novas: as células do quadrado do pixel.
            for (const IP v : {E.a, E.b}) {
                bool byNew = dirty[i] != 0;
                const i64 c0 = G.col(v.x - 1), c1 = G.col(v.x + 1), r0 = G.row(v.y - 1), r1 = G.row(v.y + 1);
                for (i64 cx = c0; cx <= c1 && !byNew; ++cx) {
                    for (i64 cy = r0; cy <= r1 && !byNew; ++cy) {
                        const u64 k = G.key(cx, cy);
                        const auto it = cell_begin(cells, k);
                        byNew = it != cells.end() && it->first == k;
                    }
                }
                if (byNew) hot.push_back(v);
            }
        }
        if (over) return kOverBudget;
    }
    std::sort(fresh.begin(), fresh.end());
    fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
    hot.insert(hot.end(), fresh.begin(), fresh.end());
    std::sort(hot.begin(), hot.end());
    hot.erase(std::unique(hot.begin(), hot.end()), hot.end());
    // Pixels quentes nas células em volta do quadrado deles.
    auto cells_of = [&](const std::vector<IP>& pts, CellItems& list) {
        list.reserve(pts.size() * 2);
        for (u32 h = 0; h < pts.size(); ++h) {
            const IP c = pts[h];
            const i64 c0 = G.col(c.x - 1), c1 = G.col(c.x + 1), r0 = G.row(c.y - 1), r1 = G.row(c.y + 1);
            for (i64 cx = c0; cx <= c1; ++cx)
                for (i64 cy = r0; cy <= r1; ++cy) list.emplace_back(G.key(cx, cy), h);
        }
        std::sort(list.begin(), list.end());
    };
    CellItems hotCells, freshCells;
    cells_of(hot, hotCells);
    if (!full && !fresh.empty()) cells_of(fresh, freshCells);
    std::vector<std::vector<IP>> splits(n);
    usize count = 0;
    for (u32 i = 0; i < n; ++i) {
        // As velhas já passaram pelos vértices de agora: só os cruzamentos novos.
        const bool all = full || dirty[i];
        if (!all && fresh.empty()) continue;
        const std::vector<IP>& pts = all ? hot : fresh;
        const CellItems& list = all ? hotCells : freshCells;
        const Edge& E = edges[i];
        std::vector<IP>& s = splits[i];
        G.cover(E.a, E.b, [&](u64 k) {
            for (auto it = cell_begin(list, k); it != list.end() && it->first == k; ++it) {
                const IP c = pts[it->second];
                if (c == E.a || c == E.b) continue;
                if (crosses_hot(E.a, E.b, c)) s.push_back(c);
            }
        });
        if (s.size() > 1) {
            std::sort(s.begin(), s.end());
            s.erase(std::unique(s.begin(), s.end()), s.end());
        }
        count += s.size();
    }
    if (count == 0) return 0;
    std::vector<Edge> out;
    std::vector<u8> mark;
    out.reserve(n + count);
    mark.reserve(n + count);
    for (usize i = 0; i < n; ++i) {
        const Edge& E = edges[i];
        if (splits[i].empty()) { out.push_back(E); mark.push_back(0); continue; }
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
        mark.resize(out.size(), 1);
    }
    edges.swap(out);
    dirty.swap(mark);
    return count;
}

struct UEdge {
    IP lo, hi;   ///< lo < hi
};

/// Orçamento do arranjo exato, por CUSTO MEDIDO. O tempo do arranjo no host
/// (Release x64, o melhor de cinco, com a máquina ocupada por outras
/// compilações — os números pecam para mais) segue de perto
///     arestas × kUsPerEdge + cruzamentos × kUsPerCrossing
/// com cada cruzamento próprio contado uma vez (ele vira arestas novas nas
/// passadas, na fusão e no enrolamento):
///     sem cruzamentos:  6 400 arestas 11 ms · 25 600 arestas 51 ms
///     só cruzamentos:   1 611 em 8,8 ms · 6 448 em 43 ms · 25 284 em 260 ms
///     traço à mão livre de largura 30:  2 404 arestas + 2 944 cruz. 24 ms ·
///         4 757 + 4 640 em 39 ms · 9 455 + 12 599 em 109 ms
/// O teto é o custo estimado de kExactBudgetUs (~60 ms no host: ~29 mil
/// arestas sem cruzamento, ou ~8,5 mil cruzamentos): abaixo dele o recorte é
/// exato; acima, a geometria sai pela amostragem (raster_arrange), em tempo
/// limitado — nenhum quadro fica preso num caminho de centenas de milhares
/// de cruzamentos.
constexpr f64 kUsPerEdge = 2.0;
constexpr f64 kUsPerCrossing = 7.0;
constexpr f64 kExactBudgetUs = 60000.0;

std::atomic<f64> g_budgetScale{1.0};
std::atomic<u64> g_statExact{0}, g_statSampled{0}, g_statLastEdges{0}, g_statLastCrossings{0};
thread_local f32 t_clipTolerance = 0.0f;   ///< ClipToleranceScope (0 = ninguém disse)

/// Arranjo exato. Falso = passou de `budget` cruzamentos próprios (`out`
/// vazio); com `budget` = SIZE_MAX nunca desiste. `crossings` soma os achados.
template <class Inside>
bool arrange(std::vector<Edge>& edges, u32 sets, Inside inside, std::vector<Contour>& out, usize budget, usize& crossings) {
    out.clear();
    if (edges.empty()) return true;
    std::vector<u8> dirty;   // vazio: a primeira passada olha tudo
    for (int pass = 0; pass < 8; ++pass) {
        const usize sp = split_pass(edges, dirty, budget, crossings);
        if (sp == kOverBudget) return false;
        if (sp == 0) break;
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
    // Arestas com contagem zero em todos os conjuntos não contam (ida e volta).
    std::vector<Edge> live;   // a = lo, b = hi, set = índice em ue
    for (u32 i = 0; i < ue.size(); ++i) {
        for (u32 s = 0; s < sets; ++s) {
            if (cnt[static_cast<usize>(i) * sets + s] != 0) { live.push_back(Edge{ue[i].lo, ue[i].hi, i}); break; }
        }
    }
    if (live.empty()) return true;
    const usize m = live.size();
    auto C = [&](u32 e, u32 s) { return cnt[static_cast<usize>(live[e].set) * sets + s]; };   // e = índice em `live`

    // Enrolamento logo à esquerda de cada aresta, pela GRADE: do ponto q até
    // perto da borda direita da célula (x = X), dali na vertical até a linha de
    // referência da fileira (y = Y) e dali o raio para a direita, somado uma
    // vez por fileira (cruzamentos ordenados, soma do fim). X e Y não são
    // inteiros (nenhum vértice cai neles) e as frações são irracionais entre
    // si: o ponto (X, Y) não cai em cima de aresta nenhuma entre vértices
    // inteiros (com 0,5 e 0,5 caía no meio de toda aresta de passos ímpares, e
    // a célula inteira saía com o enrolamento errado por um).
    // Cada consulta só olha as arestas da própria célula. Antes o raio de cada
    // aresta varria a faixa inteira em y: em centenas de milhares de arestas,
    // segundos.
    constexpr f64 kRefX = 0.6180339887498949, kRefY = 0.41421356237309515;
    const CellGrid G = make_grid(live);
    CellItems cells;
    cells.reserve(m * 4);
    for (u32 i = 0; i < m; ++i) G.cover(live[i].a, live[i].b, [&](u64 k) { cells.emplace_back(k, i); });
    std::sort(cells.begin(), cells.end());
    struct Query { f64 px, py; i64 c, r; u32 e; };
    std::vector<Query> qs(m);
    const f64 eps = 1e-3;
    for (u32 i = 0; i < m; ++i) {
        const IP lo = live[i].a, hi = live[i].b;
        const f64 dx = static_cast<f64>(hi.x - lo.x), dy = static_cast<f64>(hi.y - lo.y);
        const f64 len = std::sqrt(dx * dx + dy * dy);
        // Logo à esquerda do meio, e um fio ao longo da aresta: num lado
        // vertical o meio cai em y inteiro (o y de outros vértices), e o
        // caminho até a referência passaria exatamente por um vértice.
        const f64 along = 0.37 * eps;
        const f64 px = (static_cast<f64>(lo.x) + static_cast<f64>(hi.x)) * 0.5 + (dx * along - dy * eps) / len;
        const f64 py = (static_cast<f64>(lo.y) + static_cast<f64>(hi.y)) * 0.5 + (dy * along + dx * eps) / len;
        qs[i] = Query{px, py, G.col(static_cast<i64>(std::floor(px))), G.row(static_cast<i64>(std::floor(py))), i};
    }
    std::sort(qs.begin(), qs.end(), [](const Query& a, const Query& b) { return a.r < b.r || (a.r == b.r && a.e < b.e); });
    // Sentido no raio horizontal: +1 se lo → hi sobe.
    auto up = [&](u32 e) { return live[e].b.y > live[e].a.y ? 1 : -1; };
    auto x_at = [&](u32 e, f64 y) {
        const IP u = live[e].a, v = live[e].b;
        return static_cast<f64>(u.x) + (y - static_cast<f64>(u.y)) * static_cast<f64>(v.x - u.x) / static_cast<f64>(v.y - u.y);
    };
    auto straddles_y = [&](u32 e, f64 y) {   // meio aberto, como o raio de sempre
        const f64 uy = static_cast<f64>(live[e].a.y), vy = static_cast<f64>(live[e].b.y);
        return (uy <= y && y < vy) || (vy <= y && y < uy);
    };
    struct Cross { f64 x; u32 e; };
    std::vector<Cross> rowX;
    std::vector<i32> suffix;   // (rowX.size() + 1) × sets
    std::vector<u32> stamp(m, 0xFFFFFFFFu);
    std::vector<i32> wl(sets), wr(sets);
    std::vector<u8> keep(m, 0);   // 1 = dentro à esquerda, 2 = dentro à direita
    for (usize qa = 0; qa < qs.size();) {
        usize qb = qa;
        const i64 r = qs[qa].r;
        while (qb < qs.size() && qs[qb].r == r) ++qb;
        const f64 Y = static_cast<f64>(G.oy + r * G.cs) + kRefY;
        // Os cruzamentos da linha de referência desta fileira.
        rowX.clear();
        auto it = std::lower_bound(cells.begin(), cells.end(), std::make_pair(G.key(0, r), u32{0}));
        for (; it != cells.end() && G.row_of(it->first) == r; ++it) {
            const u32 e = it->second;
            if (stamp[e] == static_cast<u32>(r)) continue;
            stamp[e] = static_cast<u32>(r);
            if (straddles_y(e, Y)) rowX.push_back(Cross{x_at(e, Y), e});
        }
        std::sort(rowX.begin(), rowX.end(), [](const Cross& a, const Cross& b) { return a.x < b.x || (a.x == b.x && a.e < b.e); });
        suffix.assign((rowX.size() + 1) * sets, 0);
        for (usize k = rowX.size(); k-- > 0;) {
            for (u32 s = 0; s < sets; ++s) suffix[k * sets + s] = suffix[(k + 1) * sets + s] + up(rowX[k].e) * C(rowX[k].e, s);
        }
        for (usize qi = qa; qi < qb; ++qi) {
            const Query& q = qs[qi];
            const f64 X = static_cast<f64>(G.ox + (q.c + 1) * G.cs) - kRefX;
            // Raio a partir de (X, Y): os cruzamentos com x > X.
            const usize k0 = static_cast<usize>(std::upper_bound(rowX.begin(), rowX.end(), X, [](f64 x, const Cross& c) { return x < c.x; }) - rowX.begin());
            for (u32 s = 0; s < sets; ++s) wl[s] = suffix[k0 * sets + s];
            auto ct = std::lower_bound(cells.begin(), cells.end(), std::make_pair(G.key(q.c, r), u32{0}));
            const u64 key = G.key(q.c, r);
            for (; ct != cells.end() && ct->first == key; ++ct) {
                const u32 e = ct->second;
                // Horizontal: de q até X na altura py.
                if (straddles_y(e, q.py)) {
                    const f64 xc = x_at(e, q.py);
                    i32 sgn = 0;
                    if (q.px < X) { if (q.px < xc && xc <= X) sgn = up(e); }
                    else if (X < xc && xc <= q.px) sgn = -up(e);
                    if (sgn != 0) for (u32 s = 0; s < sets; ++s) wl[s] += sgn * C(e, s);
                }
                // Vertical: de (X, py) até (X, Y). lo → hi vai para a direita
                // (lo.x < X < hi.x); por cima dela é a esquerda (+contagem).
                const IP u = live[e].a, v = live[e].b;
                if (static_cast<f64>(u.x) < X && X < static_cast<f64>(v.x)) {
                    const f64 yc = static_cast<f64>(u.y) + (X - static_cast<f64>(u.x)) * static_cast<f64>(v.y - u.y) / static_cast<f64>(v.x - u.x);
                    if (std::min(q.py, Y) < yc && yc < std::max(q.py, Y)) {
                        const i32 sgn = q.py > yc ? 1 : -1;
                        for (u32 s = 0; s < sets; ++s) wl[s] += sgn * C(e, s);
                    }
                }
            }
            for (u32 s = 0; s < sets; ++s) wr[s] = wl[s] - C(q.e, s);
            const bool il = inside(wl), ir = inside(wr);
            if (il != ir) keep[q.e] = il ? 1 : 2;
        }
        qa = qb;
    }
    struct DEdge { IP a, b; };
    std::vector<DEdge> kept;
    for (u32 i = 0; i < m; ++i) {
        if (keep[i] == 1) kept.push_back(DEdge{live[i].a, live[i].b});
        else if (keep[i] == 2) kept.push_back(DEdge{live[i].b, live[i].a});
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
            auto ot = outgoing.find(b);
            if (ot == outgoing.end()) break;
            const f64 ix = static_cast<f64>(b.x - a.x), iy = static_cast<f64>(b.y - a.y);
            u32 best = kInvalidIndex;
            f64 bestAng = -10.0;
            for (u32 cand : ot->second) {
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
        Contour Cn;
        Cn.closed = true;
        Cn.pts.reserve(clean.size());
        for (IP p : clean) Cn.pts.push_back(Vec2{static_cast<f32>(static_cast<f64>(p.x) / kGrid), static_cast<f32>(static_cast<f64>(p.y) / kGrid)});
        if (std::fabs(signed_area(Cn.pts)) < 1e-6) continue;
        out.push_back(std::move(Cn));
    }
    return true;
}

// -----------------------------------------------------------------------------
// Reserva por amostragem
// -----------------------------------------------------------------------------

/// Douglas–Peucker num trecho aberto [a, b] de `p` (marca os que ficam).
void dp_mark(const std::vector<Vec2>& p, usize a, usize b, f64 eps2, std::vector<u8>& keep) {
    // Pilha explícita: um contorno de dezenas de milhares de pontos não pode estourar a pilha.
    std::vector<std::pair<usize, usize>> st{{a, b}};
    while (!st.empty()) {
        const auto [i, j] = st.back();
        st.pop_back();
        if (j <= i + 1) continue;
        const f64 ax = p[i].x, ay = p[i].y, dx = static_cast<f64>(p[j].x) - ax, dy = static_cast<f64>(p[j].y) - ay;
        const f64 L2 = dx * dx + dy * dy;
        f64 worst = -1.0;
        usize at = i;
        for (usize k = i + 1; k < j; ++k) {
            const f64 ex = static_cast<f64>(p[k].x) - ax, ey = static_cast<f64>(p[k].y) - ay;
            f64 d2;
            if (L2 <= 0.0) d2 = ex * ex + ey * ey;
            else { const f64 c = ex * dy - ey * dx; d2 = c * c / L2; }
            if (d2 > worst) { worst = d2; at = k; }
        }
        if (worst > eps2) {
            keep[at] = 1;
            st.push_back({i, at});
            st.push_back({at, j});
        }
    }
}

/// O mesmo recorte por AMOSTRAGEM, quando o arranjo exato passaria do
/// orçamento: a COBERTURA de cada célula de uma grade fina (quatro linhas de
/// varredura por célula, a regra exata em cada uma, a largura coberta exata em
/// x), o contorno de nível 50 % (quadrados em marcha, com o ponto interpolado
/// em cada lado), uma simplificação de uma fração do passo e, por fim, o
/// arranjo exato sobre esses poucos anéis (que já quase não se cruzam) para
/// sair tudo limpo.
///
/// O PASSO é a tolerância de quem chama (ClipToleranceScope: a do achatamento,
/// 0,2 px na tela) ou 1/16 px sem ela; numa borda reta o nível de 50 % cai em
/// cima dela, e o erro fica numa fração do passo. A grade NÃO é guardada: cada
/// fileira é uma lista de degraus (só as células onde a cobertura muda) e o
/// contorno sai de duas fileiras por vez — o custo cresce com o comprimento da
/// borda, não com a área, e uma forma de tela cheia sai com o mesmo passo de
/// uma pequena. Três tetos mantêm o tempo limitado (e só eles engrossam o
/// passo): fileiras, colunas e contas de aresta por linha de varredura.
template <class Inside>
void raster_arrange(const std::vector<Edge>& edges, u32 sets, Inside inside, std::vector<Contour>& out) {
    out.clear();
    if (edges.empty()) return;
    i64 minx = edges[0].a.x, miny = edges[0].a.y, maxx = minx, maxy = miny;
    f64 sumDy = 0.0;
    for (const Edge& e : edges) {
        minx = std::min({minx, e.a.x, e.b.x}); maxx = std::max({maxx, e.a.x, e.b.x});
        miny = std::min({miny, e.a.y, e.b.y}); maxy = std::max({maxy, e.a.y, e.b.y});
        sumDy += static_cast<f64>(std::llabs(e.b.y - e.a.y));
    }
    constexpr int kSub = 4;
    constexpr f64 kMaxRows = 24000.0, kMaxCols = 2.0e6, kMaxEdgeEvals = 1.2e7;
    const f64 W = static_cast<f64>(maxx - minx) + 1.0, H = static_cast<f64>(maxy - miny) + 1.0;
    const f64 wanted = t_clipTolerance > 0.0f ? static_cast<f64>(t_clipTolerance) * kGrid : kGrid / 16.0;
    const f64 step = std::max({wanted, kGrid / 64.0, H / kMaxRows, W / kMaxCols, sumDy * kSub / kMaxEdgeEvals});
    // Célula (i, j): centro em x0 + i·passo, y0 + j·passo. Uma volta de células
    // vazias em toda a borda: os contornos fecham sozinhos.
    const f64 x0 = static_cast<f64>(minx) - 1.5 * step, y0 = static_cast<f64>(miny) - 1.5 * step;
    const usize nx = static_cast<usize>(std::ceil(W / step)) + 4, ny = static_cast<usize>(std::ceil(H / step)) + 4;
    // Arestas (não horizontais) por y de começo; ativas por linha.
    std::vector<u32> order;
    order.reserve(edges.size());
    for (u32 i = 0; i < edges.size(); ++i) if (edges[i].a.y != edges[i].b.y) order.push_back(i);
    auto ylo = [&](u32 i) { return std::min(edges[i].a.y, edges[i].b.y); };
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return ylo(a) < ylo(b); });
    /// Aresta ativa: x na linha de agora (= base + y·slope), até onde vai, conjunto e sentido.
    struct Act { f64 x, base, slope, yEnd; u32 set; i32 dir; };
    std::vector<Act> active;   // sempre na ordem em x da última linha
    std::vector<i32> w(sets);
    std::vector<f32> acc(nx + 2, 0.0f);   // degraus da fileira (só as células tocadas saem de zero)
    std::vector<u32> touched;
    /// A fileira como função em degraus: de `col` em diante (até o próximo) a cobertura é `val`.
    struct Step { u32 col; u8 val; };
    std::vector<Step> rowA, rowB;   // a fileira de baixo (j) e a de cima (j + 1)
    /// Segmento do contorno: do lado `from` ao lado `to` (ids), com o ponto de `from`.
    struct Link { u64 from, to; Vec2 p; };
    std::vector<Link> link;
    auto in = [](u8 v) { return v >= 128; };
    // Lados da grade: horizontal (i, j)–(i+1, j) = 2k, vertical (i, j)–(i, j+1) = 2k + 1.
    auto hid = [&](usize i, usize j) { return static_cast<u64>(j * nx + i) * 2; };
    auto vid = [&](usize i, usize j) { return static_cast<u64>(j * nx + i) * 2 + 1; };
    // Onde a cobertura passa por 50 % entre as duas células do lado (a → b).
    auto mid = [](u8 a, u8 b) { return std::clamp((127.5 - static_cast<f64>(a)) / (static_cast<f64>(b) - static_cast<f64>(a)), 0.0, 1.0); };
    auto hpos = [&](usize i, usize j, u8 a, u8 b) {
        return Vec2{static_cast<f32>((x0 + (static_cast<f64>(i) + mid(a, b)) * step) / kGrid), static_cast<f32>((y0 + static_cast<f64>(j) * step) / kGrid)};
    };
    auto vpos = [&](usize i, usize j, u8 a, u8 b) {
        return Vec2{static_cast<f32>((x0 + static_cast<f64>(i) * step) / kGrid), static_cast<f32>((y0 + (static_cast<f64>(j) + mid(a, b)) * step) / kGrid)};
    };
    // Quadrados em marcha na célula (i, j), cantos em sentido anti-horário
    // v0 = (i, j), v1 = (i+1, j), v2 = (i+1, j+1), v3 = (i, j+1): sai (dentro →
    // fora) num lado e liga ao próximo lado, no mesmo sentido, em que entra —
    // o dentro fica à esquerda do segmento.
    auto cell = [&](usize i, usize j, u8 v0, u8 v1, u8 v2, u8 v3) {
        const bool c[4] = {in(v0), in(v1), in(v2), in(v3)};
        if (c[0] == c[1] && c[1] == c[2] && c[2] == c[3]) return;
        const u64 side[4] = {hid(i, j), vid(i + 1, j), hid(i, j + 1), vid(i, j)};
        for (int k = 0; k < 4; ++k) {
            if (!(c[k] && !c[(k + 1) & 3])) continue;
            for (int s = 1; s < 4; ++s) {
                const int t = (k + s) & 3;
                if (c[t] || !c[(t + 1) & 3]) continue;
                const Vec2 p = k == 0 ? hpos(i, j, v0, v1) : k == 1 ? vpos(i + 1, j, v1, v2) : k == 2 ? hpos(i, j + 1, v3, v2) : vpos(i, j, v0, v3);
                link.push_back(Link{side[k], side[t], p});
                break;
            }
        }
    };
    // O contorno entre a fileira j (rowA) e a j + 1 (rowB): só as células em
    // volta de um degrau são gerais; num trecho em que as duas fileiras ficam
    // constantes (dentro numa, fora na outra) a borda é uma reta só.
    auto march = [&](usize j) {
        usize ia = 0, ib = 0;
        u8 a = 0, b = 0;
        while (ia < rowA.size() || ib < rowB.size()) {
            const u32 c = std::min(ia < rowA.size() ? rowA[ia].col : 0xFFFFFFFFu, ib < rowB.size() ? rowB[ib].col : 0xFFFFFFFFu);
            const u8 a0 = a, b0 = b;   // os valores na coluna c − 1
            if (ia < rowA.size() && rowA[ia].col == c) a = rowA[ia++].val;
            if (ib < rowB.size() && rowB[ib].col == c) b = rowB[ib++].val;
            if (c > 0) cell(c - 1, j, a0, a, b, b0);
            const u32 next = std::min(ia < rowA.size() ? rowA[ia].col : 0xFFFFFFFFu, ib < rowB.size() ? rowB[ib].col : 0xFFFFFFFFu);
            if (next == 0xFFFFFFFFu || next <= c + 1 || in(a) == in(b)) continue;
            // Células c .. next − 2: do lado vertical em c ao lado vertical em next − 1.
            const usize first = c, last = next - 1;
            if (in(a)) link.push_back(Link{vid(last, j), vid(first, j), vpos(last, j, a, b)});
            else link.push_back(Link{vid(first, j), vid(last, j), vpos(first, j, a, b)});
        }
    };
    usize next = 0;
    for (usize j = 0; j < ny; ++j) {
        touched.clear();
        for (int sub = 0; sub < kSub; ++sub) {
            const f64 y = y0 + (static_cast<f64>(j) - 0.5 + (static_cast<f64>(sub) + 0.5) / kSub) * step;
            while (next < order.size() && static_cast<f64>(ylo(order[next])) <= y) {
                const Edge& e = edges[order[next++]];
                const f64 slope = static_cast<f64>(e.b.x - e.a.x) / static_cast<f64>(e.b.y - e.a.y);
                active.push_back(Act{0.0, static_cast<f64>(e.a.x) - static_cast<f64>(e.a.y) * slope, slope,
                                     static_cast<f64>(std::max(e.a.y, e.b.y)), e.set, e.b.y > e.a.y ? 1 : -1});
            }
            // Tira as que acabaram, anda o x das outras e mantém a ordem por
            // inserção: de uma linha para a seguinte quase nada troca de lugar,
            // e a passada sai em tempo quase linear.
            usize keepN = 0;
            for (usize k = 0; k < active.size(); ++k) {
                if (active[k].yEnd <= y) continue;
                Act a = active[k];
                a.x = a.base + y * a.slope;
                usize h = keepN++;
                for (; h > 0 && active[h - 1].x > a.x; --h) active[h] = active[h - 1];
                active[h] = a;
            }
            active.resize(keepN);
            if (active.empty()) continue;
            // Raio para a direita: à esquerda de tudo o enrolamento é a soma
            // (zero num conjunto fechado); cada cruzamento passado tira a parte
            // dele. Cada troca dentro/fora entra na fileira como um degrau em x
            // (a fração da célula onde cai e o resto na seguinte).
            std::fill(w.begin(), w.end(), 0);
            for (const Act& c : active) w[c.set] += c.dir;
            bool state = false;
            for (const Act& c : active) {
                w[c.set] -= c.dir;
                const bool now = inside(w);
                if (now == state) continue;
                state = now;
                const f64 u = std::clamp((c.x - x0) / step + 0.5, 0.0, static_cast<f64>(nx));
                const usize i = std::min(nx - 1, static_cast<usize>(u));
                const f32 f = static_cast<f32>(u - static_cast<f64>(i)), v = (now ? 1.0f : -1.0f) / kSub;
                acc[i] += v * (1.0f - f);
                acc[i + 1] += v * f;
                touched.push_back(static_cast<u32>(i));
                touched.push_back(static_cast<u32>(i + 1));
            }
        }
        // A fileira em degraus: a soma corrida só muda nas células tocadas.
        rowB.clear();
        if (!touched.empty()) {
            std::sort(touched.begin(), touched.end());
            touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
            f32 run = 0.0f;
            for (u32 t : touched) {
                run += acc[t];
                acc[t] = 0.0f;
                if (t < nx) rowB.push_back(Step{t, static_cast<u8>(std::clamp(run, 0.0f, 1.0f) * 255.0f + 0.5f)});
            }
        }
        if (j > 0) march(j - 1);
        rowA.swap(rowB);
    }
    std::sort(link.begin(), link.end(), [](const Link& a, const Link& b) { return a.from < b.from; });
    auto find_link = [&](u64 id) {
        const auto it = std::lower_bound(link.begin(), link.end(), id, [](const Link& l, u64 v) { return l.from < v; });
        return it != link.end() && it->from == id ? static_cast<usize>(it - link.begin()) : link.size();
    };
    std::vector<u8> done(link.size(), 0);
    std::vector<Contour> loops;
    std::vector<Vec2> pts;
    const f64 eps = 0.15 * step / kGrid;
    for (usize s0 = 0; s0 < link.size(); ++s0) {
        if (done[s0]) continue;
        pts.clear();
        usize at = s0;
        for (usize guard = 0; guard <= link.size(); ++guard) {
            done[at] = 1;
            pts.push_back(link[at].p);
            at = find_link(link[at].to);
            if (at == link.size() || done[at]) break;
        }
        if (pts.size() < 3) continue;
        // Simplifica (fechado: corta no ponto mais longe do primeiro).
        usize far = 0;
        f64 best = -1.0;
        for (usize k = 1; k < pts.size(); ++k) {
            const f64 d = static_cast<f64>((pts[k] - pts[0]).length_sq());
            if (d > best) { best = d; far = k; }
        }
        pts.push_back(pts[0]);
        std::vector<u8> keepPt(pts.size(), 0);
        keepPt[0] = keepPt[far] = keepPt[pts.size() - 1] = 1;
        dp_mark(pts, 0, far, eps * eps, keepPt);
        dp_mark(pts, far, pts.size() - 1, eps * eps, keepPt);
        Contour c;
        c.closed = true;
        for (usize k = 0; k + 1 < pts.size(); ++k) if (keepPt[k]) c.pts.push_back(pts[k]);
        if (c.pts.size() >= 3) loops.push_back(std::move(c));
    }
    // Os anéis já trazem o dentro à esquerda: o não-zero deles é a região.
    std::vector<Edge> clean;
    for (const Contour& c : loops) {
        const usize n = c.pts.size();
        IP prev = quant(c.pts[n - 1]);
        for (usize k = 0; k < n; ++k) {
            const IP p = quant(c.pts[k]);
            if (p != prev) clean.push_back(Edge{prev, p, 0});
            prev = p;
        }
    }
    usize ignored = 0;
    arrange(clean, 1, [](const std::vector<i32>& ww) { return ww[0] != 0; }, out, std::numeric_limits<usize>::max(), ignored);
}

/// Avisa UMA vez por execução que um recorte passou do orçamento.
void warn_over_budget(usize edges, usize crossings) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true))
        AUREA_LOG_WARN("vetor: recorte acima do orcamento do arranjo exato (%zu arestas, %zu cruzamentos contados); a geometria sai por amostragem fina para nao travar o quadro", edges, crossings);
}

/// O recorte com o teto: exato quando o custo estimado cabe no orçamento, por
/// amostragem quando não.
template <class Inside>
void arrange_budgeted(std::vector<Edge>& edges, u32 sets, Inside inside, std::vector<Contour>& out) {
    const usize n = edges.size();
    const f64 budgetUs = kExactBudgetUs * g_budgetScale.load(std::memory_order_relaxed);
    const f64 left = budgetUs - static_cast<f64>(n) * kUsPerEdge;   // o que sobra para os cruzamentos
    usize crossings = 0;
    bool exact = false;
    if (left >= 0.0) {
        const f64 most = left / kUsPerCrossing;
        const usize budget = most >= 1.0e18 ? std::numeric_limits<usize>::max() : static_cast<usize>(most);
        const std::vector<Edge> original = edges;
        exact = arrange(edges, sets, inside, out, budget, crossings);
        if (!exact) raster_arrange(original, sets, inside, out);
    } else {
        raster_arrange(edges, sets, inside, out);
    }
    if (!exact) warn_over_budget(n, crossings);
    (exact ? g_statExact : g_statSampled).fetch_add(1, std::memory_order_relaxed);
    g_statLastEdges.store(n, std::memory_order_relaxed);
    g_statLastCrossings.store(crossings, std::memory_order_relaxed);
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

ClipStats clip_stats() noexcept {
    ClipStats s;
    s.exact = g_statExact.load(std::memory_order_relaxed);
    s.sampled = g_statSampled.load(std::memory_order_relaxed);
    s.lastEdges = g_statLastEdges.load(std::memory_order_relaxed);
    s.lastCrossings = g_statLastCrossings.load(std::memory_order_relaxed);
    return s;
}

void clip_set_budget_scale(f64 scale) noexcept {
    g_budgetScale.store(std::isfinite(scale) && scale > 0.0 ? scale : 1.0, std::memory_order_relaxed);
}

ClipToleranceScope::ClipToleranceScope(f32 tolerance) noexcept : previous_(t_clipTolerance) {
    if (std::isfinite(tolerance) && tolerance > 0.0f) t_clipTolerance = tolerance;
}
ClipToleranceScope::~ClipToleranceScope() { t_clipTolerance = previous_; }

void resolve_fill(const std::vector<Contour>& in, FillRule rule, std::vector<Contour>& out) {
    std::vector<Edge> edges;
    append_edges(in, 0, edges);
    arrange_budgeted(edges, 1, [rule](const std::vector<i32>& w) { return rule_in(w[0], rule); }, out);
}

void boolean_op(const std::vector<std::vector<Contour>>& sets, FillRule rule, BoolOp op, std::vector<Contour>& out) {
    out.clear();
    const u32 k = static_cast<u32>(sets.size());
    if (k == 0) return;
    std::vector<Edge> edges;
    for (u32 s = 0; s < k; ++s) append_edges(sets[s], s, edges);
    arrange_budgeted(edges, k, [&](const std::vector<i32>& w) {
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
