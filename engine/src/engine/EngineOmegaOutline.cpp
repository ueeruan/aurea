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
//   - as outras 20+ formas: a MESMA distância com sinal do shader
//     (shape::signed_distance) amostrada numa grade, o nível zero traçado por
//     marching squares, os cantos vivos achados pelo ângulo e cada trecho entre
//     cantos ajustado em bézier (vector::fit_curve). Furos (anel, engrenagem…)
//     ficam como contornos a mais com a regra par-ímpar.
//  A forma é avaliada no instante do cabeçote (tamanho e parâmetros animados
//  viram o valor de agora; as trilhas de forma saem). A camada mantém id,
//  transform, tempo, efeitos e máscaras. Um passo de desfazer.
// =============================================================================
#include "aurea/Engine.hpp"

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
    g.name = "Contorno";
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
        // 400 células no lado maior: ~0,25 % da forma (sub-pixel até 100 px).
        const f32 cs = std::max(0.05f, std::max(half.x, half.y) * 2.0f / 400.0f);
        const Vec2 ext{half.x + 3.0f * cs, half.y + 3.0f * cs};
        const shape::SdfParams params = shape::sdf_params(sh, sh.filled, sw);
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
