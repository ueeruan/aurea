// =============================================================================
//  Omega 0.3.2 · área 06 (editores de conteúdo): regras do motor.
//   - PathEdit: inserir mantém a curva, travas das alças, canto ↔ curva, remover;
//   - furo do contorno, perfil do pincel e grupo de apagar na geometria;
//   - documento vetorial v3 (e v2 ainda abre);
//   - desenho (caneta, pincel, preencher, borracha) com um desfazer por traço;
//   - edit_vector_path, largura da caixa de texto, alça da forma e fontes.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/vector/PathEdit.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

namespace {

BezierPath curved_segment() {
    // Um trecho em S, com as duas alças: o caso que exige De Casteljau exato.
    BezierPath p;
    p.closed = false;
    p.v.push_back(BezierVertex{Vec2{0, 0}, Vec2{}, Vec2{40, 80}});
    p.v.push_back(BezierVertex{Vec2{200, 0}, Vec2{-40, -80}, Vec2{}});
    return p;
}

f64 interior_area(const VectorMesh& m) {
    // Só os triângulos do interior (d = 1e4): a franja de AA não conta.
    f64 a = 0.0;
    for (usize i = 0; i + 5 < m.verts.size(); i += 6) {
        if (m.verts[i + 1].x < 1.0e3f) continue;
        const Vec4 p0 = m.verts[i], p1 = m.verts[i + 2], p2 = m.verts[i + 4];
        a += std::fabs((p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y)) * 0.5;
    }
    return a;
}

VectorGroup filled_square(f32 x, f32 y, f32 s) {
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Free;
    p.path.closed = true;
    p.path.v = {BezierVertex{Vec2{x, y}}, BezierVertex{Vec2{x + s, y}}, BezierVertex{Vec2{x + s, y + s}}, BezierVertex{Vec2{x, y + s}}};
    g.paths.push_back(p);
    g.fill.enabled = true;
    g.stroke.enabled = false;
    return g;
}

struct ContentRig {
    Engine e;
    ContentRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    }
    ~ContentRig() { e.shutdown(); }
    bool undo() {
        Command c;
        c.type = CommandType::Undo;
        return e.apply_command(c).ok();
    }
    Layer* layer(u64 id) {
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        return comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    }
};

std::string repo_path(const char* relative) {
    return (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / relative).string();
}

} // namespace

AUREA_TEST(OmegaContent, InsertKeepsTheCurveAndReturnsTheNewIndex) {
    BezierPath p = curved_segment();
    Vec2 before[9];
    for (int k = 0; k < 9; ++k) AUREA_CHECK(pathedit::point_on_segment(p, 0, static_cast<f32>(k + 1) / 10.0f, before[k]));
    const i32 at = pathedit::insert_at(p, 0, 0.5f);
    AUREA_CHECK_EQ(at, 1);
    AUREA_CHECK_EQ(p.v.size(), usize{3});
    // A curva continua igual: os pontos de t ∈ (0, 0,5] caem no 1º trecho novo
    // (t' = 2t) e os de (0,5, 1) no 2º (t' = 2t − 1).
    for (int k = 0; k < 9; ++k) {
        const f32 t = static_cast<f32>(k + 1) / 10.0f;
        Vec2 q{};
        if (t <= 0.5f) AUREA_CHECK(pathedit::point_on_segment(p, 0, t * 2.0f, q));
        else AUREA_CHECK(pathedit::point_on_segment(p, 1, t * 2.0f - 1.0f, q));
        AUREA_CHECK_NEAR(q.x, before[k].x, 1e-3);
        AUREA_CHECK_NEAR(q.y, before[k].y, 1e-3);
    }
    // Fechado: o último trecho (de volta ao 0) põe o ponto novo no FIM.
    BezierPath tri;
    tri.closed = true;
    tri.v = {BezierVertex{Vec2{0, 0}}, BezierVertex{Vec2{100, 0}}, BezierVertex{Vec2{0, 100}}};
    AUREA_CHECK_EQ(pathedit::insert_at(tri, 2, 0.5f), 3);
    AUREA_CHECK_NEAR(tri.v[3].p.x, 0.0, 1e-4);
    AUREA_CHECK_NEAR(tri.v[3].p.y, 50.0, 1e-4);
    AUREA_CHECK_EQ(pathedit::insert_at(tri, 9, 0.5f), -1);
}

