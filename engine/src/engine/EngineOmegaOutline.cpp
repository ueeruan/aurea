// =============================================================================
//  Aurea / engine / EngineOmegaOutline.cpp
//
//  Correções da 0.3.4 (bug 11): "Converter em contorno". Uma forma VIVA
//  (camada de forma SDF, timeline/ShapeGeometry.hpp) vira uma camada VETORIAL
//  (kShapeVector) com a mesma silhueta, o mesmo preenchimento e o mesmo
//  contorno, para editar ponto a ponto no "Editar pontos".
//
//  Geometria:
//   - retângulo e elipse: os caminhos exatos (vector::make_rect/make_ellipse);
//   - caminho livre antigo (tipo 2): os próprios pontos;
//   - 0.5.0: as outras formas saem de curvas EXATAS, forma a forma (arcos,
//     retas e curvas por amostragem analítica): o número de pontos depende só
//     dos parâmetros, nunca do tamanho, e o morph casa ponto a ponto, como as
//     formas vivas do Alight Motion. Furos (anel, engrenagem…) ficam como
//     contornos a mais com a regra par-ímpar;
//   - o que muda de topologia (flor com pétalas soltas, número de pontas
//     fracionário no meio de uma animação) cai no caminho antigo: a MESMA
//     distância com sinal do shader amostrada numa grade, o nível zero traçado
//     por marching squares e cada trecho entre cantos ajustado em bézier.
//  A forma é avaliada no instante do cabeçote (tamanho e parâmetros animados
//  viram o valor de agora; as trilhas de forma saem). A camada mantém id,
//  transform, tempo, efeitos e máscaras. Um passo de desfazer.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/DefaultNames.hpp"

