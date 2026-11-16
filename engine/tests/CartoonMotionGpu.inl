#include "aurea/effects/TwitchMotion.hpp"

namespace {
void cartoon_fixed(EffectInstance &effect) {
    using namespace cartoon_motion;
    effect.params[MotionSource].constant = ParamValue::scalar(2);
    effect.params[Shutter].constant = ParamValue::scalar(1440);
    effect.params[FixedSamples].constant = ParamValue::scalar(4);
    effect.params[Fill].constant = ParamValue::scalar(2);
    effect.params[Original].constant = ParamValue::boolean(false);
}
} // namespace
AUREA_TEST(Gpu, CartoonMoblurKeepsTheCurrentHostTransform) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 72, 30);
    const LayerId id = scene.comp->add_layer(LayerKind::Shape, "cartoon source");
    Layer *layer = scene.comp->layer(id);
    layer->shape.bounds = {0, 0, 6, 6};
    layer->shape.fillColor = {1, 1, 1, 1};
    layer->transform.position = {0, 30, 0};
    Track &x = layer->tracks.get_or_create(TrackProperty::PositionX);
    x.set(FrameIndex{0}, 0, Interpolation::Linear);
    x.set(FrameIndex{10}, 100, Interpolation::Linear);
    cartoon_fixed(scene.add_effect(id, "aurea.motion.cartoon_moblur"));
    FrameSnapshot snapshot;
    RenderSettings settings;settings.finalQuality=true;
    gpu().renderer.prepare(*scene.comp,scene.project,FrameIndex{10},&scene.media,&Scene::lookup,&scene,settings,1,0,DecodeMode::Still,1,snapshot);
    AUREA_CHECK_EQ(snapshot.nested.size(),4u);
    AUREA_CHECK(!snapshot.plans.empty() && !snapshot.plans.front().evals.empty());
    for (const auto& sample:snapshot.nested) AUREA_CHECK(sample && sample->temporalSourceLayer && !sample->layers.empty());
    const FloatImage image = scene.render(FrameIndex{10}, 1, true);
    (void)write_png("build/reference/effects-20261115/cartoon-twitch/aurea-direct-debug.png",image.encoded());
    for (u32 px : {62u, 72u, 82u, 92u})
        AUREA_CHECK_MSG(image.v(px, 32).x < .01f, "host motion was applied before the temporal effect");
    AUREA_CHECK(image.v(102, 32).x > .9f);
}

AUREA_TEST(Gpu, CartoonMoblurPreservesHistoricalPrefixTransforms) {
    AUREA_REQUIRE_GPU();
    Scene scene(160,72,30);
    const LayerId id=scene.comp->add_layer(LayerKind::Shape,"prefix transform source");
    Layer* layer=scene.comp->layer(id);
    layer->shape.bounds={0,0,6,6};
    layer->shape.fillColor={1,1,1,1};
    layer->transform.position={0,30,0};
    Track& host=layer->tracks.get_or_create(TrackProperty::PositionX);
    host.set(FrameIndex{0},0,Interpolation::Linear);
    host.set(FrameIndex{10},100,Interpolation::Linear);
    const u32 transform=scene.add_effect(id,effect_keys::kTransform).id;
    Track& prefix=layer->tracks.get_or_create(TrackProperty::EffectParam,transform,param_track_key(1,0));
    prefix.set(FrameIndex{6},.5f,Interpolation::Linear);
    prefix.set(FrameIndex{9},5.5f,Interpolation::Linear);
    cartoon_fixed(scene.add_effect(id,"aurea.motion.cartoon_moblur"));
    const FloatImage image=scene.render(FrameIndex{10},1,true);
    for (u32 px:{102u,112u,122u,132u})
        AUREA_CHECK_MSG(image.v(px,32).x>.9f,"historical prefix transform missing");
    for (u32 px:{62u,72u,82u,92u,142u})
        AUREA_CHECK(image.v(px,32).x<.01f);
}

AUREA_TEST(Gpu, CartoonMoblurSharesDecoderForSubframeSamples) {
    AUREA_REQUIRE_GPU();
    Scene scene(64,36,30);
    SyntheticConfig config;
    config.width=64;config.height=36;config.fps=30;
    config.pattern=SyntheticPattern::FrameGray;
    const LayerId id=scene.video(config,32,18);
    // The media still has frame 9 even though the layer starts at frame 10.
    scene.comp->layer(id)->start=FrameIndex{10};
    scene.comp->layer(id)->offset=FrameIndex{10};
    auto& effect=scene.add_effect(id,"aurea.motion.cartoon_moblur");
    cartoon_fixed(effect);
    effect.params[cartoon_motion::FixedSamples].constant=ParamValue::scalar(50);
    effect.params[cartoon_motion::Shutter].constant=ParamValue::scalar(360);
    const FloatImage image=scene.render(FrameIndex{10},1,true);
    const f32 expected=(static_cast<f32>(frame_gray_code(9))-16.f)/219.f;
    AUREA_CHECK_NEAR(srgb_encode(image.v(32,18).x),expected,.002f);
    AUREA_CHECK_MSG(scene.factory->opened.load()<=2,"subframes opened duplicate source decoders");
}

