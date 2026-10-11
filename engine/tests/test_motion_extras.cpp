#include "TestFramework.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/CanvasFit.hpp"
#include "aurea/media/PreviewBuffer.hpp"
#include "aurea/timeline/Layer.hpp"
#include <limits>

using namespace aurea;
namespace {
struct MotionFixture {
    EffectRegistry registry;
    std::vector<ParamValue> values;
    LayerPlacement placement;
    EffectEval eval;
    MotionFixture(const char* key) {
        register_builtin_effects(registry);
        const auto id=registry.find_key(key);
        eval.effect=registry.find(id);
        AUREA_CHECK(eval.effect!=nullptr);
        const auto* p=registry.params(id);
        if(p)for(u32 i=0;i<p->count();++i)values.push_back(p->at(i).defaultValue);
        placement.layerWidth=100;placement.layerHeight=80;
        eval.values=values.data();eval.count=u32(values.size());eval.placement=&placement;
    }
    Mat4 matrix(i64 frame,double fps,float& opacity) {
        eval.localTime=FrameIndex{frame};eval.framesPerSecond=fps;
        Mat4 m;AUREA_CHECK(eval.effect->fold_into_composite(eval,m,opacity));return m;
    }
};
}
AUREA_TEST(MotionExtras, CatalogueContainsEveryRequestedEffect) {
    EffectRegistry r;register_builtin_effects(r);
    for(const char* key:{"aurea.motion.blink","aurea.key.chroma_basic","aurea.key.chroma","aurea.motion.flicker",
        "aurea.key.color_luma","aurea.key.matte_choker","aurea.key.luma","aurea.key.solid_matte","aurea.transform.offset",
        "aurea.motion.pulse_size","aurea.motion.random_displacement","aurea.motion.random_jitter","aurea.transform.raster",
        "aurea.transform.stretch_axis","aurea.motion.swing_range","aurea.transform.scale_assist","aurea.motion.spin",
        "aurea.repeat.grid","aurea.repeat.linear","aurea.repeat.radial","aurea.repeat.basic","aurea.repeat.path",
        "aurea.repeat.scatter","aurea.distort.squeeze","aurea.distort.fisheye"}) {
        const auto id=r.find_key(key);AUREA_CHECK_MSG(r.find(id)!=nullptr,key);
        AUREA_CHECK(r.params(id)&&r.params(id)->count()>0);
    }
}
AUREA_TEST(MotionExtras, BlinkDutyAndSpinUseSeconds) {
    MotionFixture blink("aurea.motion.blink");float a,b;
    blink.matrix(0,30,a);AUREA_CHECK_NEAR(a,1,0);
    blink.matrix(10,30,a);AUREA_CHECK_NEAR(a,0,0);
    blink.matrix(20,60,b);AUREA_CHECK_NEAR(a,b,0);
    blink.values[2]=ParamValue::scalar(0);blink.matrix(0,30,a);AUREA_CHECK_NEAR(a,0,0);
    blink.values[2]=ParamValue::scalar(100);blink.matrix(10,30,a);AUREA_CHECK_NEAR(a,1,0);
    MotionFixture spin("aurea.motion.spin");
    const auto m=spin.matrix(30,30,a),n=spin.matrix(60,60,b);
    AUREA_CHECK_NEAR(m.col[0].x,0,1e-5);AUREA_CHECK_NEAR(m.col[0].y,1,1e-5);
    for(int i=0;i<4;++i)AUREA_CHECK_EQ(m.col[i],n.col[i]);
    const auto center=m*Vec4{50,40,0,1};AUREA_CHECK_NEAR(center.x,50,1e-5);AUREA_CHECK_NEAR(center.y,40,1e-5);
}
AUREA_TEST(MotionExtras, RandomMotionIsStableAcrossSeekingAndFrameRates) {
    for(const char* key:{"aurea.motion.random_displacement","aurea.motion.random_jitter","aurea.motion.flicker"}) {
        MotionFixture f(key);float a,b,c;
        const auto at=f.matrix(21,30,a);f.matrix(210,30,b);const auto again=f.matrix(42,60,c);
        for(int i=0;i<4;++i)AUREA_CHECK_EQ(at.col[i],again.col[i]);AUREA_CHECK_EQ(a,c);
        const u32 seed=3;f.values[seed]=ParamValue::scalar(777);
        const auto other=f.matrix(21,30,b);
        AUREA_CHECK(a!=b||at.col[3]!=other.col[3]);
    }
}
AUREA_TEST(MotionExtras, FitAndFillCenterRotatedMirroredMedia) {
    std::array<float,11> a{1920,1080,1080,1920,1,1,540,960,0,0,0};
    auto fit=canvas_fit(a,false),fill=canvas_fit(a,true);
    AUREA_CHECK_NEAR(fit[0],.5625,1e-6);AUREA_CHECK_NEAR(fill[0],1920./1080,1e-6);
    AUREA_CHECK_NEAR(fill[2],960,1e-5);AUREA_CHECK_NEAR(fill[3],540,1e-5);
    a[8]=90;fit=canvas_fit(a,false);fill=canvas_fit(a,true);
    AUREA_CHECK_NEAR(fit[0],1,1e-6);AUREA_CHECK_NEAR(fill[0],1,1e-6);
    a[4]=-1;a[6]=0;a[7]=0;fit=canvas_fit(a,false);
    AUREA_CHECK_NEAR(fit[0],-1,1e-6);AUREA_CHECK_NEAR(fit[2],1920,1e-3);AUREA_CHECK_NEAR(fit[3],1080,1e-3);
    a[2]=0;AUREA_CHECK_EQ(canvas_fit(a,true)[4],0);
    a[2]=std::numeric_limits<float>::quiet_NaN();AUREA_CHECK_EQ(canvas_fit(a,false)[4],0);
}
AUREA_TEST(MotionExtras, BufferRespectsDecoderAndMemoryCapacity) {
    AUREA_CHECK_EQ(preview_buffer_frames(33333,1,20),6u);
    AUREA_CHECK_EQ(preview_buffer_frames(33333,1,2),2u);
    AUREA_CHECK_EQ(preview_buffer_frames(4167,16,20),8u);
    AUREA_CHECK_EQ(preview_buffer_frames(33333,-1,20),6u);
    AUREA_CHECK_EQ(preview_buffer_frames(33333,1,0),0u);
    AUREA_CHECK_EQ(preview_buffer_frames(0,1,20),0u);
}

