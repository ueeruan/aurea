// =============================================================================
//  Lote X do AE (2026-11-12, gap_system item 26): o conteúdo do grupo vetorial
//  em ORDEM LIVRE (vector/VectorTree.hpp) — operador vale para o que está
//  acima, vários Preencher/Contorno/Aparar/Repetidor por grupo, grupos dentro
//  de grupos, inclinação do grupo — e a migração: o grupo de sempre escrito
//  como lista dá a geometria antiga exata. O AE é só a especificação.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/animation/Curve.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/vector/Vector.hpp"
#include "aurea/vector/VectorTree.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

namespace {

VectorPath rect_path(Vec2 center, Vec2 size, f32 round = 0.0f) {
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.center = center;
    p.size = size;
    p.roundness = round;
    return p;
}

VectorPath ellipse_path(Vec2 center, Vec2 size) {
    VectorPath p;
    p.kind = VectorPathKind::Ellipse;
    p.center = center;
    p.size = size;
    return p;
}

VectorPath star_path(Vec2 center) {
    VectorPath p;
    p.kind = VectorPathKind::Star;
    p.center = center;
    p.points = 5.0f;
    p.outerRadius = 80.0f;
    p.innerRadius = 35.0f;
    return p;
}

/// Os grupos "de sempre" que cobrem a cadeia inteira (as mesmas combinações
/// dos documentos gravados: preencher, contorno, tracejado, afinar, aparar,
/// mesclar, repetidor com escala e opacidade, operadores, furo, degradê).
std::vector<VectorGroup> classic_fixtures() {
    std::vector<VectorGroup> out;
    {   // 0 · retângulo arredondado, preenchimento + contorno
        VectorGroup g;
        g.paths.push_back(rect_path({120, 90}, {160, 100}, 18));
        g.fill.paint.color = Vec4{0.9f, 0.2f, 0.1f, 1};
        g.stroke.enabled = true;
        g.stroke.width = 9;
        g.stroke.paint.color = Vec4{0, 0, 0, 1};
        g.position = Vec2{30, 20};
        g.rotation = 12;
        g.scale = Vec2{110, 90};
        g.opacity = 80;
        out.push_back(g);
    }
    {   // 1 · estrela aparada com tracejado e afinar
        VectorGroup g;
        g.paths.push_back(star_path({200, 150}));
        g.fill.enabled = false;
        g.stroke.enabled = true;
        g.stroke.width = 6;
        g.stroke.cap = 1;
        g.stroke.join = 1;
        g.stroke.dashes = {14, 6};
        g.stroke.dashOffset = 3;
        g.stroke.taperStart = 20;
        g.stroke.taperEnd = 30;
        g.trim.enabled = true;
        g.trim.start = 10;
        g.trim.end = 75;
        g.trim.offset = 20;
        out.push_back(g);
    }
    {   // 2 · dois caminhos mesclados (subtrair), par-ímpar, degradê
        VectorGroup g;
        g.paths.push_back(rect_path({150, 150}, {180, 120}));
        g.paths.push_back(ellipse_path({200, 150}, {120, 120}));
        g.merge = 2;
        g.fill.rule = 1;
        g.fill.paint.type = 1;
        g.fill.paint.start = Vec2{60, 150};
        g.fill.paint.end = Vec2{240, 150};
        g.fill.paint.stops = {VectorStop{0, Vec4{1, 0, 0, 1}}, VectorStop{1, Vec4{0, 0, 1, 1}}};
        g.stroke.enabled = true;
        g.stroke.width = 4;
        out.push_back(g);
    }
    {   // 3 · repetidor com escala, giro e opacidade, cópias embaixo
        VectorGroup g;
        g.paths.push_back(ellipse_path({60, 60}, {70, 50}));
        g.stroke.enabled = true;
        g.stroke.width = 3;
        g.repeater.enabled = true;
        g.repeater.copies = 4.5f;
        g.repeater.position = Vec2{55, 12};
        g.repeater.scale = 115;
        g.repeater.rotation = 9;
        g.repeater.startOpacity = 100;
        g.repeater.endOpacity = 30;
        g.repeater.offset = 0.5f;
        out.push_back(g);
    }
    {   // 4 · repetidor por cima, escala X/Y separada, trim sequencial em dois caminhos
        VectorGroup g;
        g.paths.push_back(rect_path({40, 40}, {50, 50}));
        g.paths.push_back(rect_path({110, 40}, {40, 60}));
        g.trim.enabled = true;
        g.trim.end = 60;
        g.trim.mode = 1;
        g.fill.enabled = false;
        g.stroke.enabled = true;
        g.stroke.width = 5;
        g.repeater.enabled = true;
        g.repeater.copies = 3;
        g.repeater.position = Vec2{0, 70};
        g.repeater.scale = 90;
        g.repeater.scaleY = 120;
        g.repeater.scaleLinked = 0;
        g.repeater.above = 1;
        out.push_back(g);
    }
    {   // 5 · operadores de caminho em ordem (arredondar cantos, depois zigue-zague) + furo
        VectorGroup g;
        g.paths.push_back(rect_path({150, 100}, {200, 120}));
        VectorPath hole = ellipse_path({150, 100}, {60, 60});
        hole.exclude = true;
        g.paths.push_back(hole);
        VectorModifier round = make_modifier(VectorModifierType::RoundCorners);
        round.params[kVecModRoundRadius] = 20;
        VectorModifier zig = make_modifier(VectorModifierType::ZigZag);
        zig.params[kVecModZigSize] = 6;
        zig.params[kVecModZigRidges] = 4;
        g.modifiers = {round, zig};
        g.stroke.enabled = true;
        g.stroke.width = 2;
        out.push_back(g);
    }
    {   // 6 · só preenchimento, caminho livre aberto e fechado juntos
        VectorGroup g;
        VectorPath open;
        open.kind = VectorPathKind::Free;
        open.path.closed = false;
        open.path.v = {BezierVertex{{10, 10}, {}, {30, 0}}, BezierVertex{{90, 60}, {-20, -20}, {20, 20}}, BezierVertex{{30, 120}, {30, 0}, {}}};
        g.paths.push_back(open);
        g.paths.push_back(rect_path({160, 60}, {60, 60}));
        out.push_back(g);
    }
    return out;
}

struct Vtx {
    Vec2 p, grad;
    f32 d, alpha;
    std::vector<f32> paint;   // o bloco da tinta do vértice (12 vec4)
};

std::vector<Vtx> vertices(const VectorMesh& m) {
    std::vector<Vtx> out;
    const u32 n = m.vertex_count();
    out.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        const Vec4 a = m.verts[i * 2], b = m.verts[i * 2 + 1];
        Vtx v;
        v.p = Vec2{a.x, a.y};
        v.grad = Vec2{a.z, a.w};
        v.d = b.x;
        v.alpha = b.z;
        const usize paint = static_cast<usize>(b.y) * kPaintVec4;
        for (usize k = 0; k < kPaintVec4 && paint + k < m.paints.size(); ++k) {
            const Vec4 q = m.paints[paint + k];
            v.paint.insert(v.paint.end(), {q.x, q.y, q.z, q.w});
        }
        out.push_back(std::move(v));
    }
    return out;
}

