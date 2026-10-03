// Testes do motor de keyframes.
//
// O que estes testes protegem: a avaliação acontece ~150 mil vezes por segundo
// num projeto grande. Um erro aqui não trava o app — ele faz a animação sair
// sutilmente errada, que é o tipo de bug que ninguém reporta e todo mundo sente.
#include "TestFramework.hpp"
#include "aurea/animation/Curve.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

AUREA_TEST(Track, ConfigurableBounceHasContinuousLandingsAndReversesExactly) {
    Track track; track.set(FrameIndex{0},0); track.set(FrameIndex{1000},1);
    track.set_interpolation(FrameIndex{0},Interpolation::Bounce,3.f/8,.5f,1,-10);
    AUREA_CHECK_NEAR(track.keys[0].by2,-10,1e-6);
    const auto value = [](float t,int count,float strength,bool reverse=false) {
        return apply_easing(Interpolation::Bounce,t,count/8.f,strength,reverse?0.f:1.f,-10);
    };
    AUREA_CHECK_NEAR(value(1.f/2.75f,3,.5f),1,1e-5);
    AUREA_CHECK_NEAR(value(1.5f/2.75f,3,.5f),.75f,1e-5);
    for (int count=1;count<=8;++count) for(float strength:{.1f,.5f,.9f}) {
        float prev=0;
        for(int i=0;i<=10000;++i) {
            const float t=i/10000.f, v=value(t,count,strength);
            AUREA_CHECK(v>=0 && v<=1.000001f);
            AUREA_CHECK_NEAR(v,1-value(1-t,count,strength,true),1e-5);
            AUREA_CHECK(std::fabs(v-prev)<.02f); prev=v;
        }
        AUREA_CHECK_NEAR(value(0,count,strength),0,1e-6);
        AUREA_CHECK_NEAR(value(1,count,strength),1,1e-6);
    }
}

AUREA_TEST(Track, BounceElasticAndFourStepsHaveRealDistinctMotion) {
    static_assert(static_cast<u8>(Interpolation::CustomCurve) == 6);
    static_assert(static_cast<u8>(Interpolation::Bounce) == 7);
    static_assert(static_cast<u8>(Interpolation::Steps) == 9);
    Track t; t.set(FrameIndex{0}, 10.f); t.set(FrameIndex{1000}, 110.f);
    for (auto mode : {Interpolation::Bounce, Interpolation::Elastic, Interpolation::Steps}) {
        t.set_interpolation(FrameIndex{0}, mode, .33f, 0.f, .67f, 1.f);
        AUREA_CHECK_NEAR(t.sample(FrameIndex{0}), 10.f, 1e-6);
        AUREA_CHECK_NEAR(t.sample(FrameIndex{1000}), 110.f, 1e-6);
        for (int frame = 0; frame <= 1000; ++frame)
            AUREA_CHECK(std::isfinite(t.sample(FrameIndex{frame})));
        if (mode == Interpolation::Bounce) {
            AUREA_CHECK_NEAR(t.sample(FrameIndex{500}), 110.f, 1e-4);
            AUREA_CHECK_NEAR(t.sample(FrameIndex{625}), 85.f, 1e-4);
            AUREA_CHECK(t.sample(FrameIndex{825}) > t.sample(FrameIndex{625}));
        } else if (mode == Interpolation::Elastic) {
            AUREA_CHECK(t.sample(FrameIndex{160}) > 140.f);
            AUREA_CHECK(t.sample(FrameIndex{330}) < 100.f);
        } else {
            AUREA_CHECK_NEAR(t.sample(FrameIndex{249}), 10.f, 1e-6);
            AUREA_CHECK_NEAR(t.sample(FrameIndex{250}), 35.f, 1e-6);
            AUREA_CHECK_NEAR(t.sample(FrameIndex{749}), 60.f, 1e-6);
            AUREA_CHECK_NEAR(t.sample(FrameIndex{750}), 85.f, 1e-6);
        }
    }
}

