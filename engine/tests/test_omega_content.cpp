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
#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/render/MaskRaster.hpp"
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
    AUREA_CHECK_NEAR(s[0], 5.0, 1e-6);   // kDocVersion (VectorDocument.cpp)
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

// =============================================================================
// 0.5.0 (Alight Motion): morph da máscara com contagens diferentes e a mesma
// topologia em todos os keyframes. Referência: docs/parity/motion-alight-ae-0.5.0.md.
// =============================================================================
namespace {
std::vector<MaskPoint> mask_square(f32 x, f32 y, f32 s) {
    std::vector<MaskPoint> p(4);
    p[0].position = Vec2{x, y}; p[1].position = Vec2{x + s, y};
    p[2].position = Vec2{x + s, y + s}; p[3].position = Vec2{x, y + s};
    return p;
}
void seek_to(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}
} // namespace

AUREA_TEST(OmegaContent, PointKeysAnimateOnlyTheirOwnPoints) {
    const auto square = [](f32 x) {
        BezierPath p; p.closed = true;
        p.v = {BezierVertex{{x, 0.f}, {}, {}}, BezierVertex{{x + 100.f, 0.f}, {}, {}}, BezierVertex{{x + 100.f, 100.f}, {}, {}}};
        return p;
    };
    VectorPath path;
    path.kind = VectorPathKind::Free;
    PathKey a; a.frame = 0; a.path = square(0.f); a.curve = 0;
    PathKey c; c.frame = 30; c.path = square(60.f); c.curve = 0;
    PathKey m; m.frame = 15; m.path = square(0.f); m.path.v[1].p.x = 500.f; m.points = {0, 1, 0}; m.curve = 0;
    path.keys = {a, m, c};
    const f32 ease = vector::path_key_ease(a, .5f);
    const BezierPath mid = vector::path_at(path, 15.0);
    AUREA_CHECK_NEAR(mid.v[1].p.x, 500.f, 1e-3);                        // o ponto do key
    AUREA_CHECK_NEAR(mid.v[0].p.x, 60.f * ease, 1e-3);                  // os outros ignoram o key do meio
    AUREA_CHECK_NEAR(mid.v[2].p.x, 100.f + 60.f * ease, 1e-3);
    // Fora dos keys: segura o valor do ponto.
    AUREA_CHECK_NEAR(vector::path_at(path, -5.0).v[1].p.x, 100.f, 1e-3);
    AUREA_CHECK_NEAR(vector::path_at(path, 40.0).v[1].p.x, 160.f, 1e-3);
    // Sem máscara: o morph normal continua igual.
    VectorPath plain = path;
    plain.keys = {a, c};
    AUREA_CHECK_NEAR(vector::path_at(plain, 15.0).v[1].p.x, 100.f + 60.f * ease, 1e-3);
    // A máscara passa pelo documento.
    VectorData d;
    VectorGroup g; g.paths.push_back(path); d.groups.push_back(g);
    std::vector<f32> s; std::string names;
    encode_document(d, s, names);
    VectorData back;
    AUREA_CHECK(decode_document(s.data(), s.size(), names, back));
    AUREA_CHECK(back == d);
    AUREA_CHECK(back.groups[0].paths[0].keys[1].points == (std::vector<u8>{0, 1, 0}));

    // Pela API: criar, somar, tirar e editar só marca o ponto mexido.
    ContentRig r;
    const auto id = r.e.add_vector_layer(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    using Op = Engine::PathOp;
    const f32 p0[] = {0.f, 0.f, 0.f}, p1[] = {100.f, 0.f, 0.f}, p2[] = {100.f, 100.f, 0.f};
    r.e.edit_vector_path(*id, 0, 0, Op::Append, p0, 3, false);
    r.e.edit_vector_path(*id, 0, 0, Op::Append, p1, 3, false);
    r.e.edit_vector_path(*id, 0, 0, Op::Append, p2, 3, false);
    const auto keys = [&]() -> std::vector<PathKey>& { return r.layer(*id)->shape.vector.groups[0].paths[0].keys; };
    const u32 one[] = {1};
    AUREA_CHECK(r.e.toggle_vector_path_points_key(*id, 0, 0, one, 1));
    AUREA_CHECK_EQ(keys().size(), usize{1});
    AUREA_CHECK(keys()[0].points == (std::vector<u8>{0, 1, 0}));
    // Mover o ponto 2 no mesmo key: ele entra na máscara.
    const f32 move2[] = {5.f, 0.f, 2.f};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::MovePoints, move2, 3, false), 2);
    AUREA_CHECK(keys()[0].points == (std::vector<u8>{0, 1, 1}));
    // Inserir leva a máscara junto e o novo ponto já vale.
    const f32 ins[] = {0.f, 0.5f};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Insert, ins, 2, false), 1);
    AUREA_CHECK(keys()[0].points == (std::vector<u8>{0, 1, 1, 1}));
    // Remover idem.
    const f32 rem[] = {2.f};
    AUREA_CHECK(r.e.edit_vector_path(*id, 0, 0, Op::Remove, rem, 1, false) >= 0);
    AUREA_CHECK(keys()[0].points == (std::vector<u8>{0, 1, 1}));
    // Tirar todos os pontos apaga o key; desfazer volta.
    const u32 both[] = {1, 2};
    AUREA_CHECK(r.e.toggle_vector_path_points_key(*id, 0, 0, both, 2));
    AUREA_CHECK(keys().empty());
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(keys().size(), usize{1});
    // Índice inválido não cria nada.
    const u32 bad[] = {99};
    AUREA_CHECK(!r.e.toggle_vector_path_points_key(*id, 0, 0, bad, 1));
}