AUREA_TEST(OmegaContent, HandleLocksAngleAndLength) {
    BezierPath p;
    p.v = {BezierVertex{Vec2{0, 0}, Vec2{-30, 0}, Vec2{60, 0}}};
    // Solta: só a alça mexida anda.
    AUREA_CHECK(pathedit::move_handle(p, 0, true, Vec2{0, 60}, false, false));
    AUREA_CHECK_NEAR(p.v[0].in.x, -30.0, 1e-5);
    AUREA_CHECK_NEAR(p.v[0].in.y, 0.0, 1e-5);
    // Ângulo travado: a oposta na mesma reta, com o comprimento DELA (30).
    AUREA_CHECK(pathedit::move_handle(p, 0, true, Vec2{0, 0}, true, false));
    const Vec2 dir = p.v[0].out.normalized();
    AUREA_CHECK_NEAR(p.v[0].in.length(), 30.0, 1e-3);
    AUREA_CHECK_NEAR(p.v[0].in.normalized().dot(dir), -1.0, 1e-4);
    // Ângulo e comprimento: a oposta espelha a mexida.
    AUREA_CHECK(pathedit::move_handle(p, 0, false, Vec2{-10, 0}, true, true));
    AUREA_CHECK_NEAR(p.v[0].out.length(), p.v[0].in.length(), 1e-3);
    AUREA_CHECK_NEAR(p.v[0].out.normalized().dot(p.v[0].in.normalized()), -1.0, 1e-4);
    AUREA_CHECK(!pathedit::move_handle(p, 5, true, Vec2{1, 1}, false, false));
}

AUREA_TEST(OmegaContent, PointTypeAppendCloseAndRemove) {
    BezierPath p;
    p.closed = false;
    AUREA_CHECK_EQ(pathedit::append_point(p, Vec2{0, 0}, false), 0u);
    AUREA_CHECK_EQ(pathedit::append_point(p, Vec2{90, 0}, false), 1u);
    AUREA_CHECK_EQ(pathedit::append_point(p, Vec2{90, 90}, false), 2u);
    AUREA_CHECK_EQ(pathedit::append_point(p, Vec2{-30, 0}, true), 0u);   // no começo
    AUREA_CHECK_NEAR(p.v[0].p.x, -30.0, 1e-5);
    AUREA_CHECK(!pathedit::point_is_curve(p, 2));
    // Curva: alças a 1/3 dos trechos, na direção que liga os vizinhos.
    AUREA_CHECK(pathedit::set_point_type(p, 2, true));
    AUREA_CHECK(pathedit::point_is_curve(p, 2));
    AUREA_CHECK_NEAR(p.v[2].in.length(), 30.0, 1e-3);
    AUREA_CHECK_NEAR(p.v[2].out.length(), 30.0, 1e-3);
    AUREA_CHECK_NEAR(p.v[2].in.normalized().dot(p.v[2].out.normalized()), -1.0, 1e-4);
    AUREA_CHECK(pathedit::set_point_type(p, 2, false));
    AUREA_CHECK(!pathedit::point_is_curve(p, 2));
    AUREA_CHECK(!pathedit::set_point_type(p, 2, false));   // já é canto
    AUREA_CHECK(pathedit::set_closed(p, true));
    AUREA_CHECK(!pathedit::set_closed(p, true));
    AUREA_CHECK(pathedit::remove_point(p, 0));
    AUREA_CHECK(pathedit::remove_point(p, 0));
    AUREA_CHECK(!p.closed);   // menos de 3 pontos abre
    const u32 both[2] = {0, 1};
    pathedit::move_points(p, both, 2, Vec2{5, -5});
    AUREA_CHECK_NEAR(p.v[1].p.x, 95.0, 1e-5);
    AUREA_CHECK_NEAR(p.v[1].p.y, 85.0, 1e-5);
    AUREA_CHECK_NEAR(pathedit::segment_length(p, 0), 90.0, 1e-2);
}

