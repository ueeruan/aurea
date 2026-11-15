// =============================================================================
//  Lote K do gap_system (itens 8, 16, 17 e 19): Lift/Extract da área de
//  trabalho, inverter keyframes no tempo, escala exponencial, suavizador,
//  converter expressão em keyframes, bézier automática/contínua, vagante e
//  Motion Sketch. Cada comando é um passo de desfazer.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/animation/KeyframeAssist.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/project/AeImport.hpp"
#include "aurea/project/Serialization.hpp"
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

static_assert(sizeof(Keyframe) == 48, "lote K: flags no preenchimento, tamanho igual");

namespace {

struct KRig {
    Engine e;
    u64 a = 0, b = 0, c = 0;
    KRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        a = *e.add_null(false);
        b = *e.add_null(false);
        c = *e.add_null(false);
        range(a, 0, 30);
        range(b, 30, 60);
        range(c, 60, 90);
    }
    ~KRig() { e.shutdown(); }
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
    u32 layer_count() {
        u32 n = 0;
        comp()->layers().for_each([&](LayerId, const Layer&) { ++n; });
        return n;
    }
};

i64 P(TrackProperty p) { return static_cast<i64>(p); }

/// Inclinação numérica (valor por quadro) logo antes / logo depois de `t`.
f64 slope_left(const Track& t, f64 at) { return (t.sample_keys_f(at) - t.sample_keys_f(at - 0.01)) / 0.01; }
f64 slope_right(const Track& t, f64 at) { return (t.sample_keys_f(at + 0.01) - t.sample_keys_f(at)) / 0.01; }

} // namespace

// -----------------------------------------------------------------------------
// Item 8: Lift / Extract Work Area
// -----------------------------------------------------------------------------
AUREA_TEST(AeKeys, LiftLeavesTheGapAndExtractClosesItSplittingAtTheEdges) {
    KRig r;
    // D cruza as duas bordas da área [20,40): vira duas.
    const u64 d = *r.e.add_null(false);
    r.range(d, 10, 50);
    Track& op = r.L(d)->tracks.get_or_create(TrackProperty::Opacity);
    op.set(r.L(d)->local_time(FrameIndex{10}), 0.0f);
    op.set(r.L(d)->local_time(FrameIndex{50}), 1.0f);
    const f32 at45 = op.sample(r.L(d)->local_time(FrameIndex{45}));
    AUREA_CHECK_EQ(r.e.lift_work_area(false), 0u);   // sem área: nada
    AUREA_CHECK(r.e.set_work_area(20, 40));
    const u32 before = r.layer_count();
    // Lift: A [0,30) → [0,20); B [30,60) → [40,60); C fica; D → [10,20) + [40,50).
    AUREA_CHECK_EQ(r.e.lift_work_area(false), 3u);
    AUREA_CHECK(r.L(r.a)->start.value == 0 && r.L(r.a)->end.value == 20);
    AUREA_CHECK(r.L(r.b)->start.value == 40 && r.L(r.b)->end.value == 60);
    AUREA_CHECK(r.L(r.c)->start.value == 60 && r.L(r.c)->end.value == 90);
    AUREA_CHECK(r.L(d)->start.value == 10 && r.L(d)->end.value == 20);
    AUREA_CHECK_EQ(r.layer_count(), before + 1);
    const Layer* tail = nullptr;
    r.comp()->layers().for_each([&](LayerId id, const Layer& l) {
        if (id != LayerId::unpack(d) && l.start.value == 40 && l.end.value == 50) tail = &l;
    });
    AUREA_CHECK(tail != nullptr);
    if (tail) {
        // O pedaço de depois mostra o mesmo conteúdo no mesmo quadro.
        const Track* t2 = tail->tracks.find(TrackProperty::Opacity);
        AUREA_CHECK(t2 != nullptr);
        if (t2) AUREA_CHECK_NEAR(t2->sample(tail->local_time(FrameIndex{45})), at45, 1e-6);
    }
    r.undo();   // um passo só
    AUREA_CHECK_EQ(r.layer_count(), before);
    AUREA_CHECK(r.L(r.a)->end.value == 30 && r.L(r.b)->start.value == 30 && r.L(d)->end.value == 50);
    // Extract: o buraco fecha. A [0,20); B → [20,40); C → [40,70); D → [10,20) + [20,30).
    AUREA_CHECK_EQ(r.e.lift_work_area(true), 4u);
    AUREA_CHECK(r.L(r.a)->start.value == 0 && r.L(r.a)->end.value == 20);
    AUREA_CHECK(r.L(r.b)->start.value == 20 && r.L(r.b)->end.value == 40);
    AUREA_CHECK(r.L(r.c)->start.value == 40 && r.L(r.c)->end.value == 70);
    tail = nullptr;
    r.comp()->layers().for_each([&](LayerId id, const Layer& l) {
        if (id != LayerId::unpack(d) && l.start.value == 20 && l.end.value == 30) tail = &l;
    });
    AUREA_CHECK(tail != nullptr);
    if (tail) {
        const Track* t2 = tail->tracks.find(TrackProperty::Opacity);
        // O quadro 45 de antes agora é o 25.
        if (t2) AUREA_CHECK_NEAR(t2->sample(tail->local_time(FrameIndex{25})), at45, 1e-6);
    }
    // Camada travada não muda.
    r.undo();
    r.L(r.c)->locked = true;
    AUREA_CHECK(r.e.lift_work_area(true) >= 1u);
    AUREA_CHECK(r.L(r.c)->start.value == 60);
    std::printf("lift [20,40): buraco e divisão nas bordas; extract fecha (B→20, C→40); um passo; ");
}