// Força da bézier (easePower, como no app antigo): ×1 é a bézier de sempre;
// ×2/×3 aplicam a MESMA curva de novo sobre o resultado, sem mexer nas alças.
AUREA_TEST(Track, BezierPowerRepeatsTheSameCurveOnItsOwnResult) {
    Track t; t.set(FrameIndex{0}, 0.f); t.set(FrameIndex{100}, 200.f);
    t.set_interpolation(FrameIndex{0}, Interpolation::Bezier, .42f, 0.f, .58f, 1.f);
    AUREA_CHECK_EQ(static_cast<u32>(t.keys[0].easePower), 1u);
    for (int f = 0; f <= 100; f += 5) {
        const f32 u = static_cast<f32>(f) / 100.f;
        // Força 1: exatamente a conta anterior ao campo (projetos antigos não mudam).
        AUREA_CHECK_EQ(t.sample(FrameIndex{f}), lerpf(0.f, 200.f, cubic_bezier(.42f, 0.f, .58f, 1.f, u)));
    }
    t.set_interpolation(FrameIndex{0}, Interpolation::Bezier, .42f, 0.f, .58f, 1.f, 2);
    AUREA_CHECK_EQ(static_cast<u32>(t.keys[0].easePower), 2u);
    const f32 once = cubic_bezier(.42f, 0.f, .58f, 1.f, .25f);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{25}), 200.f * cubic_bezier(.42f, 0.f, .58f, 1.f, once), 1e-4);
    AUREA_CHECK(t.sample(FrameIndex{25}) < 200.f * once);          // acentua o ease-in
    AUREA_CHECK_NEAR(t.sample(FrameIndex{100}), 200.f, 1e-6);       // as pontas não mudam
    // 0 = mantém a força; mudar só a curva não zera o ×2.
    t.set_interpolation(FrameIndex{0}, Interpolation::Bezier, .1f, .9f, .2f, 1.f, 0);
    AUREA_CHECK_EQ(static_cast<u32>(t.keys[0].easePower), 2u);
    // Fora da faixa é presa a 1..3; nomeados ignoram a força.
    t.set_interpolation(FrameIndex{0}, Interpolation::Bezier, .42f, 0.f, .58f, 1.f, 9);
    AUREA_CHECK_EQ(static_cast<u32>(t.keys[0].easePower), 3u);
    t.set_interpolation(FrameIndex{0}, Interpolation::EaseIn, 0.f, 0.f, 1.f, 1.f);
    AUREA_CHECK_NEAR(t.sample(FrameIndex{50}), 50.f, 1e-4);
    static_assert(sizeof(Keyframe) == 48);
}


// -----------------------------------------------------------------------------
// Curvas com parâmetros (pedido de beta: "aquele tipo de curva que passa do
// ponto e balança"): Overshoot, Elástico configurável e Quique. Os parâmetros
// vão nos floats da bézier com o marcador kEaseParamMarker em by2.
// -----------------------------------------------------------------------------
namespace {
Keyframe param_key(Interpolation kind, f32 a, f32 b, bool reverse = false) {
    Keyframe k;
    k.interp = kind;
    k.bx1 = a; k.by1 = b; k.bx2 = reverse ? 0.0f : 1.0f; k.by2 = kEaseParamMarker;
    return k;
}
} // namespace

