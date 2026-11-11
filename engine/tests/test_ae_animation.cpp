// =============================================================================
//  AE animation (2026-11-11, gap_system itens 5, 6 e 7): keys do texto-fonte
//  (congelados, desfazer, arquivo), as funções de espaço das expressões
//  (toComp/fromComp/toWorld/fromWorld + Vec, lookAt, sourceRectAtTime,
//  comp("Nome")), Auto-Orientar (caminho / câmera) e a câmera de dois nós.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include "aurea/timeline/Timeline.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;

namespace {

using TP = TrackProperty;

struct Rig {
    Engine e;
    bool ok = false;
    Rig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(640, 360, 30.0, nullptr).ok();
    }
    ~Rig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void seek(i64 frame) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.apply_command(s).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    void set_text(u64 id, const char* s) {
        Command c;
        c.type = CommandType::TextSetContent;
        c.layer_ref.layer = LayerId::unpack(id);
        AUREA_CHECK(e.apply_command(c, s).ok());
    }
    std::string shown(u64 id) {
        TextData t;
        return e.query_text(id, t) ? t.content : std::string("<falhou>");
    }
    static std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
};

/// Timeline sem motor (o arranjo do test_expr): camadas montadas à mão.
struct ExprRig {
    Timeline tl;
    Composition* comp = nullptr;
    ExprRig(u32 w = 1920, u32 h = 1080) {
        const CompositionId id = tl.create_composition("C", w, h, 30.0);
        tl.set_current(id);
        comp = tl.composition(id);
    }
    Layer* add(LayerKind kind, const char* name, LayerId* outId = nullptr) {
        const LayerId id = comp->add_layer(kind, name);
        Layer* l = comp->layer(id);
        l->start = FrameIndex{0};
        l->end = FrameIndex{300};
        if (outId) *outId = id;
        return l;
    }
    /// Valor da expressão `src` na trilha `p` da camada `l`, no quadro `frame`.
    f32 eval(Layer* l, TrackProperty p, const char* src, i64 frame = 0) {
        Track& t = l->tracks.get_or_create(p);
        t.expression = expr::compile(src);
        t.expressionEnabled = true;
        const expr::Scope scope(tl);
        const f32 v = t.value_or(FrameIndex{frame}, -12345.0f);
        t.expression.reset();
        return v;
    }
    std::string error(Layer* l, TrackProperty p, const char* src) {
        Track& t = l->tracks.get_or_create(p);
        t.expression = expr::compile(src);
        t.expressionEnabled = true;
        {
            const expr::Scope scope(tl);
            (void)t.value_or(FrameIndex{0}, 0.0f);
        }
        const std::string msg = t.expression->error().message;
        t.expression.reset();
        return msg;
    }
};

Vec3 axis(const Mat4& m, int c) {
    return Vec3{m.col[c].x, m.col[c].y, m.col[c].z}.normalized();
}

TextKey key(i64 frame, const char* content, Vec4 color = Vec4{1, 1, 1, 1}) {
    TextKey k;
    k.frame = frame;
    k.content = content;
    k.color = color;
    return k;
}

} // namespace

// -----------------------------------------------------------------------------
// Texto-fonte: amostragem congelada (sem fonte, sem motor)
// -----------------------------------------------------------------------------
AUREA_TEST(AeAnimation, SourceTextKeysHoldBeforeBetweenAndAfter) {
    TextData t;
    t.content = "base";
    TextData out;
    AUREA_CHECK(!text::apply_source_text(t, 5, out));   // sem keys: nada muda
    AUREA_CHECK_EQ(text::source_key_index(t, 5), -1);
    (void)text::upsert_source_key(t, key(10, "dez", Vec4{1, 0, 0, 1}));
    (void)text::upsert_source_key(t, key(0, "zero"));
    (void)text::upsert_source_key(t, key(20, "vinte"));
    AUREA_CHECK_EQ(t.sourceKeys.size(), 3u);
    AUREA_CHECK_EQ(t.sourceKeys[0].frame, i64{0});
    AUREA_CHECK_EQ(t.sourceKeys[2].frame, i64{20});
    // Antes do 1º key vale o 1º; entre keys, o anterior (hold); depois, o último.
    auto at = [&](i64 f) { TextData o; AUREA_CHECK(text::apply_source_text(t, f, o)); return o; };
    AUREA_CHECK(at(-7).content == "zero");
    AUREA_CHECK(at(0).content == "zero");
    AUREA_CHECK(at(9).content == "zero");
    AUREA_CHECK(at(10).content == "dez");
    AUREA_CHECK(at(10).color.y == 0.0f);   // o estilo do key vale junto
    AUREA_CHECK(at(19).content == "dez");
    AUREA_CHECK(at(20).content == "vinte");
    AUREA_CHECK(at(1000).content == "vinte");
    // Mesmo quadro = troca, não duplica.
    (void)text::upsert_source_key(t, key(10, "DEZ"));
    AUREA_CHECK_EQ(t.sourceKeys.size(), 3u);
    AUREA_CHECK(at(12).content == "DEZ");
    // A chave do cache de pixels muda junto com o key (o layout é refeito).
    AUREA_CHECK(text::raster_key(at(5), 1.0f) != text::raster_key(at(12), 1.0f));
    AUREA_CHECK(text::raster_key(at(12), 1.0f) == text::raster_key(at(15), 1.0f));
    std::printf("hold: antes/entre/depois dos keys, estilo junto, chave de cache por key; ");
}