/// Compara as duas malhas vértice a vértice (a tinta pelo CONTEÚDO: o índice
/// dela pode mudar de lugar). Devolve a maior diferença de posição.
f32 compare_meshes(const VectorMesh& a, const VectorMesh& b, bool& same) {
    same = a.vertex_count() == b.vertex_count();
    if (!same) return 1e30f;
    const std::vector<Vtx> va = vertices(a), vb = vertices(b);
    f32 worst = 0.0f;
    for (usize i = 0; i < va.size(); ++i) {
        worst = std::max({worst, std::fabs(va[i].p.x - vb[i].p.x), std::fabs(va[i].p.y - vb[i].p.y),
                          std::fabs(va[i].grad.x - vb[i].grad.x), std::fabs(va[i].grad.y - vb[i].grad.y)});
        if (std::fabs(va[i].d - vb[i].d) > 1e-5f || std::fabs(va[i].alpha - vb[i].alpha) > 1e-6f || va[i].paint != vb[i].paint) same = false;
    }
    if (worst > 1e-5f) same = false;
    return worst;
}

Vec2 ring_min(const std::vector<Contour>& rings, const Affine2& m) {
    Vec2 mn{1e30f, 1e30f};
    for (const Contour& c : rings) for (Vec2 p : c.pts) { const Vec2 t = m.apply(p); mn = Vec2{std::min(mn.x, t.x), std::min(mn.y, t.y)}; }
    return mn;
}
Vec2 ring_max(const std::vector<Contour>& rings, const Affine2& m) {
    Vec2 mx{-1e30f, -1e30f};
    for (const Contour& c : rings) for (Vec2 p : c.pts) { const Vec2 t = m.apply(p); mx = Vec2{std::max(mx.x, t.x), std::max(mx.y, t.y)}; }
    return mx;
}

VectorGroup free_group(std::vector<VectorItemKind> order) {
    VectorGroup g;
    g.fill.enabled = false;
    g.stroke.enabled = false;
    g.freeOrder = true;
    g.order = std::move(order);
    return g;
}

struct TreeRig {
    Engine e;
    u64 layer = 0;
    TreeRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
        const auto id = e.add_vector_layer(1);
        AUREA_CHECK(id.ok());
        if (id.ok()) layer = *id;
    }
    ~TreeRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L() { return comp()->layer(LayerId::unpack(layer)); }
    VectorData& data() { return L()->shape.vector; }
    bool edit(u32 op, u32 a = 0, u32 b = 0, u32 c = 0, const std::vector<f32>& values = {}, const char* name = nullptr, std::vector<u32> path = {0}) {
        return e.vector_tree_edit(layer, path.data(), static_cast<u32>(path.size()), op, a, b, c, values.empty() ? nullptr : values.data(),
                                  static_cast<u32>(values.size()), name, false);
    }
    bool undo() {
        Command u;
        u.type = CommandType::Undo;
        return e.apply_command(u).ok();
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
};

constexpr VectorItemKind P = VectorItemKind::Path, F = VectorItemKind::Fill, S = VectorItemKind::Stroke, T = VectorItemKind::Trim,
                         R = VectorItemKind::Repeater, M = VectorItemKind::Merge, O = VectorItemKind::Modifier, G = VectorItemKind::Group;

} // namespace