AUREA_TEST(OmegaContent, HoleContourCutsTheFillButKeepsItsStroke) {
    VectorGroup g = filled_square(0, 0, 100);
    VectorPath hole = g.paths[0];
    hole.path.v = {BezierVertex{Vec2{25, 25}}, BezierVertex{Vec2{75, 25}}, BezierVertex{Vec2{75, 75}}, BezierVertex{Vec2{25, 75}}};
    g.paths.push_back(hole);
    GroupGeometry plain;
    group_geometry(g, 0.0, 0.05f, plain);
    AUREA_CHECK_NEAR(area_of(plain.fill), 10000.0, 1.0);   // mesmo sentido em não-zero: sem furo
    g.paths[1].exclude = true;
    GroupGeometry cut;
    group_geometry(g, 0.0, 0.05f, cut);
    AUREA_CHECK_NEAR(area_of(cut.fill), 7500.0, 1.0);
    g.stroke.enabled = true;
    g.stroke.width = 4.0f;
    GroupGeometry stroked;
    group_geometry(g, 0.0, 0.05f, stroked);
    // Contorno de fora + o do furo: 2 × perímetro × largura (≈ 4·100·4 + 4·50·4).
    AUREA_CHECK(area_of(stroked.stroke) > 2300.0);
}

AUREA_TEST(OmegaContent, BrushProfileThinsTheStroke) {
    Contour line;
    line.closed = false;
    line.pts = {Vec2{0, 0}, Vec2{200, 0}};
    std::vector<Contour> constant, thin;
    stroke_to_rings({line}, 20.0f, 0, 0, 4.0f, 0.05f, constant);
    const std::vector<f32> profile{1.0f, 0.2f};
    StrokeTaper taper;
    taper.profile = &profile;
    stroke_to_rings({line}, 20.0f, 0, 0, 4.0f, 0.05f, thin, taper);
    AUREA_CHECK_NEAR(area_of(constant), 4000.0, 2.0);
    // Largura de 20 a 4 em linha reta: a área média (12 × 200).
    AUREA_CHECK_NEAR(area_of(thin), 2400.0, 30.0);
}

AUREA_TEST(OmegaContent, EraserGroupRemovesWhatIsBelowOnly) {
    std::vector<VectorGroup> groups{filled_square(0, 0, 100)};
    VectorMesh plain;
    build_mesh(groups, 0.0, 1.0f, plain);
    const f64 full = interior_area(plain);
    AUREA_CHECK(full > 9000.0);
    // Borracha: uma faixa vertical de 20 px no meio.
    VectorGroup eraser;
    VectorPath line;
    line.kind = VectorPathKind::Free;
    line.path.closed = false;
    line.path.v = {BezierVertex{Vec2{50, -20}}, BezierVertex{Vec2{50, 120}}};
    eraser.paths.push_back(line);
    eraser.fill.enabled = false;
    eraser.stroke.enabled = true;
    eraser.stroke.width = 20.0f;
    eraser.composite = kVectorCompositeErase;
    groups.push_back(eraser);
    VectorMesh erased;
    build_mesh(groups, 0.0, 1.0f, erased);
    const f64 rest = interior_area(erased);
    std::printf("    inteiro %.1f, com borracha %.1f\n", full, rest);
    AUREA_CHECK(rest < full - 1500.0);
    AUREA_CHECK(rest > full - 2600.0);
    AUREA_CHECK(content_hash(groups, 0.0) != content_hash({groups[0]}, 0.0));
    // A borracha sozinha não pinta nada; um grupo DEPOIS dela não é apagado.
    std::vector<VectorGroup> onlyEraser{eraser, filled_square(200, 0, 50)};
    VectorMesh after;
    build_mesh(onlyEraser, 0.0, 1.0f, after);
    AUREA_CHECK_NEAR(interior_area(after), 2500.0, 250.0);
}

