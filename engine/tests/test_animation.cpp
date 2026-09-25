// Testes do motor de keyframes.
//
// O que estes testes protegem: a avaliação acontece ~150 mil vezes por segundo
// num projeto grande. Um erro aqui não trava o app — ele faz a animação sair
// sutilmente errada, que é o tipo de bug que ninguém reporta e todo mundo sente.
#include "TestFramework.hpp"
#include "aurea/animation/Curve.hpp"

using namespace aurea;

AUREA_TEST(Track, EmptyTrackReturnsStatic) {
    Track t;
    t.staticValue = 0.75f;
    AUREA_CHECK_NEAR(t.sample(FrameIndex{100}), 0.75f, 1e-6);
}

AUREA_TEST(Track, SingleKeyframeIsConstant) {
    Track t;
    t.set(FrameIndex{10}, 0.5f);
    // Antes, no ponto e depois: o valor de um único keyframe vale para toda a
    // timeline. É o que o usuário espera ao animar uma propriedade pela
    // primeira vez — ele não quer que a camada suma antes do keyframe.
    AUREA_CHECK_NEAR(t.sample(FrameIndex{0}), 0.5f, 1e-6);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{10}), 0.5f, 1e-6);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{500}), 0.5f, 1e-6);
}

AUREA_TEST(Track, LinearInterpolationMidpoint) {
    Track t;
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{100}, 1.0f);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{50}), 0.5f, 1e-5);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{25}), 0.25f, 1e-5);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{75}), 0.75f, 1e-5);
}

AUREA_TEST(Track, SampleBeforeFirstKeyframeClamps) {
    Track t;
    t.set(FrameIndex{50}, 0.2f);
    t.set(FrameIndex{100}, 0.9f);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{0}), 0.2f, 1e-6);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{49}), 0.2f, 1e-6);
}

AUREA_TEST(Track, SampleAfterLastKeyframeClamps) {
    Track t;
    t.set(FrameIndex{0}, 0.2f);
    t.set(FrameIndex{50}, 0.9f);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{500}), 0.9f, 1e-6);
}

AUREA_TEST(Track, HoldKeepsLeftValueUntilNext) {
    Track t;
    t.set(FrameIndex{0}, 0.0f, Interpolation::Hold);
    t.set(FrameIndex{100}, 1.0f);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{99}), 0.0f, 1e-6);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{100}), 1.0f, 1e-6);
}

AUREA_TEST(Track, SetAtExistingTimeReplaces) {
    Track t;
    t.set(FrameIndex{10}, 1.0f);
    t.set(FrameIndex{20}, 2.0f);
    t.set(FrameIndex{10}, 5.0f);
    AUREA_CHECK_EQ(t.keys.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(t.sample(FrameIndex{10}), 5.0f, 1e-6);
}

AUREA_TEST(Track, InsertKeepsSortedOrder) {
    Track t;
    t.set(FrameIndex{50}, 0.5f);
    t.set(FrameIndex{10}, 0.1f);
    t.set(FrameIndex{90}, 0.9f);
    t.set(FrameIndex{30}, 0.3f);
    AUREA_CHECK_EQ(t.keys.size(), static_cast<usize>(4));
    for (usize i = 1; i < t.keys.size(); ++i) {
        AUREA_CHECK(t.keys[i - 1].time.value < t.keys[i].time.value);
    }
}

AUREA_TEST(Track, RemoveExistingKeyframe) {
    Track t;
    t.set(FrameIndex{10}, 1.0f);
    t.set(FrameIndex{20}, 2.0f);
    AUREA_CHECK(t.remove(FrameIndex{10}));
    AUREA_CHECK_EQ(t.keys.size(), static_cast<usize>(1));
    AUREA_CHECK(!t.remove(FrameIndex{999}));
}

AUREA_TEST(Track, MoveKeyframePreservesOrder) {
    Track t;
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 1.0f);
    t.set(FrameIndex{20}, 2.0f);
    const u32 idx = t.move(FrameIndex{10}, FrameIndex{25});
    AUREA_CHECK(idx != kInvalidIndex);
    AUREA_CHECK_EQ(t.keys.back().time.value, static_cast<i64>(25));
    AUREA_CHECK_NEAR(t.keys.back().value, 1.0f, 1e-6);
}

AUREA_TEST(Track, MoveOntoExistingReplaces) {
    Track t;
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 1.0f);
    t.set(FrameIndex{20}, 2.0f);
    t.move(FrameIndex{10}, FrameIndex{20});
    // O destino é substituído: arrastar um keyframe sobre outro sobrepõe.
    AUREA_CHECK_EQ(t.keys.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(t.sample(FrameIndex{20}), 1.0f, 1e-6);
}

AUREA_TEST(Track, SetInterpolationOnlyTouchesBezierFieldsForBezier) {
    Track t;
    t.set(FrameIndex{0}, 0.0f);
    t.set_interpolation(FrameIndex{0}, Interpolation::Linear, 0.9f, 0.9f, 0.9f, 0.9f);
    // Voltar para linear não deve gravar control points — se gravasse, um
    // clique acidental em "linear" apagaria a curva configurada.
    AUREA_CHECK_NEAR(t.keys[0].bx1, 0.33f, 1e-6);
    t.set_interpolation(FrameIndex{0}, Interpolation::Bezier, 0.1f, 0.2f, 0.3f, 0.4f);
    AUREA_CHECK_NEAR(t.keys[0].bx1, 0.1f, 1e-6);
    AUREA_CHECK_NEAR(t.keys[0].by2, 0.4f, 1e-6);
}