// -----------------------------------------------------------------------------
// Texto-fonte no motor: adicionar, editar (auto-key), apagar, mover, desfazer
// -----------------------------------------------------------------------------
AUREA_TEST(AeAnimation, SourceTextEngineCommandsAreUndoable) {
    Rig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const auto added = r.e.add_text("AB");
    if (!added.ok()) { std::printf("(sem fonte no host: pulado) "); return; }
    const u64 id = *added;
    r.L(id)->end = FrameIndex{60};
    r.seek(0);
    AUREA_CHECK_EQ(r.e.add_text_source_key(id), i64{0});
    i64 frames[8]{};
    AUREA_CHECK_EQ(r.e.query_text_source_keys(id, frames, 8), 1u);
    // Com keys, editar o texto no cabeçote cria o key ali (cronômetro do AE).
    r.seek(10);
    r.set_text(id, "XYZ");
    AUREA_CHECK_EQ(r.e.query_text_source_keys(id, frames, 8), 2u);
    AUREA_CHECK_EQ(frames[1], i64{10});
    r.seek(5);
    AUREA_CHECK(r.shown(id) == "AB");
    r.seek(10);
    AUREA_CHECK(r.shown(id) == "XYZ");
    r.seek(50);
    AUREA_CHECK(r.shown(id) == "XYZ");
    // A timeline vê os keys como trilha virtual congelada.
    bridge::KeyframeRow rows[16];
    const u32 n = r.e.query_keyframes(id, rows, 16);
    u32 sourceRows = 0;
    for (u32 i = 0; i < n; ++i) if (rows[i].property == static_cast<u32>(TrackProperty::TextSource)) {
        ++sourceRows;
        AUREA_CHECK_EQ(rows[i].interpolation, static_cast<u32>(Interpolation::Hold));
    }
    AUREA_CHECK_EQ(sourceRows, 2u);
    Engine::KeyframeNavigation nav;
    AUREA_CHECK(r.e.query_keyframe_navigation(id, static_cast<u32>(TrackProperty::TextSource), 0, 0, 5, nav));
    AUREA_CHECK_EQ(nav.visibleCount, 2u);
    // Trocar o conteúdo do key; desfazer volta.
    AUREA_CHECK(r.e.set_text_source_key(id, 10, "Q"));
    std::string content;
    AUREA_CHECK(r.e.text_source_key_content(id, 10, content) && content == "Q");
    r.undo();
    AUREA_CHECK(r.e.text_source_key_content(id, 10, content) && content == "XYZ");
    // Mover pelo comando de keyframe da timeline (um passo de desfazer).
    Command mv;
    mv.type = CommandType::KeyframeMove;
    mv.keyframe_move.track = TrackRef{LayerId::unpack(id), TrackProperty::TextSource, 0, 0};
    mv.keyframe_move.fromTime = FrameIndex{10};
    mv.keyframe_move.toTime = FrameIndex{20};
    AUREA_CHECK(r.e.apply_command(mv).ok());
    r.seek(15);
    AUREA_CHECK(r.shown(id) == "AB");
    r.seek(20);
    AUREA_CHECK(r.shown(id) == "XYZ");
    r.undo();
    r.seek(15);
    AUREA_CHECK(r.shown(id) == "XYZ");
    // Apagar o último key deixa parado o texto do cabeçote.
    AUREA_CHECK(r.e.delete_text_source_key(id, 0));
    r.seek(3);
    AUREA_CHECK(r.e.delete_text_source_key(id, 10));
    AUREA_CHECK_EQ(r.e.query_text_source_keys(id, frames, 8), 0u);
    AUREA_CHECK(r.L(id)->text.content == "XYZ");
    r.undo();
    AUREA_CHECK_EQ(r.e.query_text_source_keys(id, frames, 8), 1u);
    r.undo();
    AUREA_CHECK_EQ(r.e.query_text_source_keys(id, frames, 8), 2u);
    // A trilha não aceita expressão nem cria Track de verdade.
    AUREA_CHECK(!r.e.set_expression(id, static_cast<u32>(TrackProperty::TextSource), 0, 0, "1").ok());
    AUREA_CHECK(r.L(id)->tracks.find(TrackProperty::TextSource) == nullptr);
    std::printf("keys: auto-key ao editar, mover/trocar/apagar com desfazer, trilha congelada na timeline; ");
}