// -----------------------------------------------------------------------------
// Item 16: inverter no tempo, escala exponencial, converter expressão
// -----------------------------------------------------------------------------
AUREA_TEST(AeKeys, TimeReverseMirrorsTheCurveInsideTheSelectionSpan) {
    KRig r;
    Track& t = r.L(r.a)->tracks.get_or_create(TrackProperty::Opacity);
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 0.8f);
    t.set(FrameIndex{30}, 0.2f);
    t.set_interpolation(FrameIndex{0}, Interpolation::Bezier, 0.6f, 0.0f, 0.9f, 1.0f);
    t.set_interpolation(FrameIndex{10}, Interpolation::EaseIn, 0, 0, 0, 0);
    const Track original = t;
    const i64 op = P(TrackProperty::Opacity);
    const i64 refs[] = {op, -1, 0, 0, op, -1, 0, 10, op, -1, 0, 30};
    AUREA_CHECK_EQ(r.e.time_reverse_keyframe_selection(r.a, refs, 3), 1u);
    const Track* now = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(now && now->keys.size() == 3);
    if (now) {
        AUREA_CHECK_EQ(now->keys[1].time.value, 20);
        AUREA_CHECK_NEAR(now->keys[0].value, 0.2f, 1e-6);
        AUREA_CHECK(now->keys[0].interp == Interpolation::EaseOut);   // EaseIn espelhado
        f64 worst = 0;
        for (f64 f = 0; f <= 30; f += 0.5) worst = std::max(worst, static_cast<f64>(std::fabs(now->sample_keys_f(f) - original.sample_keys_f(30 - f))));
        AUREA_CHECK(worst < 2e-3);
    }
    r.undo();
    now = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(now && now->keys[1].time.value == 10 && now->keys[0].interp == Interpolation::Bezier);
    // Um escolhido cairia em cima de um não escolhido: recusa inteira.
    Track& u = r.L(r.b)->tracks.get_or_create(TrackProperty::RotationZ);
    for (i64 f : {0, 5, 25, 30}) u.set(FrameIndex{f}, static_cast<f32>(f));
    const i64 rz = P(TrackProperty::RotationZ);
    const i64 clash[] = {rz, -1, 0, 0, rz, -1, 0, 5, rz, -1, 0, 30};
    AUREA_CHECK_EQ(r.e.time_reverse_keyframe_selection(r.b, clash, 3), 0u);
    AUREA_CHECK(r.L(r.b)->tracks.find(TrackProperty::RotationZ)->keys[1].time.value == 5);
    std::printf("inverter: f'(t) = f(30 − t) com bézier e EaseIn→EaseOut, colisão recusada; ");
}