#include "aurea/timeline/ShapeFill.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace aurea {
namespace {

/// A forma no instante: os parâmetros animados (trilhas ShapeParam 1..14) no
/// valor de agora. `centered` = havia trilha de geometria (o renderer centra a
/// caixa na âncora; ver Renderer.cpp, caso Shape).
ShapeData shape_now(const Layer& l, FrameIndex local, bool& centered) noexcept {
    ShapeData s = l.shape;
    centered = false;
    for (u32 param = 1; param < shape::kParamTotal; ++param) {
        const Track* tr = l.tracks.find(TrackProperty::ShapeParam, 0, param);
        if (!tr) continue;
        if (param <= shape::kParamHeight) centered = true;
        if (tr->keys.empty()) continue;
        if (f32* field = shape::param_field(s, param)) *field = shape::clamp_param(param, tr->sample(local));
    }
    return s;
}

/// Uma volta fechada do nível zero (px da caixa, centro na origem).
using Loop = std::vector<Vec2>;

f32 loop_area(const Loop& p) noexcept {
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

/// Bézier fechada de uma volta: corta nos cantos vivos (ângulo > 35° numa janela
/// de `win` px) e ajusta cada trecho entre cantos; sem cantos, uma curva só.
BezierPath fit_loop(const Loop& raw, f32 win, f32 error) {
    const usize n = raw.size();
    std::vector<f32> s(n + 1, 0.0f);   // comprimento acumulado (volta fechada)
    for (usize i = 1; i <= n; ++i) s[i] = s[i - 1] + (raw[i % n] - raw[i - 1]).length();
    const f32 total = s[n];
    BezierPath out;
    out.closed = true;
    if (total <= 0.0f) return out;
    auto along = [&](usize i, f32 d) {
        // O ponto a `d` px (com sinal) de i, andando pela volta.
        usize k = i;
        f32 acc = 0.0f;
        for (usize step = 0; step < n; ++step) {
            const usize nk = d > 0.0f ? (k + 1) % n : (k + n - 1) % n;
            acc += (raw[nk] - raw[k]).length();
            k = nk;
            if (acc >= std::fabs(d)) break;
        }
        return raw[k];
    };
    std::vector<f32> angle(n, 0.0f);
    for (usize i = 0; i < n; ++i) {
        const Vec2 u = raw[i] - along(i, -win), w = along(i, win) - raw[i];
        const f32 lu = u.length(), lw = w.length();
        if (lu < 1e-6f || lw < 1e-6f) continue;
        const f32 c = std::clamp(u.dot(w) / (lu * lw), -1.0f, 1.0f);
        angle[i] = std::acos(c);
    }
    constexpr f32 kCorner = 35.0f * 3.14159265f / 180.0f;
    std::vector<usize> corners;
    for (usize i = 0; i < n; ++i) {
        if (angle[i] < kCorner) continue;
        // Máximo local na janela (o canto é um ponto só).
        // Empate (platô): fica o primeiro do platô.
        bool best = true;
        f32 d = 0.0f;
        for (usize k = (i + 1) % n; best && d < win && k != i; k = (k + 1) % n) {
            d += (raw[k] - raw[(k + n - 1) % n]).length();
            if (angle[k] > angle[i]) best = false;
        }
        d = 0.0f;
        for (usize k = (i + n - 1) % n; best && d < win && k != i; k = (k + n - 1) % n) {
            d += (raw[(k + 1) % n] - raw[k]).length();
            if (angle[k] >= angle[i]) best = false;
        }
        if (best) corners.push_back(i);
    }
    if (corners.empty()) {
        Loop closed = raw;
        closed.push_back(raw.front());
        BezierPath p = vector::fit_curve(closed, error, true);
        p.closed = true;
        return p;
    }
    for (usize c = 0; c < corners.size(); ++c) {
        const usize a = corners[c], b = corners[(c + 1) % corners.size()];
        Loop piece;
        for (usize k = a;; k = (k + 1) % n) {
            piece.push_back(raw[k]);
            if (k == b && piece.size() > 1) break;
            if (piece.size() > n + 1) break;
        }
        BezierPath part = vector::fit_curve(piece, error, false);
        if (part.v.size() < 2) continue;
        // O ajuste pode largar o último ponto (passo mínimo): os cantos ficam exatos.
        part.v.front().p = piece.front();
        part.v.back().p = piece.back();
        if (out.v.empty()) {
            out.v = part.v;
        } else {
            out.v.back().out = part.v.front().out;   // o canto: alças independentes
            out.v.insert(out.v.end(), part.v.begin() + 1, part.v.end());
        }
    }
    if (out.v.size() > 1) {
        // O último vértice é o primeiro canto: funde.
        out.v.front().in = out.v.back().in;
        out.v.pop_back();
    }
    return out;
}

// =============================================================================
// Contorno exato (0.5.0)
// =============================================================================

constexpr f32 kPi = 3.14159265358979f;
constexpr f32 kTau = kPi * 2.0f;

/// Giro no sentido horário da tela (y para baixo): o topo vai para a direita.
Vec2 turned(Vec2 q, f32 a) noexcept {
    const f32 c = std::cos(a), s = std::sin(a);
    return Vec2{q.x * c - q.y * s, q.x * s + q.y * c};
}

/// Caneta de um caminho fechado. `st` estica tudo no fim (formas "redondas" em
/// caixa não quadrada); bézier continua exata sob a transformação afim.
struct Pen {
    Vec2 st{1.0f, 1.0f};
    BezierPath path;

    [[nodiscard]] Vec2 map(Vec2 q) const noexcept { return Vec2{q.x * st.x, q.y * st.y}; }
    [[nodiscard]] bool at_end(Vec2 mapped) const noexcept {
        return !path.v.empty() && (path.v.back().p - mapped).length() < 1e-3f;
    }
    void to(Vec2 q) {
        const Vec2 m = map(q);
        if (!at_end(m)) path.v.push_back(BezierVertex{m, {}, {}});
    }
    /// Arco de elipse; a1 > a0 anda no sentido horário da tela.
    void arc(Vec2 c, f32 rx, f32 ry, f32 a0, f32 a1) {
        const f32 span = a1 - a0;
        const auto point = [&](f32 a) { return Vec2{c.x + rx * std::cos(a), c.y + ry * std::sin(a)}; };
        const auto speed = [&](f32 a) { return Vec2{-rx * std::sin(a), ry * std::cos(a)}; };
        to(point(a0));
        if (std::fabs(span) < 1e-5f) return;
        const i32 n = std::max(1, static_cast<i32>(std::ceil(std::fabs(span) / (kPi * 0.5f) - 1e-4f)));
        const f32 step = span / static_cast<f32>(n);
        const f32 k = 4.0f / 3.0f * std::tan(step * 0.25f);
        for (i32 i = 0; i < n; ++i) {
            const f32 a = a0 + step * static_cast<f32>(i), b = a + step;
            path.v.back().out = map(speed(a) * k);
            path.v.push_back(BezierVertex{map(point(b)), map(speed(b) * -k), {}});
        }
    }
    /// Curva qualquer `f(t)`, com `n` trechos e tangente pela derivada numérica.
    template <typename F>
    void curve(F&& f, f32 t0, f32 t1, u32 n) {
        const f32 step = (t1 - t0) / static_cast<f32>(n);
        const f32 eps = step * 0.01f;
        const auto handle = [&](f32 t) { return (f(t + eps) - f(t - eps)) * (step / (6.0f * eps)); };
        to(f(t0));
        for (u32 i = 0; i < n; ++i) {
            const f32 a = t0 + step * static_cast<f32>(i), b = t0 + step * static_cast<f32>(i + 1);
            path.v.back().out = map(handle(a));
            path.v.push_back(BezierVertex{map(f(b)), map(handle(b) * -1.0f), {}});
        }
    }
    /// Fecha: o último ponto em cima do primeiro vira um só.
    [[nodiscard]] BezierPath finish() {
        if (path.v.size() > 1 && (path.v.back().p - path.v.front().p).length() < 1e-3f) {
            path.v.front().in = path.v.back().in;
            path.v.pop_back();
        }
        path.closed = true;
        return std::move(path);
    }
};

BezierPath polygon(std::initializer_list<Vec2> pts) {
    Pen pen;
    for (const Vec2 q : pts) pen.to(q);
    return pen.finish();
}

[[nodiscard]] bool whole(f32 count) noexcept { return std::fabs(count - std::round(count)) < 1e-3f; }

/// Primeira metade da borda da fita da onda: acha o `t` cujo ponto tem x = alvo.
template <typename F>
f32 solve_edge(F&& side, f32 lo, f32 hi, f32 target) {
    for (int i = 0; i < 40; ++i) {
        const f32 mid = (lo + hi) * 0.5f;
        if (side(mid).x < target) lo = mid; else hi = mid;
    }
    return (lo + hi) * 0.5f;
}

/// As curvas exatas da forma, centradas na origem. false = usar o traçado.
bool exact_outline(u32 type, const shape::SdfParams& p, Vec2 half, std::vector<BezierPath>& out) {
    const f32 hx = half.x, hy = half.y;
    const f32 m = std::min(hx, hy);
    const Vec2 st{hx / m, hy / m};
    const f32 corner = p.shape.x, count = p.shape.y;
    const f32 inner = std::clamp(p.shape.z, 0.05f, 0.95f);
    const f32 depth = p.extra.y, tip = p.extra.z, thick = p.extra.w;
    const f32 sweep = p.more.x, head = p.more.y, shaft = p.more.z, amp = p.more.w;
    Pen pen;
    pen.st = st;
    switch (type) {
        case shape::kPolygon: {
            if (!whole(count)) return false;
            const i32 n = std::max(3, static_cast<i32>(std::lround(count)));
            for (i32 k = 0; k < n; ++k) pen.to(turned(Vec2{0.0f, -m}, kTau * static_cast<f32>(k) / static_cast<f32>(n)));
            out.push_back(pen.finish());
            return true;
        }
        case shape::kStar: {
            if (!whole(count)) return false;
            const i32 n = std::max(3, static_cast<i32>(std::lround(count)));
            const f32 an = kPi / static_cast<f32>(n);
            for (i32 k = 0; k < n; ++k) {
                const f32 a = an * 2.0f * static_cast<f32>(k);
                pen.to(turned(Vec2{0.0f, -inner * m}, a));   // o vale fica no topo
                pen.to(turned(Vec2{0.0f, -m}, a + an));
            }
            out.push_back(pen.finish());
            return true;
        }
        case shape::kCross: {
            const f32 t = m * inner;
            out.push_back(polygon({{-t, -hy}, {t, -hy}, {t, -t}, {hx, -t}, {hx, t}, {t, t}, {t, hy}, {-t, hy}, {-t, t}, {-hx, t}, {-hx, -t}, {-t, -t}}));
            return true;
        }
        case shape::kRing: {
            // As duas bordas são níveis da distância à elipse do meio: em caixa
            // não quadrada não são elipses. O raio sai da mesma conta do shader.
            const f32 ring = m * (1.0f - inner) * 0.5f;
            const Vec2 mid{hx - ring, hy - ring};
            const auto edge = [&](f32 level) {
                return [=](f32 t) {
                    const Vec2 d{mid.x * std::cos(t), mid.y * std::sin(t)};
                    f32 lo = 0.0f, hi = 3.0f;
                    for (int i = 0; i < 48; ++i) {
                        const f32 k = (lo + hi) * 0.5f;
                        const Vec2 q = d * k;
                        const f32 k0 = Vec2{q.x / mid.x, q.y / mid.y}.length();
                        const f32 k1 = Vec2{q.x / (mid.x * mid.x), q.y / (mid.y * mid.y)}.length();
                        const f32 sd = k0 < 1e-6f ? -std::min(mid.x, mid.y) : k0 * (k0 - 1.0f) / k1;
                        if (sd < level) lo = k; else hi = k;
                    }
                    return d * ((lo + hi) * 0.5f);
                };
            };
            Pen outer, hole;
            outer.curve(edge(ring), 0.0f, kTau, 8u);
            hole.curve(edge(-ring), 0.0f, kTau, 8u);
            out.push_back(outer.finish());
            out.push_back(hole.finish());
            return true;
        }
        case shape::kPie: {
            if (sweep >= 359.9f) { out.push_back(vector::make_ellipse(Vec2{0.0f, 0.0f}, Vec2{hx * 2.0f, hy * 2.0f})); return true; }
            // O corte é uma reta no ângulo real; o arco anda no parâmetro da elipse.
            const f32 s = sweep * kPi / 180.0f;
            f32 t = std::atan2(hx * std::sin(s), hy * std::cos(s));
            if (t < 0.0f) t += kTau;
            Pen flat;
            flat.to(Vec2{0.0f, 0.0f});
            flat.arc(Vec2{0.0f, 0.0f}, hx, hy, 0.0f, t);
            out.push_back(flat.finish());
            return true;
        }
        case shape::kArc: {
            const f32 T = std::clamp(thick, 0.02f, 0.5f) * 2.0f * m;
            if (sweep >= 359.9f) {
                out.push_back(vector::make_ellipse(Vec2{0.0f, 0.0f}, Vec2{hx * 2.0f, hy * 2.0f}));
                out.push_back(vector::make_ellipse(Vec2{0.0f, 0.0f}, Vec2{(m - T) * 2.0f * st.x, (m - T) * 2.0f * st.y}));
                return true;
            }
            const f32 s = sweep * kPi / 180.0f;
            pen.arc(Vec2{0.0f, 0.0f}, m, m, 0.0f, s);
            pen.arc(Vec2{0.0f, 0.0f}, m - T, m - T, s, 0.0f);
            out.push_back(pen.finish());
            return true;
        }
        case shape::kFlower: {
            // Acima de 0,5 as pétalas se soltam no centro: outra topologia.
            if (!whole(count) || depth > 0.5f) return false;
            const f32 n = std::max(3.0f, std::round(count));
            Pen flat;
            flat.curve([&](f32 a) {
                const Vec2 d{std::cos(a), std::sin(a)};
                const f32 r = m * ((1.0f - depth) + depth * std::cos(n * a));
                return d * (r / Vec2{d.x / st.x, d.y / st.y}.length());
            }, 0.0f, kTau, static_cast<u32>(n) * 4u);
            out.push_back(flat.finish());
            return true;
        }
        case shape::kArrow: {
            const f32 s = hx * (1.0f - 2.0f * head), hs = hy * shaft * 0.5f;
            out.push_back(polygon({{-hx, -hs}, {s, -hs}, {s, -hy}, {hx, 0.0f}, {s, hy}, {s, hs}, {-hx, hs}}));
            return true;
        }
        case shape::kRightTriangle:
            out.push_back(polygon({{-hx, -hy}, {hx, hy}, {-hx, hy}}));
            return true;
        case shape::kTrapezoid: {
            const f32 top = hx * inner;
            out.push_back(polygon({{-top, -hy}, {top, -hy}, {hx, hy}, {-hx, hy}}));
            return true;
        }
        case shape::kParallelogram: {
            const f32 sk = hx * inner;   // 2·sk da forma
            out.push_back(polygon({{-hx + sk, -hy}, {hx, -hy}, {hx - sk, hy}, {-hx, hy}}));
            return true;
        }
        case shape::kGear: {
            if (!whole(count)) return false;
            const i32 n = std::max(3, static_cast<i32>(std::lround(count)));
            const f32 root = m * (1.0f - depth), sector = kTau / static_cast<f32>(n);
            const f32 tw = root * std::sin(sector * 0.25f), yb = root * std::cos(sector * 0.25f);
            for (i32 k = 0; k < n; ++k) {
                const f32 a = sector * static_cast<f32>(k);
                pen.to(turned(Vec2{-tw, -yb}, a));
                pen.to(turned(Vec2{-tw, -m}, a));
                pen.to(turned(Vec2{tw, -m}, a));
                pen.arc(Vec2{0.0f, 0.0f}, root, root, a + sector * 0.25f - kPi * 0.5f, a + sector * 0.75f - kPi * 0.5f);
            }
            out.push_back(pen.finish());
            const f32 hub = inner * root * 2.0f;
            out.push_back(vector::make_ellipse(Vec2{0.0f, 0.0f}, Vec2{hub * st.x, hub * st.y}));
            return true;
        }
        case shape::kDoubleArrow: {
            const f32 s = hx * (1.0f - 2.0f * head), t = hy * inner;
            if (s <= 0.0f) return false;   // as duas pontas se cruzam
            out.push_back(polygon({{-s, -t}, {s, -t}, {s, -hy}, {hx, 0.0f}, {s, hy}, {s, t}, {-s, t}, {-s, hy}, {-hx, 0.0f}, {-s, -hy}}));
            return true;
        }
        case shape::kLine:
            out.push_back(vector::make_rect(Vec2{0.0f, 0.0f}, Vec2{hx * 2.0f, std::max(0.5f, hy * thick) * 2.0f}, corner));
            return true;
        case shape::kDiamond: {
            const f32 yw = hy * (2.0f * std::clamp(tip, 0.05f, 0.95f) - 1.0f);
            out.push_back(polygon({{0.0f, -hy}, {hx, yw}, {0.0f, hy}, {-hx, yw}}));
            return true;
        }
        case shape::kHeart: {
            const f32 dl = std::clamp(depth, 0.05f, 0.95f) * 0.5f;
            const f32 R = (1.0f + dl) - std::sqrt(2.0f * dl);
            const Vec2 C{1.0f - R, R - 1.0f}, P{0.0f, 1.0f};
            const Vec2 v = P - C;
            const f32 L = v.length();
            const Vec2 vh = v / L;
            const f32 ca = R / L, sa = std::sqrt(std::max(0.0f, 1.0f - ca * ca));
            const Vec2 T = C + (vh * ca + Vec2{vh.y, -vh.x} * sa) * R;
            const f32 notchY = C.y - std::sqrt(std::max(0.0f, R * R - C.x * C.x));
            f32 a0 = std::atan2(notchY - C.y, -C.x), a1 = std::atan2(T.y - C.y, T.x - C.x);
            if (a1 < a0) a1 += kTau;
            pen.arc(C * m, R * m, R * m, a0, a1);
            pen.to(P * m);
            const Vec2 C2{-C.x, C.y};
            f32 b0 = std::atan2(T.y - C.y, -T.x - C2.x), b1 = std::atan2(notchY - C.y, C.x);
            if (b1 < b0) b1 += kTau;
            pen.arc(C2 * m, R * m, R * m, b0, b1);
            out.push_back(pen.finish());
            return true;
        }
        case shape::kSeal: {
            const f32 n = std::clamp(std::round(count), 3.0f, 64.0f), h = kPi / n;
            const f32 dmax = 1.0f - 1.0f / (std::cos(h) + std::sin(h));
            const f32 dl = 0.01f + std::clamp(depth, 0.0f, 1.0f) * std::max(0.0f, dmax - 0.01f);
            const f32 v = 1.0f - dl, c = (1.0f - v * v) / (2.0f * (1.0f - v * std::cos(h))), b = 1.0f - c;
            const f32 vx = v * std::sin(h), vy = v * std::cos(h);
            f32 a0 = std::atan2(c - vy, -vx), a1 = std::atan2(c - vy, vx);
            if (a1 < a0) a1 += kTau;
            for (i32 k = 0; k < static_cast<i32>(n); ++k) {
                const f32 a = h * 2.0f * static_cast<f32>(k);
                pen.arc(turned(Vec2{0.0f, -c * m}, a), b * m, b * m, a0 + a, a1 + a);
            }
            out.push_back(pen.finish());
            return true;
        }
        case shape::kBubble: {
            const f32 bodyH = hy * 2.0f * 0.76f, bottom = -hy + bodyH;
            const f32 r = std::clamp(corner, 0.0f, std::min(hx, bodyH * 0.5f));
            const f32 tx = std::clamp(hx * (2.0f * std::clamp(tip, 0.0f, 1.0f) - 1.0f), -hx * 0.92f, hx * 0.92f);
            const f32 bw = hx * 0.16f, lo = -hx + r + bw, hi = hx - r - bw;
            const f32 xb = lo < hi ? std::clamp(tx * 0.55f, lo, hi) : 0.0f;
            const f32 yb = bottom - std::min(bodyH * 0.25f, std::max(r, 2.0f));
            const f32 t = (bottom - yb) / (hy - yb);
            const f32 xR = (xb + bw) + (tx - xb - bw) * t, xL = (xb - bw) + (tx - xb + bw) * t;
            // A cauda precisa sair do trecho reto de baixo.
            if (xR > hx - r || xL < -hx + r || xL >= xR) return false;
            Pen flat;
            flat.arc(Vec2{hx - r, -hy + r}, r, r, -kPi * 0.5f, 0.0f);
            flat.arc(Vec2{hx - r, bottom - r}, r, r, 0.0f, kPi * 0.5f);
            flat.to(Vec2{xR, bottom});
            flat.to(Vec2{tx, hy});
            flat.to(Vec2{xL, bottom});
            flat.arc(Vec2{-hx + r, bottom - r}, r, r, kPi * 0.5f, kPi);
            flat.arc(Vec2{-hx + r, -hy + r}, r, r, kPi, kPi * 1.5f);
            out.push_back(flat.finish());
            return true;
        }
        case shape::kBolt: {
            static constexpr f32 kBase[14] = {-0.05f, -1.0f, 0.60f, -1.0f, 0.12f, -0.12f, 0.70f, -0.12f,
                                              -0.42f, 1.0f, -0.02f, 0.14f, -0.62f, 0.14f};
            const f32 k = (std::clamp(tip, 0.0f, 1.0f) - 0.5f) * 0.8f;
            Pen flat;
            for (int i = 0; i < 7; ++i) flat.to(Vec2{(kBase[2 * i] - k * kBase[2 * i + 1]) * hx, kBase[2 * i + 1] * hy});
            out.push_back(flat.finish());
            return true;
        }
        case shape::kWave: {
            const f32 n = std::clamp(std::round(count), 1.0f, 32.0f);
            const f32 T = std::clamp(thick, 0.02f, 1.0f) * 2.0f * hy;
            const f32 a = std::clamp(amp, 0.0f, 1.0f) * std::max(0.0f, hy - T * 0.5f);
            const f32 k = kPi * n / std::max(hx, 1e-3f);
            if (a < 1e-3f) {
                out.push_back(vector::make_rect(Vec2{0.0f, 0.0f}, Vec2{hx * 2.0f, T}, 0.0f));
                return true;
            }
            // Com a curva mais fechada que a espessura, a borda dobra sobre si.
            if (T * 0.5f * a * k * k > 0.9f) return false;
            const auto side = [&](f32 sign) {
                return [=](f32 t) {
                    const f32 slope = a * k * std::cos(k * (t + hx));
                    const f32 len = std::sqrt(1.0f + slope * slope);
                    return Vec2{t - sign * T * 0.5f * slope / len, a * std::sin(k * (t + hx)) + sign * T * 0.5f / len};
                };
            };
            const auto top = side(-1.0f), bottom = side(1.0f);
            const u32 segments = static_cast<u32>(n) * 8u;
            Pen flat;
            flat.curve(top, solve_edge(top, -hx - T, -hx + T, -hx), solve_edge(top, hx - T, hx + T, hx), segments);
            flat.curve(bottom, solve_edge(bottom, hx - T, hx + T, hx), solve_edge(bottom, -hx - T, -hx + T, -hx), segments);
            out.push_back(flat.finish());
            return true;
        }
        case shape::kBlob: {
            const f32 n = std::clamp(std::round(count), 2.0f, 16.0f);
            const f32 a = std::clamp(depth, 0.05f, 0.95f) * 0.35f, R = m / (1.0f + a);
            const Vec4 ph = p.blob;
            pen.curve([&](f32 th) {
                const f32 r = 1.0f + a * (0.55f * std::cos(n * th + ph.x) + 0.30f * std::cos((n + 1.0f) * th + ph.y) + 0.15f * std::cos(2.0f * th + ph.z));
                return Vec2{R * r * std::cos(th), R * r * std::sin(th)};
            }, 0.0f, kTau, static_cast<u32>(n) * 4u + 8u);
            out.push_back(pen.finish());
            return true;
        }
        case shape::kMoon: {
            const f32 d = depth, Y = m * std::sqrt(std::max(0.0f, 1.0f - d * d));
            const f32 a = std::atan2(Y, m * d);
            pen.arc(Vec2{0.0f, 0.0f}, m, m, a, kTau - a);
            pen.arc(Vec2{2.0f * m * d, 0.0f}, m, m, kPi + a, kPi - a);
            out.push_back(pen.finish());
            return true;
        }
        case shape::kTeardrop: {
            const f32 r1 = std::max(1e-3f, m), r2 = std::clamp(corner, 0.0f, r1 * 0.99f);
            const f32 yc = hy - r1, h = yc + hy - r2;
            if (h <= 1e-3f || (r1 - r2) / h >= 1.0f) {
                out.push_back(vector::make_ellipse(Vec2{0.0f, yc}, Vec2{r1 * 2.0f, r1 * 2.0f}));
                return true;
            }
            const f32 b = (r1 - r2) / h, a = std::sqrt(1.0f - b * b), alpha = std::atan2(b, a);
            Pen flat;
            if (r2 > 1e-3f) flat.arc(Vec2{0.0f, yc - h}, r2, r2, -kPi + alpha, -alpha);
            else flat.to(Vec2{0.0f, yc - h});
            flat.arc(Vec2{0.0f, yc}, r1, r1, -alpha, kPi + alpha);
            out.push_back(flat.finish());
            return true;
        }
        default:
            return false;
    }
}

void translate(BezierPath& p, Vec2 by) noexcept {
    for (BezierVertex& v : p.v) v.p = v.p + by;
}

} // namespace

bool Engine::convert_shape_to_outline(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Shape || l->shape.shapeType == kShapeVector) return false;
    const u32 type = l->shape.shapeType;
    if (type > shape::kLastSdfType || type == 11u) return false;
    const FrameIndex local = l->local_time(playback_.current());
    bool centered = false;
    const ShapeData sh = shape_now(*l, local, centered);
    const f32 w = std::max(1.0f, sh.bounds.w), h = std::max(1.0f, sh.bounds.h);
    // A caixa no espaço da camada: na origem, ou centrada na âncora quando há
    // trilha de geometria (a mesma regra do renderer e dos cantos do palco).
    const Vec2 origin = centered ? Vec2{l->transform.anchor.x - w * 0.5f, l->transform.anchor.y - h * 0.5f} : Vec2{0.0f, 0.0f};
    const Vec2 centre{origin.x + w * 0.5f, origin.y + h * 0.5f};
    const shape_fill::Eval fill = shape_fill::evaluate(*l, static_cast<f64>(local.value));
    const bool stroke = sh.strokeWidth > 0.0f && sh.strokeColor.w > 0.0f;
    const f32 sw = stroke ? sh.strokeWidth : 0.0f;
    // shape.frag: a forma encolhe meio contorno para o traço caber na caixa.
    const Vec2 half{std::max(0.5f, w * 0.5f - sw * 0.5f), std::max(0.5f, h * 0.5f - sw * 0.5f)};