AUREA_TEST(OmegaContent, DocumentV3KeepsNewFieldsAndV2StillOpens) {
    VectorData d;
    d.drawing = true;
    d.groups.push_back(filled_square(0, 0, 10));
    d.groups[0].composite = kVectorCompositeErase;
    d.groups[0].stroke.widthProfile = {1.0f, 0.5f, 0.25f};
    d.groups[0].paths[0].exclude = true;
    std::vector<f32> s;
    std::string names;
    encode_document(d, s, names);
    AUREA_CHECK_NEAR(s[0], 3.0, 1e-6);
    VectorData back;
    AUREA_CHECK(decode_document(s.data(), s.size(), names, back));
    AUREA_CHECK(back == d);
    // Um fluxo v2 (sem perfil, apagar, furo nem desenho) feito à mão.
    const std::vector<f32> v2{
        2, 1,
        1, 0, 0, 0, 0, 0, 100, 100, 0, 100,                 // visível, mesclar, transform
        1, 0, 0, 1, 1, 1, 1, -100, 0, 100, 0, 100, 0,       // preencher + tinta
        0, 6, 0, 0, 4, 0, 0, 0, 1, 1, 1, 1, -100, 0, 100, 0, 100, 0,   // contorno + tinta
        0, 0, 0,                                            // afinar (v2)
        0, 0, 100, 0, 0,                                    // aparar
        0, 3, 0, 0, 0, 120, 0, 100, 0, 100, 100, 0,         // repetidor
        1,                                                  // 1 caminho
        0, 0, 0, 0, 200, 200, 0, 5, 100, 50, 0, 0, 0,       // caminho
        0, 0,                                               // bezier vazio
        0,                                                  // sem keyframes
    };
    VectorData old;
    AUREA_CHECK(decode_document(v2.data(), v2.size(), "Grupo", old));
    AUREA_CHECK_EQ(old.groups.size(), usize{1});
    if (old.groups.size() == 1) {
        AUREA_CHECK(!old.drawing);
        AUREA_CHECK_EQ(old.groups[0].composite, u8{0});
        AUREA_CHECK(old.groups[0].stroke.widthProfile.empty());
        AUREA_CHECK(!old.groups[0].paths[0].exclude);
    }
}