// -----------------------------------------------------------------------------
// Arquivo: v68 ida e volta; padrão = projeto antigo
// -----------------------------------------------------------------------------
AUREA_TEST(AeAnimation, AdditionsRoundTripInVersion68AndDefaultsStayOff) {
    AUREA_CHECK_EQ(kTimelineSectionVersion >= 68u, true);
    Rig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 plain = *r.e.add_null(false);
    const u64 orient = *r.e.add_null(true);
    const u64 cam = *r.e.add_camera();
    AUREA_CHECK(r.e.set_layer_auto_orient(orient, 1));
    AUREA_CHECK(!r.e.set_layer_auto_orient(cam, 2));   // câmera não se vira para a câmera
    AUREA_CHECK(r.e.set_camera_two_node(cam, true));
    AUREA_CHECK(r.e.set_camera_poi(cam, 123.5f, -45.0f, 600.0f));
    const auto added = r.e.add_text("um");
    u64 text = 0;
    if (added.ok()) {
        text = *added;
        r.seek(0);
        AUREA_CHECK_EQ(r.e.add_text_source_key(text), i64{0});
        r.seek(7);
        r.set_text(text, "dois");
        AUREA_CHECK(r.e.set_text_span(text, 0, 2, true, Vec4{0, 1, 0, 1}, 700, 1.5f));
        // B5: o eixo da inclinação do animador tem valor parado (antes só trilha).
        AUREA_CHECK_EQ(r.e.add_text_animator(text, kTextPropSkew), 0);
        AUREA_CHECK(r.e.set_text_anim_param(text, 0, text::kSkewAxis, 30.0f));
    }
    const std::string path = Rig::temp("aurea_ae_animation_v68.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* p = r.L(plain);
    const Layer* o = r.L(orient);
    const Layer* c = r.L(cam);
    AUREA_CHECK(p && o && c);
    if (!p || !o || !c) return;
    AUREA_CHECK_EQ(p->autoOrient, u8{0});
    AUREA_CHECK_EQ(o->autoOrient, u8{1});
    AUREA_CHECK(c->camera.twoNode);
    AUREA_CHECK_NEAR(c->camera.poi.x, 123.5f, 1e-4f);
    AUREA_CHECK_NEAR(c->camera.poi.y, -45.0f, 1e-4f);
    AUREA_CHECK_NEAR(c->camera.poi.z, 600.0f, 1e-4f);
    if (text) {
        const Layer* t = r.L(text);
        AUREA_CHECK(t && t->text.sourceKeys.size() == 2u);
        if (t && t->text.sourceKeys.size() == 2u) {
            AUREA_CHECK(t->text.sourceKeys[0].content == "um");
            AUREA_CHECK(t->text.animators.size() == 1u && t->text.animators[0].skewAxis == 30.0f);
            AUREA_CHECK(t->text.sourceKeys[1].content == "dois");
            AUREA_CHECK_EQ(t->text.sourceKeys[1].frame, i64{7});
            AUREA_CHECK_EQ(t->text.sourceKeys[1].spans.size(), 1u);
            if (!t->text.sourceKeys[1].spans.empty()) {
                AUREA_CHECK_EQ(t->text.sourceKeys[1].spans[0].weight, u16{700});
                AUREA_CHECK_NEAR(t->text.sourceKeys[1].spans[0].color.y, 1.0f, 1e-6f);
            }
        }
    }
    // Padrão (camada nova = projeto de antes da v68): tudo desligado.
    Layer fresh;
    AUREA_CHECK_EQ(fresh.autoOrient, u8{0});
    AUREA_CHECK(!fresh.camera.twoNode && fresh.text.sourceKeys.empty());
    std::remove(path.c_str());
    std::printf("v68: auto-orientar, dois nós + POI e keys (com trechos) voltam iguais; ");
}

// -----------------------------------------------------------------------------
// Expressões: espaço da camada com pai girado e escalado
// -----------------------------------------------------------------------------
AUREA_TEST(AeAnimation, ToCompFromCompThroughRotatedScaledParent) {
    ExprRig x;
    LayerId parentId, childId;
    Layer* parent = x.add(LayerKind::Null, "Pai", &parentId);
    Layer* child = x.add(LayerKind::Null, "Filho", &childId);
    Layer* probe = x.add(LayerKind::Null, "Sonda");
    parent->transform.position = Vec3{100, 50, 0};
    parent->transform.rotation = Vec3{0, 0, 90};
    parent->transform.scale = Vec3{2, 2, 1};
    parent->transform.anchor = Vec3{0, 0, 0};
    child->parent = parentId;
    child->transform.position = Vec3{10, 0, 0};
    child->transform.anchor = Vec3{0, 0, 0};
    // (5,0) no filho → (15,0) no pai → ×2 → (30,0) → 90° → (0,30) → +(100,50).
    const TP px = TrackProperty::PositionX, py = TrackProperty::PositionY;
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").toComp([5, 0])[0]"), 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, py, "thisComp.layer(\"Filho\").toComp([5, 0])[1]"), 80.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").fromComp([100, 80])[0]"), 5.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, py, "thisComp.layer(\"Filho\").fromComp([100, 80])[1]"), 0.0f, 1e-3f);
    // No 2D o mundo é a composição.
    AUREA_CHECK_NEAR(x.eval(probe, py, "thisComp.layer(\"Filho\").toWorld([5, 0])[1]"), 80.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").fromWorld([100, 80])[0]"), 5.0f, 1e-3f);
    // Vec: sem a translação (direção girada e escalada).
    AUREA_CHECK_NEAR(x.eval(probe, py, "thisComp.layer(\"Filho\").toCompVec([1, 0])[1]"), 2.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").toWorldVec([1, 0])[0]"), 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").fromWorldVec([0, 2])[0]"), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, py, "thisComp.layer(\"Filho\").fromCompVec([-2, 0])[1]"), 1.0f, 1e-3f);
    // Sem camada na frente: a própria (a sonda está na origem, sem pai).
    probe->transform.position = Vec3{7, 9, 0};
    probe->transform.anchor = Vec3{0, 0, 0};
    AUREA_CHECK_NEAR(x.eval(probe, TrackProperty::Opacity, "toComp([1, 1])[0]"), 8.0f / 100.0f, 1e-5f);
    // O instante opcional: o pai anda; toComp(p, t) segue o tempo pedido.
    Track& moving = parent->tracks.get_or_create(TrackProperty::PositionX);
    (void)moving.set(FrameIndex{0}, 100.0f, Interpolation::Linear);
    (void)moving.set(FrameIndex{30}, 400.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").toComp([5, 0], 0.5)[0]"), 250.0f, 1e-2f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Filho\").toComp([5, 0])[0]", 30), 400.0f, 1e-2f);
    // A mesma conta do render.
    const Mat4 w = world::world_2d_frac(*x.comp, *child, 15.0);
    AUREA_CHECK_NEAR((w * Vec4{5, 0, 0, 1}).x, 250.0f, 1e-2f);
    // Erros claros: ponto que não é vetor, camada achatada.
    AUREA_CHECK(!x.error(probe, px, "toComp(3)").empty());
    child->transform.scale = Vec3{0, 0, 1};
    AUREA_CHECK(!x.error(probe, px, "thisComp.layer(\"Filho\").fromComp([1, 1])[0]").empty());
    std::printf("toComp/fromComp/toWorld/fromWorld (+Vec) pelo pai girado 90° e 200%%: (5,0) -> (100,80); ");
}