AUREA_TEST(OmegaContent, OpenAtCutsTheChosenSegmentInEveryKey) {
    ContentRig r;
    const auto id = r.e.add_vector_layer(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    using Op = Engine::PathOp;
    for (const f32 x : {0.f, 100.f, 200.f, 300.f}) {
        const f32 p[] = {x, 0.f, 0.f};
        r.e.edit_vector_path(*id, 0, 0, Op::Append, p, 3, false);
    }
    const f32 close[] = {1.f};
    r.e.edit_vector_path(*id, 0, 0, Op::SetClosed, close, 1, false);
    VectorPath& vp = r.layer(*id)->shape.vector.groups[0].paths[0];
    vp.keys.resize(2);
    vp.keys[0].frame = 0; vp.keys[0].path = vp.path;
    vp.keys[1].frame = 10; vp.keys[1].path = vp.path;
    vp.keys[1].path.v[0].p.y = 50.f;
    vp.keys[0].points = {1, 0, 0, 0};
    const f32 cut[] = {1.f};                                       // trecho entre os pontos 1 e 2
    AUREA_CHECK(r.e.edit_vector_path(*id, 0, 0, Op::OpenAt, cut, 1, false) >= 0);
    const VectorPath& after = r.layer(*id)->shape.vector.groups[0].paths[0];
    AUREA_CHECK(!after.path.closed);
    AUREA_CHECK_NEAR(after.path.v[0].p.x, 200.f, 1e-3f);           // o corte vira as pontas
    AUREA_CHECK_NEAR(after.path.v[3].p.x, 100.f, 1e-3f);
    for (const PathKey& k : after.keys) {
        AUREA_CHECK(!k.path.closed);
        AUREA_CHECK_NEAR(k.path.v[0].p.x, 200.f, 1e-3f);
    }
    AUREA_CHECK_NEAR(after.keys[1].path.v[2].p.y, 50.f, 1e-3f);    // o ponto 0 continua sendo ele
    AUREA_CHECK(after.keys[0].points == (std::vector<u8>{0, 0, 1, 0}));
    const f32 bad[] = {9.f};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::OpenAt, bad, 1, false), -1);
}