AUREA_TEST(OmegaContent, DrawingStrokesAreGroupsWithOneUndoEach) {
    ContentRig r;
    const auto layer = r.e.add_drawing_layer();
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) return;
    AUREA_CHECK_EQ(r.e.vector_layer_flags(*layer), 3u);
    const f32 red[4] = {1, 0, 0, 1};
    std::vector<f32> xyp;
    for (int i = 0; i <= 20; ++i) xyp.insert(xyp.end(), {100.0f + 10.0f * i, 100.0f + 3.0f * i, 0.2f + 0.04f * i});
    AUREA_CHECK(r.e.add_drawing_stroke(*layer, Engine::kDrawPen, red, 12.0f, xyp.data(), xyp.size()).ok());
    AUREA_CHECK(r.e.add_drawing_stroke(*layer, Engine::kDrawBrush, red, 12.0f, xyp.data(), xyp.size()).ok());
    std::vector<f32> loop;
    for (int i = 0; i < 24; ++i) {
        const f32 a = static_cast<f32>(i) / 24.0f * 6.2831853f;
        loop.insert(loop.end(), {320.0f + 60.0f * std::cos(a), 180.0f + 60.0f * std::sin(a), 1.0f});
    }
    AUREA_CHECK(r.e.add_drawing_stroke(*layer, Engine::kDrawFill, red, 4.0f, loop.data(), loop.size()).ok());
    AUREA_CHECK(r.e.add_drawing_stroke(*layer, Engine::kDrawEraser, nullptr, 30.0f, xyp.data(), xyp.size()).ok());
    Layer* l = r.layer(*layer);
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    const auto& g = l->shape.vector.groups;
    AUREA_CHECK_EQ(g.size(), usize{4});
    if (g.size() == 4) {
        AUREA_CHECK(g[0].stroke.enabled && !g[0].fill.enabled && g[0].stroke.widthProfile.empty());
        AUREA_CHECK_NEAR(g[0].stroke.width, 12.0, 1e-3);
        AUREA_CHECK(!g[1].stroke.widthProfile.empty());   // a pressão variou
        AUREA_CHECK(g[2].fill.enabled && !g[2].stroke.enabled && g[2].paths[0].path.closed);
        AUREA_CHECK_EQ(g[3].composite, kVectorCompositeErase);
        AUREA_CHECK_NEAR(g[0].fill.paint.color.y, 0.0, 1e-6);
    }
    // Um traço = um desfazer.
    AUREA_CHECK(r.undo());
    l = r.layer(*layer);
    AUREA_CHECK(l && l->shape.vector.groups.size() == 3);
    // Preencher com menos de 3 pontos distintos é recusado sem passo de desfazer.
    const f32 two[6] = {10, 10, 1, 10, 10, 1};
    AUREA_CHECK(!r.e.add_drawing_stroke(*layer, Engine::kDrawFill, red, 4.0f, two, 6).ok());
    // layerId 0 cria a camada de desenho.
    const auto fresh = r.e.add_drawing_stroke(0, Engine::kDrawPen, red, 8.0f, xyp.data(), xyp.size());
    AUREA_CHECK(fresh.ok());
    if (fresh.ok()) AUREA_CHECK_EQ(r.e.vector_layer_flags(*fresh), 3u);
    const auto plain = r.e.add_vector_layer(1);
    AUREA_CHECK(plain.ok());
    if (plain.ok()) AUREA_CHECK_EQ(r.e.vector_layer_flags(*plain), 1u);
}

AUREA_TEST(OmegaContent, EditVectorPathAppliesRulesWithUndo) {
    ContentRig r;
    const auto id = r.e.add_vector_layer(0);   // caminho livre vazio
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    using Op = Engine::PathOp;
    const f32 a[3] = {100, 100, 0}, b[3] = {200, 100, 0}, c[3] = {200, 200, 0};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Append, a, 3, false), 0);
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Append, b, 3, false), 1);
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Append, c, 3, false), 2);
    const f32 ins[2] = {0, 0.5f};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Insert, ins, 2, false), 1);
    Layer* l = r.layer(*id);
    AUREA_CHECK(l && l->shape.vector.groups[0].paths[0].path.v.size() == 4);
    if (l) AUREA_CHECK_NEAR(l->shape.vector.groups[0].paths[0].path.v[1].p.x, 150.0, 1e-3);
    // Arrasto: o 1º envio abre o passo, os seguintes continuam.
    const f32 move1[3] = {5, 0, 1}, move2[3] = {5, 0, 1};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::MovePoints, move1, 3, false), 1);
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::MovePoints, move2, 3, true), 1);
    l = r.layer(*id);
    if (l) AUREA_CHECK_NEAR(l->shape.vector.groups[0].paths[0].path.v[1].p.x, 160.0, 1e-3);
    AUREA_CHECK(r.undo());   // o arrasto inteiro sai de uma vez
    l = r.layer(*id);
    if (l) AUREA_CHECK_NEAR(l->shape.vector.groups[0].paths[0].path.v[1].p.x, 150.0, 1e-3);
    const f32 curve[2] = {1, 1};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::SetType, curve, 2, false), 1);
    const f32 closeIt[1] = {1}, hole[1] = {1};
    AUREA_CHECK(r.e.edit_vector_path(*id, 0, 0, Op::SetClosed, closeIt, 1, false) >= 0);
    AUREA_CHECK(r.e.edit_vector_path(*id, 0, 0, Op::SetExclude, hole, 1, false) >= 0);
    l = r.layer(*id);
    if (l) {
        AUREA_CHECK(l->shape.vector.groups[0].paths[0].path.closed);
        AUREA_CHECK(l->shape.vector.groups[0].paths[0].exclude);
        AUREA_CHECK(l->shape.vector.groups[0].paths[0].path.v[1].out.length() > 1.0f);
    }
    const f32 removeIt[1] = {3};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Remove, removeIt, 1, false), 2);
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Remove, removeIt, 1, false), -1);   // não existe mais
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 3, 0, Op::Remove, removeIt, 1, false), -1);   // grupo errado
}