AUREA_TEST(MotionExtras, MoveAlongPathUsesClipClockAndExplicitGuide) {
    struct PathResources : EffectResources {
        f32 phase = -1; u64 guide = 0;
        TextureHandle curve_lut(const CurveData&) noexcept override { return {}; }
        std::vector<Vec4> repeat_path(const Layer*, u32 count, f32 progress, u64 id) noexcept override {
            AUREA_CHECK_EQ(count, 1u); phase = progress; guide = id;
            return {{100 * progress, 40, kDeg2Rad * 90, 0}};
        }
    } resources;
    MotionFixture f("aurea.move.path"); Layer layer; layer.offset = FrameIndex{45};
    f.eval.layer = &layer; f.eval.resources = &resources;
    f.values[0].ref = 123;
    f.eval.localTime = FrameIndex{75}; f.eval.framesPerSecond = 30;
    f.eval.effect->resolve_resources(f.eval);
    AUREA_CHECK_NEAR(resources.phase, .5f, 1e-6); AUREA_CHECK_EQ(resources.guide, 123u);
    f32 opacity = 0; const auto m = f.matrix(75, 30, opacity);
    const auto center = m * Vec4{50, 40, 0, 1};
    AUREA_CHECK_NEAR(center.x, 50, 1e-5); AUREA_CHECK_NEAR(center.y, 40, 1e-5);
    AUREA_CHECK_NEAR(m.col[0].y, 1, 1e-5);
    f.eval.localTime = FrameIndex{165}; f.eval.effect->resolve_resources(f.eval);
    AUREA_CHECK_NEAR(resources.phase, 1, 1e-6);
    f.values[5] = ParamValue::scalar(1); f.eval.effect->resolve_resources(f.eval);
    AUREA_CHECK_NEAR(resources.phase, 0, 1e-6);
    f.values[3] = ParamValue::scalar(0); f.values[1] = ParamValue::scalar(25);
    f.eval.effect->resolve_resources(f.eval); AUREA_CHECK_NEAR(resources.phase, .25f, 1e-6);
    f.eval.pathSamples.reset(); const auto identity = f.matrix(75, 30, opacity);
    AUREA_CHECK_EQ(identity.col[3], Mat4::identity().col[3]);
}