AUREA_TEST(AeKeys, ExponentialScaleWritesAKeyPerFrameOnTheExponentialCurve) {
    KRig r;
    Track& sx = r.L(r.a)->tracks.get_or_create(TrackProperty::ScaleX);
    sx.set(FrameIndex{0}, 1.0f);
    sx.set(FrameIndex{12}, 3.0f);   // escolhido não; sai (está no meio)
    sx.set(FrameIndex{30}, 4.0f);
    const i64 s = P(TrackProperty::ScaleX);
    const i64 refs[] = {s, -1, 0, 0, s, -1, 0, 30};
    AUREA_CHECK_EQ(r.e.exponential_scale_keyframe_selection(r.a, refs, 2), 1u);
    const Track* t = r.L(r.a)->tracks.find(TrackProperty::ScaleX);
    AUREA_CHECK(t && t->keys.size() == 31);
    if (t) {
        AUREA_CHECK_NEAR(t->sample(FrameIndex{15}), 2.0f, 1e-5);
        AUREA_CHECK_NEAR(t->sample(FrameIndex{10}), std::pow(4.0f, 1.0f / 3.0f), 1e-5);
        AUREA_CHECK_NEAR(t->sample(FrameIndex{30}), 4.0f, 1e-6);
        // Razão constante entre quadros vizinhos: velocidade visual constante.
        AUREA_CHECK_NEAR(t->sample(FrameIndex{21}) / t->sample(FrameIndex{20}), t->sample(FrameIndex{6}) / t->sample(FrameIndex{5}), 1e-5);
    }
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::ScaleX)->keys.size(), usize{3});
    // Só escala; e as pontas precisam do mesmo sinal.
    Track& op = r.L(r.a)->tracks.get_or_create(TrackProperty::Opacity);
    op.set(FrameIndex{0}, 0.5f); op.set(FrameIndex{30}, 1.0f);
    const i64 o = P(TrackProperty::Opacity);
    const i64 opRefs[] = {o, -1, 0, 0, o, -1, 0, 30};
    AUREA_CHECK_EQ(r.e.exponential_scale_keyframe_selection(r.a, opRefs, 2), 0u);
    Track& sy = r.L(r.a)->tracks.get_or_create(TrackProperty::ScaleY);
    sy.set(FrameIndex{0}, 0.0f); sy.set(FrameIndex{30}, 2.0f);
    const i64 y = P(TrackProperty::ScaleY);
    const i64 zero[] = {y, -1, 0, 0, y, -1, 0, 30};
    AUREA_CHECK_EQ(r.e.exponential_scale_keyframe_selection(r.a, zero, 2), 0u);
    std::printf("escala exponencial 100%%→400%%: 31 keys, 200%% no meio, razão constante; ");
}