AUREA_TEST(AeAnimation, ToWorldAndToCompOnThreeDLayersUseTheRenderCamera) {
    ExprRig x(1000, 500);
    LayerId id;
    Layer* l = x.add(LayerKind::Null, "N3D", &id);
    Layer* probe = x.add(LayerKind::Null, "Sonda");
    l->threeD = true;
    l->transform.position = Vec3{500, 250, 0};
    l->transform.anchor = Vec3{0, 0, 0};
    l->transform.rotation = Vec3{0, 90, 0};   // gira em Y: o X da camada vira o −Z do mundo
    const TP px = TrackProperty::PositionX, pz = TrackProperty::PositionZ;
    const Mat4 m = world::world_3d_frac(*x.comp, *l, 0.0);
    const Vec4 expected = m * Vec4{10, 0, 0, 1};
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"N3D\").toWorld([10, 0, 0])[0]"), expected.x, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, pz, "thisComp.layer(\"N3D\").toWorld([10, 0, 0])[2]"), expected.z, 1e-3f);
    AUREA_CHECK_NEAR(std::fabs(expected.z), 10.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"N3D\").fromWorld(thisComp.layer(\"N3D\").toWorld([10, 0, 0]))[0]"), 10.0f, 1e-3f);
    // Câmera padrão (sem camada de câmera): no plano z = 0 a composição é 1:1;
    // um ponto mais fundo encolhe para o centro na razão d/(d + z), d = 1,2·h.
    l->transform.rotation = Vec3{0, 0, 0};
    l->transform.position = Vec3{700, 250, 0};
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"N3D\").toComp([0, 0, 0])[0]"), 700.0f, 0.05f);
    const f32 d = 500.0f * 1.2f;
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"N3D\").toComp([0, 0, 300])[0]"), 500.0f + 200.0f * d / (d + 300.0f), 0.05f);
    // fromComp na camada 3D = o ponto do PLANO da camada sob o pixel.
    l->transform.rotation = Vec3{30, 20, 0};
    AUREA_CHECK_NEAR(x.eval(probe, px, "var p = thisComp.layer(\"N3D\").toComp([40, -25]); thisComp.layer(\"N3D\").fromComp(p)[0]"), 40.0f, 0.05f);
    AUREA_CHECK_NEAR(x.eval(probe, TrackProperty::PositionY, "var p = thisComp.layer(\"N3D\").toComp([40, -25]); thisComp.layer(\"N3D\").fromComp(p)[1]"), -25.0f, 0.05f);
    std::printf("3D: toWorld = mundo do render, toComp pela câmera padrão (d/(d+z)), fromComp acha o plano; ");
}