AUREA_TEST(Easing, OvershootPassesTheEndAndLandsExactly) {
    static_assert(static_cast<u8>(Interpolation::Overshoot) == 10, "valor gravado no projeto");
    static_assert(kLastInterpolation == Interpolation::Overshoot);
    for (f32 amount : {0.0f, 0.1f, kOvershootDefaultAmount, 0.6f, 1.0f}) {
        const Keyframe k = param_key(Interpolation::Overshoot, amount, 0.0f);
        AUREA_CHECK_EQ(keyframe_ease(k, 0.0f), 0.0f);
        AUREA_CHECK_EQ(keyframe_ease(k, 1.0f), 1.0f);
        f32 peak = 0.0f, low = 1.0f;
        for (int i = 0; i <= 1000; ++i) {
            const f32 v = keyframe_ease(k, i / 1000.0f);
            AUREA_CHECK(std::isfinite(v));
            peak = std::max(peak, v); low = std::min(low, v);
        }
        AUREA_CHECK(low >= 0.0f);                 // o overshoot "de saída" não recua no começo
        if (amount == 0.0f) AUREA_CHECK(peak <= 1.0f + 1e-6f);   // s = 0: cúbica sem passar
        else AUREA_CHECK(peak > 1.0f);            // passa do fim — é a graça
    }
    // O padrão é o clássico s = 1,70158: ≈ 10 % além do fim.
    f32 peak = 0.0f;
    for (int i = 0; i <= 4000; ++i) peak = std::max(peak, keyframe_ease(param_key(Interpolation::Overshoot, kOvershootDefaultAmount, 0), i / 4000.0f));
    AUREA_CHECK_NEAR(peak, 1.1f, 0.002f);
    // Mais quantidade, mais longe.
    f32 small = 0, big = 0;
    for (int i = 0; i <= 1000; ++i) {
        small = std::max(small, keyframe_ease(param_key(Interpolation::Overshoot, 0.2f, 0), i / 1000.0f));
        big = std::max(big, keyframe_ease(param_key(Interpolation::Overshoot, 0.8f, 0), i / 1000.0f));
    }
    AUREA_CHECK(big > small);
    // Sem o marcador (escolhida por um caminho que não grava parâmetros): o padrão.
    Keyframe plain; plain.interp = Interpolation::Overshoot;
    AUREA_CHECK_EQ(keyframe_ease(plain, 0.4f), keyframe_ease(param_key(Interpolation::Overshoot, kOvershootDefaultAmount, 0), 0.4f));
    // Invertida = antecipação: recua abaixo de 0 antes de partir, espelho exato.
    const Keyframe in = param_key(Interpolation::Overshoot, 0.5f, 0, true);
    const Keyframe out = param_key(Interpolation::Overshoot, 0.5f, 0, false);
    f32 dip = 1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const f32 t = i / 1000.0f;
        AUREA_CHECK_NEAR(keyframe_ease(in, t), 1.0f - keyframe_ease(out, 1.0f - t), 1e-5);
        dip = std::min(dip, keyframe_ease(in, t));
    }
    AUREA_CHECK(dip < 0.0f);
}

