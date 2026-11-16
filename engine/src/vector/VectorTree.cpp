// =============================================================================
//  Aurea / vector / VectorTree.cpp
//
//  Lote X do AE (gap_system item 26): avaliação do grupo vetorial pela ORDEM
//  dos itens (de cima para baixo, como a shape layer do After Effects), com
//  vários Preencher / Contorno / Aparar / Repetidor / Mesclar por grupo e
//  grupos dentro de grupos. Ver vector/VectorTree.hpp.
//
//  A avaliação chama as MESMAS funções do caminho de sempre (path_at,
//  apply_modifiers, flatten, boolean_op, trim, resolve_fill, dash,
//  stroke_to_rings), na mesma sequência quando a ordem é a de sempre: por isso
//  `classic_order` dá a geometria antiga exata (teste VectorTree.ClassicOrder…).
// =============================================================================
#include "aurea/vector/VectorTree.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace aurea::vector {
namespace {

constexpr u32 kKinds = static_cast<u32>(VectorItemKind::Count);
constexpr u32 kMaxDepth = 16;

bool single_slot(VectorItemKind k) noexcept {
    return k == VectorItemKind::Fill || k == VectorItemKind::Stroke || k == VectorItemKind::Trim
        || k == VectorItemKind::Repeater || k == VectorItemKind::Merge;
}

const VectorFill& fill_at(const VectorGroup& g, u32 n) noexcept { return n == 0 ? g.fill : g.moreFills[n - 1]; }
const VectorStroke& stroke_at(const VectorGroup& g, u32 n) noexcept { return n == 0 ? g.stroke : g.moreStrokes[n - 1]; }
const VectorTrim& trim_at(const VectorGroup& g, u32 n) noexcept { return n == 0 ? g.trim : g.moreTrims[n - 1]; }
const VectorRepeater& repeater_at(const VectorGroup& g, u32 n) noexcept { return n == 0 ? g.repeater : g.moreRepeaters[n - 1]; }
u8 merge_at(const VectorGroup& g, u32 n) noexcept { return n == 0 ? g.merge : g.moreMerges[n - 1]; }
VectorFill& fill_at(VectorGroup& g, u32 n) noexcept { return n == 0 ? g.fill : g.moreFills[n - 1]; }
VectorStroke& stroke_at(VectorGroup& g, u32 n) noexcept { return n == 0 ? g.stroke : g.moreStrokes[n - 1]; }
VectorTrim& trim_at(VectorGroup& g, u32 n) noexcept { return n == 0 ? g.trim : g.moreTrims[n - 1]; }
VectorRepeater& repeater_at(VectorGroup& g, u32 n) noexcept { return n == 0 ? g.repeater : g.moreRepeaters[n - 1]; }
u8& merge_at(VectorGroup& g, u32 n) noexcept { return n == 0 ? g.merge : g.moreMerges[n - 1]; }

/// O item de sempre (o primeiro do tipo) está ligado?
bool slot_enabled(const VectorGroup& g, VectorItemKind k) noexcept {
    switch (k) {
        case VectorItemKind::Fill: return g.fill.enabled;
        case VectorItemKind::Stroke: return g.stroke.enabled;
        case VectorItemKind::Trim: return g.trim.enabled;
        case VectorItemKind::Repeater: return g.repeater.enabled;
        case VectorItemKind::Merge: return g.merge != 0;
        default: return true;
    }
}

/// n-ésimo item do tipo em cada posição da ordem.
std::vector<u32> ordinals(const std::vector<VectorItemKind>& order) {
    u32 seen[kKinds]{};
    std::vector<u32> out(order.size(), 0);
    for (usize i = 0; i < order.size(); ++i) out[i] = seen[static_cast<u32>(order[i])]++;
    return out;
}

// O mesmo `copy_count` do Vector.cpp (lá é interno).
u32 copy_count(const VectorRepeater& r, f32& lastAlpha) {
    const f32 c = std::clamp(r.copies, 0.0f, 500.0f);
    const u32 n = static_cast<u32>(std::ceil(c - 1e-4f));
    const f32 frac = c - std::floor(c);
    lastAlpha = frac > 1e-4f ? frac : 1.0f;
    return n;
}

/// Um caminho acumulado: ainda bézier, ou já achatado (depois de Mesclar/Aparar).
struct Geo {
    BezierPath bez;
    Contour line;
    bool flat = false;
    u8 hole = 0;
};

BezierPath polyline_bezier(const Contour& c) {
    BezierPath b;
    b.closed = c.closed;
    b.v.reserve(c.pts.size());
    for (Vec2 p : c.pts) b.v.push_back(BezierVertex{p, {}, {}});
    return b;
}

/// Linhas (sem furo) e furos achatados, na ordem dos caminhos — sem mexer em `geo`.
void lines_of(const std::vector<Geo>& geo, f32 tol, std::vector<Contour>& lines, std::vector<Contour>& holes) {
    lines.clear();
    holes.clear();
    for (const Geo& g : geo) {
        Contour c;
        if (g.flat) {
            c = g.line;
        } else {
            if (g.bez.v.empty()) continue;
            flatten(g.bez, tol, c);
        }
        if (c.pts.empty()) continue;
        (g.hole ? holes : lines).push_back(std::move(c));
    }
}

void set_lines(std::vector<Geo>& geo, std::vector<Contour>&& lines, std::vector<Contour>&& holes) {
    geo.clear();
    geo.reserve(lines.size() + holes.size());
    for (Contour& c : lines) {
        Geo g;
        g.line = std::move(c);
        g.flat = true;
        geo.push_back(std::move(g));
    }
    for (Contour& c : holes) {
        Geo g;
        g.line = std::move(c);
        g.flat = true;
        g.hole = 1;
        geo.push_back(std::move(g));
    }
}

Geo transformed(const Geo& g, const Affine2& m) {
    Geo r;
    r.flat = g.flat;
    r.hole = g.hole;
    if (g.flat) {
        r.line.closed = g.line.closed;
        r.line.pts.reserve(g.line.pts.size());
        for (Vec2 p : g.line.pts) r.line.pts.push_back(m.apply(p));
    } else {
        r.bez = transform_path(g.bez, m);
    }
    return r;
}

struct Eval {
    f64 frame = 0.0;
    TreeResult* res = nullptr;