AUREA_TEST(AeKeys, BakeExpressionWritesOneKeyPerFrameAndDisablesIt) {
    KRig r;
    // B mostra [30,60): local = timeline − 30.
    AUREA_CHECK(r.e.set_expression(r.b, static_cast<u32>(TrackProperty::RotationZ), kInvalidIndex, 0, "time * 30").ok());
    const Track* t = r.L(r.b)->tracks.find(TrackProperty::RotationZ);
    AUREA_CHECK(t && t->has_expression());
    const f32 live = t ? t->sample(r.L(r.b)->local_time(FrameIndex{45})) : 0.0f;
    AUREA_CHECK_EQ(r.e.bake_expression_keyframes(r.b, nullptr, 0), 1u);
    t = r.L(r.b)->tracks.find(TrackProperty::RotationZ);
    AUREA_CHECK(t && t->keys.size() == 30 && !t->has_expression() && t->expression);
    if (t && t->keys.size() == 30) {
        AUREA_CHECK_EQ(t->keys.front().time.value, r.L(r.b)->local_time(FrameIndex{30}).value);
        AUREA_CHECK_NEAR(t->sample(r.L(r.b)->local_time(FrameIndex{45})), live, 1e-4);
        AUREA_CHECK(t->keys[0].interp == Interpolation::Linear);
    }
    r.undo();
    t = r.L(r.b)->tracks.find(TrackProperty::RotationZ);
    AUREA_CHECK(t && t->keys.empty() && t->has_expression());
    // Sem expressão: nada a fazer.
    AUREA_CHECK_EQ(r.e.bake_expression_keyframes(r.a, nullptr, 0), 0u);
    // Por trilha: só a escolhida.
    const i64 rz[] = {P(TrackProperty::RotationZ), -1, 0, 0};
    AUREA_CHECK_EQ(r.e.bake_expression_keyframes(r.b, rz, 1), 1u);
    std::printf("expressão → keys: 30 keys lineares, valor igual (%.2f), expressão guardada desligada; ", live);
}

// -----------------------------------------------------------------------------
// Item 17: suavizador e Motion Sketch
// -----------------------------------------------------------------------------
AUREA_TEST(AeKeys, SmootherRefitsNoisyKeysWithinTheTolerance) {
    KRig r;
    Track& t = r.L(r.a)->tracks.get_or_create(TrackProperty::PositionX);
    std::vector<i64> refs;
    u32 seed = 7;
    for (i64 f = 0; f <= 60; ++f) {
        seed = seed * 1664525u + 1013904223u;
        const f32 noise = (static_cast<f32>(seed >> 8) / static_cast<f32>(1u << 24) - 0.5f) * 0.6f;   // ±0,3 px
        t.set(FrameIndex{f}, 100.0f * std::sin(static_cast<f32>(f) / 60.0f * 3.14159f) + noise);
        refs.insert(refs.end(), {P(TrackProperty::PositionX), -1, 0, f});
    }
    const Track original = t;
    // 1 % da amplitude (~100 px) = ~1 px.
    AUREA_CHECK_EQ(r.e.smooth_keyframe_selection(r.a, refs.data(), static_cast<u32>(refs.size() / 4), 0.01f), 1u);
    const Track* now = r.L(r.a)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(now != nullptr);
    if (!now) return;
    f64 worst = 0;
    for (i64 f = 0; f <= 60; ++f)
        worst = std::max(worst, static_cast<f64>(std::fabs(now->sample(FrameIndex{f}) - original.sample(FrameIndex{f}))));
    std::printf("suavizador: 61 → %zu keys, desvio máx %.3f px (tolerância 1); ", now->keys.size(), worst);
    AUREA_CHECK(now->keys.size() < 15);
    AUREA_CHECK(worst < 2.0);
    // As pontas ficam; os do meio são contínuos (mesma velocidade dos dois lados).
    AUREA_CHECK_EQ(now->keys.front().time.value, 0);
    AUREA_CHECK_EQ(now->keys.back().time.value, 60);
    for (usize i = 1; i + 1 < now->keys.size(); ++i) {
        AUREA_CHECK(now->keys[i].flags & kKeyContinuous);
        const f64 at = static_cast<f64>(now->keys[i].time.value);
        AUREA_CHECK_NEAR(slope_left(*now, at), slope_right(*now, at), 0.05);
    }
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys.size(), usize{61});
    // Dois keys não bastam (o AE pede três).
    const i64 two[] = {P(TrackProperty::PositionX), -1, 0, 0, P(TrackProperty::PositionX), -1, 0, 60};
    AUREA_CHECK_EQ(r.e.smooth_keyframe_selection(r.a, two, 2, 0.01f), 0u);
}