AUREA_TEST(OmegaContent, SetFirstRotatesOnlyTheKeyAtThePlayhead) {
    ContentRig r;
    const auto id = r.e.add_vector_layer(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    using Op = Engine::PathOp;
    for (const f32 x : {0.f, 100.f, 200.f}) {
        const f32 p[] = {x, 0.f, 0.f};
        r.e.edit_vector_path(*id, 0, 0, Op::Append, p, 3, false);
    }
    const f32 close[] = {1.f};
    r.e.edit_vector_path(*id, 0, 0, Op::SetClosed, close, 1, false);
    VectorPath& vp = r.layer(*id)->shape.vector.groups[0].paths[0];
    vp.keys.resize(2);
    vp.keys[0].frame = 0; vp.keys[0].path = vp.path; vp.keys[0].points = {1, 0, 0};
    vp.keys[1].frame = 10; vp.keys[1].path = vp.path;
    seek_to(r.e, 0);
    const f32 first[] = {2.f};
    AUREA_CHECK(r.e.edit_vector_path(*id, 0, 0, Op::SetFirst, first, 1, false) >= 0);
    const VectorPath& after = r.layer(*id)->shape.vector.groups[0].paths[0];
    AUREA_CHECK_NEAR(after.keys[0].path.v[0].p.x, 200.f, 1e-3f);   // só o key do cabeçote gira
    AUREA_CHECK_NEAR(after.keys[1].path.v[0].p.x, 0.f, 1e-3f);
    AUREA_CHECK(after.keys[0].points == (std::vector<u8>{0, 1, 0})); // a escolha de pontos gira junto
}

AUREA_TEST(OmegaContent, MaskPointKeysAnimateOnlyTheirOwnPoints) {
    Mask m;
    m.closed = true;
    MaskPathKey a, b;
    a.frame = 0; a.points = mask_square(0, 0, 100);
    b.frame = 10; b.points = mask_square(0, 0, 100);
    b.points[1].position.x = 300.f;
    b.points[2].position.x = 300.f;
    b.pointMask = {0, 1, 0, 0};                                    // só o ponto 1 anima neste key
    m.pathKeys = {a, b};
    std::vector<MaskPoint> out;
    mask::evaluate_path(m, 5.0, out);
    AUREA_CHECK_EQ(out.size(), usize{4});
    AUREA_CHECK(out[1].position.x > 100.f);
    AUREA_CHECK_NEAR(out[2].position.x, 100.f, 1e-3f);             // o 2 ignora o key que não é dele

    ContentRig r;
    const auto id = r.e.add_vector_layer(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const f32 sq[24] = {0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 100, 100, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
    const i32 mid = r.e.add_mask(*id, sq, 4, true);
    AUREA_CHECK(mid >= 0);
    if (mid < 0) return;
    const auto keys = [&]() -> std::vector<MaskPathKey>& { return r.layer(*id)->masks[0].pathKeys; };
    const u32 one[] = {1};
    AUREA_CHECK(r.e.toggle_mask_path_points_key(*id, static_cast<u32>(mid), one, 1));
    AUREA_CHECK_EQ(keys().size(), usize{1});
    AUREA_CHECK(keys()[0].pointMask == (std::vector<u8>{0, 1, 0, 0}));
    using Op = Engine::PathOp;
    const f32 ins[] = {0.f, 0.5f};
    AUREA_CHECK_EQ(r.e.edit_mask_path(*id, static_cast<u32>(mid), Op::Insert, ins, 2, false), 1);
    AUREA_CHECK(keys()[0].pointMask == (std::vector<u8>{0, 1, 1, 0, 0}));
    const u32 two[] = {1, 2};
    AUREA_CHECK(r.e.toggle_mask_path_points_key(*id, static_cast<u32>(mid), two, 2));
    AUREA_CHECK(keys().empty());
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(keys().size(), usize{1});
    const u32 bad[] = {99};
    AUREA_CHECK(!r.e.toggle_mask_path_points_key(*id, static_cast<u32>(mid), bad, 1));
}

AUREA_TEST(OmegaContent, MaskMorphsBetweenKeysWithDifferentPointCounts) {
    Mask m;
    m.closed = true;
    MaskPathKey a, b;
    a.frame = 0; a.points = mask_square(0, 0, 100);                // 4 pontos
    b.frame = 10;
    for (int i = 0; i < 8; ++i) {                                  // 8 pontos: octógono de raio 60 em (50, 50)
        const f32 ang = static_cast<f32>(i) * 0.7853981634f;
        MaskPoint q;
        q.position = Vec2{50 + 60 * std::cos(ang), 50 + 60 * std::sin(ang)};
        b.points.push_back(q);
    }
    m.pathKeys = {a, b};
    std::vector<MaskPoint> out;
    mask::evaluate_path(m, 0.0, out);
    AUREA_CHECK_EQ(out.size(), usize{4});                          // no key: a forma dele, como estava
    mask::evaluate_path(m, 10.0, out);
    AUREA_CHECK_EQ(out.size(), usize{8});
    // No meio: morph de verdade (antes segurava o quadrado até o quadro 10).
    mask::evaluate_path(m, 5.0, out);
    AUREA_CHECK_EQ(out.size(), usize{8});
    std::vector<MaskPoint> early;
    mask::evaluate_path(m, 1.0, early);
    AUREA_CHECK_EQ(early.size(), usize{8});
    bool differsFromSquare = false;
    for (const MaskPoint& q : early)
        differsFromSquare |= !(std::fabs(q.position.x) < 1e-3f || std::fabs(q.position.x - 100) < 1e-3f || std::fabs(q.position.x - 50) < 1e-3f);
    AUREA_CHECK(differsFromSquare);
    // O mesmo resultado do morph do vetor (uma regra só).
    BezierPath pa, pb;
    for (const MaskPoint& q : a.points) pa.v.push_back(BezierVertex{q.position, q.inTangent, q.outTangent});
    for (const MaskPoint& q : b.points) pb.v.push_back(BezierVertex{q.position, q.inTangent, q.outTangent});
    const BezierPath expect = lerp_path(pa, pb, path_key_ease(mask::ease_of(a), 0.5f));
    for (usize k = 0; k < out.size(); ++k) {
        AUREA_CHECK_NEAR(out[k].position.x, expect.v[k].p.x, 1e-4);
        AUREA_CHECK_NEAR(out[k].position.y, expect.v[k].p.y, 1e-4);
    }
    // Key de segurar continua segurando, com qualquer contagem.
    m.pathKeys[0].interp = 0;
    mask::evaluate_path(m, 5.0, out);
    AUREA_CHECK_EQ(out.size(), usize{4});
}

AUREA_TEST(OmegaContent, VectorPointEditsKeepTheSameTopologyInEveryKey) {
    ContentRig r;
    const auto id = r.e.add_vector_layer(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    using Op = Engine::PathOp;
    Layer* l = r.layer(*id);
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    const i64 k0 = l->local_time(FrameIndex{0}).value, k1 = l->local_time(FrameIndex{20}).value;
    VectorPath& vp = l->shape.vector.groups[0].paths[0];
    vp.kind = VectorPathKind::Free;
    auto tri = [](f32 s) {
        BezierPath p;
        p.closed = true;
        p.v = {BezierVertex{Vec2{0, 0}}, BezierVertex{Vec2{s, 0}}, BezierVertex{Vec2{0, s}}};
        return p;
    };
    PathKey a; a.frame = k0; a.path = tri(100);
    PathKey b; b.frame = k1; b.path = tri(200);
    vp.keys = {a, b};
    seek_to(r.e, 0);
    // Inserir no trecho 0 em t = 0,5 com o cabeçote no 1º key: os dois keys
    // ganham o ponto, cada um no meio do PRÓPRIO trecho 0.
    const f32 ins[2] = {0, 0.5f};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Insert, ins, 2, false), 1);
    l = r.layer(*id);
    if (!l) return;
    const std::vector<PathKey>& keys = l->shape.vector.groups[0].paths[0].keys;
    AUREA_CHECK_EQ(keys.size(), usize{2});
    AUREA_CHECK_EQ(keys[0].path.v.size(), usize{4});
    AUREA_CHECK_EQ(keys[1].path.v.size(), usize{4});
    AUREA_CHECK_NEAR(keys[0].path.v[1].p.x, 50.0, 1e-3);
    AUREA_CHECK_NEAR(keys[1].path.v[1].p.x, 100.0, 1e-3);       // a forma do 2º key não mudou
    // Acrescentar: ponto parado no mesmo lugar em todos os keys.
    const f32 app[3] = {-30, -40, 0};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::Append, app, 3, false), 4);
    l = r.layer(*id);
    const std::vector<PathKey>& k2 = l->shape.vector.groups[0].paths[0].keys;
    AUREA_CHECK_EQ(k2[0].path.v.size(), usize{5});
    AUREA_CHECK_EQ(k2[1].path.v.size(), usize{5});
    AUREA_CHECK_NEAR(k2[1].path.v[4].p.x, -30.0, 1e-4);
    AUREA_CHECK_NEAR(k2[1].path.v[4].p.y, -40.0, 1e-4);
    // Apagar o ponto inserido: sai dos dois.
    const f32 rem[1] = {1};
    AUREA_CHECK(r.e.edit_vector_path(*id, 0, 0, Op::Remove, rem, 1, false) >= 0);
    l = r.layer(*id);
    const std::vector<PathKey>& k3 = l->shape.vector.groups[0].paths[0].keys;
    AUREA_CHECK_EQ(k3[0].path.v.size(), usize{4});
    AUREA_CHECK_EQ(k3[1].path.v.size(), usize{4});
    AUREA_CHECK_NEAR(k3[1].path.v[1].p.x, 200.0, 1e-4);         // o (200, 0) original voltou para o índice 1
    AUREA_CHECK_NEAR(k3[1].path.v[1].p.y, 0.0, 1e-4);
    // Mover um ponto continua sendo só do key do cabeçote.
    const f32 mv[3] = {7, 0, 0};
    AUREA_CHECK_EQ(r.e.edit_vector_path(*id, 0, 0, Op::MovePoints, mv, 3, false), 0);
    l = r.layer(*id);
    const std::vector<PathKey>& k4 = l->shape.vector.groups[0].paths[0].keys;
    AUREA_CHECK_NEAR(k4[0].path.v[0].p.x, 7.0, 1e-4);
    AUREA_CHECK_NEAR(k4[1].path.v[0].p.x, 0.0, 1e-4);
    // Um desfazer por operação: volta o "apagar" nos DOIS keys.
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.undo());
    l = r.layer(*id);
    const std::vector<PathKey>& k5 = l->shape.vector.groups[0].paths[0].keys;
    AUREA_CHECK_EQ(k5[0].path.v.size(), usize{5});
    AUREA_CHECK_EQ(k5[1].path.v.size(), usize{5});
}

