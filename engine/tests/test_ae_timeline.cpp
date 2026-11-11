// =============================================================================
//  Timeline do AE (2026-11-11): marcas com duração/comentário e marcas de
//  camada (CRUD, desfazer, arquivo, expressões), escalar keyframes no tempo,
//  Easy Ease / In / Out, área de trabalho (prévia em loop, aparar, exportar) e
//  alinhar/distribuir no motor (B6 do gap_system).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/playback/Playback.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/Timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct TlRig {
    Engine e;
    u64 a = 0, b = 0, c = 0;
    TlRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        // Três nulos em fila: A [0,30) B [30,60) C [60,90).
        a = *e.add_null(false);
        b = *e.add_null(false);
        c = *e.add_null(false);
        range(a, 0, 30);
        range(b, 30, 60);
        range(c, 60, 90);
    }
    ~TlRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void range(u64 id, i64 s, i64 en) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = LayerId::unpack(id);
        cmd.layer_range.start = FrameIndex{s};
        cmd.layer_range.end = FrameIndex{en};
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
};

/// O mesmo arranjo do test_expr: uma Timeline sem motor.
struct ExprRig {
    Timeline tl;
    Composition* comp = nullptr;
    ExprRig() {
        const CompositionId id = tl.create_composition("C", 1920, 1080, 30.0);
        tl.set_current(id);
        comp = tl.composition(id);
    }
    Layer* add(const char* name, i64 start) {
        const LayerId id = comp->add_layer(LayerKind::Shape, name);
        Layer* l = comp->layer(id);
        l->start = FrameIndex{start};
        l->end = FrameIndex{start + 300};
        return l;
    }
};

f32 expr_at(ExprRig& rig, Track& t, const char* src, i64 localFrame) {
    t.expression = expr::compile(src);
    t.expressionEnabled = true;
    const expr::Scope scope(rig.tl);
    return t.value_or(FrameIndex{localFrame}, 0.0f);
}

bool box_of(Engine& e, u64 id, f32& l, f32& t, f32& r, f32& b) {
    bridge::LayerDetailPOD d;
    if (!e.query_layer_detail(id, d) || !(d.geomFlags & bridge::kGeomCornersValid)) return false;
    l = r = d.corners[0]; t = b = d.corners[1];
    for (int i = 1; i < 4; ++i) {
        l = std::min(l, d.corners[i * 2]); r = std::max(r, d.corners[i * 2]);
        t = std::min(t, d.corners[i * 2 + 1]); b = std::max(b, d.corners[i * 2 + 1]);
    }
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// Marcas da composição: duração e comentário
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, CompMarkerDurationAndCommentAreUndoableAndSurviveTheOldEditor) {
    TlRig r;
    AUREA_CHECK(r.e.edit_marker_ex(-1, 12, 0xFF00FF00u, "refrão", 18, "entra o coro"));
    i64 duration = -1;
    std::string comment;
    AUREA_CHECK(r.e.marker_details(12, duration, comment));
    AUREA_CHECK_EQ(duration, 18);
    AUREA_CHECK(comment == "entra o coro");
    // O editor antigo (cor/nome/arrasto) não perde duração nem comentário.
    AUREA_CHECK(r.e.edit_marker(12, 15, 0xFFFF0000u, "ponte"));
    AUREA_CHECK(r.e.marker_details(15, duration, comment));
    AUREA_CHECK_EQ(duration, 18);
    AUREA_CHECK(comment == "entra o coro");
    AUREA_CHECK(r.e.move_marker(15, 20));
    AUREA_CHECK(r.e.marker_details(20, duration, comment) && duration == 18 && comment == "entra o coro");
    // Recusas sem histórico: duração negativa, destino ocupado, texto enorme.
    AUREA_CHECK(!r.e.edit_marker_ex(20, 20, 0xFF00FF00u, "x", -1, ""));
    AUREA_CHECK(r.e.edit_marker_ex(-1, 40, 0xFF00FF00u, "outra", 0, ""));
    AUREA_CHECK(!r.e.edit_marker_ex(20, 40, 0xFF00FF00u, "x", 3, ""));
    AUREA_CHECK(!r.e.edit_marker_ex(20, 20, 0xFF00FF00u, "x", 3, std::string(kMaxMarkerText + 1, 'a')));
    // Um passo de desfazer por edição.
    AUREA_CHECK(r.e.edit_marker_ex(20, 20, 0xFF00FF00u, "ponte", 5, "curto"));
    AUREA_CHECK(r.e.marker_details(20, duration, comment) && duration == 5 && comment == "curto");
    r.undo();
    AUREA_CHECK(r.e.marker_details(20, duration, comment) && duration == 18 && comment == "entra o coro");
    std::printf("marca da composição: duração 18, comentário mantido no editor antigo e no arrasto; ");
}