AUREA_TEST(Easing, ElasticOscillatesDampsAndSettlesOnTheEnd) {
    // O padrão com parâmetros é a curva elástica de sempre (3 ciclos, k 6).
    Keyframe legacy; legacy.interp = Interpolation::Elastic;
    const Keyframe def = param_key(Interpolation::Elastic, kElasticDefaultCycles, kElasticDefaultDamping);
    for (int i = 0; i <= 200; ++i) AUREA_CHECK_NEAR(keyframe_ease(def, i / 200.0f), keyframe_ease(legacy, i / 200.0f), 1e-5);
    for (int cycles = 1; cycles <= 8; ++cycles) {
        for (f32 damping : {0.0f, 0.25f, 0.6f, 1.0f}) {
            const Keyframe k = param_key(Interpolation::Elastic, cycles / 8.0f, damping);
            AUREA_CHECK_EQ(keyframe_ease(k, 0.0f), 0.0f);
            AUREA_CHECK_EQ(keyframe_ease(k, 1.0f), 1.0f);
            // Passa do valor final e volta várias vezes; cada ida além de 1 é
            // menor que a anterior além de 1, e cada volta aquém, menor que a
            // anterior aquém: a mola perde energia (o lado de cima e o de baixo
            // não são simétricos com pouco amortecimento — o pouso é forçado em 1).
            int crossings = 0;
            f32 prev = -1.0f, extreme = 0.0f, lastAbove = 1e9f, lastBelow = 1e9f;
            bool decreasing = true;
            for (int i = 1; i < 8000; ++i) {
                const f32 d = keyframe_ease(k, i / 8000.0f) - 1.0f;
                AUREA_CHECK(std::isfinite(d));
                if ((prev < 0.0f && d >= 0.0f) || (prev > 0.0f && d <= 0.0f)) {
                    if (crossings > 0) {
                        f32& last = prev > 0.0f ? lastAbove : lastBelow;
                        if (extreme > last + 1e-5f) decreasing = false;
                        last = extreme;
                    }
                    ++crossings;
                    extreme = 0.0f;
                }
                extreme = std::max(extreme, std::fabs(d));
                prev = d;
            }
            AUREA_CHECK(crossings >= cycles);
            AUREA_CHECK(decreasing);
        }
    }
    // Passa do fim (é a mola) e, no padrão, assenta: perto do fim já está em 1.
    f32 peak = 0.0f;
    for (int i = 0; i <= 1000; ++i) peak = std::max(peak, keyframe_ease(def, i / 1000.0f));
    AUREA_CHECK(peak > 1.2f);
    for (int i = 900; i <= 1000; ++i) AUREA_CHECK(std::fabs(keyframe_ease(def, i / 1000.0f) - 1.0f) < 0.01f);
    // Mais amortecimento = assenta antes.
    auto tail = [](f32 damping) {
        f32 m = 0.0f;
        for (int i = 500; i <= 1000; ++i) m = std::max(m, std::fabs(keyframe_ease(param_key(Interpolation::Elastic, 0.5f, damping), i / 1000.0f) - 1.0f));
        return m;
    };
    AUREA_CHECK(tail(0.8f) < tail(0.1f));
    // Invertida: o espelho exato.
    const Keyframe rev = param_key(Interpolation::Elastic, 0.5f, 0.3f, true);
    const Keyframe fwd = param_key(Interpolation::Elastic, 0.5f, 0.3f, false);
    for (int i = 0; i <= 500; ++i) AUREA_CHECK_NEAR(keyframe_ease(rev, i / 500.0f), 1.0f - keyframe_ease(fwd, 1.0f - i / 500.0f), 1e-5);
}

AUREA_TEST(Easing, BounceNeverPassesTheEnd) {
    Keyframe legacy; legacy.interp = Interpolation::Bounce;
    for (int count = 1; count <= 8; ++count) for (f32 r : {0.1f, 0.5f, 0.9f}) for (bool reverse : {false, true}) {
        const Keyframe k = param_key(Interpolation::Bounce, count / 8.0f, r, reverse);
        AUREA_CHECK_EQ(keyframe_ease(k, 0.0f), 0.0f);
        AUREA_CHECK_EQ(keyframe_ease(k, 1.0f), 1.0f);
        for (int i = 0; i <= 2000; ++i) {
            const f32 v = keyframe_ease(k, i / 2000.0f);
            AUREA_CHECK(v <= 1.0f + 1e-6f && v >= -1e-6f);
        }
    }
    for (int i = 0; i <= 2000; ++i) AUREA_CHECK(keyframe_ease(legacy, i / 2000.0f) <= 1.0f + 1e-6f);
}