    VectorGroup g;
    g.name = default_name(DefaultName::Outline);
    if (type == shape::kRect) {
        VectorPath p;
        p.path = vector::make_rect(centre, Vec2{half.x * 2.0f, half.y * 2.0f}, sh.cornerRadius);
        g.paths.push_back(std::move(p));
    } else if (type == shape::kEllipse) {
        VectorPath p;
        p.path = vector::make_ellipse(centre, Vec2{half.x * 2.0f, half.y * 2.0f});
        g.paths.push_back(std::move(p));
    } else if (type == shape::kPath) {
        if (sh.path.size() < 2) return false;
        VectorPath p;
        p.path.closed = sh.filled;
        for (const Vec2& q : sh.path) p.path.v.push_back(BezierVertex{q, {}, {}});
        g.paths.push_back(std::move(p));
    } else {
        const shape::SdfParams params = shape::sdf_params(sh, sh.filled, sw);
        std::vector<BezierPath> exact;
        if (exact_outline(type, params, half, exact)) {
            for (BezierPath& bp : exact) {
                translate(bp, centre);
                VectorPath p;
                p.path = std::move(bp);
                g.paths.push_back(std::move(p));
            }
        } else {
            // 400 células no lado maior: ~0,25 % da forma (sub-pixel até 100 px).
            const f32 cs = std::max(0.05f, std::max(half.x, half.y) * 2.0f / 400.0f);
            const Vec2 ext{half.x + 3.0f * cs, half.y + 3.0f * cs};
            const std::vector<Loop> loops = trace_zero(
                [&](Vec2 q) { return shape::signed_distance(type, params, q, half); }, ext, cs);
            if (loops.empty()) return false;
            for (const Loop& loop : loops) {
                BezierPath bp = fit_loop(loop, cs * 3.0f, std::max(0.1f, cs * 0.6f));
                if (bp.v.size() < 2) continue;
                translate(bp, centre);
                VectorPath p;
                p.path = std::move(bp);
                g.paths.push_back(std::move(p));
            }
        }
        if (g.paths.empty()) return false;
        g.fill.rule = 1;   // par-ímpar: os furos ficam furos em qualquer sentido
    }
    for (VectorPath& p : g.paths) p.kind = VectorPathKind::Free;