AUREA_TEST(Gpu, CartoonMoblurExcludesHistoricalHostOpacityAndPreservesPrefixOpacity) {
    AUREA_REQUIRE_GPU();
    Scene scene(64,36,30);
    const LayerId id=scene.solid(12,12,Vec4{1,1,1,1},32,18);
    Layer* layer=scene.comp->layer(id);
    Track& opacity=layer->tracks.get_or_create(TrackProperty::Opacity);
    opacity.set(FrameIndex{9},0,Interpolation::Linear);
    opacity.set(FrameIndex{10},.5f,Interpolation::Linear);
    layer->transitionIn=1;
    layer->transitionInFrames=20;
    scene.add_effect(id,effect_keys::kTransform).params[4].constant=ParamValue::scalar(50);
    auto& effect=scene.add_effect(id,"aurea.motion.cartoon_moblur");
    cartoon_fixed(effect);
    effect.params[cartoon_motion::FixedSamples].constant=ParamValue::scalar(1);
    effect.params[cartoon_motion::Shutter].constant=ParamValue::scalar(360);
    const FloatImage image=scene.render(FrameIndex{10},1,true);
    // Prefix opacity .5, present host opacity .5, present transition opacity .5.
    AUREA_CHECK_NEAR(image.v(32,18).x,.125f,.002f);
}
AUREA_TEST(Gpu, CartoonMoblurSamplesAnimatedPrecompContent) {
    AUREA_REQUIRE_GPU();
    Scene scene(kPtW, kPtH, 30);
    const LayerId id = precomp_temporal_square(scene, true, false);
    cartoon_fixed(scene.add_effect(id, "aurea.motion.cartoon_moblur"));
    const FloatImage trail = scene.render(FrameIndex{30}, 1, true);
    (void)write_png("build/reference/effects-20261115/cartoon-twitch/aurea-nested-debug.png",trail.encoded());
    AUREA_CHECK_MSG(trail.v(58, 36).x > .9f, "past child content missing");
    AUREA_CHECK_MSG(trail.v(75, 36).x < .1f, "present child content leaked into past-only trail");
}

AUREA_TEST(Gpu, CartoonMoblurAdjustmentReevaluatesItsAnimatedPrefix) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 72, 30);
    scene.comp->set_background(Color{0, 0, 0, 0});
    scene.comp->set_transparent_background(true);
    const LayerId id = scene.comp->add_layer(LayerKind::Shape, "moving source");
    Layer *source = scene.comp->layer(id);
    source->shape.bounds = {0, 0, 6, 6};
    source->shape.fillColor = {1, 1, 1, 1};
    source->transform.position = {0, 30, 0};
    Track &x = source->tracks.get_or_create(TrackProperty::PositionX);
    x.set(FrameIndex{0}, 0, Interpolation::Linear);
    x.set(FrameIndex{10}, 100, Interpolation::Linear);
    const LayerId adjustment = scene.solid(128, 72, Vec4{1, 1, 1, 1}, 64, 36);
    Layer *layer = scene.comp->layer(adjustment);
    layer->adjustment = true;
    EffectInstance &exposure = scene.add_effect(adjustment, effect_keys::kExposure);
    const u32 exposureId = exposure.id;
    Track &stops = layer->tracks.get_or_create(TrackProperty::EffectParam, exposureId, param_track_key(0, 0));
    stops.set(FrameIndex{6}, -4, Interpolation::Linear);
    stops.set(FrameIndex{9}, -1, Interpolation::Linear);
    cartoon_fixed(scene.add_effect(adjustment, "aurea.motion.cartoon_moblur"));
    const FloatImage image = scene.render(FrameIndex{10}, 1, true);
    (void)write_png("build/reference/effects-20261115/cartoon-twitch/aurea-adjustment-debug.png",image.encoded());
    AUREA_CHECK_NEAR(image.v(62, 32).x, .0625f, .01f);
    AUREA_CHECK_NEAR(image.v(72, 32).x, .125f, .01f);
    AUREA_CHECK_NEAR(image.v(82, 32).x, .25f, .01f);
    AUREA_CHECK_NEAR(image.v(92, 32).x, .5f, .01f);
    AUREA_CHECK(image.v(102, 32).x < .01f);
}