AUREA_TEST(AeAnimation, LookAtPointsTheLayerZAxisAtTheTarget) {
    ExprRig x;
    Layer* probe = x.add(LayerKind::Null, "Sonda");
    const Vec3 from{100, 200, -50}, to{400, -100, 350};
    const f32 rx = x.eval(probe, TrackProperty::RotationX, "lookAt([100, 200, -50], [400, -100, 350])[0]");
    const f32 ry = x.eval(probe, TrackProperty::RotationY, "lookAt([100, 200, -50], [400, -100, 350])[1]");
    const f32 rz = x.eval(probe, TrackProperty::RotationZ, "lookAt([100, 200, -50], [400, -100, 350])[2]");
    AUREA_CHECK_NEAR(rz, 0.0f, 1e-6f);
    Layer l;
    l.kind = LayerKind::Null;
    l.threeD = true;
    l.transform.rotation = Vec3{rx, ry, 0};
    const Mat4 m = world::layer_matrix_3d_frac(l, 0.0, 30.0);
    const Vec3 want = (to - from).normalized();
    const Vec3 z = axis(m, 2);
    AUREA_CHECK_NEAR(z.x, want.x, 1e-4f);
    AUREA_CHECK_NEAR(z.y, want.y, 1e-4f);
    AUREA_CHECK_NEAR(z.z, want.z, 1e-4f);
    // Mesmo ponto: orientação neutra (sem NaN).
    AUREA_CHECK_NEAR(x.eval(probe, TrackProperty::RotationX, "lookAt([1, 2, 3], [1, 2, 3])[0]"), 0.0f, 1e-6f);
    std::printf("lookAt: rotação X %.2f°, Y %.2f° leva o Z da camada ao alvo; ", rx, ry);
}

AUREA_TEST(AeAnimation, SourceRectAtTimeForShapeNullTextAndPrecomp) {
    ExprRig x;
    Layer* shape = x.add(LayerKind::Shape, "Forma");
    shape->shape.shapeType = 0;
    shape->shape.bounds = Rect{0, 0, 200, 120};
    shape->shape.strokeWidth = 10.0f;
    Track& width = shape->tracks.get_or_create(TrackProperty::ShapeParam, 0, 5);
    (void)width.set(FrameIndex{0}, 200.0f, Interpolation::Linear);
    (void)width.set(FrameIndex{30}, 400.0f, Interpolation::Linear);
    Layer* probe = x.add(LayerKind::Null, "Sonda");
    const TP px = TrackProperty::PositionX;
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Forma\").sourceRectAtTime(0).width"), 200.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Forma\").sourceRectAtTime(0.5).width"), 300.0f, 1e-2f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Forma\").sourceRectAtTime(0, true).left"), -5.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Forma\").sourceRectAtTime(0, true).height"), 130.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "var r = thisComp.layer(\"Sonda\").sourceRectAtTime(); r.width + r.height"), 200.0f, 1e-3f);
    // Pré-composição: a caixa é a da composição de dentro.
    const CompositionId innerId = x.tl.create_composition("Dentro", 300, 200, 30.0);
    Layer* pre = x.add(LayerKind::Composition, "Pre");
    pre->nested.composition = innerId;
    AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Pre\").sourceRectAtTime().width"), 300.0f, 1e-3f);
    // Texto (com fonte no host): a mesma caixa do layout, e o key do texto-fonte vale.
    Layer* text = x.add(LayerKind::Text, "Texto");
    text->text.content = "A";
    text->text.size = 50.0f;
    text->text.alignment = 0;
    if (const auto font = text::FontManager::instance().font_for(text->text)) {
        TextData longer = text->text;
        longer.content = "AAAAAA";
        const f32 w1 = text::measure(*font, text->text).width, w6 = text::measure(*font, longer).width;
        AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Texto\").sourceRectAtTime(0).width"), w1, 0.01f);
        AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Texto\").sourceRectAtTime(0).left"), 2.0f, 1e-3f);
        TextKey k;
        k.frame = 15;
        text::capture_source_key(longer, k);
        text->text.sourceKeys.push_back(k);
        AUREA_CHECK_NEAR(x.eval(probe, px, "thisComp.layer(\"Texto\").sourceRectAtTime(0).width"), w6, 0.01f);   // antes do 1º key: o 1º
        AUREA_CHECK(w6 > w1 * 3.0f);
    } else {
        std::printf("(texto: sem fonte no host) ");
    }
    AUREA_CHECK(!x.error(probe, px, "sourceRectAtTime().depth").empty());
    std::printf("sourceRectAtTime: forma animada, extensões do contorno, nulo, pré-composição, texto com key; ");
}