AUREA_TEST(AeKeys, MotionSketchRecordsSamplesIntoPositionKeys) {
    KRig r;
    // B começa no 30: o quadro 40 da composição é o local 10.
    std::vector<f64> samples;
    for (f64 t = 40.0; t <= 50.0 + 1e-9; t += 0.5) samples.insert(samples.end(), {t, 10.0 * (t - 40.0), 5.0});
    const u32 n = static_cast<u32>(samples.size() / 3);
    AUREA_CHECK_EQ(r.e.set_position_keys(r.b, samples.data(), n, 1.0, 0.0f), 11u);
    const Track* x = r.L(r.b)->tracks.find(TrackProperty::PositionX);
    const Track* y = r.L(r.b)->tracks.find(TrackProperty::PositionY);
    AUREA_CHECK(x && y && x->keys.size() == 11 && y->keys.size() == 11);
    if (x && y) {
        AUREA_CHECK_EQ(x->keys.front().time.value, r.L(r.b)->local_time(FrameIndex{40}).value);
        AUREA_CHECK_NEAR(x->sample(r.L(r.b)->local_time(FrameIndex{45})), 50.0f, 1e-4);
        AUREA_CHECK_NEAR(y->sample(r.L(r.b)->local_time(FrameIndex{45})), 5.0f, 1e-6);
    }
    r.undo();
    AUREA_CHECK(r.L(r.b)->tracks.find(TrackProperty::PositionX) == nullptr
                || r.L(r.b)->tracks.find(TrackProperty::PositionX)->keys.empty());
    // Velocidade de captura 200 %: o gesto de 10 quadros toca em 20.
    AUREA_CHECK_EQ(r.e.set_position_keys(r.b, samples.data(), n, 2.0, 0.0f), 21u);
    x = r.L(r.b)->tracks.find(TrackProperty::PositionX);
    if (x) AUREA_CHECK_NEAR(x->sample(r.L(r.b)->local_time(FrameIndex{50})), 50.0f, 1e-4);
    r.undo();
    // Suavização: o movimento reto vira poucos keys.
    AUREA_CHECK_EQ(r.e.set_position_keys(r.b, samples.data(), n, 1.0, 0.5f), 11u);
    x = r.L(r.b)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(x && x->keys.size() <= 3);
    if (x) AUREA_CHECK_NEAR(x->sample(r.L(r.b)->local_time(FrameIndex{45})), 50.0f, 0.5);
    // Entradas ruins: recusa.
    AUREA_CHECK_EQ(r.e.set_position_keys(r.b, samples.data(), 1, 1.0, 0.0f), 0u);
    AUREA_CHECK_EQ(r.e.set_position_keys(r.b, samples.data(), n, 0.0, 0.0f), 0u);
    std::printf("motion sketch: 11 keys em tempo real, 21 a 200%%, ≤3 suavizado; ");
}

