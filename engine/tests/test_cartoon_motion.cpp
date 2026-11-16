#include "TestFramework.hpp"
#include "aurea/effects/CartoonMotion.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/TwitchMotion.hpp"
using namespace aurea;
namespace {
struct CartoonFixture {
    EffectRegistry registry;
    Layer layer;
    EffectEval eval;
    std::vector<ParamValue> values;
    CartoonFixture() {
        register_builtin_effects(registry);
        const auto id = registry.find_key("aurea.motion.cartoon_moblur");
        const auto *params = registry.params(id);
        AUREA_CHECK(params != nullptr);
        if (params)
            for (u32 i = 0; i < params->count(); ++i)
                values.push_back(params->at(i).defaultValue);
        eval.effect = registry.find(id);
        eval.values = values.data();
        eval.count = static_cast<u32>(values.size());
        eval.layer = &layer;
        eval.fractionalTime = 10.5;
    }
};
} // namespace
AUREA_TEST(CartoonMotion, FixedSamplesExcludePresentAndPreserveSubframes) {
    CartoonFixture f;
    f.values[cartoon_motion::MotionSource] = ParamValue::scalar(2);
    f.values[cartoon_motion::FixedSamples] = ParamValue::scalar(4);
    f.values[cartoon_motion::Shutter] = ParamValue::scalar(360);
    const auto samples = cartoon_motion::samples(f.eval);
    AUREA_CHECK_EQ(samples.size(), 4u);
    for (u32 i = 0; i < samples.size(); ++i)
        AUREA_CHECK_NEAR(samples[i].time, 9.5 + i * .25, 1e-12);
}
AUREA_TEST(CartoonMotion, ExponentialEasingAndLInfinityMotionMatchContract) {
    AUREA_CHECK_NEAR(cartoon_motion::ease(.25f, 0), std::exp2(-7.5f), 1e-7f);
    AUREA_CHECK_NEAR(cartoon_motion::ease(.75f, 1), 1.f - std::exp2(-7.5f), 1e-7f);
    AUREA_CHECK_NEAR(cartoon_motion::ease(.5f, 2), .5f, 1e-7f);
    EffectMotionState a, b;
    b.position = {3, 4, 0};
    AUREA_CHECK_NEAR(cartoon_motion::distance(a, b, 1, 1), 4, 0);
    b.scale = {1.2f, 1, 1};
    AUREA_CHECK_NEAR(cartoon_motion::distance(a, b, 2, 1), 40, 1e-4f);
}
AUREA_TEST(CartoonMotion, SourceOffsetIsAppliedOnceToTemporalRequests) {
    CartoonFixture f;
    f.layer.start=FrameIndex{20};
    f.layer.offset=FrameIndex{5};
    f.eval.fractionalTime=15.5;
    f.values[cartoon_motion::MotionSource]=ParamValue::scalar(2);
    f.values[cartoon_motion::FixedSamples]=ParamValue::scalar(4);
    f.values[cartoon_motion::Shutter]=ParamValue::scalar(360);
    const auto samples=cartoon_motion::samples(f.eval);
    AUREA_CHECK_EQ(samples.size(),4u);
    for (u32 i=0;i<samples.size();++i)
        AUREA_CHECK_NEAR(samples[i].time,29.5+i*.25,1e-12);
}
AUREA_TEST(CartoonMotion, NativeDefaultsAndLegacyTwitchRemainIndependent) {
    CartoonFixture f;
    AUREA_CHECK_EQ(f.values.size(), 21u);
    AUREA_CHECK_NEAR(f.values[cartoon_motion::Shutter].as_float(), 360, 0);
    AUREA_CHECK_NEAR(f.values[cartoon_motion::TimeThreshold].as_float(), 20, 0);
    AUREA_CHECK_EQ(f.registry.params(f.registry.find_key(effect_keys::kTwitch))->count(), 7u);
}

AUREA_TEST(TwitchReference, RandomJumpMatchesTheReferenceSequence) {
    u32 state = 17;
    for (u32 cell = 0; cell < 100; ++cell) {
        state = 17;
        for (u32 i = 0; i < cell + 6; ++i)
            state = state * 214013u + 2531011u;
        AUREA_CHECK_NEAR(twitch_motion::random(17, cell), static_cast<f64>((state >> 16) & 32767) / 32767.,
                         1e-15);
    }
}
AUREA_TEST(TwitchReference, PulsesAreBoundedAndStateIndependent) {
    for (f64 time : {0., .5, 1., 10.25, 100000.5}) {
        const f64 value = twitch_motion::pulse(time, 6, 17, 1, 1);
        AUREA_CHECK(value >= 0 && value <= 1);
        AUREA_CHECK_NEAR(value, twitch_motion::pulse(time, 6, 17, 1, 1), 0);
    }
    EffectRegistry registry;
    register_builtin_effects(registry);
    AUREA_CHECK_EQ(registry.params(registry.find_key("aurea.motion.twitch_reference"))->count(),
                   static_cast<u32>(twitch_motion::Count));
    AUREA_CHECK_EQ(registry.params(registry.find_key(effect_keys::kTwitch))->count(), 7u);
}