AUREA_TEST(AeAnimation, CompByNameReachesLayersOfAnotherComposition) {
    ExprRig x;
    Layer* probe = x.add(LayerKind::Null, "Sonda");
    const CompositionId otherId = x.tl.create_composition("Outra", 640, 480, 30.0);
    Composition* other = x.tl.composition(otherId);
    const LayerId farId = other->add_layer(LayerKind::Null, "Longe");
    Layer* far = other->layer(farId);
    far->start = FrameIndex{0};
    far->end = FrameIndex{300};
    far->transform.position = Vec3{321, 77, 0};
    far->transform.anchor = Vec3{0, 0, 0};
    far->transform.scale = Vec3{3, 3, 1};
    const TP px = TrackProperty::PositionX;
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"Outra\").width"), 640.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"outra\").numLayers"), 1.0f, 1e-3f);   // sem caixa
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"Outra\").layer(\"Longe\").transform.position[0]"), 321.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"Outra\").layer(1).position[1]"), 77.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"Outra\").layer(\"Longe\").toComp([10, 0])[0]"), 351.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"Outra\").layer(\"Longe\").index"), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(x.eval(probe, px, "comp(\"C\").layer(\"Sonda\").index"), 1.0f, 1e-3f);   // a própria composição
    AUREA_CHECK(!x.error(probe, px, "comp(\"Nenhuma\").width").empty());
    AUREA_CHECK(!x.error(probe, px, "comp(3)").empty());
    std::printf("comp(\"Outra\"): largura, camadas por nome/índice, transform e toComp lá dentro; ");
}

// -----------------------------------------------------------------------------
// Auto-Orientar e câmera de dois nós
// -----------------------------------------------------------------------------
AUREA_TEST(AeAnimation, AutoOrientAlongPathFollowsTheMotionTangent) {
    ExprRig x;
    Layer* l = x.add(LayerKind::Null, "Seta");
    l->transform.anchor = Vec3{0, 0, 0};
    Track& tx = l->tracks.get_or_create(TrackProperty::PositionX);
    Track& ty = l->tracks.get_or_create(TrackProperty::PositionY);
    (void)tx.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)tx.set(FrameIndex{30}, 100.0f, Interpolation::Linear);
    (void)ty.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)ty.set(FrameIndex{30}, 100.0f, Interpolation::Linear);
    auto angle = [&](f64 f, bool threeD) {
        const Mat4 m = threeD ? world::world_3d_frac(*x.comp, *l, f) : world::world_2d_frac(*x.comp, *l, f);
        return std::atan2(m.col[0].y, m.col[0].x) * 180.0f / kPi;
    };
    AUREA_CHECK_NEAR(angle(15, false), 0.0f, 1e-4f);    // desligado: sem giro
    l->autoOrient = 1;
    AUREA_CHECK_NEAR(angle(15, false), 45.0f, 1e-3f);
    AUREA_CHECK_NEAR(angle(45, false), 45.0f, 1e-3f);   // depois do último key: a direção de chegada
    l->transform.rotation = Vec3{0, 0, 10};             // a rotação soma por cima
    AUREA_CHECK_NEAR(angle(15, false), 55.0f, 1e-3f);
    l->transform.rotation = Vec3{0, 0, 0};
    // Volta para a esquerda: 180°.
    (void)tx.set(FrameIndex{60}, -100.0f, Interpolation::Linear);
    (void)ty.set(FrameIndex{60}, 100.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(std::fabs(angle(45, false)), 180.0f, 1e-3f);
    // 3D: o X da camada segue a tangente também em profundidade.
    l->threeD = true;
    Track& tz = l->tracks.get_or_create(TrackProperty::PositionZ);
    (void)tz.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)tz.set(FrameIndex{30}, 100.0f, Interpolation::Linear);
    const Vec3 xAxis = axis(world::world_3d_frac(*x.comp, *l, 15.0), 0);
    const Vec3 want = Vec3{1, 1, 1}.normalized();
    AUREA_CHECK_NEAR(xAxis.x, want.x, 1e-4f);
    AUREA_CHECK_NEAR(xAxis.y, want.y, 1e-4f);
    AUREA_CHECK_NEAR(xAxis.z, want.z, 1e-4f);
    std::printf("auto-orientar no caminho: 45° na diagonal, +10° de rotação, 180° na volta, eixo X na tangente 3D; ");
}