AUREA_TEST(Track, FindBeforeHandlesAllPositions) {
    Track t;
    t.set(FrameIndex{10}, 1.0f);
    t.set(FrameIndex{20}, 2.0f);
    t.set(FrameIndex{30}, 3.0f);

    AUREA_CHECK_EQ(t.find_before(FrameIndex{5}), kInvalidIndex);
    AUREA_CHECK_EQ(t.find_before(FrameIndex{10}), static_cast<u32>(0));
    AUREA_CHECK_EQ(t.find_before(FrameIndex{15}), static_cast<u32>(0));
    AUREA_CHECK_EQ(t.find_before(FrameIndex{20}), static_cast<u32>(1));
    AUREA_CHECK_EQ(t.find_before(FrameIndex{1000}), static_cast<u32>(2));
}

AUREA_TEST(TrackSet, GetOrCreateReturnsSameTrack) {
    TrackSet set;
    Track& a = set.get_or_create(TrackProperty::Opacity);
    Track& b = set.get_or_create(TrackProperty::Opacity);
    AUREA_CHECK(&a == &b);
    AUREA_CHECK_EQ(set.size(), static_cast<u32>(1));
}

AUREA_TEST(TrackSet, EffectParamsAreDistinctKeyed) {
    TrackSet set;
    // Duas propriedades de dois efeitos diferentes: sem a chave composta, a
    // animação de um efeito alimentaria o outro.
    set.set_static(TrackProperty::EffectParam, 1.0f, 0, 0);
    set.set_static(TrackProperty::EffectParam, 2.0f, 0, 1);
    set.set_static(TrackProperty::EffectParam, 3.0f, 1, 0);
    AUREA_CHECK_EQ(set.size(), static_cast<u32>(3));

    const Track* t = set.find(TrackProperty::EffectParam, 0, 1);
    AUREA_CHECK(t != nullptr);
    AUREA_CHECK_NEAR(t->staticValue, 2.0f, 1e-6);
}

AUREA_TEST(TrackSet, HasAnimationOnlyWithMultipleKeyframes) {
    TrackSet set;
    set.set_static(TrackProperty::Opacity, 0.5f);
    AUREA_CHECK(!set.has_animation());

    set.get_or_create(TrackProperty::ScaleX).set(FrameIndex{0}, 1.0f);
    AUREA_CHECK(!set.has_animation());   // um keyframe só não é animação

    set.get_or_create(TrackProperty::ScaleX).set(FrameIndex{10}, 2.0f);
    AUREA_CHECK(set.has_animation());
}

AUREA_TEST(Track, FlatBezierTimeHandlesDoNotSnapNearEndpoints) {
    Track track;
    track.set(FrameIndex{0}, 0.0f, Interpolation::Bezier);
    track.set(FrameIndex{1000000}, 1.0f);
    for (const bool flatAtEnd : {false, true}) {
        const f32 control = flatAtEnd ? 1.0f : 0.0f;
        track.set_interpolation(FrameIndex{0}, Interpolation::Bezier,
                                control, 1.0f, control, 0.0f);
        // x=t^3 or x=1-(1-t)^3 has an independent analytic inverse.
        // These frames formerly snapped almost to the endpoint because the
        // x residual was small while the value residual exceeded 2 percent.
        for (const i64 frame : {1LL, 3LL, 10LL, 100LL, 999900LL, 999990LL, 999997LL, 999999LL}) {
            const f32 x = static_cast<f32>(static_cast<f64>(frame) / 1000000.0);
            const f64 t = flatAtEnd ? 1.0 - std::cbrt(1.0 - static_cast<f64>(x))
                                    : std::cbrt(static_cast<f64>(x));
            const f64 expected = 3.0 * t - 6.0 * t * t + 4.0 * t * t * t;
            AUREA_CHECK_NEAR(track.sample(FrameIndex{frame}), expected, 2e-6);
        }
    }
}

AUREA_TEST(Track, BezierRandomReverseSeekPreservesCurveAndOvershoot) {
    Track track;
    track.set(FrameIndex{0}, -30.0f, Interpolation::Bezier);
    track.set(FrameIndex{1000}, 70.0f, Interpolation::Hold);
    track.set(FrameIndex{1500}, 10.0f);
    const f32 handles[][4] = {
        {0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
        {1.0f, -2.0f, 0.0f, 3.0f}, {0.8f, 2.0f, 0.2f, 2.0f},
        {0.42f, 0.0f, 0.58f, 1.0f}, {0.1f, -1.0f, 0.9f, -1.0f}
    };
    auto bezier = [](f64 t, f64 a, f64 b) {
        return 3.0 * (1.0 - t) * (1.0 - t) * t * a +
               3.0 * (1.0 - t) * t * t * b + t * t * t;
    };
    for (const auto& h : handles) {
        track.set_interpolation(FrameIndex{0}, Interpolation::Bezier, h[0], h[1], h[2], h[3]);
        for (i64 order = 0; order <= 1500; ++order) {
            // Coprime permutation visits every frame, repeatedly crossing
            // interpolation segments in both directions (scrubbing/export).
            const i64 frame = (order * 997) % 1501;
            f64 expected = frame == 1500 ? 10.0 : 70.0;
            if (frame < 1000) {
                const f32 x = static_cast<f32>(static_cast<f64>(frame) / 1000.0);
                f64 lo = 0.0, hi = 1.0;
                for (int iteration = 0; iteration < 60; ++iteration) {
                    const f64 mid = (lo + hi) * 0.5;
                    const f64 value = bezier(mid, h[0], h[2]);
                    if (value == x) { lo = hi = mid; break; }
                    if (value < x) lo = mid; else hi = mid;
                }
                expected = -30.0 + 100.0 * bezier((lo + hi) * 0.5, h[1], h[3]);
            }
            AUREA_CHECK_NEAR(track.sample(FrameIndex{frame}), expected, 3e-5);
        }
    }
}