// -----------------------------------------------------------------------------
// Marcas da camada: CRUD, desfazer, andam com a camada
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, LayerMarkersCrudUndoAndTravelWithTheLayer) {
    TlRig r;
    AUREA_CHECK(r.e.edit_layer_marker(r.b, -1, 40, 0xFF3366FFu, "batida", 6, "o soco"));
    AUREA_CHECK_EQ(r.L(r.b)->markers.size(), usize{1});
    AUREA_CHECK_EQ(r.L(r.b)->markers[0].frame.value, 10);   // tempo local: 40 − início 30
    i64 q[8]{};
    AUREA_CHECK_EQ(r.e.query_layer_markers(r.b, q, 2), 1u);
    AUREA_CHECK_EQ(q[0], 40); AUREA_CHECK_EQ(q[1], 6); AUREA_CHECK_EQ(q[2], i64{0xFF3366FF}); AUREA_CHECK_EQ(q[3], 0);
    std::string label, comment;
    AUREA_CHECK(r.e.layer_marker_text(r.b, 40, label, comment) && label == "batida" && comment == "o soco");
    // Segunda marca e destino ocupado.
    AUREA_CHECK(r.e.edit_layer_marker(r.b, -1, 50, 0xFF00FF00u, "", 0, ""));
    AUREA_CHECK(!r.e.move_layer_marker(r.b, 50, 40));
    AUREA_CHECK(r.e.move_layer_marker(r.b, 50, 45));
    i64 all[16]{};
    AUREA_CHECK_EQ(r.e.query_all_layer_markers(all, 4), 2u);
    AUREA_CHECK_EQ(all[0], static_cast<i64>(r.b));
    AUREA_CHECK_EQ(all[1], 40); AUREA_CHECK_EQ(all[5], 45);
    // A camada anda (mover inteira, operação 6): a marca vai junto.
    AUREA_CHECK(r.e.edit_clip_time(r.b, 6, 100));
    AUREA_CHECK_EQ(r.e.query_layer_markers(r.b, q, 2), 2u);
    AUREA_CHECK_EQ(q[0], 110);
    r.undo();
    AUREA_CHECK_EQ(r.e.query_layer_markers(r.b, q, 2), 2u);
    AUREA_CHECK_EQ(q[0], 40);
    // Apagar, desfazer, e camada travada recusa.
    AUREA_CHECK(r.e.delete_layer_marker(r.b, 45));
    AUREA_CHECK_EQ(r.L(r.b)->markers.size(), usize{1});
    r.undo();
    AUREA_CHECK_EQ(r.L(r.b)->markers.size(), usize{2});
    AUREA_CHECK(!r.e.delete_layer_marker(r.b, 41));
    r.L(r.b)->locked = true;
    AUREA_CHECK(!r.e.edit_layer_marker(r.b, -1, 55, 0xFF00FF00u, "", 0, ""));
    AUREA_CHECK(!r.e.delete_layer_marker(r.b, 40));
    r.L(r.b)->locked = false;
    // Editar tudo de uma vez é um passo.
    AUREA_CHECK(r.e.edit_layer_marker(r.b, 40, 42, 0xFFFFFFFFu, "golpe", 9, "mais forte"));
    AUREA_CHECK(r.e.layer_marker_text(r.b, 42, label, comment) && label == "golpe" && comment == "mais forte");
    r.undo();
    AUREA_CHECK(r.e.layer_marker_text(r.b, 40, label, comment) && label == "batida");
    // Duplicar a camada copia as marcas (a cópia é independente).
    std::printf("marcas da camada: local 10 ↔ composição 40, andam com a camada, desfazer em cada passo; ");
}