AUREA_TEST(OmegaContent, TextBoxWidthHandleFollowsAlignment) {
    const std::string roboto = repo_path("engine/assets/fonts/Roboto-Regular.ttf");
    text::set_default_font_path(roboto);
    {
        ContentRig r;
        const auto id = r.e.add_text("Omega");   // nasce centralizado
        AUREA_CHECK(id.ok());
        if (id.ok()) {
            Layer* l = r.layer(*id);
            AUREA_CHECK(l && l->text.alignment == 1 && l->text.boxMode == 0);
            AUREA_CHECK(r.e.drag_text_box_width(*id, 0.0f, 0.0f, false));
            l = r.layer(*id);
            const f32 w0 = l ? l->text.box.w : 0.0f;
            AUREA_CHECK(l && l->text.boxMode == 1);
            const Vec3 pos0 = l ? l->transform.position : Vec3{};
            // Centro: + 2·dx, e a posição não anda.
            AUREA_CHECK(r.e.drag_text_box_width(*id, 30.0f, 0.0f, true));
            l = r.layer(*id);
            if (l) {
                AUREA_CHECK_NEAR(l->text.box.w, w0 + 60.0f, 0.51);
                AUREA_CHECK(l->transform.position == pos0);
            }
            // Esquerda: + dx, e o centro anda meio dx (a borda esquerda fica).
            Command align;
            align.type = CommandType::TextSetAlignment;
            align.text_align = {LayerId::unpack(*id), 0};
            AUREA_CHECK(r.e.apply_command(align).ok());
            l = r.layer(*id);
            const f32 w1 = l ? l->text.box.w : 0.0f;
            const f32 x1 = l ? l->transform.position.x : 0.0f;
            AUREA_CHECK(r.e.drag_text_box_width(*id, 40.0f, 0.0f, false));
            l = r.layer(*id);
            if (l) {
                AUREA_CHECK_NEAR(l->text.box.w, w1 + 40.0f, 0.51);
                AUREA_CHECK_NEAR(l->transform.position.x, x1 + 20.0f, 0.51);
            }
            // Mínimo de 50 px.
            AUREA_CHECK(r.e.drag_text_box_width(*id, -5000.0f, 0.0f, false));
            l = r.layer(*id);
            if (l) AUREA_CHECK_NEAR(l->text.box.w, 50.0, 1e-3);
        }
    }
    text::set_default_font_path("");
}