    /// `top` = as camadas de tinta do grupo, de CIMA para baixo.
    void group(const VectorGroup& g, f32 tol, std::vector<Geo>& geo, std::vector<TreePaint>& top, u32 depth) {
        const ClipToleranceScope scope(tol);
        const std::vector<VectorItemKind> order = effective_order(g);
        const std::vector<u32> nth = ordinals(order);
        bool merged = false;
        for (usize i = 0; i < order.size(); ++i) {
            const u32 n = nth[i];
            switch (order[i]) {
                case VectorItemKind::Path: {
                    if (n >= g.paths.size()) break;
                    Geo p;
                    p.bez = path_at(g.paths[n], frame);
                    if (p.bez.v.empty()) break;
                    p.hole = g.paths[n].exclude ? 1 : 0;
                    geo.push_back(std::move(p));
                    break;
                }
                case VectorItemKind::Modifier: {
                    if (n >= g.modifiers.size() || !g.modifiers[n].enabled || geo.empty()) break;
                    std::vector<BezierPath> beziers;
                    std::vector<u8> tags;
                    beziers.reserve(geo.size());
                    for (Geo& p : geo) {
                        beziers.push_back(p.flat ? polyline_bezier(p.line) : std::move(p.bez));
                        tags.push_back(p.hole);
                    }
                    apply_modifiers(std::vector<VectorModifier>{g.modifiers[n]}, beziers, tol, &tags);
                    geo.clear();
                    for (usize b = 0; b < beziers.size(); ++b) {
                        if (beziers[b].v.empty()) continue;
                        Geo p;
                        p.bez = std::move(beziers[b]);
                        p.hole = b < tags.size() ? tags[b] : 0;
                        geo.push_back(std::move(p));
                    }
                    break;
                }
                case VectorItemKind::Merge: {
                    const u8 mode = merge_at(g, n);
                    if (mode == 0) break;
                    std::vector<Contour> lines, holes, result;
                    lines_of(geo, tol, lines, holes);
                    std::vector<std::vector<Contour>> sets;
                    for (Contour& c : lines) if (c.pts.size() >= 3) sets.push_back({std::move(c)});
                    const FillRule rule = g.fill.rule == 1 ? FillRule::EvenOdd : FillRule::NonZero;
                    if (!sets.empty()) boolean_op(sets, rule, static_cast<BoolOp>(std::clamp<u8>(mode, 1, 4)), result);
                    set_lines(geo, std::move(result), std::move(holes));
                    merged = true;
                    break;
                }
                case VectorItemKind::Trim: {
                    const VectorTrim& t = trim_at(g, n);
                    if (!t.enabled) break;
                    std::vector<Contour> lines, holes;
                    lines_of(geo, tol, lines, holes);
                    trim(lines, t.start * 0.01f, t.end * 0.01f, t.offset * 0.01f, t.mode == 1);
                    set_lines(geo, std::move(lines), std::move(holes));
                    break;
                }
                case VectorItemKind::Fill: {
                    const VectorFill& f = fill_at(g, n);
                    if (!f.enabled || !(f.paint.opacity > 0.0f)) break;
                    std::vector<Contour> lines, holes, closed, rings;
                    lines_of(geo, tol, lines, holes);
                    for (Contour& c : lines) if (c.pts.size() >= 3) closed.push_back(std::move(c));
                    const FillRule rule = f.rule == 1 ? FillRule::EvenOdd : FillRule::NonZero;
                    // Depois da mesclagem os anéis já saem limpos (a regra de sempre).
                    resolve_fill(closed, merged ? FillRule::NonZero : rule, rings);
                    if (!holes.empty() && !rings.empty()) {
                        std::vector<Contour> cut;
                        for (const Contour& c : holes) if (c.pts.size() >= 3) cut.push_back(c);
                        if (!cut.empty()) {
                            std::vector<Contour> holeRings, rest;
                            resolve_fill(cut, FillRule::NonZero, holeRings);
                            if (!holeRings.empty()) {
                                boolean_op({rings, holeRings}, FillRule::NonZero, BoolOp::Subtract, rest);
                                rings = std::move(rest);
                            }
                        }
                    }
                    if (rings.empty()) break;
                    TreePaint layer;
                    layer.rings = std::move(rings);
                    layer.paint = static_cast<u32>(res->paints.size());
                    res->paints.push_back(f.paint);
                    top.push_back(std::move(layer));
                    break;
                }
                case VectorItemKind::Stroke: {
                    const VectorStroke& st = stroke_at(g, n);
                    if (!st.enabled || !(st.width > 0.0f) || !(st.paint.opacity > 0.0f)) break;
                    std::vector<Contour> lines, holes, rings;
                    lines_of(geo, tol, lines, holes);
                    lines.insert(lines.end(), holes.begin(), holes.end());
                    if (!st.dashes.empty()) dash(lines, st.dashes, st.dashOffset);
                    const StrokeTaper taper{st.taperStart * 0.01f, st.taperEnd * 0.01f, st.taperEase * 0.01f,
                                            st.widthProfile.empty() ? nullptr : &st.widthProfile};
                    stroke_to_rings(lines, st.width, st.cap, st.join, st.miterLimit, tol, rings, taper);
                    if (rings.empty()) break;
                    TreePaint layer;
                    layer.rings = std::move(rings);
                    layer.paint = static_cast<u32>(res->paints.size());
                    res->paints.push_back(st.paint);
                    top.push_back(std::move(layer));
                    break;
                }
                case VectorItemKind::Repeater: {
                    const VectorRepeater& r = repeater_at(g, n);
                    if (!r.enabled) break;
                    f32 lastAlpha = 1.0f;
                    const u32 copies = copy_count(r, lastAlpha);
                    if (copies == 0) {
                        // Zero cópias: nada do que está acima sobra.
                        top.clear();
                        geo.clear();
                        break;
                    }
                    const auto alpha = [&](u32 k) {
                        const f32 t = copies > 1 ? static_cast<f32>(k) / static_cast<f32>(copies - 1) : 0.0f;
                        f32 a = std::clamp((r.startOpacity + (r.endOpacity - r.startOpacity) * t) * 0.01f, 0.0f, 1.0f);
                        if (k + 1 == copies) a *= lastAlpha;
                        return a;
                    };
                    // Ordem de desenho de sempre: as cópias em sequência ("embaixo":
                    // a cópia 0 por último), cada uma com as camadas de baixo para cima.
                    std::vector<TreePaint> drawn;
                    drawn.reserve(top.size() * copies);
                    const auto copy = [&](u32 k) {
                        const Affine2 R = repeater_matrix(r, static_cast<f32>(k) + r.offset);
                        const f32 a = alpha(k);
                        for (usize l = top.size(); l-- > 0;) {
                            TreePaint c = top[l];
                            c.M = R * c.M;
                            c.alpha *= a;
                            drawn.push_back(std::move(c));
                        }
                    };
                    if (r.above) for (u32 k = 0; k < copies; ++k) copy(k);
                    else for (u32 k = copies; k-- > 0;) copy(k);
                    std::reverse(drawn.begin(), drawn.end());
                    top = std::move(drawn);
                    // Os caminhos acima também se repetem (um Preencher abaixo pinta todas as cópias).
                    std::vector<Geo> repeated;
                    repeated.reserve(geo.size() * copies);
                    for (u32 k = 0; k < copies; ++k) {
                        const Affine2 R = repeater_matrix(r, static_cast<f32>(k) + r.offset);
                        for (const Geo& p : geo) repeated.push_back(transformed(p, R));
                    }
                    geo = std::move(repeated);
                    break;
                }
                case VectorItemKind::Group: {
                    if (n >= g.children.size() || depth + 1 >= kMaxDepth) break;
                    const VectorGroup& child = g.children[n];
                    if (!child.visible || !(child.opacity > 0.0f)) break;
                    const Affine2 C = group_matrix(child);
                    std::vector<Geo> childGeo;
                    std::vector<TreePaint> childTop;
                    group(child, tol / std::max(1e-3f, C.max_scale()), childGeo, childTop, depth + 1);
                    const f32 a = std::clamp(child.opacity * 0.01f, 0.0f, 1.0f);
                    for (TreePaint& layer : childTop) {
                        layer.M = C * layer.M;
                        layer.alpha *= a;
                        top.push_back(std::move(layer));
                    }
                    for (const Geo& p : childGeo) geo.push_back(transformed(p, C));
                    break;
                }
                default: break;
            }
        }
    }
};

struct TreeHasher {
    u64 h = 1469598103934665603ull;
    void bytes(const void* p, usize n) noexcept {
        const auto* b = static_cast<const u8*>(p);
        for (usize i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    }
    void f(f32 v) noexcept { bytes(&v, sizeof(v)); }
    void u(u64 v) noexcept { bytes(&v, sizeof(v)); }
    void v2(Vec2 v) noexcept { f(v.x); f(v.y); }
    void v4(Vec4 v) noexcept { f(v.x); f(v.y); f(v.z); f(v.w); }
    void path(const BezierPath& p) noexcept {
        u(p.v.size());
        u(p.closed);
        for (const BezierVertex& v : p.v) { v2(v.p); v2(v.in); v2(v.out); }
    }
    void paint(const VectorPaint& p) noexcept {
        u(p.type); v4(p.color); v2(p.start); v2(p.end); f(p.opacity);
        u(p.stops.size());
        for (const VectorStop& s : p.stops) { f(s.pos); v4(s.color); }
    }
    void fill(const VectorFill& x) noexcept { u(x.enabled); u(x.rule); paint(x.paint); }
    void stroke(const VectorStroke& s) noexcept {
        u(s.enabled); paint(s.paint); f(s.width); u(s.cap); u(s.join); f(s.miterLimit); f(s.dashOffset);
        f(s.taperStart); f(s.taperEnd); f(s.taperEase);
        u(s.widthProfile.size());
        for (f32 w : s.widthProfile) f(w);
        u(s.dashes.size());
        for (f32 d : s.dashes) f(d);
    }
    void trim(const VectorTrim& t) noexcept { u(t.enabled); f(t.start); f(t.end); f(t.offset); u(t.mode); }
    void repeater(const VectorRepeater& r) noexcept {
        u(r.enabled); f(r.copies); f(r.offset); v2(r.anchor); v2(r.position); f(r.scale); f(r.rotation);
        f(r.startOpacity); f(r.endOpacity); u(r.above); u(r.scaleLinked); f(r.scaleY);
    }
    /// Um grupo de dentro inteiro (o de primeiro nível já está na chave de sempre).
    void whole(const VectorGroup& g, f64 frame, u32 depth) noexcept {
        u(g.visible); u(g.merge); u(g.composite);
        u(g.paths.size());
        bool morph = false;
        for (const VectorPath& p : g.paths) {
            u(static_cast<u64>(p.kind)); u(p.reversed); v2(p.center); v2(p.size); f(p.roundness); f(p.points);
            f(p.outerRadius); f(p.innerRadius); f(p.outerRoundness); f(p.innerRoundness); f(p.rotation); u(p.exclude);
            path(p.path);
            u(p.keys.size());
            for (const PathKey& k : p.keys) { u(static_cast<u64>(k.frame)); u(k.ease); path(k.path); u(k.curve); u(k.easePower); f(k.bx1); f(k.by1); f(k.bx2); f(k.by2); }
            morph = morph || p.keys.size() > 1;
        }
        if (morph) bytes(&frame, sizeof(frame));
        fill(g.fill); stroke(g.stroke); trim(g.trim); repeater(g.repeater);
        u(g.modifiers.size());
        for (const VectorModifier& m : g.modifiers) {
            u(static_cast<u64>(m.type)); u(m.enabled);
            for (f32 v : m.params) f(v);
            if (modifier_animated(m)) bytes(&m.time, sizeof(m.time));
        }
        v2(g.position); v2(g.anchor); v2(g.scale); f(g.rotation); f(g.opacity);
        extras(g, frame, depth);
    }
    void extras(const VectorGroup& g, f64 frame, u32 depth) noexcept {
        f(g.skew); f(g.skewAxis);
        u(g.freeOrder);
        u(g.order.size());
        for (VectorItemKind k : g.order) u(static_cast<u64>(k));
        u(g.moreFills.size());
        for (const VectorFill& x : g.moreFills) fill(x);
        u(g.moreStrokes.size());
        for (const VectorStroke& x : g.moreStrokes) stroke(x);
        u(g.moreTrims.size());
        for (const VectorTrim& x : g.moreTrims) trim(x);
        u(g.moreRepeaters.size());
        for (const VectorRepeater& x : g.moreRepeaters) repeater(x);
        u(g.moreMerges.size());
        for (u8 m : g.moreMerges) u(m);
        u(g.children.size());
        if (depth + 1 < kMaxDepth) for (const VectorGroup& c : g.children) whole(c, frame, depth + 1);
    }
};

} // namespace

// =============================================================================
// Ordem
// =============================================================================

bool group_has_tree(const VectorGroup& g) noexcept {
    return g.freeOrder || !g.children.empty() || !g.moreFills.empty() || !g.moreStrokes.empty() || !g.moreTrims.empty()
        || !g.moreRepeaters.empty() || !g.moreMerges.empty() || g.skew != 0.0f;
}

bool data_has_tree(const VectorData& d) noexcept {
    for (const VectorGroup& g : d.groups) if (group_has_tree(g)) return true;
    return false;
}

u32 item_count(const VectorGroup& g, VectorItemKind kind) noexcept {
    switch (kind) {
        case VectorItemKind::Path: return static_cast<u32>(g.paths.size());
        case VectorItemKind::Modifier: return static_cast<u32>(g.modifiers.size());
        case VectorItemKind::Group: return static_cast<u32>(g.children.size());
        case VectorItemKind::Fill: return 1u + static_cast<u32>(g.moreFills.size());
        case VectorItemKind::Stroke: return 1u + static_cast<u32>(g.moreStrokes.size());
        case VectorItemKind::Trim: return 1u + static_cast<u32>(g.moreTrims.size());
        case VectorItemKind::Repeater: return 1u + static_cast<u32>(g.moreRepeaters.size());
        case VectorItemKind::Merge: return 1u + static_cast<u32>(g.moreMerges.size());
        default: return 0;
    }
}

std::vector<VectorItemKind> classic_order(const VectorGroup& g) {
    std::vector<VectorItemKind> o;
    o.reserve(g.paths.size() + g.modifiers.size() + g.children.size() + 5);
    // Grupos de dentro num grupo sem ordem própria: em cima de tudo.
    for (usize i = 0; i < g.children.size(); ++i) o.push_back(VectorItemKind::Group);
    for (usize i = 0; i < g.paths.size(); ++i) o.push_back(VectorItemKind::Path);
    for (usize i = 0; i < g.modifiers.size(); ++i) o.push_back(VectorItemKind::Modifier);
    o.push_back(VectorItemKind::Merge);
    o.push_back(VectorItemKind::Trim);
    o.push_back(VectorItemKind::Stroke);     // o contorno por cima do preenchimento
    o.push_back(VectorItemKind::Fill);
    o.push_back(VectorItemKind::Repeater);
    return o;
}

std::vector<VectorItemKind> effective_order(const VectorGroup& g) {
    if (!g.freeOrder) return classic_order(g);
    std::vector<VectorItemKind> o;
    o.reserve(g.order.size() + 4);
    u32 seen[kKinds]{};
    // 1 · Entradas sem item saem (as últimas do tipo).
    for (VectorItemKind k : g.order) {
        const u32 ki = static_cast<u32>(k);
        if (ki >= kKinds) continue;
        if (seen[ki] >= item_count(g, k)) continue;
        ++seen[ki];
        o.push_back(k);
    }
    const auto last_of = [&](VectorItemKind k) -> i32 {
        for (usize i = o.size(); i-- > 0;) if (o[i] == k) return static_cast<i32>(i);
        return -1;
    };
    // 2 · Caminho, operador e grupo sem entrada (criados por um comando antigo):
    // depois do último do mesmo tipo.
    for (VectorItemKind k : {VectorItemKind::Group, VectorItemKind::Path, VectorItemKind::Modifier}) {
        const u32 ki = static_cast<u32>(k);
        while (seen[ki] < item_count(g, k)) {
            i32 at = last_of(k);
            if (at < 0 && k == VectorItemKind::Modifier) at = last_of(VectorItemKind::Path);
            o.insert(o.begin() + (at + 1), k);
            ++seen[ki];
        }
    }
    // 3 · O item de sempre LIGADO sem entrada (o painel do celular ligou): no
    // lugar que ele tem na ordem de sempre.
    const auto geometry_end = [&]() -> usize {
        i32 at = std::max(last_of(VectorItemKind::Path), std::max(last_of(VectorItemKind::Modifier), last_of(VectorItemKind::Group)));
        at = std::max(at, std::max(last_of(VectorItemKind::Merge), last_of(VectorItemKind::Trim)));
        return static_cast<usize>(at + 1);
    };
    if (seen[static_cast<u32>(VectorItemKind::Merge)] == 0 && g.merge != 0) {
        const i32 at = std::max(last_of(VectorItemKind::Path), std::max(last_of(VectorItemKind::Modifier), last_of(VectorItemKind::Group)));
        o.insert(o.begin() + (at + 1), VectorItemKind::Merge);
    }
    if (seen[static_cast<u32>(VectorItemKind::Trim)] == 0 && g.trim.enabled) o.insert(o.begin() + static_cast<std::ptrdiff_t>(geometry_end()), VectorItemKind::Trim);
    if (seen[static_cast<u32>(VectorItemKind::Stroke)] == 0 && g.stroke.enabled) {
        // Por cima do primeiro Preencher; sem nenhum, depois da geometria.
        usize at = geometry_end();
        for (usize i = 0; i < o.size(); ++i) if (o[i] == VectorItemKind::Fill) { at = std::min(at, i); break; }
        o.insert(o.begin() + static_cast<std::ptrdiff_t>(std::min(at, o.size())), VectorItemKind::Stroke);
    }
    if (seen[static_cast<u32>(VectorItemKind::Fill)] == 0 && g.fill.enabled) {
        // O PRIMEIRO Preencher é o de sempre: antes de qualquer outro, depois
        // do último Contorno que estiver acima deles.
        usize at = geometry_end();
        const i32 stroke = last_of(VectorItemKind::Stroke);
        if (stroke >= 0) at = std::max(at, static_cast<usize>(stroke + 1));
        for (usize i = 0; i < o.size(); ++i) if (o[i] == VectorItemKind::Fill) { at = std::min(at, i); break; }
        o.insert(o.begin() + static_cast<std::ptrdiff_t>(std::min(at, o.size())), VectorItemKind::Fill);
    }
    if (seen[static_cast<u32>(VectorItemKind::Repeater)] == 0 && g.repeater.enabled) o.push_back(VectorItemKind::Repeater);
    return o;
}

void materialize_order(VectorGroup& g) {
    if (g.freeOrder) {
        g.order = effective_order(g);
        return;
    }
    // Da ordem de sempre só entram os itens que valem: os desligados ficam
    // guardados sem entrada (não poluem a árvore) e voltam ao serem ligados.
    std::vector<VectorItemKind> o;
    for (VectorItemKind k : classic_order(g)) {
        if (single_slot(k) && !slot_enabled(g, k)) continue;
        o.push_back(k);
    }
    g.order = std::move(o);
    g.freeOrder = true;
}

// =============================================================================
// Avaliação
// =============================================================================

void evaluate_tree(const VectorGroup& g, f64 frame, f32 tolerance, TreeResult& out) {
    out.layers.clear();
    out.paints.clear();
    Eval e;
    e.frame = frame;
    e.res = &out;
    std::vector<Geo> geo;
    std::vector<TreePaint> top;
    e.group(g, tolerance, geo, top, 0);
    std::reverse(top.begin(), top.end());
    out.layers = std::move(top);
}

f32 tree_repeat_scale(const VectorGroup& g) noexcept {
    f32 best = 1.0f;
    const auto one = [&](const VectorRepeater& r) {
        if (!r.enabled) return;
        f32 lastAlpha = 1.0f;
        const u32 copies = copy_count(r, lastAlpha);
        if (copies == 0) return;
        const f32 s = std::max(std::fabs(r.scale), r.scaleLinked ? 0.0f : std::fabs(r.scaleY)) * 0.01f;
        f32 rep = std::max(1.0f, std::pow(std::max(s, 1e-3f), std::max(0.0f, static_cast<f32>(copies) - 1.0f + std::fabs(r.offset))));
        rep = std::min(rep, 64.0f);
        best = std::max(best, rep);
    };
    one(g.repeater);
    for (const VectorRepeater& r : g.moreRepeaters) one(r);
    for (const VectorGroup& c : g.children) best = std::max(best, tree_repeat_scale(c));
    return best;
}

bool tree_bounds(const VectorGroup& g, f64 frame, Vec2& mn, Vec2& mx) {
    TreeResult res;
    evaluate_tree(g, frame, 0.5f, res);
    const Affine2 G = group_matrix(g);
    bool any = false;
    for (const TreePaint& layer : res.layers) {
        const Affine2 M = G * layer.M;
        for (const Contour& c : layer.rings) {
            for (Vec2 q : c.pts) {
                const Vec2 t = M.apply(q);
                if (!any) { mn = mx = t; any = true; continue; }
                mn = Vec2{std::min(mn.x, t.x), std::min(mn.y, t.y)};
                mx = Vec2{std::max(mx.x, t.x), std::max(mx.y, t.y)};
            }
        }
    }
    return any;
}

u64 tree_hash(const VectorGroup& g, f64 frame) noexcept {
    if (!group_has_tree(g)) return 0;
    TreeHasher H;
    H.u(0x54524545ull);   // "TREE"
    H.extras(g, frame, 0);
    return H.h | 1ull;
}

// =============================================================================
// Listagem e edição
// =============================================================================
namespace {

void row_values(const VectorGroup& g, VectorItemKind kind, u32 n, TreeRow& row) {
    f32* v = row.values;
    u32 c = 0;
    const auto put = [&](f32 x) { if (c < kTreeMaxValues) v[c++] = x; };
    switch (kind) {
        case VectorItemKind::Path: {
            const VectorPath& p = g.paths[n];
            put(static_cast<f32>(p.kind)); put(p.center.x); put(p.center.y); put(p.size.x); put(p.size.y); put(p.roundness);
            put(p.points); put(p.outerRadius); put(p.innerRadius); put(p.rotation);
            row.enabled = true;
            break;
        }
        case VectorItemKind::Fill: {
            const VectorFill& f = fill_at(g, n);
            put(f.paint.color.x); put(f.paint.color.y); put(f.paint.color.z); put(f.paint.opacity); put(static_cast<f32>(f.rule));
            row.enabled = f.enabled;
            break;
        }
        case VectorItemKind::Stroke: {
            const VectorStroke& s = stroke_at(g, n);
            put(s.paint.color.x); put(s.paint.color.y); put(s.paint.color.z); put(s.paint.opacity); put(s.width);
            put(static_cast<f32>(s.cap)); put(static_cast<f32>(s.join)); put(s.miterLimit);
            row.enabled = s.enabled;
            break;
        }
        case VectorItemKind::Trim: {
            const VectorTrim& t = trim_at(g, n);
            put(t.start); put(t.end); put(t.offset); put(static_cast<f32>(t.mode));
            row.enabled = t.enabled;
            break;
        }
        case VectorItemKind::Repeater: {
            const VectorRepeater& r = repeater_at(g, n);
            put(r.copies); put(r.offset); put(r.position.x); put(r.position.y); put(r.scale); put(r.scaleLinked ? r.scale : r.scaleY);
            put(r.rotation); put(r.startOpacity); put(r.endOpacity); put(static_cast<f32>(r.above)); put(r.anchor.x); put(r.anchor.y);
            row.enabled = r.enabled;
            break;
        }
        case VectorItemKind::Merge: {
            put(static_cast<f32>(merge_at(g, n)));
            row.enabled = merge_at(g, n) != 0;
            break;
        }
        case VectorItemKind::Modifier: {
            const VectorModifier& m = g.modifiers[n];
            put(static_cast<f32>(m.type));
            for (u32 i = 0; i < kVecModParams; ++i) put(m.params[i]);
            row.enabled = m.enabled;
            break;
        }
        default: break;
    }
    row.valueCount = c;
}

void group_row(const VectorGroup& g, u32 depth, u32 ordinal, u32 position, std::vector<TreeRow>& out) {
    TreeRow row;
    row.depth = depth;
    row.kind = VectorItemKind::Group;
    row.ordinal = ordinal;
    row.position = position;
    row.enabled = g.visible;
    row.name = g.name;
    const f32 vals[10] = {g.position.x, g.position.y, g.anchor.x, g.anchor.y, g.scale.x, g.scale.y, g.rotation, g.opacity, g.skew, g.skewAxis};
    std::memcpy(row.values, vals, sizeof(vals));
    row.valueCount = 10;
    out.push_back(std::move(row));
    if (depth + 1 >= kMaxDepth) return;
    const std::vector<VectorItemKind> order = effective_order(g);
    const std::vector<u32> nth = ordinals(order);
    for (usize i = 0; i < order.size(); ++i) {
        if (order[i] == VectorItemKind::Group) {
            if (nth[i] < g.children.size()) group_row(g.children[nth[i]], depth + 1, nth[i], static_cast<u32>(i), out);
            continue;
        }
        // Na ordem de sempre os itens desligados não aparecem (não são da árvore ainda).
        if (!g.freeOrder && single_slot(order[i]) && !slot_enabled(g, order[i])) continue;
        TreeRow row2;
        row2.depth = depth + 1;
        row2.kind = order[i];
        row2.ordinal = nth[i];
        row2.position = static_cast<u32>(i);
        row_values(g, order[i], nth[i], row2);
        out.push_back(std::move(row2));
    }
}

/// Move o item `from` para `to` no armazenamento de um tipo e registra o mapa.
template <typename T>
void rotate_storage(std::vector<T>& v, u32 from, u32 to) {
    if (from == to || from >= v.size() || to >= v.size()) return;
    if (from < to) std::rotate(v.begin() + from, v.begin() + from + 1, v.begin() + to + 1);
    else std::rotate(v.begin() + to, v.begin() + from, v.begin() + from + 1);
}

/// O item de sempre + os a mais como uma lista só (para girar/apagar) e de volta.
template <typename T>
std::vector<T> joined(const T& first, const std::vector<T>& more) {
    std::vector<T> all;
    all.reserve(more.size() + 1);
    all.push_back(first);
    all.insert(all.end(), more.begin(), more.end());
    return all;
}
template <typename T>
void split(std::vector<T>&& all, T& first, std::vector<T>& more) {
    if (all.empty()) return;
    first = std::move(all.front());
    more.assign(std::make_move_iterator(all.begin() + 1), std::make_move_iterator(all.end()));
}

void move_storage(VectorGroup& g, VectorItemKind kind, u32 from, u32 to) {
    switch (kind) {
        case VectorItemKind::Path: rotate_storage(g.paths, from, to); break;
        case VectorItemKind::Modifier: rotate_storage(g.modifiers, from, to); break;
        case VectorItemKind::Group: rotate_storage(g.children, from, to); break;
        case VectorItemKind::Fill: { auto all = joined(g.fill, g.moreFills); rotate_storage(all, from, to); split(std::move(all), g.fill, g.moreFills); break; }
        case VectorItemKind::Stroke: { auto all = joined(g.stroke, g.moreStrokes); rotate_storage(all, from, to); split(std::move(all), g.stroke, g.moreStrokes); break; }
        case VectorItemKind::Trim: { auto all = joined(g.trim, g.moreTrims); rotate_storage(all, from, to); split(std::move(all), g.trim, g.moreTrims); break; }
        case VectorItemKind::Repeater: { auto all = joined(g.repeater, g.moreRepeaters); rotate_storage(all, from, to); split(std::move(all), g.repeater, g.moreRepeaters); break; }
        case VectorItemKind::Merge: { auto all = joined(g.merge, g.moreMerges); rotate_storage(all, from, to); split(std::move(all), g.merge, g.moreMerges); break; }
        default: break;
    }
}

void fill_remap(TreeRemap* remap, VectorItemKind kind, u32 count, u32 from, u32 to, bool removed) {
    if (!remap) return;
    remap->kind = kind;
    remap->map.resize(count);
    for (u32 i = 0; i < count; ++i) remap->map[i] = i;
    if (removed) {
        for (u32 i = 0; i < count; ++i) remap->map[i] = i == from ? kInvalidIndex : (i > from ? i - 1 : i);
        remap->changed = true;
        return;
    }
    if (from == to) return;
    for (u32 i = 0; i < count; ++i) {
        if (i == from) remap->map[i] = to;
        else if (from < to && i > from && i <= to) remap->map[i] = i - 1;
        else if (from > to && i >= to && i < from) remap->map[i] = i + 1;
    }
    remap->changed = true;
}

} // namespace

void list_tree(const VectorData& data, std::vector<TreeRow>& out) {
    out.clear();
    for (usize gi = 0; gi < data.groups.size(); ++gi) group_row(data.groups[gi], 0, static_cast<u32>(gi), static_cast<u32>(gi), out);
}

VectorGroup* group_at(VectorData& data, const u32* path, u32 depth) noexcept {
    if (!path || depth == 0 || depth > kMaxDepth || path[0] >= data.groups.size()) return nullptr;
    VectorGroup* g = &data.groups[path[0]];
    for (u32 d = 1; d < depth; ++d) {
        if (path[d] >= g->children.size()) return nullptr;
        g = &g->children[path[d]];
    }
    return g;
}

const VectorGroup* group_at(const VectorData& data, const u32* path, u32 depth) noexcept {
    return group_at(const_cast<VectorData&>(data), path, depth);
}

bool add_item(VectorGroup& g, VectorItemKind kind, u32 position, u32 variant, const std::string& name, TreeRemap* remap) {
    if (static_cast<u32>(kind) >= kKinds) return false;
    materialize_order(g);
    const u32 pos = std::min<u32>(position, static_cast<u32>(g.order.size()));
    // O n-ésimo do tipo que a entrada nova vai ser.
    u32 ordinal = 0;
    for (u32 i = 0; i < pos; ++i) if (g.order[i] == kind) ++ordinal;
    u32 listed = 0;
    for (VectorItemKind k : g.order) if (k == kind) ++listed;
    u32 stored = 0;   // onde o item novo nasce no armazenamento
    switch (kind) {
        case VectorItemKind::Path: {
            if (g.paths.size() >= 4096) return false;
            VectorPath p;
            p.kind = static_cast<VectorPathKind>(std::min<u32>(variant, 4));
            if (p.kind == VectorPathKind::Free) p.path = make_rect(p.center, p.size, 0.0f);
            g.paths.push_back(std::move(p));
            stored = static_cast<u32>(g.paths.size() - 1);
            break;
        }
        case VectorItemKind::Modifier: {
            if (g.modifiers.size() >= kVecMaxModifiers || variant >= static_cast<u32>(VectorModifierType::Count)) return false;
            g.modifiers.push_back(make_modifier(static_cast<VectorModifierType>(variant)));
            stored = static_cast<u32>(g.modifiers.size() - 1);
            break;
        }
        case VectorItemKind::Group: {
            if (g.children.size() >= 256) return false;
            VectorGroup child;
            child.name = name.empty() ? std::string("Grupo") : name;
            child.fill.enabled = false;
            child.stroke.enabled = false;
            child.freeOrder = true;   // nasce vazio, na ordem livre
            g.children.push_back(std::move(child));
            stored = static_cast<u32>(g.children.size() - 1);
            break;
        }
        case VectorItemKind::Fill: {
            if (listed == 0) { g.fill = VectorFill{}; g.fill.enabled = true; stored = 0; break; }
            if (g.moreFills.size() >= 64) return false;
            g.moreFills.push_back(VectorFill{});
            stored = static_cast<u32>(g.moreFills.size());
            break;
        }
        case VectorItemKind::Stroke: {
            VectorStroke s;
            s.enabled = true;
            if (listed == 0) { g.stroke = s; stored = 0; break; }
            if (g.moreStrokes.size() >= 64) return false;
            g.moreStrokes.push_back(s);
            stored = static_cast<u32>(g.moreStrokes.size());
            break;
        }
        case VectorItemKind::Trim: {
            VectorTrim t;
            t.enabled = true;
            if (listed == 0) { g.trim = t; stored = 0; break; }
            if (g.moreTrims.size() >= 64) return false;
            g.moreTrims.push_back(t);
            stored = static_cast<u32>(g.moreTrims.size());
            break;
        }
        case VectorItemKind::Repeater: {
            VectorRepeater r;
            r.enabled = true;
            if (listed == 0) { g.repeater = r; stored = 0; break; }
            if (g.moreRepeaters.size() >= 16) return false;
            g.moreRepeaters.push_back(r);
            stored = static_cast<u32>(g.moreRepeaters.size());
            break;
        }
        case VectorItemKind::Merge: {
            if (listed == 0) { g.merge = 1; stored = 0; break; }
            if (g.moreMerges.size() >= 64) return false;
            g.moreMerges.push_back(1);
            stored = static_cast<u32>(g.moreMerges.size());
            break;
        }
        default: return false;
    }
    g.order.insert(g.order.begin() + pos, kind);
    // A entrada nova é a `ordinal`-ésima do tipo: o item sobe até lá.
    if (stored != ordinal) move_storage(g, kind, stored, ordinal);
    fill_remap(remap, kind, item_count(g, kind), stored, ordinal, false);
    if (remap && stored != ordinal) {
        // O mapa fala dos itens ANTIGOS: o novo (que nasceu em `stored`) não tem origem.
        remap->map.erase(remap->map.begin() + stored);
    } else if (remap && !remap->map.empty()) {
        remap->map.pop_back();
    }
    return true;
}

bool remove_item(VectorGroup& g, u32 position, TreeRemap* remap) {
    materialize_order(g);
    if (position >= g.order.size()) return false;
    const VectorItemKind kind = g.order[position];
    u32 ordinal = 0;
    for (u32 i = 0; i < position; ++i) if (g.order[i] == kind) ++ordinal;
    const u32 count = item_count(g, kind);
    if (ordinal >= count) return false;
    switch (kind) {
        case VectorItemKind::Path: g.paths.erase(g.paths.begin() + ordinal); break;
        case VectorItemKind::Modifier: g.modifiers.erase(g.modifiers.begin() + ordinal); break;
        case VectorItemKind::Group: g.children.erase(g.children.begin() + ordinal); break;
        case VectorItemKind::Fill: {
            if (g.moreFills.empty()) { g.fill.enabled = false; break; }
            auto all = joined(g.fill, g.moreFills);
            all.erase(all.begin() + ordinal);
            split(std::move(all), g.fill, g.moreFills);
            break;
        }
        case VectorItemKind::Stroke: {
            if (g.moreStrokes.empty()) { g.stroke.enabled = false; break; }
            auto all = joined(g.stroke, g.moreStrokes);
            all.erase(all.begin() + ordinal);
            split(std::move(all), g.stroke, g.moreStrokes);
            break;
        }
        case VectorItemKind::Trim: {
            if (g.moreTrims.empty()) { g.trim.enabled = false; break; }
            auto all = joined(g.trim, g.moreTrims);
            all.erase(all.begin() + ordinal);
            split(std::move(all), g.trim, g.moreTrims);
            break;
        }
        case VectorItemKind::Repeater: {
            if (g.moreRepeaters.empty()) { g.repeater.enabled = false; break; }
            auto all = joined(g.repeater, g.moreRepeaters);
            all.erase(all.begin() + ordinal);
            split(std::move(all), g.repeater, g.moreRepeaters);
            break;
        }
        case VectorItemKind::Merge: {
            if (g.moreMerges.empty()) { g.merge = 0; break; }
            auto all = joined(g.merge, g.moreMerges);
            all.erase(all.begin() + ordinal);
            split(std::move(all), g.merge, g.moreMerges);
            break;
        }
        default: return false;
    }
    g.order.erase(g.order.begin() + position);
    fill_remap(remap, kind, count, ordinal, ordinal, true);
    return true;
}

bool move_item(VectorGroup& g, u32 from, u32 to, TreeRemap* remap) {
    materialize_order(g);
    if (from >= g.order.size()) return false;
    to = std::min<u32>(to, static_cast<u32>(g.order.size() - 1));
    const VectorItemKind kind = g.order[from];
    u32 before = 0;
    for (u32 i = 0; i < from; ++i) if (g.order[i] == kind) ++before;
    rotate_storage(g.order, from, to);
    u32 after = 0;
    for (u32 i = 0; i < to; ++i) if (g.order[i] == kind) ++after;
    if (before != after) move_storage(g, kind, before, after);
    fill_remap(remap, kind, item_count(g, kind), before, after, false);
    return true;
}

bool set_item_enabled(VectorGroup& g, u32 position, bool enabled) {
    materialize_order(g);
    if (position >= g.order.size()) return false;
    const VectorItemKind kind = g.order[position];
    u32 n = 0;
    for (u32 i = 0; i < position; ++i) if (g.order[i] == kind) ++n;
    if (n >= item_count(g, kind)) return false;
    switch (kind) {
        case VectorItemKind::Fill: fill_at(g, n).enabled = enabled; return true;
        case VectorItemKind::Stroke: stroke_at(g, n).enabled = enabled; return true;
        case VectorItemKind::Trim: trim_at(g, n).enabled = enabled; return true;
        case VectorItemKind::Repeater: repeater_at(g, n).enabled = enabled; return true;
        case VectorItemKind::Modifier: g.modifiers[n].enabled = enabled; return true;
        case VectorItemKind::Group: g.children[n].visible = enabled; return true;
        case VectorItemKind::Merge: if (!enabled) merge_at(g, n) = 0; else if (merge_at(g, n) == 0) merge_at(g, n) = 1; return true;
        default: return false;   // caminho não tem liga/desliga
    }
}

bool set_item_values(VectorGroup& g, u32 position, const f32* values, u32 count) {
    if (!values) return false;
    for (u32 i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
    materialize_order(g);
    if (position >= g.order.size()) return false;
    const VectorItemKind kind = g.order[position];
    u32 n = 0;
    for (u32 i = 0; i < position; ++i) if (g.order[i] == kind) ++n;
    if (n >= item_count(g, kind)) return false;
    const auto v = [&](u32 i, f32 fallback) { return i < count ? values[i] : fallback; };
    switch (kind) {
        case VectorItemKind::Path: {
            VectorPath& p = g.paths[n];
            // O tipo não muda aqui (trocar de paramétrico para livre é outro comando).
            p.center = Vec2{v(1, p.center.x), v(2, p.center.y)};
            p.size = Vec2{std::max(0.0f, v(3, p.size.x)), std::max(0.0f, v(4, p.size.y))};
            p.roundness = std::max(0.0f, v(5, p.roundness));
            p.points = std::clamp(v(6, p.points), 3.0f, 100.0f);
            p.outerRadius = std::max(0.0f, v(7, p.outerRadius));
            p.innerRadius = std::max(0.0f, v(8, p.innerRadius));
            p.rotation = v(9, p.rotation);
            return true;
        }
        case VectorItemKind::Fill: {
            VectorFill& f = fill_at(g, n);
            f.paint.color = Vec4{std::clamp(v(0, f.paint.color.x), 0.0f, 1.0f), std::clamp(v(1, f.paint.color.y), 0.0f, 1.0f),
                                 std::clamp(v(2, f.paint.color.z), 0.0f, 1.0f), f.paint.color.w};
            f.paint.opacity = std::clamp(v(3, f.paint.opacity), 0.0f, 100.0f);
            f.rule = v(4, static_cast<f32>(f.rule)) > 0.5f ? 1 : 0;
            return true;
        }
        case VectorItemKind::Stroke: {
            VectorStroke& s = stroke_at(g, n);
            s.paint.color = Vec4{std::clamp(v(0, s.paint.color.x), 0.0f, 1.0f), std::clamp(v(1, s.paint.color.y), 0.0f, 1.0f),
                                 std::clamp(v(2, s.paint.color.z), 0.0f, 1.0f), s.paint.color.w};
            s.paint.opacity = std::clamp(v(3, s.paint.opacity), 0.0f, 100.0f);
            s.width = std::clamp(v(4, s.width), 0.0f, 10000.0f);
            s.cap = static_cast<u8>(std::clamp(v(5, static_cast<f32>(s.cap)), 0.0f, 2.0f));
            s.join = static_cast<u8>(std::clamp(v(6, static_cast<f32>(s.join)), 0.0f, 2.0f));
            s.miterLimit = std::clamp(v(7, s.miterLimit), 1.0f, 100.0f);
            return true;
        }
        case VectorItemKind::Trim: {
            VectorTrim& t = trim_at(g, n);
            t.start = std::clamp(v(0, t.start), 0.0f, 100.0f);
            t.end = std::clamp(v(1, t.end), 0.0f, 100.0f);
            t.offset = v(2, t.offset);
            t.mode = v(3, static_cast<f32>(t.mode)) > 0.5f ? 1 : 0;
            return true;
        }
        case VectorItemKind::Repeater: {
            VectorRepeater& r = repeater_at(g, n);
            r.copies = std::clamp(v(0, r.copies), 0.0f, 500.0f);
            r.offset = v(1, r.offset);
            r.position = Vec2{v(2, r.position.x), v(3, r.position.y)};
            const f32 sx = v(4, r.scale), sy = v(5, r.scaleLinked ? r.scale : r.scaleY);
            r.scale = sx;
            r.scaleY = sy;
            r.scaleLinked = std::fabs(sx - sy) < 1e-4f ? 1 : 0;
            r.rotation = v(6, r.rotation);
            r.startOpacity = std::clamp(v(7, r.startOpacity), 0.0f, 100.0f);
            r.endOpacity = std::clamp(v(8, r.endOpacity), 0.0f, 100.0f);
            r.above = v(9, static_cast<f32>(r.above)) > 0.5f ? 1 : 0;
            r.anchor = Vec2{v(10, r.anchor.x), v(11, r.anchor.y)};
            return true;
        }
        case VectorItemKind::Merge: {
            merge_at(g, n) = static_cast<u8>(std::clamp(v(0, static_cast<f32>(merge_at(g, n))), 0.0f, 4.0f));
            return true;
        }
        case VectorItemKind::Modifier: {
            VectorModifier& m = g.modifiers[n];
            for (u32 i = 0; i < kVecModParams; ++i) m.params[i] = clamp_modifier_param(m.type, i, v(i + 1, m.params[i]));
            return true;
        }
        case VectorItemKind::Group: {
            VectorGroup& c = g.children[n];
            c.position = Vec2{v(0, c.position.x), v(1, c.position.y)};
            c.anchor = Vec2{v(2, c.anchor.x), v(3, c.anchor.y)};
            c.scale = Vec2{v(4, c.scale.x), v(5, c.scale.y)};
            c.rotation = v(6, c.rotation);
            c.opacity = std::clamp(v(7, c.opacity), 0.0f, 100.0f);
            c.skew = std::clamp(v(8, c.skew), -85.0f, 85.0f);
            c.skewAxis = v(9, c.skewAxis);
            return true;
        }
        default: return false;
    }
}

} // namespace aurea::vector