// -----------------------------------------------------------------------------
// Arquivo: v67
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, MarkersAndWorkAreaRoundTripThroughTheProjectFile) {
    TlRig r;
    r.L(r.a)->name = "A";
    r.L(r.b)->name = "B";
    AUREA_CHECK(r.e.edit_marker_ex(-1, 5, 0xFF00FF00u, "início", 30, "abre com o logo"));
    AUREA_CHECK(r.e.edit_marker_ex(-1, 8, 0xFF0000FFu, "sem extra", 0, ""));
    AUREA_CHECK(r.e.edit_layer_marker(r.b, -1, 35, 0xFF3366FFu, "pulo", 4, "comentário com acentuação"));
    AUREA_CHECK(r.e.edit_layer_marker(r.b, -1, 58, 0xFF3366FFu, "fim", 0, ""));
    AUREA_CHECK(r.e.set_work_area(10, 50));
    const std::string path = r.temp("aurea_ae_timeline.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    i64 duration = 0;
    std::string comment;
    AUREA_CHECK(r.e.marker_details(5, duration, comment));
    AUREA_CHECK_EQ(duration, 30);
    AUREA_CHECK(comment == "abre com o logo");
    AUREA_CHECK(r.e.marker_details(8, duration, comment) && duration == 0 && comment.empty());
    i64 in = 0, out = 0;
    AUREA_CHECK(r.e.query_work_area(in, out));
    AUREA_CHECK_EQ(in, 10);
    AUREA_CHECK_EQ(out, 50);
    const Layer* lb = nullptr;
    r.comp()->layers().for_each([&](LayerId, const Layer& l) { if (l.name == "B") lb = &l; });
    AUREA_CHECK(lb != nullptr);
    if (lb) {
        AUREA_CHECK_EQ(lb->markers.size(), usize{2});
        if (lb->markers.size() == 2) {
            AUREA_CHECK_EQ(lb->markers[0].frame.value, 5);
            AUREA_CHECK_EQ(lb->markers[0].duration, 4);
            AUREA_CHECK(lb->markers[0].label == "pulo");
            AUREA_CHECK(lb->markers[0].comment == "comentário com acentuação");
            AUREA_CHECK_EQ(lb->markers[0].color, 0xFF3366FFu);
            AUREA_CHECK_EQ(lb->markers[1].frame.value, 28);
        }
    }
    std::remove(path.c_str());
    std::printf("v67: duração/comentário, marcas da camada e área de trabalho voltam iguais; ");
}

// -----------------------------------------------------------------------------
// Expressões: thisLayer.marker e thisComp.marker
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, ExpressionsReadLayerAndCompMarkers) {
    ExprRig rig;
    Marker comp{FrameIndex{60}, 0xFFF7C34Fu, kMarkerManual, "drop"};
    comp.duration = 15;
    comp.comment = "solta o grave";
    rig.comp->put_marker(comp);
    Layer* a = rig.add("A", 30);   // começa no quadro 30
    Marker m1{FrameIndex{10}, 0xFFF7C34Fu, kMarkerManual, "um"};
    m1.duration = 6;
    m1.comment = "primeiro";
    Marker m2{FrameIndex{40}, 0xFFF7C34Fu, kMarkerManual, "dois"};
    a->markers = {m1, m2};
    Track& rz = a->tracks.get_or_create(TrackProperty::RotationZ);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.numKeys", 0), 2.0, 1e-9);
    // Tempo da COMPOSIÇÃO: local 10 + início 30 = 40 → 4/3 s.
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.key(1).time", 0), 40.0 / 30.0, 1e-5);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.key(1).duration", 0), 6.0 / 30.0, 1e-5);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.key(1).comment == \"primeiro\" ? 7 : 3", 0), 7.0, 1e-9);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.key(\"dois\").frame", 0), 70.0, 1e-9);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.key(\"primeiro\").index", 0), 1.0, 1e-9);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.nearestKey(2.2).index", 0), 2.0, 1e-9);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.nearestKey(1).index", 0), 1.0, 1e-9);
    // thisComp.marker continua a régua; agora com duração e comentário.
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisComp.marker.numKeys", 0), 1.0, 1e-9);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisComp.marker.key(1).duration", 0), 0.5, 1e-5);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisComp.marker.key(1).time", 0), 2.0, 1e-5);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisComp.marker.key(1).comment == \"solta o grave\" ? 1 : 0", 0), 1.0, 1e-9);
    AUREA_CHECK_NEAR(expr_at(rig, rz, "marker.key(\"solta o grave\").frame", 0), 60.0, 1e-9);
    // Outra camada pelo nome.
    Layer* b = rig.add("B", 0);
    Track& bz = b->tracks.get_or_create(TrackProperty::RotationZ);
    AUREA_CHECK_NEAR(expr_at(rig, bz, "thisComp.layer(\"A\").marker.key(2).time", 0), 70.0 / 30.0, 1e-5);
    // Sem marcas: erro com fallback, nunca derruba.
    AUREA_CHECK_NEAR(expr_at(rig, bz, "thisLayer.marker.key(1).time", 0), 0.0, 1e-9);
    AUREA_CHECK(!bz.expression->error().ok);
    AUREA_CHECK_NEAR(expr_at(rig, bz, "thisLayer.marker.numKeys", 0), 0.0, 1e-9);
    // Marca antiga (sem comentário): `comment` responde o nome, como antes.
    AUREA_CHECK_NEAR(expr_at(rig, rz, "thisLayer.marker.key(2).comment == \"dois\" ? 1 : 0", 0), 1.0, 1e-9);
    std::printf("thisLayer.marker key/nearestKey/numKeys em tempo da composição, duration e comment; ");
}