AUREA_TEST(AeAnimation, AutoOrientTowardsCameraAndTwoNodeCameraAimAtTheTarget) {
    ExprRig x(1000, 500);
    LayerId camId;
    Layer* cam = x.add(LayerKind::Camera, "Câmera", &camId);
    cam->camera.active = true;
    cam->transform.position = Vec3{200, 100, -800};
    Layer* card = x.add(LayerKind::Null, "Cartão");
    card->threeD = true;
    card->transform.position = Vec3{700, 350, 300};
    card->transform.anchor = Vec3{0, 0, 0};
    card->autoOrient = 2;
    // Para a câmera: o +Z da camada aponta da câmera para ela.
    const Vec3 z = axis(world::world_3d_frac(*x.comp, *card, 0.0), 2);
    const Vec3 want = (card->transform.position - cam->transform.position).normalized();
    AUREA_CHECK_NEAR(z.x, want.x, 1e-4f);
    AUREA_CHECK_NEAR(z.y, want.y, 1e-4f);
    AUREA_CHECK_NEAR(z.z, want.z, 1e-4f);
    // Com pai: a câmera vai para o espaço do pai (a conta continua no mundo).
    LayerId parentId;
    Layer* parent = x.add(LayerKind::Null, "Pai", &parentId);
    parent->threeD = true;
    parent->transform.anchor = Vec3{0, 0, 0};
    parent->transform.rotation = Vec3{0, 0, 40};
    parent->transform.position = Vec3{50, 20, 0};
    card->parent = parentId;
    const Mat4 cw = world::world_3d_frac(*x.comp, *card, 0.0);
    const Vec3 cardWorld{cw.col[3].x, cw.col[3].y, cw.col[3].z};
    const Vec3 z2 = axis(cw, 2);
    const Vec3 want2 = (cardWorld - cam->transform.position).normalized();
    AUREA_CHECK_NEAR(z2.x, want2.x, 1e-4f);
    AUREA_CHECK_NEAR(z2.y, want2.y, 1e-4f);
    AUREA_CHECK_NEAR(z2.z, want2.z, 1e-4f);
    // Câmera de dois nós: mira no POI; o POI projeta no centro da imagem.
    cam->camera.twoNode = true;
    cam->camera.poi = Vec3{650, 120, 400};
    const Vec3 look = axis(world::world_3d_frac(*x.comp, *cam, 0.0), 2);
    const Vec3 wantLook = (cam->camera.poi - cam->transform.position).normalized();
    AUREA_CHECK_NEAR(look.x, wantLook.x, 1e-4f);
    AUREA_CHECK_NEAR(look.y, wantLook.y, 1e-4f);
    AUREA_CHECK_NEAR(look.z, wantLook.z, 1e-4f);
    const Vec4 h = world::comp_from_world(*x.comp, 0.0) * Vec4{cam->camera.poi.x, cam->camera.poi.y, cam->camera.poi.z, 1};
    AUREA_CHECK_NEAR(h.x / h.w, 500.0f, 0.05f);
    AUREA_CHECK_NEAR(h.y / h.w, 250.0f, 0.05f);
    // POI animado (trilhas CameraPoi*): a mira acompanha.
    Track& poiX = cam->tracks.get_or_create(TrackProperty::CameraPoiX);
    (void)poiX.set(FrameIndex{0}, 650.0f, Interpolation::Linear);
    (void)poiX.set(FrameIndex{30}, -250.0f, Interpolation::Linear);
    const Vec4 h30 = world::comp_from_world(*x.comp, 30.0) * Vec4{-250.0f, 120.0f, 400.0f, 1};
    AUREA_CHECK_NEAR(h30.x / h30.w, 500.0f, 0.05f);
    AUREA_CHECK_NEAR(h30.y / h30.w, 250.0f, 0.05f);
    // A rotação gira POR CIMA da mira (Z = rolar em volta do eixo de visão).
    cam->transform.rotation = Vec3{0, 0, 30};
    const Vec3 rolled = axis(world::world_3d_frac(*x.comp, *cam, 0.0), 2);
    AUREA_CHECK_NEAR(rolled.x, wantLook.x, 1e-4f);
    AUREA_CHECK_NEAR(rolled.z, wantLook.z, 1e-4f);
    std::printf("para a câmera (com e sem pai) e câmera de dois nós: POI no centro, animado, rotação por cima; ");
}