    // Preenchimento: cor sólida ou degradê (pontas relativas à caixa → px).
    g.fill.enabled = sh.filled && shape_fill::paints(fill);
    g.fill.paint.opacity = 100.0f;
    if (fill.kind == 1) {
        g.fill.paint.type = fill.type == 1 ? 2 : 1;   // angular não existe no vetor: linear
        g.fill.paint.color = fill.color0;
        g.fill.paint.start = Vec2{origin.x + fill.start.x * w, origin.y + fill.start.y * h};
        g.fill.paint.end = Vec2{origin.x + fill.end.x * w, origin.y + fill.end.y * h};
        g.fill.paint.stops = {VectorStop{0.0f, fill.color0}, VectorStop{1.0f, fill.color1}};
    } else {
        g.fill.paint.type = 0;
        g.fill.paint.color = fill.color;
    }
    // Contorno: centrado na borda, como no shader.
    g.stroke.enabled = stroke;
    g.stroke.width = stroke ? sh.strokeWidth : 6.0f;
    g.stroke.paint.type = 0;
    g.stroke.paint.color = stroke ? sh.strokeColor : Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    g.stroke.join = 1;
    g.stroke.cap = 1;
    if (sh.trimEnabled) {
        g.trim.enabled = true;
        g.trim.start = std::clamp(sh.trimStart, 0.0f, 1.0f) * 100.0f;
        g.trim.end = std::clamp(sh.trimEnd, 0.0f, 1.0f) * 100.0f;
        g.trim.offset = sh.trimOffset * 100.0f;
    }

    history_.before_mutation(*comp, project_->timeline().current(), "converter em contorno");
    bump_model_revision_locked();
    // As trilhas da forma viva (geometria 1..14 e preenchimento 32..47) não
    // valem para o vetor: a forma fica no valor de agora.
    l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::ShapeParam; });
    ShapeData next;
    next.shapeType = kShapeVector;
    next.bounds = Rect{0.0f, 0.0f, static_cast<f32>(comp->width()), static_cast<f32>(comp->height())};
    next.vector.groups.push_back(std::move(g));
    l->shape = std::move(next);
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