// -----------------------------------------------------------------------------
// Migração: o grupo de sempre escrito como lista dá a MESMA geometria
// -----------------------------------------------------------------------------
AUREA_TEST(VectorTree, ClassicGroupsWrittenAsAnOrderedListKeepTheExactGeometry) {
    const std::vector<VectorGroup> fixtures = classic_fixtures();
    f32 worst = 0.0f;
    u32 verts = 0;
    for (usize i = 0; i < fixtures.size(); ++i) {
        for (f32 density : {1.0f, 2.5f}) {
            VectorMesh before, after;
            build_mesh({fixtures[i]}, 0.0, density, before);
            VectorGroup listed = fixtures[i];
            materialize_order(listed);
            AUREA_CHECK(listed.freeOrder && !listed.order.empty());
            AUREA_CHECK(group_has_tree(listed));
            build_mesh({listed}, 0.0, density, after);
            bool same = false;
            const f32 d = compare_meshes(before, after, same);
            if (!same) std::printf("[grupo %zu densidade %.1f: %u x %u vertices, dif %.6f] ", i, density, before.vertex_count(), after.vertex_count(), d);
            AUREA_CHECK(same);
            AUREA_CHECK(before.vertex_count() > 0);
            if (same) worst = std::max(worst, d);
            verts += before.vertex_count();
            // A caixa também.
            Vec2 mn0, mx0, mn1, mx1;
            const bool b0 = bounds_of({fixtures[i]}, 0.0, mn0, mx0), b1 = bounds_of({listed}, 0.0, mn1, mx1);
            AUREA_CHECK(b0 && b1);
            // A caixa de sempre é a dos pontos de controle com folga; a da árvore é a da tinta: cabe dentro.
            AUREA_CHECK(mn1.x >= mn0.x - 1.0f && mn1.y >= mn0.y - 1.0f && mx1.x <= mx0.x + 1.0f && mx1.y <= mx0.y + 1.0f);
        }
    }
    // Todos juntos, na mesma camada (a ordem entre grupos não muda).
    std::vector<VectorGroup> listed = fixtures;
    for (VectorGroup& g : listed) materialize_order(g);
    VectorMesh before, after;
    build_mesh(fixtures, 0.0, 1.0f, before);
    build_mesh(listed, 0.0, 1.0f, after);
    bool same = false;
    (void)compare_meshes(before, after, same);
    AUREA_CHECK(same);
    // A chave de conteúdo muda (a árvore entra nela), e é estável.
    AUREA_CHECK(content_hash(fixtures, 0.0) != content_hash(listed, 0.0));
    AUREA_CHECK_EQ(content_hash(listed, 0.0), content_hash(listed, 0.0));
    std::printf("%zu grupos de sempre x 2 densidades: %u vertices iguais (maior dif %.7f px); ", fixtures.size(), verts, worst);
}

AUREA_TEST(VectorTree, FlatDocumentStaysVersion6AndTheTreeTravelsApart) {
    VectorData d;
    d.groups = classic_fixtures();
    std::vector<f32> doc0, tree0;
    std::string names0;
    encode_document(d, doc0, names0);
    AUREA_CHECK_NEAR(doc0[0], 6.0f, 1e-6f);
    AUREA_CHECK(!data_has_tree(d));
    // Com a árvore: o documento de sempre continua na versão 6 (os painéis do
    // celular o leem) e com os MESMOS bytes do primeiro item de cada tipo.
    VectorData t = d;
    materialize_order(t.groups[0]);
    AUREA_CHECK(add_item(t.groups[0], VectorItemKind::Fill, 0, 0, ""));
    AUREA_CHECK(add_item(t.groups[0], VectorItemKind::Group, 0, 0, "Dentro"));
    AUREA_CHECK(add_item(t.groups[0].children[0], VectorItemKind::Path, 0, static_cast<u32>(VectorPathKind::Ellipse), ""));
    AUREA_CHECK(add_item(t.groups[0].children[0], VectorItemKind::Stroke, 99, 0, ""));
    t.groups[0].children[0].skew = 15;
    t.groups[0].children[0].skewAxis = 30;
    t.groups[1].skew = -10;
    AUREA_CHECK(data_has_tree(t));
    std::vector<f32> doc1;
    std::string names1;
    encode_document(t, doc1, names1);
    AUREA_CHECK_NEAR(doc1[0], 6.0f, 1e-6f);
    AUREA_CHECK(names1 == names0);
    // O documento de sempre + a árvore à parte reconstroem tudo.
    std::vector<f32> tree;
    encode_tree(t, tree);
    AUREA_CHECK(!tree.empty());
    VectorData back;
    AUREA_CHECK(decode_document(doc1.data(), doc1.size(), names1, back));
    AUREA_CHECK(!data_has_tree(back));
    AUREA_CHECK(decode_tree(tree.data(), tree.size(), back));
    AUREA_CHECK(back == t);
    // Fluxo cortado ou de outra camada: nada muda.
    VectorData other = d;
    AUREA_CHECK(!decode_tree(tree.data(), tree.size() / 2, other));
    AUREA_CHECK(other == d);
    other.groups.pop_back();
    const VectorData shorter = other;
    AUREA_CHECK(!decode_tree(tree.data(), tree.size(), other));
    AUREA_CHECK(other == shorter);
    std::printf("documento v6 intacto (%zu floats), arvore a parte (%zu floats); ", doc1.size(), tree.size());
}