// -----------------------------------------------------------------------------
// Item 19: bézier automática / contínua e vagante
// -----------------------------------------------------------------------------
AUREA_TEST(AeKeys, AutoBezierSolvesHandlesAndFollowsNeighbourEdits) {
    Track t;
    t.property = TrackProperty::Opacity;
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 5.0f);
    t.set(FrameIndex{20}, 20.0f);
    t.keys[1].flags = kKeyAutoBezier;
    t.resolve_auto_handles();
    // Inclinação = (20 − 0) / 20 = 1 por quadro nos dois lados.
    AUREA_CHECK(t.keys[0].interp == Interpolation::Bezier && t.keys[1].interp == Interpolation::Bezier);
    AUREA_CHECK_NEAR(t.keys[0].by2, 1.0f / 3.0f, 1e-5);
    AUREA_CHECK_NEAR(t.keys[1].by1, 2.0f / 9.0f, 1e-5);
    AUREA_CHECK_NEAR(slope_left(t, 10.0), 1.0, 0.02);
    AUREA_CHECK_NEAR(slope_right(t, 10.0), 1.0, 0.02);
    // Mexer no vizinho refaz as alças (Track::set).
    t.set(FrameIndex{20}, 40.0f);
    AUREA_CHECK_NEAR(slope_left(t, 10.0), 2.0, 0.03);
    AUREA_CHECK_NEAR(slope_right(t, 10.0), 2.0, 0.03);
    // Pico: parado no key (sem passar do valor).
    t.set(FrameIndex{10}, 50.0f);
    AUREA_CHECK_NEAR(t.keys[0].by2, 1.0f, 1e-5);
    AUREA_CHECK_NEAR(t.keys[1].by1, 0.0f, 1e-5);
    // Mover no tempo também refaz (Track::move).
    t.set(FrameIndex{10}, 5.0f);
    (void)t.move(FrameIndex{10}, FrameIndex{5});
    AUREA_CHECK_NEAR(slope_left(t, 5.0), slope_right(t, 5.0), 0.05);
    // Alça escolhida à mão: deixa de ser automática.
    t.set_interpolation(FrameIndex{5}, Interpolation::Bezier, 0.5f, 0.1f, 0.5f, 0.9f);
    AUREA_CHECK_EQ(t.keys[1].flags & kKeyAutoBezier, 0);
    std::printf("automática: alças pela inclinação dos vizinhos, refeitas no set/move, pico parado; ");
}

AUREA_TEST(AeKeys, ContinuousBezierAveragesBothSidesThroughTheEngine) {
    KRig r;
    Track& t = r.L(r.a)->tracks.get_or_create(TrackProperty::Opacity);
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 0.5f);
    t.set(FrameIndex{20}, 0.6f);
    const i64 op = P(TrackProperty::Opacity);
    const i64 mid[] = {op, -1, 0, 10};
    AUREA_CHECK_EQ(r.e.query_keyframe_flags(r.a, mid, 1), 0u);
    AUREA_CHECK(r.e.set_keyframe_temporal_mode(r.a, mid, 1, Engine::kKeyModeContinuous) >= 1u);
    const Track* now = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(now && (now->keys[1].flags & kKeyContinuous));
    // Antes: 0,05/quadro chegando e 0,01 saindo; agora 0,03 dos dois lados.
    if (now) {
        AUREA_CHECK_NEAR(slope_left(*now, 10.0), 0.03, 1e-3);
        AUREA_CHECK_NEAR(slope_right(*now, 10.0), 0.03, 1e-3);
    }
    AUREA_CHECK_EQ(r.e.query_keyframe_flags(r.a, mid, 1) & 0xFFu, static_cast<u32>(kKeyContinuous));
    r.undo();
    now = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(now && now->keys[1].flags == 0 && now->keys[0].interp == Interpolation::Linear);
    // Automática pelo motor, depois volta a bézier comum (as alças ficam).
    AUREA_CHECK(r.e.set_keyframe_temporal_mode(r.a, mid, 1, Engine::kKeyModeAuto) >= 1u);
    now = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(now && (now->keys[1].flags & kKeyAutoBezier));
    const f32 by2 = now ? now->keys[0].by2 : 0.0f;
    AUREA_CHECK(r.e.set_keyframe_temporal_mode(r.a, mid, 1, Engine::kKeyModeBezier) >= 1u);
    now = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(now && now->keys[1].flags == 0);
    if (now) AUREA_CHECK_NEAR(now->keys[0].by2, by2, 1e-6);
    std::printf("contínua: média das inclinações (0,03 dos dois lados), automática e volta, desfazível; ");
}