// -----------------------------------------------------------------------------
// Keyframes: escalar no tempo e Easy Ease
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, ScaleKeyframeSelectionStretchesAroundTheAnchorInOneStep) {
    TlRig r;
    Layer* l = r.L(r.a);
    for (i64 f : {0, 10, 20}) l->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{f}, static_cast<f32>(f) / 20.0f);
    l->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 5.0f);   // não escolhido
    const i64 op = static_cast<i64>(TrackProperty::Opacity);
    const i64 refs[] = {op, -1, 0, 0, op, -1, 0, 10, op, -1, 0, 20};
    AUREA_CHECK_EQ(r.e.scale_keyframe_selection(r.a, refs, 3, 0, 2.0), 3u);
    const Track* t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys.size() == 3);
    if (t && t->keys.size() == 3) {
        AUREA_CHECK_EQ(t->keys[1].time.value, 20);
        AUREA_CHECK_EQ(t->keys[2].time.value, 40);
        AUREA_CHECK_NEAR(t->keys[2].value, 1.0f, 1e-6);
    }
    r.undo();
    t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys.size() == 3 && t->keys[2].time.value == 20);
    // Âncora no último: encolher para a metade puxa para ele.
    AUREA_CHECK_EQ(r.e.scale_keyframe_selection(r.a, refs, 3, 20, 0.5), 3u);
    t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys[0].time.value == 10 && t->keys[1].time.value == 15 && t->keys[2].time.value == 20);
    r.undo();
    // Dois keys no mesmo quadro: recusa inteira, nada muda.
    AUREA_CHECK_EQ(r.e.scale_keyframe_selection(r.a, refs, 3, 0, 0.01), 0u);
    t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys[1].time.value == 10);
    AUREA_CHECK_EQ(r.e.scale_keyframe_selection(r.a, refs, 3, 0, 1.0), 0u);   // nada a fazer
    AUREA_CHECK_EQ(r.e.scale_keyframe_selection(r.a, refs, 3, 0, -2.0), 0u);
    std::printf("escalar ×2 em volta de 0 e ×0,5 em volta de 20, colisão recusada; ");
}