// -----------------------------------------------------------------------------
// Ordem livre: quem está acima de quem
// -----------------------------------------------------------------------------
AUREA_TEST(VectorTree, PaintOrderFollowsTheListAndOperatorsOnlyReachWhatIsAbove) {
    // Preencher acima do Contorno: o preenchimento é desenhado por cima.
    VectorGroup g = free_group({P, F, S});
    g.paths.push_back(rect_path({0, 0}, {100, 100}));
    g.fill.enabled = true;
    g.fill.paint.color = Vec4{1, 0, 0, 1};
    g.stroke.enabled = true;
    g.stroke.width = 10;
    g.stroke.paint.color = Vec4{0, 0, 1, 1};
    TreeResult r;
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 2u);
    if (r.layers.size() != 2) return;
    // layers[0] fica por baixo: o contorno (azul); por cima, o preenchimento (vermelho).
    AUREA_CHECK_NEAR(r.paints[r.layers[0].paint].color.z, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.paints[r.layers[1].paint].color.x, 1.0f, 1e-6f);
    g.order = {P, S, F};
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_NEAR(r.paints[r.layers[0].paint].color.x, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.paints[r.layers[1].paint].color.z, 1.0f, 1e-6f);

    // Aparar ABAIXO do Preencher e ACIMA do Contorno: o preenchimento sai
    // inteiro, o contorno só na metade do caminho.
    g.order = {P, F, T, S};
    g.trim.enabled = true;
    g.trim.start = 0;
    g.trim.end = 50;
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 2u);
    if (r.layers.size() != 2) return;
    const f64 fillArea = area_of(r.layers[1].rings), strokeArea = area_of(r.layers[0].rings);
    AUREA_CHECK_NEAR(fillArea, 100.0 * 100.0, 1.0);
    // Metade do perímetro (200 px) × 10 de largura = 2000, mais as pontas/cantos.
    AUREA_CHECK(strokeArea > 1900.0 && strokeArea < 2300.0);
    // Aparar ACIMA dos dois: os dois veem o caminho aparado (o preenchimento fecha a metade).
    g.order = {P, T, F, S};
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 2u);
    if (r.layers.size() == 2) AUREA_CHECK(std::fabs(area_of(r.layers[1].rings)) < 100.0 * 100.0 * 0.6);
    // Um caminho ABAIXO do Preencher não é pintado por ele.
    g.trim.enabled = false;
    g.stroke.enabled = false;   // (ligado e fora da lista ele voltaria ao lugar de sempre)
    g.order = {P, F, P};
    g.paths.push_back(rect_path({300, 0}, {50, 50}));
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 1u);
    if (!r.layers.empty()) AUREA_CHECK_NEAR(area_of(r.layers[0].rings), 100.0 * 100.0, 1.0);
    std::printf("preencher/contorno na ordem da lista; aparar so alcanca o que esta acima; ");
}

AUREA_TEST(VectorTree, SeveralFillsStrokesTrimsAndMergesInOneGroup) {
    // Dois contornos (um largo embaixo, um fino em cima) e dois preenchimentos.
    VectorGroup g = free_group({P, S, S, F, F});
    g.paths.push_back(ellipse_path({0, 0}, {100, 100}));
    g.stroke.enabled = true;
    g.stroke.width = 2;
    g.stroke.paint.color = Vec4{1, 1, 1, 1};
    VectorStroke wide;
    wide.enabled = true;
    wide.width = 20;
    wide.paint.color = Vec4{0, 0, 0, 1};
    g.moreStrokes.push_back(wide);
    g.fill.enabled = true;
    g.fill.paint.color = Vec4{1, 0, 0, 1};
    VectorFill second;
    second.paint.color = Vec4{0, 1, 0, 1};
    second.paint.opacity = 50;
    g.moreFills.push_back(second);
    TreeResult r;
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 4u);
    if (r.layers.size() != 4) return;
    // De baixo para cima: preencher 2 (verde), preencher 1 (vermelho), contorno largo, contorno fino.
    AUREA_CHECK_NEAR(r.paints[r.layers[0].paint].color.y, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.paints[r.layers[0].paint].opacity, 50.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.paints[r.layers[1].paint].color.x, 1.0f, 1e-6f);
    AUREA_CHECK(std::fabs(area_of(r.layers[2].rings)) > std::fabs(area_of(r.layers[3].rings)) * 5.0);
    AUREA_CHECK_EQ(item_count(g, VectorItemKind::Stroke), 2u);

    // Dois Aparar em sequência: 0..50 % e depois 50..100 % do que sobrou = o 2º quarto.
    VectorGroup t = free_group({P, T, T, S});
    t.paths.push_back(rect_path({0, 0}, {100, 100}));
    t.stroke.enabled = true;
    t.stroke.width = 4;
    t.trim.enabled = true;
    t.trim.end = 50;
    VectorTrim half;
    half.enabled = true;
    half.start = 50;
    t.moreTrims.push_back(half);
    evaluate_tree(t, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 1u);
    if (!r.layers.empty()) {
        const f64 a = std::fabs(area_of(r.layers[0].rings));
        AUREA_CHECK(a > 100.0 * 4.0 * 0.9 && a < 100.0 * 4.0 * 1.25);   // um quarto do perímetro (100 px) × 4
    }
    // Mesclar entre caminhos e, depois dele, mais um caminho que NÃO entra na conta.
    VectorGroup m = free_group({P, P, M, P, F});
    m.paths.push_back(rect_path({0, 0}, {100, 100}));
    m.paths.push_back(rect_path({50, 0}, {100, 100}));
    m.paths.push_back(rect_path({400, 0}, {10, 10}));
    m.merge = 3;   // interseção dos dois de cima: 50 × 100
    m.fill.enabled = true;
    evaluate_tree(m, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 1u);
    if (!r.layers.empty()) AUREA_CHECK_NEAR(area_of(r.layers[0].rings), 50.0 * 100.0 + 10.0 * 10.0, 2.0);
    std::printf("2 contornos + 2 preenchimentos, 2 aparar em sequencia, mesclar no meio; ");
}