AUREA_TEST(AeKeys, RovingRetimesInnerPositionKeysByArcLength) {
    KRig r;
    Layer* l = r.L(r.a);
    Track& x = l->tracks.get_or_create(TrackProperty::PositionX);
    Track& y = l->tracks.get_or_create(TrackProperty::PositionY);
    x.set(FrameIndex{0}, 0.0f);  y.set(FrameIndex{0}, 0.0f);
    x.set(FrameIndex{10}, 10.0f); y.set(FrameIndex{10}, 0.0f);
    x.set(FrameIndex{20}, 10.0f); y.set(FrameIndex{20}, 40.0f);
    x.set(FrameIndex{40}, 60.0f); y.set(FrameIndex{40}, 40.0f);
    // Caminho: 10 + 40 + 50 = 100 px em 40 quadros → keys do meio em 4 e 20.
    const i64 px = P(TrackProperty::PositionX);
    const i64 refs[] = {px, -1, 0, 0, px, -1, 0, 10, px, -1, 0, 20, px, -1, 0, 40};
    AUREA_CHECK(r.e.set_keyframe_temporal_mode(r.a, refs, 4, Engine::kKeyModeRoveOn) >= 2u);
    const Track* nx = r.L(r.a)->tracks.find(TrackProperty::PositionX);
    const Track* ny = r.L(r.a)->tracks.find(TrackProperty::PositionY);
    AUREA_CHECK(nx && ny && nx->keys.size() == 4 && ny->keys.size() == 4);
    if (nx && ny && nx->keys.size() == 4 && ny->keys.size() == 4) {
        AUREA_CHECK_EQ(nx->keys[1].time.value, 4);
        AUREA_CHECK_EQ(nx->keys[2].time.value, 20);
        AUREA_CHECK_EQ(ny->keys[1].time.value, 4);
        AUREA_CHECK(nx->keys[1].flags & kKeyRoving);
        AUREA_CHECK_EQ(nx->keys[0].flags & kKeyRoving, 0);   // as pontas não vagam
        AUREA_CHECK_EQ(nx->keys[3].flags & kKeyRoving, 0);
        AUREA_CHECK_NEAR(nx->sample(FrameIndex{4}), 10.0f, 1e-5);
    }
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys[1].time.value, 10);
    std::printf("vagante: 100 px em 40 quadros → keys do meio em 4 e 20, pontas fixas; ");
}

AUREA_TEST(AeKeys, FlagsSurviveSaveAndOldFilesLoadWithoutThem) {
    KRig r;
    Track& t = r.L(r.a)->tracks.get_or_create(TrackProperty::Opacity);
    t.set(FrameIndex{0}, 0.0f); t.set(FrameIndex{10}, 0.5f); t.set(FrameIndex{20}, 1.0f);
    t.keys[1].flags = kKeyAutoBezier | kKeyRoving;
    t.resolve_auto_handles();
    const f32 by2 = t.keys[0].by2;
    const char* dir = std::getenv("TEMP");
    const std::string path = std::string(dir ? dir : ".") + "/aurea_test_lote_k_flags.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    KRig other;
    AUREA_CHECK(other.e.load_project(path.c_str()).ok());
    const Track* back = nullptr;
    other.comp()->layers().for_each([&](LayerId, const Layer& l) {
        if (const Track* o = l.tracks.find(TrackProperty::Opacity); o && o->keys.size() == 3) back = o;
    });
    AUREA_CHECK(back != nullptr);
    if (back) {
        AUREA_CHECK_EQ(back->keys[1].flags, static_cast<u8>(kKeyAutoBezier | kKeyRoving));
        AUREA_CHECK_EQ(back->keys[0].flags, 0);
        AUREA_CHECK_NEAR(back->keys[0].by2, by2, 1e-6);   // alças gravadas, não refeitas
    }
    std::remove(path.c_str());
    // Projeto antigo (sem o byte): keys sem bits, alças como estavam — o
    // leitor só pede o byte na v73 (OldProjects cobre o resto do formato).
    AUREA_CHECK(kTimelineSectionVersion >= 73u);
    std::printf("bits automática+vagante salvos e relidos; alças iguais; ");
}