AUREA_TEST(AeTimeline, EasyEaseInOutAndBothShapeTheSegmentsAroundTheKey) {
    TlRig r;
    Layer* l = r.L(r.a);
    for (i64 f : {0, 10, 20}) l->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{f}, static_cast<f32>(f) / 20.0f);
    const i64 op = static_cast<i64>(TrackProperty::Opacity);
    const i64 middle[] = {op, -1, 0, 10};
    AUREA_CHECK_EQ(r.e.ease_keyframe_selection(r.a, middle, 1, Engine::kEasyEaseBoth), 1u);
    const Track* t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys.size() == 3);
    if (t && t->keys.size() == 3) {
        // Chegada no key do meio: velocidade zero (by2 = 1); a saída do 1º continua reta.
        AUREA_CHECK(t->keys[0].interp == Interpolation::Bezier);
        AUREA_CHECK_NEAR(t->keys[0].bx2, 2.0f / 3.0f, 1e-6);
        AUREA_CHECK_NEAR(t->keys[0].by2, 1.0f, 1e-6);
        AUREA_CHECK_NEAR(t->keys[0].by1, t->keys[0].bx1, 1e-6);
        // Saída do key do meio: velocidade zero (by1 = 0); a chegada no último continua reta.
        AUREA_CHECK(t->keys[1].interp == Interpolation::Bezier);
        AUREA_CHECK_NEAR(t->keys[1].bx1, 1.0f / 3.0f, 1e-6);
        AUREA_CHECK_NEAR(t->keys[1].by1, 0.0f, 1e-6);
        AUREA_CHECK_NEAR(t->keys[1].by2, t->keys[1].bx2, 1e-6);
        // O valor nos keys não muda; no meio do trecho, a curva desacelera.
        AUREA_CHECK_NEAR(t->sample(FrameIndex{10}), 0.5f, 1e-5);
        AUREA_CHECK(t->sample(FrameIndex{9}) > 0.45f);
    }
    r.undo();
    t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys[0].interp == Interpolation::Linear && t->keys[1].interp == Interpolation::Linear);
    // Só a entrada do último: muda o trecho anterior, só do lado da chegada.
    const i64 last[] = {op, -1, 0, 20};
    AUREA_CHECK_EQ(r.e.ease_keyframe_selection(r.a, last, 1, Engine::kEasyEaseIn), 1u);
    t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t && t->keys[1].interp == Interpolation::Bezier && t->keys[0].interp == Interpolation::Linear);
    if (t) { AUREA_CHECK_NEAR(t->keys[1].by2, 1.0f, 1e-6); AUREA_CHECK_NEAR(t->keys[1].by1, 1.0f / 3.0f, 1e-6); }
    // Saída do último não existe (não há trecho depois): nada a fazer.
    AUREA_CHECK_EQ(r.e.ease_keyframe_selection(r.a, last, 1, Engine::kEasyEaseOut), 0u);
    // Só a saída do primeiro, mantendo a chegada já suave do trecho seguinte.
    const i64 first[] = {op, -1, 0, 0};
    AUREA_CHECK_EQ(r.e.ease_keyframe_selection(r.a, first, 1, Engine::kEasyEaseOut), 1u);
    t = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    if (t) { AUREA_CHECK_NEAR(t->keys[0].by1, 0.0f, 1e-6); AUREA_CHECK_NEAR(t->keys[1].by2, 1.0f, 1e-6); }
    std::printf("Easy Ease (os dois lados), só entrada e só saída, um passo cada; ");
}