AUREA_TEST(Gpu, CartoonMoblurZeroShutterPreservesSource) {
    AUREA_REQUIRE_GPU();
    Scene scene(128,72,30);
    const LayerId id=scene.solid(12,12,Vec4{1,1,1,1},64,36);
    const FloatImage plain=scene.render();
    auto& effect=scene.add_effect(id,"aurea.motion.cartoon_moblur");
    effect.params[cartoon_motion::Shutter].constant=ParamValue::scalar(0);
    const FloatImage cartoon=scene.render();
    (void)write_png("build/reference/effects-20261115/cartoon-twitch/aurea-zero-debug.png",cartoon.encoded());
    AUREA_CHECK(max_diff(plain,cartoon)<.002f);
}

namespace {
void twitch_test_pulse(EffectInstance& effect) {
    using namespace twitch_motion;
    effect.params[Speed].constant=ParamValue::scalar(30);
    effect.params[RandomMinimum].constant=ParamValue::scalar(100);
    effect.params[EaseIn].constant=ParamValue::scalar(100);
    effect.params[EaseOut].constant=ParamValue::scalar(100);
}

bool twitch_finite_image(const FloatImage& image) {
    return std::all_of(image.px.begin(),image.px.end(),[](f32 value) { return std::isfinite(value); });
}
} // namespace

// AE oracle cases in runtime/render-cases.json: a 16 px white solid moving
// from x=-1400 (t=0) to x=1600 (t=1 s), rendered at t=0.5 s.
AUREA_TEST(Gpu, CartoonMoblurOracleFramesWhenRequested) {
    const char *outputPath = std::getenv("AUREA_CARTOON_ORACLE_OUT");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    AUREA_CHECK(!error);
    using namespace cartoon_motion;
    struct Case { const char *name; u32 source, samples, fill; };
    for (const Case sample : {Case{"cartoon-translation-solid", 1, 50, 0}, Case{"cartoon-translation-plain", 1, 50, 2},
                              Case{"cartoon-fixed4", 2, 4, 0}}) {
        Scene scene(256, 144, 30);
        scene.comp->set_transparent_background(true);
        const LayerId id = scene.solid(16, 16, {1, 1, 1, 1}, -1400, 72);
        Track &x = scene.comp->layer(id)->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0}, -1400, Interpolation::Linear);
        x.set(FrameIndex{30}, 1600, Interpolation::Linear);
        auto &effect = scene.add_effect(id, "aurea.motion.cartoon_moblur");
        effect.params[MotionSource].constant = ParamValue::scalar(static_cast<f32>(sample.source));
        effect.params[Shutter].constant = ParamValue::scalar(360);
        effect.params[FixedSamples].constant = ParamValue::scalar(static_cast<f32>(sample.samples));
        effect.params[Fill].constant = ParamValue::scalar(static_cast<f32>(sample.fill));
        effect.params[SolidColor].constant = ParamValue::color(1, 0, 0, 1);
        effect.params[Original].constant = ParamValue::boolean(false);
        const FloatImage frame = scene.render(FrameIndex{15}, 1, true);
        std::ofstream stream(std::filesystem::path(outputPath) / (std::string(sample.name) + ".rgba32f"), std::ios::binary);
        stream.write(reinterpret_cast<const char *>(frame.px.data()),
                     static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
        AUREA_CHECK(stream.good());
    }
}

AUREA_TEST(Gpu, TwitchReferenceZeroMasterAmountPreservesSource) {
    AUREA_REQUIRE_GPU();
    using namespace twitch_motion;
    Scene scene(64,36,30);
    const LayerId id=scene.image(reference_image(48,24),32,18);
    const FloatImage baseline=scene.render(FrameIndex{30},1,true);
    auto& effect=scene.add_effect(id,"aurea.motion.twitch_reference");
    twitch_test_pulse(effect);
    for (u32 parameter:{BlurEnabled,ColorEnabled,LightEnabled,ScaleEnabled,SlideEnabled,TimeEnabled})
        effect.params[parameter].constant=ParamValue::boolean(true);
    effect.params[Amount].constant=ParamValue::scalar(0);
    const FloatImage result=scene.render(FrameIndex{30},1,true);
    AUREA_CHECK(twitch_finite_image(result));
    AUREA_CHECK_MSG(max_diff(baseline,result)<.002f,"zero Twitch master amount changed the source");
}