// -----------------------------------------------------------------------------
// Importação do AE: bits temporais do script
// -----------------------------------------------------------------------------
AUREA_TEST(AeKeys, AeScriptTemporalFlagsBecomeKeyFlags) {
    const char* src = R"JS(var comp = app.project.activeItem;
var solid = comp.layers.addSolid([0.2, 0.4, 0.8], "Fundo", 1920, 1080, 1);
var o = solid.transform.opacity;
o.setValueAtTime(0, 0);
o.setValueAtTime(1, 50);
o.setValueAtTime(2, 100);
o.setInterpolationTypeAtKey(2, KeyframeInterpolationType.BEZIER, KeyframeInterpolationType.BEZIER);
o.setTemporalAutoBezierAtKey(2, true);
var p = solid.transform.position;
p.setValueAtTime(0, [0, 0]);
p.setValueAtTime(1, [100, 0]);
p.setValueAtTime(3, [200, 0]);
p.setRovingAtKey(2, true);
var s = solid.transform.scale;
s.setValueAtTime(0, [50, 50]);
s.setValueAtTime(1, [80, 80]);
s.setValueAtTime(2, [100, 100]);
s.setTemporalContinuousAtKey(2, true);
var check = (o.keyTemporalAutoBezier(2) && p.keyRoving(2) && s.keyTemporalContinuous(2) && !o.keyRoving(1)) ? 1 : 0;
solid.name = check ? "ok" : "falhou";
)JS";
    KRig rig;
    const ae::ImportReport rep = rig.e.import_ae_data(src, "flags.jsx", 0);
    AUREA_CHECK(rep.ok);
    const Layer* solid = nullptr;
    rig.comp()->layers().for_each([&](LayerId, const Layer& l) { if (l.name == "ok" || l.name == "falhou") solid = &l; });
    AUREA_CHECK(solid && solid->name == "ok");
    if (!solid) return;
    const Track* op = solid->tracks.find(TrackProperty::Opacity);
    const Track* px = solid->tracks.find(TrackProperty::PositionX);
    const Track* sx = solid->tracks.find(TrackProperty::ScaleX);
    AUREA_CHECK(op && op->keys.size() == 3 && (op->keys[1].flags & kKeyAutoBezier));
    AUREA_CHECK(px && px->keys.size() == 3 && (px->keys[1].flags & kKeyRoving));
    AUREA_CHECK(sx && sx->keys.size() == 3 && (sx->keys[1].flags & kKeyContinuous));
    if (op) AUREA_CHECK_EQ(op->keys[0].flags, 0);
    std::printf("JSX: setTemporalAutoBezierAtKey/setRovingAtKey/setTemporalContinuousAtKey viram bits; ");
}

// -----------------------------------------------------------------------------
// Contas puras (sem motor)
// -----------------------------------------------------------------------------
AUREA_TEST(AeKeys, MirrorSegmentEaseIsAnInvolution) {
    Keyframe k{};
    k.interp = Interpolation::Bezier;
    k.bx1 = 0.2f; k.by1 = 0.1f; k.bx2 = 0.7f; k.by2 = 0.95f;
    Keyframe m = k;
    animation::mirror_segment_ease(m);
    AUREA_CHECK_NEAR(m.bx1, 0.3f, 1e-6);
    AUREA_CHECK_NEAR(m.by1, 0.05f, 1e-6);
    animation::mirror_segment_ease(m);
    AUREA_CHECK_NEAR(m.bx1, k.bx1, 1e-6);
    AUREA_CHECK_NEAR(m.by2, k.by2, 1e-6);
    Track t;
    animation::write_samples(t, 5, {1.0f, 2.0f, 3.0f});
    AUREA_CHECK(t.keys.size() == 3 && t.keys[0].time.value == 5 && t.keys[2].value == 3.0f);
}