AUREA_TEST(OmegaContent, MaskPointEditsKeepTheSameTopologyInEveryKey) {
    ContentRig r;
    const auto id = r.e.add_shape(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const f32 pts[24] = {0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 100, 100, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
    const i32 mid = r.e.add_mask(*id, pts, 4, true);
    AUREA_CHECK(mid >= 0);
    if (mid < 0) return;
    Layer* l = r.layer(*id);
    if (!l || l->masks.empty()) return;
    const i64 k0 = l->local_time(FrameIndex{0}).value, k1 = l->local_time(FrameIndex{20}).value;
    MaskPathKey a, b;
    a.frame = k0; a.points = mask_square(0, 0, 100);
    b.frame = k1; b.points = mask_square(0, 0, 300);
    l->masks[0].pathKeys = {a, b};
    seek_to(r.e, 0);
    using Op = Engine::PathOp;
    const f32 ins[2] = {0, 0.5f};
    AUREA_CHECK_EQ(r.e.edit_mask_path(*id, static_cast<u32>(mid), Op::Insert, ins, 2, false), 1);
    l = r.layer(*id);
    if (!l) return;
    const Mask& m = l->masks[0];
    AUREA_CHECK_EQ(m.pathKeys.size(), usize{2});
    AUREA_CHECK_EQ(m.pathKeys[0].points.size(), usize{5});
    AUREA_CHECK_EQ(m.pathKeys[1].points.size(), usize{5});
    AUREA_CHECK_NEAR(m.pathKeys[0].points[1].position.x, 50.0, 1e-3);
    AUREA_CHECK_NEAR(m.pathKeys[1].points[1].position.x, 150.0, 1e-3);
    // Entre os keys: 5 pontos que casam (o novo anda de (50,0) a (150,0)).
    std::vector<MaskPoint> mid10;
    mask::evaluate_path(m, static_cast<f64>(k0 + (k1 - k0) / 2), mid10);
    AUREA_CHECK_EQ(mid10.size(), usize{5});
    AUREA_CHECK(mid10[1].position.x > 50.0f && mid10[1].position.x < 150.0f);
    AUREA_CHECK_NEAR(mid10[1].position.y, 0.0, 1e-3);
    // Desfazer tira dos dois keys.
    AUREA_CHECK(r.undo());
    l = r.layer(*id);
    if (l) {
        AUREA_CHECK_EQ(l->masks[0].pathKeys[0].points.size(), usize{4});
        AUREA_CHECK_EQ(l->masks[0].pathKeys[1].points.size(), usize{4});
    }
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

AUREA_TEST(OmegaContent, EveryShapeHandleRoundTripsItsOwnValue) {
    // Soltar a alça onde ela está não pode mudar o parâmetro, em caixa quadrada ou esticada.
    const u32 types[] = {shape::kRect, shape::kStar, shape::kCross, shape::kRing, shape::kPie, shape::kArc, shape::kFlower, shape::kArrow,
                         shape::kDoubleArrow, shape::kTrapezoid, shape::kParallelogram, shape::kGear, shape::kLine, shape::kDiamond,
                         shape::kHeart, shape::kSeal, shape::kBubble, shape::kBolt, shape::kWave, shape::kMoon, shape::kTeardrop};
    u32 checked = 0;
    for (const Vec2 box : {Vec2{200.f, 200.f}, Vec2{320.f, 180.f}, Vec2{120.f, 300.f}}) {
        for (const u32 type : types) {
            ShapeData sh;
            sh.shapeType = type;
            sh.bounds.w = box.x; sh.bounds.h = box.y;
            sh.cornerRadius = 20.f; sh.innerRadius = .4f; sh.points = 6.f; sh.sweep = 200.f; sh.head = .3f; sh.shaft = .4f;
            sh.thickness = .25f; sh.tip = .35f; sh.depth = .3f; sh.amplitude = .6f;
            shape::ParamHandle h[shape::kMaxParamHandles];
            const u32 n = shape::param_handles(sh, h);
            AUREA_CHECK(n >= 1u);
            for (u32 i = 0; i < n; ++i) {
                f32 v = -1.f;
                AUREA_CHECK(shape::value_from_handle(sh, h[i].param, h[i].pos, v));
                AUREA_CHECK_NEAR(v, shape::param_value(sh, h[i].param), 2e-3);
                ++checked;
            }
            f32 unused = 0.f;
            AUREA_CHECK(!shape::value_from_handle(sh, shape::kParamSeed, Vec2{0.f, 0.f}, unused));
        }
    }
    AUREA_CHECK(checked > 80u);
    // Formas sem parâmetro de silhueta só têm a caixa.
    for (const u32 type : {shape::kEllipse, shape::kPolygon, shape::kRightTriangle, shape::kBlob}) {
        ShapeData sh; sh.shapeType = type;
        shape::ParamHandle h[shape::kMaxParamHandles];
        AUREA_CHECK_EQ(shape::param_handles(sh, h), 0u);
    }
}

AUREA_TEST(OmegaContent, ShapeHandleGeometryFollowsTheSilhouette) {
    ShapeData sh;
    sh.bounds.w = 200.f; sh.bounds.h = 200.f;
    shape::ParamHandle h[shape::kMaxParamHandles];
    f32 v = 0.f;
    // Estrela: a alça está no vale do topo; descer o dedo fecha o raio interno.
    sh.shapeType = shape::kStar; sh.innerRadius = .5f; sh.points = 5.f;
    AUREA_CHECK_EQ(shape::param_handles(sh, h), 1u);
    AUREA_CHECK_NEAR(h[0].pos.y, -50.f, 1e-3);
    AUREA_CHECK(std::fabs(shape::signed_distance(sh, h[0].pos, Vec2{100.f, 100.f})) < .5f);   // em cima da borda
    AUREA_CHECK(shape::value_from_handle(sh, shape::kParamInner, Vec2{0.f, -20.f}, v));
    AUREA_CHECK_NEAR(v, .2f, 1e-4);
    // Limite do parâmetro vale no arrasto.
    AUREA_CHECK(shape::value_from_handle(sh, shape::kParamInner, Vec2{0.f, -500.f}, v));
    AUREA_CHECK_NEAR(v, .95f, 1e-4);
    // Fatia: cruzar o leste não pula de 360 para 0.
    sh.shapeType = shape::kPie; sh.sweep = 350.f;
    AUREA_CHECK(shape::value_from_handle(sh, shape::kParamSweep, Vec2{100.f, 5.f}, v));
    AUREA_CHECK_NEAR(v, 360.f, 1e-3);
    sh.sweep = 10.f;
    AUREA_CHECK(shape::value_from_handle(sh, shape::kParamSweep, Vec2{100.f, -5.f}, v));
    AUREA_CHECK_NEAR(v, 1.f, 1e-3);
    // Em todas: a alça da borda real cai em cima do contorno da forma.
    sh.shapeType = shape::kRing; sh.innerRadius = .6f;
    AUREA_CHECK_EQ(shape::param_handles(sh, h), 1u);
    AUREA_CHECK(std::fabs(shape::signed_distance(sh, h[0].pos, Vec2{100.f, 100.f})) < .5f);
    sh.shapeType = shape::kMoon; sh.depth = .25f;
    AUREA_CHECK_EQ(shape::param_handles(sh, h), 1u);
    AUREA_CHECK_NEAR(h[0].pos.x, -50.f, 1e-3);
    AUREA_CHECK(std::fabs(shape::signed_distance(sh, h[0].pos, Vec2{100.f, 100.f})) < .5f);
}

AUREA_TEST(OmegaContent, ShapeParamHandleDragRespectsKeysAndUndo) {
    ContentRig r;
    const auto id = r.e.add_shape(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    Layer* l = r.layer(*id);
    if (!l) return;
    l->shape.shapeType = shape::kStar;
    l->shape.points = 5.f;
    l->shape.innerRadius = .5f;
    l->shape.bounds.w = l->shape.bounds.h = 200.f;
    f32 out[shape::kMaxParamHandles * Engine::kShapeHandleFloats] = {};
    AUREA_CHECK_EQ(r.e.query_shape_param_handles(*id, out, shape::kMaxParamHandles * Engine::kShapeHandleFloats), 1u);
    AUREA_CHECK_NEAR(out[1], -50.f, 1e-3);
    AUREA_CHECK_EQ(static_cast<u32>(out[2]), static_cast<u32>(shape::kParamInner));
    // Um arrasto em vários passos é um desfazer só.
    AUREA_CHECK(r.e.drag_shape_param_handle(*id, shape::kParamInner, 0.f, -40.f, false));
    AUREA_CHECK(r.e.drag_shape_param_handle(*id, shape::kParamInner, 0.f, -30.f, true));
    AUREA_CHECK_NEAR(r.layer(*id)->shape.innerRadius, .3f, 1e-4);
    AUREA_CHECK(r.undo());
    AUREA_CHECK_NEAR(r.layer(*id)->shape.innerRadius, .5f, 1e-4);
    // Parâmetro sem alça nesta forma: recusado, sem mexer em nada.
    AUREA_CHECK(!r.e.drag_shape_param_handle(*id, shape::kParamSweep, 10.f, 10.f, false));
    // Animado: grava no keyframe e a alça acompanha o valor do instante.
    AUREA_CHECK(r.e.toggle_shape_param_key(*id, shape::kParamInner));
    AUREA_CHECK(r.e.drag_shape_param_handle(*id, shape::kParamInner, 0.f, -70.f, false));
    const Track* tr = r.layer(*id)->tracks.find(TrackProperty::ShapeParam, 0, shape::kParamInner);
    AUREA_CHECK(tr && tr->keys.size() == 1u);
    if (tr && !tr->keys.empty()) AUREA_CHECK_NEAR(tr->keys[0].value, .7f, 1e-4);
    AUREA_CHECK_EQ(r.e.query_shape_param_handles(*id, out, shape::kMaxParamHandles * Engine::kShapeHandleFloats), 1u);
    AUREA_CHECK_NEAR(out[1], -70.f, 1e-3);
    // Capacidade curta não escreve fora.
    AUREA_CHECK_EQ(r.e.query_shape_param_handles(*id, out, 2), 0u);
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