AUREA_TEST(Gpu, TwitchReferenceIndividualSpatialOperatorsRenderAndRemainDeterministic) {
    AUREA_REQUIRE_GPU();
    using namespace twitch_motion;
    struct OperatorCase { u32 enabled; bool lens; const char* name; };
    const OperatorCase cases[]{
        {BlurEnabled,false,"Gaussian Blur"}, {BlurEnabled,true,"Lens Blur"},
        {ColorEnabled,false,"Color"}, {LightEnabled,false,"Light"},
        {ScaleEnabled,false,"Scale"}, {SlideEnabled,false,"Slide"}
    };
    for (const auto& operation:cases) {
        Scene scene(64,36,30);
        const LayerId id=scene.image(reference_image(64,36),32,18);
        const FloatImage baseline=scene.render(FrameIndex{30},1,true);
        auto& effect=scene.add_effect(id,"aurea.motion.twitch_reference");
        twitch_test_pulse(effect);
        effect.params[operation.enabled].constant=ParamValue::boolean(true);
        effect.params[LensBlur].constant=ParamValue::boolean(operation.lens);
        effect.params[BlurAmount].constant=ParamValue::scalar(12);
        effect.params[ColorAmount].constant=ParamValue::scalar(100);
        effect.params[Colorize].constant=ParamValue::color(1,0,0,1);
        effect.params[LightAmount].constant=ParamValue::scalar(40);
        effect.params[ScaleAmount].constant=ParamValue::scalar(70);
        effect.params[SlideAmount].constant=ParamValue::scalar(20);
        effect.params[SlideSpread].constant=ParamValue::scalar(0);
        effect.params[SlideMotionBlur].constant=ParamValue::scalar(0);
        // With period one, long ease and minimum random amplitude at 100%,
        // frame 30 has a positive pulse for every isolated operator.
        AUREA_CHECK(pulse(30.5,1,0,100,100)>.9);
        const FloatImage first=scene.render(FrameIndex{30},1,true);
        (void)scene.render(FrameIndex{31},1,true);
        const FloatImage repeated=scene.render(FrameIndex{30},1,true);
        const f32 change=max_diff(baseline,first);
        const f32 difference=max_diff(first,repeated);
        std::printf("    Twitch %s: change %.6f, repeat %.6f\n",operation.name,change,difference);
        AUREA_CHECK(twitch_finite_image(first));
        AUREA_CHECK(twitch_finite_image(repeated));
        AUREA_CHECK_MSG(change>.03f,operation.name);
        AUREA_CHECK_MSG(difference<.002f,"Twitch output changed after revisiting the same frame");
    }
}

AUREA_TEST(Gpu, TwitchReferenceTimeSamplesPrecompBeforeCurrentHostTransformAndOpacity) {
    AUREA_REQUIRE_GPU();
    using namespace twitch_motion;
    Scene scene(kPtW,kPtH,30);
    const LayerId id=precomp_temporal_square(scene,true,false);
    Layer* layer=scene.comp->layer(id);
    Track& hostX=layer->tracks.get_or_create(TrackProperty::PositionX);
    hostX.set(FrameIndex{0},64,Interpolation::Linear);
    hostX.set(FrameIndex{30},94,Interpolation::Linear);
    Track& hostOpacity=layer->tracks.get_or_create(TrackProperty::Opacity);
    hostOpacity.set(FrameIndex{0},0,Interpolation::Linear);
    hostOpacity.set(FrameIndex{30},.5f,Interpolation::Linear);
    auto& prefix=scene.add_effect(id,effect_keys::kTransform);
    prefix.params[1].constant=ParamValue::vec2(.5f+8.f/kPtW,.5f);
    prefix.params[4].constant=ParamValue::scalar(50);
    const FloatImage current=scene.render(FrameIndex{30},1,true);
    auto& effect=scene.add_effect(id,"aurea.motion.twitch_reference");
    twitch_test_pulse(effect);
    effect.params[TimeEnabled].constant=ParamValue::boolean(true);
    effect.params[TimeDirection].constant=ParamValue::scalar(2);
    effect.params[TimeAmount].constant=ParamValue::scalar(50);
    std::vector<ParamValue> values;
    for (const auto& parameter:effect.params) values.push_back(parameter.constant);
    EffectEval eval;
    eval.values=values.data();
    eval.count=static_cast<u32>(values.size());
    eval.framesPerSecond=30;
    eval.fractionalTime=30;
    const i64 offset=static_cast<i64>(source_offset(eval));
    AUREA_CHECK(offset<-6 && 30+offset>=0);
    const u32 historicalCenter=static_cast<u32>(10+2*(30+offset)+30+8);
    const FloatImage result=scene.render(FrameIndex{30},1,true);
    (void)scene.render(FrameIndex{31},1,true);
    const FloatImage repeated=scene.render(FrameIndex{30},1,true);
    AUREA_CHECK(twitch_finite_image(result));
    AUREA_CHECK_MSG(max_diff(result,current)>.1f,"Time operator reused present precomp content");
    AUREA_CHECK_NEAR(result.v(historicalCenter,36).x,.25f,.01f);
    AUREA_CHECK_MSG(result.v(108,36).x<.01f,"present precomp content leaked into the past checkout");
    AUREA_CHECK_MSG(max_diff(result,repeated)<.002f,"Time operator changed after revisiting the same frame");
}