AUREA_TEST(VectorTree, RepeaterRepeatsEverythingAboveItPathsAndPaint) {
    // Preencher ACIMA do repetidor: uma camada de tinta por cópia, com a
    // opacidade da cópia. Contorno ABAIXO: uma camada só, cobrindo as cópias.
    VectorGroup g = free_group({P, F, R, S});
    g.paths.push_back(rect_path({0, 0}, {40, 40}));
    g.fill.enabled = true;
    g.repeater.enabled = true;
    g.repeater.copies = 3;
    g.repeater.position = Vec2{100, 0};
    g.repeater.startOpacity = 100;
    g.repeater.endOpacity = 0;
    g.stroke.enabled = true;
    g.stroke.width = 2;
    TreeResult r;
    evaluate_tree(g, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 4u);
    if (r.layers.size() != 4) return;
    // Em cima (as 3 últimas): os preenchimentos, a cópia 0 por último (por cima).
    AUREA_CHECK_NEAR(r.layers[3].alpha, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.layers[2].alpha, 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(r.layers[1].alpha, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.layers[3].M.tx, 0.0f, 1e-5f);
    AUREA_CHECK_NEAR(r.layers[2].M.tx, 100.0f, 1e-4f);
    AUREA_CHECK_NEAR(r.layers[1].M.tx, 200.0f, 1e-4f);
    // Embaixo: o contorno das três cópias de uma vez (de −21 a 221).
    const Vec2 mn = ring_min(r.layers[0].rings, r.layers[0].M), mx = ring_max(r.layers[0].rings, r.layers[0].M);
    AUREA_CHECK_NEAR(mn.x, -21.0f, 0.1f);
    AUREA_CHECK_NEAR(mx.x, 221.0f, 0.1f);
    AUREA_CHECK_NEAR(r.layers[0].alpha, 1.0f, 1e-6f);
    // Dois repetidores em sequência: 2 × 3 = 6 cópias do preenchimento.
    VectorGroup two = free_group({P, F, R, R});
    two.paths.push_back(rect_path({0, 0}, {10, 10}));
    two.fill.enabled = true;
    two.repeater.enabled = true;
    two.repeater.copies = 2;
    two.repeater.position = Vec2{20, 0};
    VectorRepeater rows;
    rows.enabled = true;
    rows.copies = 3;
    rows.position = Vec2{0, 30};
    two.moreRepeaters.push_back(rows);
    evaluate_tree(two, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 6u);
    f32 maxX = -1e30f, maxY = -1e30f;
    for (const TreePaint& layer : r.layers) { maxX = std::max(maxX, layer.M.tx); maxY = std::max(maxY, layer.M.ty); }
    AUREA_CHECK_NEAR(maxX, 20.0f, 1e-4f);
    AUREA_CHECK_NEAR(maxY, 60.0f, 1e-4f);
    // Zero cópias: nada do que está acima sobra.
    two.repeater.copies = 0;
    evaluate_tree(two, 0.0, 0.05f, r);
    AUREA_CHECK(r.layers.empty());
    std::printf("repetidor no meio: 3 preenchimentos + 1 contorno das copias; 2 repetidores = 6; ");
}

AUREA_TEST(VectorTree, NestedGroupsCarryTheirTransformOpacityAndSkew) {
    // Grupo de dentro com caminho + preenchimento próprio; o grupo de fora tem
    // um contorno ABAIXO do grupo de dentro: ele contorna o caminho de dentro.
    VectorGroup inner = free_group({P, F});
    inner.name = "Dentro";
    inner.paths.push_back(rect_path({0, 0}, {100, 100}));
    inner.fill.enabled = true;
    inner.fill.paint.color = Vec4{0, 1, 0, 1};
    inner.position = Vec2{200, 50};
    inner.scale = Vec2{50, 50};
    inner.opacity = 40;
    VectorGroup outer = free_group({G, S});
    outer.children.push_back(inner);
    outer.stroke.enabled = true;
    outer.stroke.width = 4;
    TreeResult r;
    evaluate_tree(outer, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 2u);
    if (r.layers.size() != 2) return;
    // Por cima: o preenchimento de dentro, com o transform e a opacidade do grupo de dentro.
    const TreePaint& fill = r.layers[1];
    AUREA_CHECK_NEAR(fill.alpha, 0.4f, 1e-6f);
    Vec2 mn = ring_min(fill.rings, fill.M), mx = ring_max(fill.rings, fill.M);
    AUREA_CHECK_NEAR(mn.x, 175.0f, 0.01f);
    AUREA_CHECK_NEAR(mx.x, 225.0f, 0.01f);
    AUREA_CHECK_NEAR(mn.y, 25.0f, 0.01f);
    AUREA_CHECK_NEAR(mx.y, 75.0f, 0.01f);
    // Por baixo: o contorno do grupo de fora em volta do caminho de dentro (já transformado), sem a opacidade de dentro.
    const TreePaint& stroke = r.layers[0];
    AUREA_CHECK_NEAR(stroke.alpha, 1.0f, 1e-6f);
    mn = ring_min(stroke.rings, stroke.M);
    mx = ring_max(stroke.rings, stroke.M);
    AUREA_CHECK_NEAR(mn.x, 173.0f, 0.05f);
    AUREA_CHECK_NEAR(mx.x, 227.0f, 0.05f);
    // Grupo escondido: não contribui com nada.
    outer.children[0].visible = false;
    evaluate_tree(outer, 0.0, 0.05f, r);
    AUREA_CHECK(r.layers.empty());
    outer.children[0].visible = true;
    // Três níveis + repetidor do grupo de fora repetindo o grupo de dentro inteiro.
    VectorGroup mid = free_group({G});
    mid.children.push_back(inner);
    mid.position = Vec2{0, 100};
    VectorGroup top = free_group({G, R});
    top.children.push_back(mid);
    top.repeater.enabled = true;
    top.repeater.copies = 2;
    top.repeater.position = Vec2{300, 0};
    evaluate_tree(top, 0.0, 0.05f, r);
    AUREA_CHECK_EQ(r.layers.size(), 2u);
    if (r.layers.size() == 2) {
        const Vec2 a = ring_min(r.layers[1].rings, r.layers[1].M), b = ring_min(r.layers[0].rings, r.layers[0].M);
        AUREA_CHECK_NEAR(a.x, 175.0f, 0.01f);
        AUREA_CHECK_NEAR(a.y, 125.0f, 0.01f);
        AUREA_CHECK_NEAR(b.x, 475.0f, 0.01f);
    }
    // Inclinação: 45° no eixo 0° leva (0, 10) para x = ∓10 (o sinal é o do AE: positivo inclina para a esquerda embaixo).
    VectorGroup skewed;
    const Affine2 plain = group_matrix(skewed);
    AUREA_CHECK_NEAR(plain.c, 0.0f, 1e-6f);
    skewed.skew = 45;
    const Affine2 k = group_matrix(skewed);
    const Vec2 moved = k.apply(Vec2{0, 10});
    AUREA_CHECK_NEAR(std::fabs(moved.x), 10.0f, 1e-4f);
    AUREA_CHECK_NEAR(moved.y, 10.0f, 1e-4f);
    AUREA_CHECK_NEAR(k.apply(Vec2{10, 0}).y, 0.0f, 1e-5f);
    // Eixo a 90°: a inclinação passa para o outro eixo.
    skewed.skewAxis = 90;
    const Vec2 turned = group_matrix(skewed).apply(Vec2{10, 0});
    AUREA_CHECK_NEAR(std::fabs(turned.y), 10.0f, 1e-3f);
    AUREA_CHECK_NEAR(turned.x, 10.0f, 1e-3f);
    // A malha inteira: o grupo de fora com filho desenha (build_mesh passa pela árvore).
    VectorMesh mesh;
    build_mesh({outer}, 0.0, 1.0f, mesh);
    AUREA_CHECK(mesh.vertex_count() > 0);
    AUREA_CHECK(mesh.min.x < 174.0f && mesh.max.x > 226.0f);
    std::printf("grupo de dentro com transform/opacidade; contorno de fora pega o caminho de dentro; inclinacao; ");
}

AUREA_TEST(VectorTree, OperatorPositionChangesTheResult) {
    // Arredondar cantos ACIMA do preenchimento arredonda; ABAIXO, não alcança.
    VectorGroup g = free_group({P, O, F});
    g.paths.push_back(rect_path({0, 0}, {100, 100}));
    VectorModifier round = make_modifier(VectorModifierType::RoundCorners);
    round.params[kVecModRoundRadius] = 30;
    g.modifiers.push_back(round);
    g.fill.enabled = true;
    TreeResult r;
    evaluate_tree(g, 0.0, 0.02f, r);
    AUREA_CHECK_EQ(r.layers.size(), 1u);
    const f64 rounded = r.layers.empty() ? 0.0 : area_of(r.layers[0].rings);
    g.order = {P, F, O};
    evaluate_tree(g, 0.0, 0.02f, r);
    const f64 square = r.layers.empty() ? 0.0 : area_of(r.layers[0].rings);
    AUREA_CHECK_NEAR(square, 100.0 * 100.0, 1.0);
    // Quatro cantos de raio 30: perde (4 − π)·30² ≈ 772.
    AUREA_CHECK_NEAR(square - rounded, (4.0 - 3.14159265) * 900.0, 40.0);
    // Operador depois do repetidor: age nas cópias (aqui, deslocar caminhos infla as duas).
    VectorGroup rep = free_group({P, R, O, F});
    rep.paths.push_back(rect_path({0, 0}, {20, 20}));
    rep.repeater.enabled = true;
    rep.repeater.copies = 2;
    rep.repeater.position = Vec2{100, 0};
    VectorModifier offset = make_modifier(VectorModifierType::OffsetPaths);
    offset.params[kVecModOffsetAmount] = 5;
    rep.modifiers.push_back(offset);
    rep.fill.enabled = true;
    evaluate_tree(rep, 0.0, 0.02f, r);
    AUREA_CHECK_EQ(r.layers.size(), 1u);
    if (!r.layers.empty()) {
        const Vec2 mn = ring_min(r.layers[0].rings, r.layers[0].M), mx = ring_max(r.layers[0].rings, r.layers[0].M);
        AUREA_CHECK_NEAR(mn.x, -15.0f, 0.2f);
        AUREA_CHECK_NEAR(mx.x, 115.0f, 0.2f);
    }
    std::printf("arredondar acima x abaixo do preenchimento: %.0f x %.0f px2; ", rounded, square);
}

// -----------------------------------------------------------------------------
// Ordem que vale e edição
// -----------------------------------------------------------------------------
AUREA_TEST(VectorTree, EffectiveOrderRepairsTheListAndKeepsThePhonePanelWorking) {
    VectorGroup g;
    g.paths.push_back(rect_path({0, 0}, {10, 10}));
    g.stroke.enabled = true;
    // O grupo de sempre: caminho, mesclar, aparar, contorno, preencher, repetidor.
    AUREA_CHECK(effective_order(g) == (std::vector<VectorItemKind>{P, M, T, S, F, R}));
    materialize_order(g);
    // Só o que vale entra na lista gravada.
    AUREA_CHECK(g.order == (std::vector<VectorItemKind>{P, S, F}));
    // O painel do celular liga o Aparar e o Repetidor (campos de sempre): entram no lugar de sempre.
    g.trim.enabled = true;
    g.repeater.enabled = true;
    AUREA_CHECK(effective_order(g) == (std::vector<VectorItemKind>{P, T, S, F, R}));
    // Um comando antigo acrescenta um caminho: entra depois do último caminho.
    g.paths.push_back(rect_path({50, 0}, {10, 10}));
    AUREA_CHECK(effective_order(g) == (std::vector<VectorItemKind>{P, P, T, S, F, R}));
    // E tira os dois: as entradas sem item saem.
    g.paths.clear();
    AUREA_CHECK(effective_order(g) == (std::vector<VectorItemKind>{T, S, F, R}));
    // Edição: acrescentar no meio, mover e remover mantêm item ↔ entrada.
    VectorGroup e = free_group({});
    vector::TreeRemap remap;
    AUREA_CHECK(add_item(e, P, 0, static_cast<u32>(VectorPathKind::Rect), ""));
    AUREA_CHECK(add_item(e, F, 99, 0, ""));
    AUREA_CHECK(add_item(e, P, 0, static_cast<u32>(VectorPathKind::Ellipse), "", &remap));   // acima do retângulo
    AUREA_CHECK(e.order == (std::vector<VectorItemKind>{P, P, F}));
    AUREA_CHECK(e.paths[0].kind == VectorPathKind::Ellipse && e.paths[1].kind == VectorPathKind::Rect);
    AUREA_CHECK(remap.changed && remap.map.size() == 1 && remap.map[0] == 1);   // o retângulo era 0, virou 1
    AUREA_CHECK(e.fill.enabled);
    AUREA_CHECK(add_item(e, F, 0, 0, ""));   // segundo preencher, em cima de tudo: vira o PRIMEIRO do tipo
    AUREA_CHECK_EQ(e.moreFills.size(), 1u);
    AUREA_CHECK(e.order == (std::vector<VectorItemKind>{F, P, P, F}));
    // Mover a elipse (posição 1) para baixo do retângulo (posição 2): os caminhos trocam.
    AUREA_CHECK(move_item(e, 1, 2, &remap));
    AUREA_CHECK(e.paths[0].kind == VectorPathKind::Rect && e.paths[1].kind == VectorPathKind::Ellipse);
    AUREA_CHECK(remap.changed && remap.map[0] == 1 && remap.map[1] == 0);
    // Valores e liga/desliga pela posição.
    const f32 fill[5] = {0.25f, 0.5f, 0.75f, 60.0f, 1.0f};
    AUREA_CHECK(set_item_values(e, 0, fill, 5));
    AUREA_CHECK_NEAR(e.fill.paint.color.y, 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(e.fill.paint.opacity, 60.0f, 1e-6f);
    AUREA_CHECK(e.fill.rule == 1);
    AUREA_CHECK(set_item_enabled(e, 3, false));
    AUREA_CHECK(!e.moreFills[0].enabled);
    AUREA_CHECK(!set_item_enabled(e, 1, false));   // caminho não tem liga/desliga
    // Remover o primeiro preencher: o segundo passa a ser o de sempre.
    AUREA_CHECK(remove_item(e, 0, &remap));
    AUREA_CHECK(e.moreFills.empty() && !e.fill.enabled);
    AUREA_CHECK(e.order == (std::vector<VectorItemKind>{P, P, F}));
    AUREA_CHECK(remove_item(e, 2));
    AUREA_CHECK(e.order == (std::vector<VectorItemKind>{P, P}) && !e.fill.enabled);
    // Linhas da árvore: o grupo e os dois caminhos.
    VectorData d;
    d.groups.push_back(e);
    std::vector<TreeRow> rows;
    list_tree(d, rows);
    AUREA_CHECK_EQ(rows.size(), 3u);
    if (rows.size() == 3) {
        AUREA_CHECK(rows[0].kind == G && rows[0].depth == 0 && rows[0].valueCount == 10);
        AUREA_CHECK(rows[1].kind == P && rows[1].depth == 1 && rows[1].position == 0);
        AUREA_CHECK_NEAR(rows[1].values[0], static_cast<f32>(VectorPathKind::Rect), 1e-6f);
        AUREA_CHECK(rows[2].kind == P && rows[2].ordinal == 1);
    }
    std::printf("ordem consertada (painel do celular + comandos antigos); acrescentar/mover/remover; ");
}

AUREA_TEST(VectorTree, EngineEditsAreUndoableKeepTracksAndSurviveThePhoneDocumentAndSaving) {
    TreeRig r;
    if (!r.layer) return;
    AUREA_CHECK_EQ(r.data().groups.size(), 1u);
    const VectorData original = r.data();
    AUREA_CHECK(!data_has_tree(original));
    const u32 depth = r.e.history().depth();
    // Trilha de forma no caminho 0 (keys de morph): tem de acompanhar o caminho.
    (void)r.L()->tracks.get_or_create(TrackProperty::VectorPath, 0, 0).set(FrameIndex{0}, 0.0f);
    // Acrescenta uma elipse ACIMA do caminho existente: um passo de desfazer.
    AUREA_CHECK(r.edit(Engine::kVectorTreeAdd, static_cast<u32>(P), 0, static_cast<u32>(VectorPathKind::Ellipse)));
    AUREA_CHECK_EQ(r.e.history().depth(), depth + 1);
    AUREA_CHECK(r.data().groups[0].freeOrder);
    AUREA_CHECK_EQ(r.data().groups[0].paths.size(), 2u);
    AUREA_CHECK(r.data().groups[0].paths[0].kind == VectorPathKind::Ellipse);
    AUREA_CHECK(r.L()->tracks.find(TrackProperty::VectorPath, 0, 1) != nullptr);
    AUREA_CHECK(r.L()->tracks.find(TrackProperty::VectorPath, 0, 0) == nullptr);
    // Segundo contorno, grupo de dentro com um caminho, inclinação do grupo.
    AUREA_CHECK(r.edit(Engine::kVectorTreeAdd, static_cast<u32>(S), 99));   // o contorno de sempre (estava desligado)
    AUREA_CHECK(r.data().groups[0].stroke.enabled && r.data().groups[0].moreStrokes.empty());
    AUREA_CHECK(r.edit(Engine::kVectorTreeAdd, static_cast<u32>(S), 99));   // e um a mais
    AUREA_CHECK(r.edit(Engine::kVectorTreeAdd, static_cast<u32>(G), 0, 0, {}, "Dentro"));
    AUREA_CHECK(r.edit(Engine::kVectorTreeAdd, static_cast<u32>(P), 0, static_cast<u32>(VectorPathKind::Star), {}, nullptr, {0, 0}));
    AUREA_CHECK(r.edit(Engine::kVectorTreeAdd, static_cast<u32>(F), 99, 0, {}, nullptr, {0, 0}));
    AUREA_CHECK(r.edit(Engine::kVectorTreeGroupValues, 0, 0, 0, {10, 20, 0, 0, 100, 100, 0, 100, 25, 45}));
    AUREA_CHECK_NEAR(r.data().groups[0].skew, 25.0f, 1e-6f);
    AUREA_CHECK_EQ(r.data().groups[0].children.size(), 1u);
    AUREA_CHECK(r.data().groups[0].children[0].name == "Dentro");
    AUREA_CHECK(!r.edit(Engine::kVectorTreeAdd, static_cast<u32>(P), 0, 0, {}, nullptr, {0, 5}));   // grupo que não existe
    AUREA_CHECK(!r.edit(99));
    // A listagem pela ponte: linhas + nomes dos grupos.
    std::vector<f32> rows;
    std::string names;
    AUREA_CHECK(r.e.vector_tree(r.layer, rows, names));
    AUREA_CHECK(rows.size() > 2 && rows[0] == 1.0f && rows[1] >= 6.0f);
    AUREA_CHECK(names.find("Dentro") != std::string::npos);
    // O painel do celular lê e grava o documento de sempre: a árvore fica.
    const VectorData withTree = r.data();
    std::vector<f32> doc;
    std::string docNames;
    AUREA_CHECK(r.e.vector_document(r.layer, doc, docNames));
    AUREA_CHECK_NEAR(doc[0], 6.0f, 1e-6f);
    VectorData phone;
    AUREA_CHECK(decode_document(doc.data(), doc.size(), docNames, phone));
    phone.groups[0].fill.paint.color = Vec4{0.1f, 0.2f, 0.3f, 1.0f};   // o celular muda a cor do preenchimento
    encode_document(phone, doc, docNames);
    AUREA_CHECK(r.e.set_vector_document(r.layer, doc.data(), doc.size(), docNames, false));
    AUREA_CHECK_NEAR(r.data().groups[0].fill.paint.color.z, 0.3f, 1e-6f);
    AUREA_CHECK(r.data().groups[0].freeOrder);
    AUREA_CHECK(r.data().groups[0].order == withTree.groups[0].order);
    AUREA_CHECK(r.data().groups[0].children == withTree.groups[0].children);
    AUREA_CHECK_EQ(r.data().groups[0].moreStrokes.size(), 1u);
    AUREA_CHECK_NEAR(r.data().groups[0].skew, 25.0f, 1e-6f);
    // Gravar e abrir: a árvore volta inteira (seção VectorTree do projeto).
    const VectorData saved = r.data();
    const std::string path = r.temp("aurea_test_vector_tree.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    Layer* loaded = nullptr;
    r.comp()->layers().for_each([&](LayerId, Layer& l) { if (l.kind == LayerKind::Shape && l.shape.shapeType == kShapeVector) loaded = &l; });
    AUREA_CHECK(loaded != nullptr);
    if (loaded) AUREA_CHECK(loaded->shape.vector == saved);
    std::remove(path.c_str());
    std::printf("editar pela ponte (desfazer, trilhas), documento do celular preserva a arvore, gravar/abrir igual; ");
}

AUREA_TEST(VectorTree, EngineUndoRestoresTheClassicGroupAndProjectsWithoutTreeStayTheSame) {
    TreeRig r;
    if (!r.layer) return;
    const VectorData original = r.data();
    AUREA_CHECK(r.edit(Engine::kVectorTreeFreeOrder, 1));
    AUREA_CHECK(r.data().groups[0].freeOrder);
    AUREA_CHECK(r.edit(Engine::kVectorTreeMove, 0, 1));
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.data() == original);
    AUREA_CHECK(!data_has_tree(r.data()));
    // Grupo novo de primeiro nível pela árvore (vazio, ordem livre).
    AUREA_CHECK(r.edit(Engine::kVectorTreeAddGroup, 0, 0, 0, {}, "Novo"));
    AUREA_CHECK_EQ(r.data().groups.size(), 2u);
    AUREA_CHECK(r.data().groups[1].name == "Novo" && r.data().groups[1].freeOrder);
    AUREA_CHECK(r.undo());
    // Projeto sem árvore: gravar não cria a seção (o arquivo é o de sempre) e abre igual.
    const std::string a = r.temp("aurea_test_vector_flat_a.aurea");
    AUREA_CHECK(r.e.save_project(a.c_str()).ok());
    AUREA_CHECK(r.e.load_project(a.c_str()).ok());
    Layer* loaded = nullptr;
    r.comp()->layers().for_each([&](LayerId, Layer& l) { if (l.kind == LayerKind::Shape && l.shape.shapeType == kShapeVector) loaded = &l; });
    AUREA_CHECK(loaded != nullptr);
    if (loaded) AUREA_CHECK(loaded->shape.vector == original);
    std::remove(a.c_str());
}