AUREA_TEST(OmegaContent, ShapeHandleResizesFromCenterOrEdge) {
    ContentRig r;
    const auto id = r.e.add_shape(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    Layer* l = r.layer(*id);
    if (!l) return;
    const f32 w0 = l->shape.bounds.w, h0 = l->shape.bounds.h;
    const Vec3 p0 = l->transform.position;
    // Do centro: o lado oposto espelha (largura + 2·du), a posição fica.
    AUREA_CHECK(r.e.resize_shape_handle(*id, 5, 10.0f, 0.0f, false, true, false));
    l = r.layer(*id);
    AUREA_CHECK_NEAR(l->shape.bounds.w, w0 + 20.0f, 1e-3);
    AUREA_CHECK(l->transform.position == p0);
    // Da borda: largura + du e o centro anda du/2 (a borda esquerda fica).
    AUREA_CHECK(r.e.resize_shape_handle(*id, 5, 10.0f, 0.0f, false, false, false));
    l = r.layer(*id);
    AUREA_CHECK_NEAR(l->shape.bounds.w, w0 + 30.0f, 1e-3);
    AUREA_CHECK_NEAR(l->transform.position.x, p0.x + 5.0f * l->transform.scale.x, 1e-3);
    // Proporção travada num canto: a altura acompanha.
    const f32 ratio = l->shape.bounds.w / l->shape.bounds.h;
    AUREA_CHECK(r.e.resize_shape_handle(*id, 2, 20.0f, 0.0f, true, true, false));
    l = r.layer(*id);
    AUREA_CHECK_NEAR(l->shape.bounds.w / l->shape.bounds.h, ratio, 1e-3);
    (void)h0;
    AUREA_CHECK(r.undo());   // um gesto, um passo
}

AUREA_TEST(OmegaContent, FontCategoryAndScriptsFromTheFile) {
    text::FontEntry roboto, naskh, jp;
    AUREA_CHECK(text::read_font_info(repo_path("engine/assets/fonts/Roboto-Regular.ttf"), roboto));
    AUREA_CHECK(text::read_font_info(repo_path("engine/assets/fonts/NotoNaskhArabic-Regular.ttf"), naskh));
    AUREA_CHECK(text::read_font_info(repo_path("android/app/src/main/assets/NotoSansJP-Regular.otf"), jp));
    std::printf("    Roboto %u/%x, Naskh %u/%x, JP %u/%x\n", static_cast<unsigned>(roboto.category), static_cast<unsigned>(roboto.scripts),
                static_cast<unsigned>(naskh.category), static_cast<unsigned>(naskh.scripts), static_cast<unsigned>(jp.category),
                static_cast<unsigned>(jp.scripts));
    AUREA_CHECK_EQ(roboto.category, u8{text::kFontSans});
    AUREA_CHECK((roboto.scripts & text::kScriptLatin) && (roboto.scripts & text::kScriptGreek) && (roboto.scripts & text::kScriptCyrillic));
    AUREA_CHECK(!(roboto.scripts & text::kScriptArabic));
    AUREA_CHECK(naskh.scripts & text::kScriptArabic);
    AUREA_CHECK((jp.scripts & text::kScriptJapanese) && (jp.scripts & text::kScriptChinese));
    AUREA_CHECK(!(jp.scripts & text::kScriptKorean));
}

AUREA_TEST(OmegaContent, RemovingAnImportedFontDeletesTheFileAndResetsTexts) {
    const std::string source = repo_path("engine/assets/fonts/Roboto-Regular.ttf");
    const std::string copy = (std::filesystem::current_path() / "aurea_omega_fonte_importada.ttf").string();
    std::error_code ec;
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::overwrite_existing, ec);
    AUREA_CHECK(!ec);
    text::set_default_font_path(source);
    {
        ContentRig r;
        const auto font = r.e.import_font(copy.c_str());
        AUREA_CHECK(font.ok());
        const auto id = r.e.add_text("Fonte");
        AUREA_CHECK(id.ok());
        if (font.ok() && id.ok()) {
            AUREA_CHECK(r.e.set_text_font(*id, font->family, font->weight, font->italic, copy));
            AUREA_CHECK(!r.e.remove_imported_font(source));   // não é importada
            AUREA_CHECK(r.e.remove_imported_font(copy));
            AUREA_CHECK(!std::filesystem::exists(copy));
            bool listed = false;
            for (const auto& f : r.e.list_fonts()) listed |= f.path == copy;
            AUREA_CHECK(!listed);
            Layer* l = r.layer(*id);
            AUREA_CHECK(l && l->text.fontPath.empty() && l->text.fontFamily.empty());
        }
    }
    std::filesystem::remove(copy, ec);
    text::set_default_font_path("");
}