// -----------------------------------------------------------------------------
// Área de trabalho
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, PlaybackLoopsInsideTheWorkArea) {
    PlaybackController p;
    p.configure(30.0, FrameIndex{100});
    p.set_play_range(FrameIndex{10}, FrameIndex{20});
    p.set_loop(true);
    u64 now = 1'000'000'000ull;
    p.seek(FrameIndex{50}, now);
    p.play(now);
    AUREA_CHECK_EQ(p.current().value, 10);   // fora da área: começa no início dela
    bool inside = true;
    i64 wraps = 0, last = 10;
    for (int i = 1; i <= 90; ++i) {
        const i64 f = p.update(now + static_cast<u64>(i) * 33'333'333ull).value;
        inside &= f >= 10 && f < 20;
        if (f < last) ++wraps;
        last = f;
    }
    AUREA_CHECK(inside);
    AUREA_CHECK(wraps >= 2);
    // Sem loop: para no último quadro da área.
    PlaybackController q;
    q.configure(30.0, FrameIndex{100});
    q.set_play_range(FrameIndex{10}, FrameIndex{20});
    q.seek(FrameIndex{12}, now);
    q.play(now);
    AUREA_CHECK_EQ(q.current().value, 12);   // dentro: começa onde está
    (void)q.update(now + 2'000'000'000ull);
    AUREA_CHECK_EQ(q.current().value, 19);
    AUREA_CHECK(!q.playing());
    std::printf("prévia: %lld voltas dentro de [10,20), sem loop para no 19; ", static_cast<long long>(wraps));
}

AUREA_TEST(AeTimeline, WorkAreaIsUndoableClampedAndTrimsTheComposition) {
    TlRig r;
    const i64 full = r.comp()->duration().value;
    i64 in = -1, out = -1;
    AUREA_CHECK(!r.e.query_work_area(in, out));
    AUREA_CHECK_EQ(in, 0); AUREA_CHECK_EQ(out, full);
    AUREA_CHECK(r.e.set_work_area(20, 50));
    AUREA_CHECK(r.e.query_work_area(in, out) && in == 20 && out == 50);
    AUREA_CHECK_EQ(r.comp()->markers().size(), usize{0});
    r.undo();
    AUREA_CHECK(!r.e.query_work_area(in, out));
    AUREA_CHECK(r.e.set_work_area(20, full + 500));   // fim preso na composição
    AUREA_CHECK(r.e.query_work_area(in, out) && in == 20 && out == full);
    AUREA_CHECK(r.e.set_work_area(0, full));          // a composição inteira = sem área
    AUREA_CHECK(!r.e.query_work_area(in, out));
    // Exportar só a área: a duração medida é a da área.
    AUREA_CHECK(r.e.set_work_area(20, 50));
    r.e.set_export_work_area_only(true);
    AUREA_CHECK(r.e.export_work_area_only());
    AUREA_CHECK_EQ(r.e.query_export_duration(false), 30);
    r.e.set_export_work_area_only(false);
    AUREA_CHECK_EQ(r.e.query_export_duration(false), full);
    // Aparar: A [0,30) → [0,10); B [30,60) → [10,30); C [60,90) sai.
    AUREA_CHECK(r.e.edit_marker_ex(-1, 25, 0xFF00FF00u, "fica", 40, ""));
    AUREA_CHECK(r.e.edit_marker_ex(-1, 70, 0xFF00FF00u, "sai", 0, ""));
    AUREA_CHECK(r.e.edit_layer_marker(r.b, -1, 40, 0xFF00FF00u, "b", 0, ""));
    Layer* la = r.L(r.a);
    la->tracks.get_or_create(TrackProperty::Opacity).set(la->local_time(FrameIndex{25}), 0.25f);
    AUREA_CHECK(r.e.trim_composition_to_work_area());
    AUREA_CHECK_EQ(r.comp()->duration().value, 30);
    AUREA_CHECK(!r.e.query_work_area(in, out));
    AUREA_CHECK(r.L(r.a) && r.L(r.a)->start.value == 0 && r.L(r.a)->end.value == 10);
    AUREA_CHECK(r.L(r.b) && r.L(r.b)->start.value == 10 && r.L(r.b)->end.value == 30);
    AUREA_CHECK(r.L(r.c) == nullptr);
    AUREA_CHECK_EQ(r.comp()->markers().size(), usize{1});
    if (!r.comp()->markers().empty()) {
        AUREA_CHECK_EQ(r.comp()->markers()[0].frame.value, 5);
        AUREA_CHECK_EQ(r.comp()->markers()[0].duration, 25);   // presa no fim novo
    }
    i64 q[4]{};
    AUREA_CHECK(r.e.query_layer_markers(r.b, q, 1) == 1u && q[0] == 20);
    // O keyframe fica no mesmo conteúdo: quadro 25 antigo = quadro 5 novo.
    const Track* op = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(op && op->keys.size() == 1 && r.L(r.a)->timeline_time(op->keys[0].time).value == 5);
    r.undo();
    AUREA_CHECK_EQ(r.comp()->duration().value, full);
    AUREA_CHECK(r.L(r.c) != nullptr);
    AUREA_CHECK(r.e.query_work_area(in, out) && in == 20 && out == 50);
    std::printf("área [20,50): desfazível, presa, exportação mede 30, aparar desloca camadas, marcas e keys; ");
}

// -----------------------------------------------------------------------------
// Alinhar / distribuir no motor (B6)
// -----------------------------------------------------------------------------
AUREA_TEST(AeTimeline, AlignAndDistributeRunInTheEngineAsOneUndoStep) {
    TlRig r;
    const auto s1 = r.e.add_shape(0), s2 = r.e.add_shape(0), s3 = r.e.add_shape(0);
    AUREA_CHECK(s1.ok() && s2.ok() && s3.ok());
    if (!s1.ok() || !s2.ok() || !s3.ok()) return;
    const u64 ids[] = {*s1, *s2, *s3};
    r.L(ids[0])->transform.position.x = 40.0f;
    r.L(ids[1])->transform.position.x = 200.0f;
    r.L(ids[2])->transform.position.x = 120.0f;
    r.L(ids[1])->transform.position.y = 60.0f;
    f32 l0, t0, r0, b0;
    AUREA_CHECK(box_of(r.e, ids[0], l0, t0, r0, b0));
    // Alinhar à esquerda da composição: todas encostam em x = 0.
    AUREA_CHECK_EQ(r.e.align_layers(ids, 3, 0), 3u);
    for (const u64 id : ids) {
        f32 l, t, rr, b;
        AUREA_CHECK(box_of(r.e, id, l, t, rr, b));
        AUREA_CHECK_NEAR(l, 0.0f, 1e-3);
    }
    r.undo();   // um passo só
    AUREA_CHECK_NEAR(r.L(ids[0])->transform.position.x, 40.0f, 1e-4);
    AUREA_CHECK_NEAR(r.L(ids[1])->transform.position.x, 200.0f, 1e-4);
    // Centro vertical (as que já estão no centro não contam como "andaram").
    AUREA_CHECK(r.e.align_layers(ids, 3, 4) >= 1u);
    for (const u64 id : ids) {
        f32 l, t, rr, b;
        AUREA_CHECK(box_of(r.e, id, l, t, rr, b));
        AUREA_CHECK_NEAR((t + b) * 0.5f, 90.0f, 1e-3);
    }
    r.undo();
    // Distribuir na horizontal: as pontas ficam, os vãos ficam iguais.
    r.L(ids[2])->transform.position.x = 70.0f;
    AUREA_CHECK(r.e.align_layers(ids, 3, 6) >= 1u);
    f32 L[3], R[3], T, B;
    for (int i = 0; i < 3; ++i) AUREA_CHECK(box_of(r.e, ids[i], L[i], T, R[i], B));
    AUREA_CHECK_NEAR(L[0], l0, 1e-3);                       // a da esquerda não andou
    AUREA_CHECK_NEAR(L[2] - R[0], L[1] - R[2], 1e-3);       // vãos iguais (0 → 2 → 1)
    AUREA_CHECK_EQ(r.e.align_layers(ids, 2, 6), 0u);        // distribuir pede 3
    // Animada: ganha keyframe no cabeçote em vez de mudar o valor parado.
    r.undo();
    Layer* a0 = r.L(ids[0]);
    a0->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 40.0f);
    a0->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{20}, 80.0f);
    AUREA_CHECK(r.e.align_layers(ids, 1, 0) == 1u);
    const Track* px = r.L(ids[0])->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(px && px->keys.size() == 2);
    if (px && !px->keys.empty()) AUREA_CHECK(std::fabs(px->keys[0].value - 40.0f) > 1e-3f);
    std::printf("alinhar/distribuir no motor: esquerda, centro V, vãos iguais, keyframe no cabeçote, um passo; ");
}
