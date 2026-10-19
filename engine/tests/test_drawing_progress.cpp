// =============================================================================
//  Progresso do desenho (0.2.5) na CPU: o corte exato dos contornos da camada
//  vetorial — comprimentos a 0/50/100 % nas duas ordens e invertido, a ordem
//  entre grupos, o preenchimento que entra no fim do contorno, a composição
//  com o Aparar, o afinar e o tracejado presos ao caminho inteiro, a malha e
//  a chave do cache; e o contrato do efeito no registro.
//  A varredura das camadas sem vetor (GPU) está em DrawingProgressGpu.inl.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

namespace {

Contour open_line(Vec2 a, Vec2 b) {
    Contour c;
    c.pts = {a, b};
    c.closed = false;
    return c;
}

f64 drawn_length(const std::vector<DrawnLine>& pieces) {
    f64 s = 0.0;
    for (const DrawnLine& d : pieces) s += length_of(d.line);
    return s;
}

/// A: 100 px em y = 0; B: 300 px em y = 50 (comprimento total 400).
std::vector<Contour> two_lines() {
    return {open_line(Vec2{0, 0}, Vec2{100, 0}), open_line(Vec2{0, 50}, Vec2{300, 50})};
}

VectorGroup line_group(Vec2 a, Vec2 b, f32 width) {
    VectorGroup g;
    VectorPath p;
    p.path.closed = false;
    p.path.v = {BezierVertex{a, {}, {}}, BezierVertex{b, {}, {}}};
    g.paths.push_back(p);
    g.fill.enabled = false;
    g.stroke.enabled = true;
    g.stroke.width = width;
    return g;
}

VectorGroup square_group(f32 side, bool stroke, bool fill) {
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.center = Vec2{0, 0};
    p.size = Vec2{side, side};
    g.paths.push_back(p);
    g.fill.enabled = fill;
    g.stroke.enabled = stroke;
    g.stroke.width = 10.0f;
    return g;
}

} // namespace

AUREA_TEST(DrawingProgress, CutLengthsInBothOrdersAndInverted) {
    const std::vector<Contour> lines = two_lines();
    std::vector<DrawnLine> out;
    // 0 %: nada; 100 %: os contornos inteiros, como estão.
    for (const bool seq : {true, false}) {
        cut_drawn(lines, 0.0f, seq, false, out);
        AUREA_CHECK(out.empty());
        cut_drawn(lines, 1.0f, seq, false, out);
        AUREA_CHECK_EQ(out.size(), usize{2});
        AUREA_CHECK_NEAR(drawn_length(out), 400.0, 1e-6);
    }
    // Sequencial 50 %: A inteiro (100) e os primeiros 100 de B.
    cut_drawn(lines, 0.5f, true, false, out);
    std::printf("    sequencial 50%%: %zu pedaco(s), %.3f px\n", out.size(), drawn_length(out));
    AUREA_CHECK_NEAR(drawn_length(out), 200.0, 1e-4);
    AUREA_CHECK_EQ(out.size(), usize{2});
    if (out.size() == 2) {
        AUREA_CHECK_NEAR(length_of(out[0].line), 100.0, 1e-4);
        AUREA_CHECK_NEAR(out[1].line.pts.back().x, 100.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[1].from, 0.0, 1e-9);
        AUREA_CHECK_NEAR(out[1].to, 100.0, 1e-4);
    }
    // Sequencial 20 %: só a metade de A (B não começou).
    cut_drawn(lines, 0.2f, true, false, out);
    AUREA_CHECK_EQ(out.size(), usize{1});
    AUREA_CHECK_NEAR(drawn_length(out), 80.0, 1e-4);
    // Simultâneo 50 %: metade de cada (50 + 150).
    cut_drawn(lines, 0.5f, false, false, out);
    AUREA_CHECK_EQ(out.size(), usize{2});
    AUREA_CHECK_NEAR(drawn_length(out), 200.0, 1e-4);
    if (out.size() == 2) {
        AUREA_CHECK_NEAR(out[0].line.pts.back().x, 50.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[1].line.pts.back().x, 150.0f, 1e-3f);
    }
    // Invertido, sequencial 50 %: o último traço primeiro — os 200 finais de B.
    cut_drawn(lines, 0.5f, true, true, out);
    AUREA_CHECK_EQ(out.size(), usize{1});
    AUREA_CHECK_NEAR(drawn_length(out), 200.0, 1e-4);
    if (out.size() == 1) {
        AUREA_CHECK_NEAR(out[0].line.pts.front().x, 100.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[0].line.pts.back().x, 300.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[0].from, 100.0, 1e-4);
        AUREA_CHECK_NEAR(out[0].to, 300.0, 1e-4);
    }
    // Invertido, simultâneo 50 %: o fim de cada um (A 50..100, B 150..300).
    cut_drawn(lines, 0.5f, false, true, out);
    AUREA_CHECK_EQ(out.size(), usize{2});
    AUREA_CHECK_NEAR(drawn_length(out), 200.0, 1e-4);
    if (out.size() == 2) {
        AUREA_CHECK_NEAR(out[0].line.pts.front().x, 50.0f, 1e-3f);
        AUREA_CHECK_NEAR(out[1].line.pts.front().x, 150.0f, 1e-3f);
    }
    // Fechado: inteiro continua fechado; pela metade abre, com metade do perímetro.
    Contour sq;
    sq.pts = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    sq.closed = true;
    cut_drawn({sq}, 1.0f, true, false, out);
    AUREA_CHECK(out.size() == 1 && out[0].line.closed);
    cut_drawn({sq}, 0.5f, true, false, out);
    AUREA_CHECK(out.size() == 1 && !out[0].line.closed);
    AUREA_CHECK_NEAR(drawn_length(out), 200.0, 1e-4);
    // Um toque do lápis (um ponto só) aparece quando o desenho chega nele.
    Contour dot;
    dot.pts = {{10, 10}};
    dot.closed = false;
    const std::vector<Contour> withDot{open_line(Vec2{0, 0}, Vec2{100, 0}), dot};
    cut_drawn(withDot, 0.5f, true, false, out);
    AUREA_CHECK_EQ(out.size(), usize{1});
    cut_drawn(withDot, 1.0f, true, false, out);
    AUREA_CHECK_EQ(out.size(), usize{2});
    cut_drawn(withDot, 0.5f, true, true, out);   // invertido: o ponto vem primeiro
    AUREA_CHECK_EQ(out.size(), usize{2});
    // NaN = o desenho inteiro (o padrão do efeito).
    cut_drawn(lines, std::nanf(""), true, false, out);
    AUREA_CHECK_NEAR(drawn_length(out), 400.0, 1e-6);
}