AUREA_TEST(AeAnimation, TwoNodeToggleKeepsTheFramingAndPoiEditsAreUndoable) {
    Rig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 cam = *r.e.add_camera();
    Layer* c = r.L(cam);
    AUREA_CHECK(c != nullptr);
    if (!c) return;
    c->transform.rotation = Vec3{10, -20, 5};
    const Mat4 before = world::world_3d_frac(*r.comp(), *c, 0.0);
    AUREA_CHECK(r.e.set_camera_two_node(cam, true));
    const Mat4 after = world::world_3d_frac(*r.comp(), *r.L(cam), 0.0);
    for (int col = 0; col < 4; ++col) {
        AUREA_CHECK_NEAR(after.col[col].x, before.col[col].x, 1e-3f);
        AUREA_CHECK_NEAR(after.col[col].y, before.col[col].y, 1e-3f);
        AUREA_CHECK_NEAR(after.col[col].z, before.col[col].z, 1e-3f);
    }
    f32 poi[4]{};
    AUREA_CHECK(r.e.query_camera_poi(cam, poi));
    AUREA_CHECK_EQ(poi[0], 1.0f);
    AUREA_CHECK(r.e.set_camera_poi(cam, 1.0f, 2.0f, 3.0f));
    AUREA_CHECK(r.e.query_camera_poi(cam, poi) && poi[1] == 1.0f && poi[3] == 3.0f);
    r.undo();
    AUREA_CHECK(r.e.query_camera_poi(cam, poi) && poi[1] != 1.0f);
    // Com key no POI, editar grava o key no cabeçote.
    Track& k = r.L(cam)->tracks.get_or_create(TrackProperty::CameraPoiX);
    (void)k.set(FrameIndex{0}, 10.0f, Interpolation::Linear);
    r.seek(12);
    AUREA_CHECK(r.e.set_camera_poi(cam, 77.0f, 0.0f, 0.0f));
    const Track* kx = r.L(cam)->tracks.find(TrackProperty::CameraPoiX);
    AUREA_CHECK(kx && kx->keys.size() == 2u);
    r.undo();
    r.undo();
    AUREA_CHECK(!r.L(cam)->camera.twoNode);
    // Auto-orientar: um passo de desfazer.
    const u64 n = *r.e.add_null(true);
    AUREA_CHECK(r.e.set_layer_auto_orient(n, 2));
    AUREA_CHECK_EQ(r.e.query_layer_auto_orient(n), 2);
    r.undo();
    AUREA_CHECK_EQ(r.e.query_layer_auto_orient(n), 0);
    std::printf("ligar dois nós não mexe no enquadramento; POI e auto-orientar com desfazer; ");
}

// -----------------------------------------------------------------------------
// Os atalhos novos das duas UIs (ExpressionSheet.kt / AnimationTools.swift)
// compilam e rodam numa camada de verdade.
// -----------------------------------------------------------------------------
AUREA_TEST(AeAnimation, HelperSnippetsCompileAndEvaluate) {
    static const char* kSnippets[] = {
        "thisComp.layer(1).toComp([0, 0])",
        "fromComp(thisComp.layer(1).toComp([0, 0]))",
        "thisComp.layer(1).toWorld(thisComp.layer(1).anchorPoint)",
        "lookAt(position, thisComp.layer(1).position)",
        "var r = thisLayer.sourceRectAtTime(); [r.left + r.width / 2, r.top + r.height / 2]",
        "comp(\"Comp 1\").layer(1).transform.position",
    };
    ExprRig x;
    x.tl.composition(x.tl.current())->set_name("Comp 1");
    Layer* a = x.add(LayerKind::Null, "A");
    a->transform.position = Vec3{40, 30, 0};
    Layer* b = x.add(LayerKind::Null, "B");
    for (const char* s : kSnippets) {
        AUREA_CHECK_MSG(expr::check_syntax(s).ok, s);
        AUREA_CHECK_MSG(x.error(b, TrackProperty::Opacity, s).empty(), s);
    }
    std::printf("%zu atalhos novos compilam e avaliam sem erro; ", sizeof(kSnippets) / sizeof(kSnippets[0]));
}