AUREA_TEST(Easing, ParametricCurvesAreDeterministicForAnyFractionalTime) {
    const Keyframe kinds[] = {param_key(Interpolation::Overshoot, 0.7f, 0), param_key(Interpolation::Elastic, 0.6f, 0.4f),
                              param_key(Interpolation::Bounce, 0.5f, 0.6f)};
    for (const Keyframe& k : kinds) {
        for (int i = 0; i <= 997; ++i) {
            const f32 t = i / 997.0f;            // frações quaisquer, não só frames inteiros
            const f32 a = keyframe_ease(k, t), b = keyframe_ease(k, t);
            AUREA_CHECK(std::memcmp(&a, &b, sizeof a) == 0);
        }
        // Fora de 0..1 (tempo antes/depois do trecho) as pontas valem.
        AUREA_CHECK_EQ(keyframe_ease(k, -0.5f), 0.0f);
        AUREA_CHECK_EQ(keyframe_ease(k, 1.5f), 1.0f);
    }
    // Na track: o valor sai da faixa [início, fim] entre as marcas e as pontas são exatas.
    Track t; t.set(FrameIndex{0}, 100.f); t.set(FrameIndex{60}, 200.f);
    t.set_interpolation(FrameIndex{0}, Interpolation::Overshoot, 0.6f, 0.0f, 1.0f, kEaseParamMarker);
    AUREA_CHECK(t.keys[0].interp == Interpolation::Overshoot && t.keys[0].by2 == kEaseParamMarker && t.keys[0].bx1 == 0.6f);
    AUREA_CHECK_EQ(t.sample(FrameIndex{0}), 100.f);
    AUREA_CHECK_EQ(t.sample(FrameIndex{60}), 200.f);
    f32 top = 0.f;
    for (int f = 0; f <= 60; ++f) top = std::max(top, t.sample(FrameIndex{f}));
    AUREA_CHECK(top > 200.f);
    t.set_interpolation(FrameIndex{0}, Interpolation::Elastic, 0.5f, 0.1f, 1.0f, kEaseParamMarker);
    AUREA_CHECK(t.keys[0].bx1 == 0.5f && t.keys[0].by1 == 0.1f);   // os parâmetros são gravados
}

AUREA_TEST(Easing, EngineSamplesAreTheEvaluatedCurve) {
    // O gráfico da UI desenha `sample_keyframe_ease`: igual ao avaliador, ponto a ponto.
    const Keyframe kinds[] = {param_key(Interpolation::Overshoot, 0.4f, 0), param_key(Interpolation::Elastic, 0.3f, 0.2f),
                              param_key(Interpolation::Bounce, 0.375f, 0.5f)};
    f32 samples[257];
    for (const Keyframe& k : kinds) {
        AUREA_CHECK_EQ(sample_keyframe_ease(k, samples, 257), 257u);
        for (u32 i = 0; i < 257; ++i) AUREA_CHECK_EQ(samples[i], keyframe_ease(k, static_cast<f32>(i) / 256.0f));
    }
    Keyframe hold; hold.interp = Interpolation::Hold;
    AUREA_CHECK_EQ(sample_keyframe_ease(hold, samples, 9), 9u);
    AUREA_CHECK_EQ(samples[7], 0.0f);
    AUREA_CHECK_EQ(samples[8], 1.0f);                 // chega no próximo keyframe
    AUREA_CHECK_EQ(sample_keyframe_ease(hold, samples, 1), 0u);
    AUREA_CHECK_EQ(sample_keyframe_ease(hold, nullptr, 9), 0u);
}

AUREA_TEST(Easing, MirrorIsExactForParametricCurves) {
    for (Interpolation kind : {Interpolation::Overshoot, Interpolation::Elastic}) {
        Keyframe plain; plain.interp = kind;          // sem parâmetros gravados
        Keyframe mirrored = plain;
        AUREA_CHECK(mirror_parametric_ease(mirrored));
        for (int i = 0; i <= 400; ++i) {
            const f32 t = i / 400.0f;
            AUREA_CHECK_NEAR(keyframe_ease(mirrored, t), 1.0f - keyframe_ease(plain, 1.0f - t), 1e-5);
        }
        AUREA_CHECK(mirror_parametric_ease(mirrored));  // espelhar duas vezes volta
        for (int i = 0; i <= 400; ++i) AUREA_CHECK_NEAR(keyframe_ease(mirrored, i / 400.0f), keyframe_ease(plain, i / 400.0f), 1e-5);
    }
    Keyframe oldBounce; oldBounce.interp = Interpolation::Bounce;
    AUREA_CHECK(!mirror_parametric_ease(oldBounce));
    Keyframe bez; bez.interp = Interpolation::Bezier;
    AUREA_CHECK(!mirror_parametric_ease(bez));
}