AUREA_TEST(DrawingProgress, GroupsFollowTheDrawOrder) {
    const std::vector<f64> lengths{100.0, 0.0, 300.0};   // o do meio não tem tinta
    std::vector<f32> a;
    group_amounts(lengths, DrawProgress{0.5f, true, false}, a);
    AUREA_CHECK_EQ(a.size(), usize{3});
    AUREA_CHECK_NEAR(a[0], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(a[2], 1.0f / 3.0f, 1e-5f);
    // Invertido: o último grupo primeiro (200 de 300), o primeiro ainda não.
    group_amounts(lengths, DrawProgress{0.5f, true, true}, a);
    AUREA_CHECK_NEAR(a[2], 2.0f / 3.0f, 1e-5f);
    AUREA_CHECK_NEAR(a[0], 0.0f, 1e-6f);
    // Simultâneo: todos na mesma fração.
    group_amounts(lengths, DrawProgress{0.5f, false, false}, a);
    for (f32 v : a) AUREA_CHECK_NEAR(v, 0.5f, 1e-6f);
    group_amounts(lengths, DrawProgress{0.0f, true, false}, a);
    AUREA_CHECK(a[0] == 0.0f && a[2] == 0.0f);
    group_amounts(lengths, DrawProgress{1.0f, true, false}, a);
    for (f32 v : a) AUREA_CHECK_NEAR(v, 1.0f, 1e-6f);
    // Comprimento em px da CAMADA: o afim conta (escala 2 = o dobro).
    AUREA_CHECK_NEAR(length_under(two_lines(), Affine2::scale(Vec2{2.0f, 2.0f})), 800.0, 1e-3);
}

AUREA_TEST(DrawingProgress, StrokeFillTrimTaperAndDashStayExact) {
    // Quadrado de 100 com contorno 10 (ponta reta) e preenchimento.
    const VectorGroup sq = square_group(100.0f, true, true);
    GroupGeometry full, plain, half;
    group_geometry(sq, 0.0, 0.05f, plain);
    DrawProgress d{1.0f, true, false};
    group_geometry(sq, 0.0, 0.05f, full, &d);
    // 100 %: a mesma geometria do grupo sem o efeito.
    AUREA_CHECK_NEAR(area_of(full.stroke), area_of(plain.stroke), 1e-6);
    AUREA_CHECK_NEAR(area_of(full.fill), area_of(plain.fill), 1e-6);
    AUREA_CHECK_NEAR(full.fillAlpha, 1.0f, 1e-6f);
    // 50 %: metade do perímetro desenhada; o preenchimento ainda não entrou.
    d.amount = 0.5f;
    group_geometry(sq, 0.0, 0.05f, half, &d);
    f64 drawn = 0.0;
    for (const Contour& c : half.drawn) drawn += length_of(c);
    std::printf("    quadrado 50%%: %.3f px desenhados, contorno %.1f (inteiro %.1f), preenchimento %.2f\n", drawn,
                area_of(half.stroke), area_of(plain.stroke), half.fillAlpha);
    AUREA_CHECK_NEAR(drawn, 200.0, 0.01);
    AUREA_CHECK_NEAR(half.fillAlpha, 0.0f, 1e-6f);
    AUREA_CHECK(half.fill.empty());
    AUREA_CHECK(area_of(half.stroke) < area_of(plain.stroke) * 0.6 && area_of(half.stroke) > area_of(plain.stroke) * 0.4);
    // Nos últimos 10 % do contorno o preenchimento acende (95 % = metade).
    d.amount = 0.95f;
    group_geometry(sq, 0.0, 0.05f, half, &d);
    AUREA_CHECK_NEAR(half.fillAlpha, 0.5f, 1e-4f);
    AUREA_CHECK(!half.fill.empty());
    // Só preenchimento: acende ao longo do trecho do grupo.
    const VectorGroup onlyFill = square_group(100.0f, false, true);
    d.amount = 0.3f;
    group_geometry(onlyFill, 0.0, 0.05f, half, &d);
    AUREA_CHECK_NEAR(half.fillAlpha, 0.3f, 1e-5f);
    // Com o Aparar (0..50 %), 50 % do desenho = um quarto do perímetro.
    VectorGroup trimmed = square_group(100.0f, true, false);
    trimmed.trim.enabled = true;
    trimmed.trim.end = 50.0f;
    d.amount = 0.5f;
    group_geometry(trimmed, 0.0, 0.05f, half, &d);
    drawn = 0.0;
    for (const Contour& c : half.drawn) drawn += length_of(c);
    AUREA_CHECK_NEAR(drawn, 100.0, 0.01);

    // Afinar no começo (50 %): a metade desenhada é a rampa do caminho INTEIRO
    // (triângulo 100 × 20 / 2 = 1000), não um afinar novo esticado no pedaço.
    VectorGroup taper = line_group(Vec2{0, 0}, Vec2{200, 0}, 20.0f);
    taper.stroke.taperStart = 50.0f;
    d.amount = 0.5f;
    group_geometry(taper, 0.0, 0.05f, half, &d);
    std::printf("    afinado 50%%: area %.1f (esperado 1000)\n", std::fabs(area_of(half.stroke)));
    AUREA_CHECK_NEAR(std::fabs(area_of(half.stroke)), 1000.0, 15.0);
    // Invertido: o fim (sem afinar) = retângulo 100 × 20.
    d.reverse = true;
    group_geometry(taper, 0.0, 0.05f, half, &d);
    AUREA_CHECK_NEAR(std::fabs(area_of(half.stroke)), 2000.0, 15.0);
    d.reverse = false;

    // Tracejado 20/10 numa linha de 300 (contorno 4): os traços visíveis são
    // os do desenho inteiro — 50 % mostra 5 traços de 20 (área 400), nas
    // duas pontas; o padrão não escorrega quando a ponta anda.
    VectorGroup dashed = line_group(Vec2{0, 0}, Vec2{300, 0}, 4.0f);
    dashed.stroke.dashes = {20.0f, 10.0f};
    for (const bool reverse : {false, true}) {
        d.amount = 0.5f;
        d.reverse = reverse;
        group_geometry(dashed, 0.0, 0.05f, half, &d);
        std::printf("    tracejado 50%% %s: area %.2f\n", reverse ? "invertido" : "normal", std::fabs(area_of(half.stroke)));
        AUREA_CHECK_NEAR(std::fabs(area_of(half.stroke)), 400.0, 1.0);
    }
    // 55 %: a ponta corta o sexto traço no meio (150..165 → 15 px a mais).
    d.amount = 0.55f;
    d.reverse = false;
    group_geometry(dashed, 0.0, 0.05f, half, &d);
    AUREA_CHECK_NEAR(std::fabs(area_of(half.stroke)), 460.0, 1.0);
}

AUREA_TEST(DrawingProgress, MeshFollowsGroupsAndCacheKeyChanges) {
    // Grupo A: 100 px em y = 0; grupo B: 300 px em y = 100 (contorno 10).
    const std::vector<VectorGroup> groups{line_group(Vec2{0, 0}, Vec2{100, 0}, 10.0f),
                                          line_group(Vec2{0, 100}, Vec2{300, 100}, 10.0f)};
    VectorMesh plain, same, quarter, both, none;
    build_mesh(groups, 0.0, 1.0f, plain);
    build_mesh(groups, 0.0, 1.0f, same, DrawProgress{1.0f, true, false});
    AUREA_CHECK_EQ(same.vertex_count(), plain.vertex_count());
    AUREA_CHECK(same.verts == plain.verts);
    // Sequencial 25 % (100 de 400): só o grupo A, inteiro.
    build_mesh(groups, 0.0, 1.0f, quarter, DrawProgress{0.25f, true, false});
    std::printf("    malha 25%% sequencial: x %.1f..%.1f, y %.1f..%.1f\n", quarter.min.x, quarter.max.x, quarter.min.y, quarter.max.y);
    AUREA_CHECK(!quarter.empty());
    AUREA_CHECK(quarter.max.y < 10.0f);
    AUREA_CHECK_NEAR(quarter.max.x, 101.0f, 0.01f);
    // Simultâneo 25 %: os dois, cada um num quarto (B até x = 75).
    build_mesh(groups, 0.0, 1.0f, both, DrawProgress{0.25f, false, false});
    AUREA_CHECK(both.max.y > 100.0f);
    AUREA_CHECK_NEAR(both.max.x, 76.0f, 0.01f);
    build_mesh(groups, 0.0, 1.0f, none, DrawProgress{0.0f, true, false});
    AUREA_CHECK(none.empty());
    // A chave do cache: inativo = a de sempre; ativo muda com a fração e a ordem.
    const u64 base = content_hash(groups, 0.0);
    AUREA_CHECK_EQ(content_hash(groups, 0.0, DrawProgress{1.0f, false, true}), base);
    const u64 h25 = content_hash(groups, 0.0, DrawProgress{0.25f, true, false});
    AUREA_CHECK(h25 != base);
    AUREA_CHECK(content_hash(groups, 0.0, DrawProgress{0.5f, true, false}) != h25);
    AUREA_CHECK(content_hash(groups, 0.0, DrawProgress{0.25f, false, false}) != h25);
    AUREA_CHECK(content_hash(groups, 0.0, DrawProgress{0.25f, true, true}) != h25);
}

AUREA_TEST(DrawingProgress, EffectContractInTheRegistry) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const EffectTypeId id = effect_type_id(effect_keys::kDrawingProgress);
    const Effect* fx = reg.find(id);
    const ParameterRegistry* params = reg.params(id);
    AUREA_CHECK(fx != nullptr && params != nullptr);
    if (!fx || !params) return;
    AUREA_CHECK(std::string(fx->info().category) == "Desenho");
    AUREA_CHECK(fx->edits_vector_geometry());
    AUREA_CHECK_EQ(params->count(), 5u);
    const char* const ids[] = {"progress", "order", "reverse", "softness", "angle"};
    for (u32 i = 0; i < 5 && i < params->count(); ++i) AUREA_CHECK(std::string(params->at(i).id) == ids[i]);
    AUREA_CHECK(params->at(0).animatable());
    AUREA_CHECK_NEAR(params->at(0).defaultValue.v[0], 100.0f, 1e-6f);
    AUREA_CHECK_EQ(params->at(1).enumCount, 2u);
    // Padrões: neutro em qualquer camada (pôr o efeito não muda nada).
    EffectInstance inst;
    initialize_instance(inst, *params);
    std::vector<ParamValue> values(params->count());
    for (u32 i = 0; i < params->count(); ++i) values[i] = inst.params[i].constant;
    EffectEval e;
    e.effect = fx;
    e.instance = &inst;
    e.values = values.data();
    e.count = params->count();
    AUREA_CHECK(fx->is_identity(e));
    DrawProgress p;
    AUREA_CHECK(fx->vector_progress(e, p));
    AUREA_CHECK(!p.active() && p.sequential && !p.reverse);
    // 40 %, simultâneo, invertido: a imagem tem passe; a camada vetorial não
    // (o corte já está na malha).
    values[0] = ParamValue::scalar(40.0f);
    values[1] = ParamValue::scalar(1.0f);
    values[2] = ParamValue::boolean(true);
    AUREA_CHECK(!fx->is_identity(e));
    AUREA_CHECK(fx->vector_progress(e, p));
    AUREA_CHECK_NEAR(p.amount, 0.4f, 1e-6f);
    AUREA_CHECK(!p.sequential && p.reverse);
    Layer vectorLayer;
    vectorLayer.kind = LayerKind::Shape;
    vectorLayer.shape.shapeType = kShapeVector;
    e.layer = &vectorLayer;
    AUREA_CHECK(fx->is_identity(e));
    Layer image;
    image.kind = LayerKind::Image;
    e.layer = &image;
    AUREA_CHECK(!fx->is_identity(e));
    // A prévia do catálogo mostra o efeito no meio do caminho.
    std::vector<ParamValue> demo(values.size());
    for (u32 i = 0; i < params->count(); ++i) demo[i] = inst.params[i].constant;
    AUREA_CHECK(fx->demo_values(inst, demo));
    AUREA_CHECK(demo[0].v[0] > 0.0f && demo[0].v[0] < 100.0f);
}
