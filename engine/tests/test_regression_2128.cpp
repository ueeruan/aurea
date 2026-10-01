#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "aurea/Engine.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/FontManager.hpp"
#include <cmath>
#include <limits>

using namespace aurea;
using namespace aurea::text;
namespace {
struct Rig2128 {
    test::SyntheticFactory factory;
    Engine e;
    Rig2128() : factory([] { test::SyntheticConfig c; c.frameCount=300; return c; }()) {
        EngineConfig c; c.workerCount=2; c.disableAutosave=true; c.mediaFactory=&factory;
        AUREA_CHECK(e.initialize(c).ok()); AUREA_CHECK(e.new_project(320,180,30,nullptr).ok());
    }
    ~Rig2128(){e.shutdown();}
    Composition* comp(){return e.project()->timeline().composition(e.project()->timeline().current());}
    LayerId video(){VideoImport v;v.sourcePath="synthetic";auto r=e.import_video(v);AUREA_CHECK(r.ok());return r.ok()?LayerId::unpack(*r):LayerId{};}
};
EffectInstance animator2128(Layer& l) {
    ParameterRegistry p; declare_animator_effect_params(p);
    EffectInstance fx; fx.type=effect_type_id(kAnimatorEffect);fx.id=l.alloc_effect_id();initialize_instance(fx,p);return fx;
}
std::vector<GlyphAnim> glyphs2128(const Layer& l, f64 time=0) {
    std::vector<GlyphUnits> units;
    for(u32 i=0;i<8;++i)units.push_back({i,i/2,i/4});
    std::vector<GlyphAnim> out(8);evaluate_animator_effects(l,time,30,units,8,4,2,out);return out;
}
}

AUREA_TEST(Regression2128, RemapGestureKeepsItsKeyWhenQueuedPlayheadChanges) {
    Rig2128 r;const auto id=r.video();auto* l=r.comp()->layer(id);
    l->start=FrameIndex{50};l->end=FrameIndex{250};l->offset=FrameIndex{20};
    for(i64 source : {140LL, 20LL, 190LL, 0LL}) {
        Command seek;seek.type=CommandType::PlaybackSeek;seek.seek.time=tick_at(FrameIndex{120},30);
        AUREA_CHECK_EQ(r.e.submit_commands(&seek,1,nullptr,0),1u);
        AUREA_CHECK(r.e.set_time_remap_value(id.pack(),50,static_cast<f32>(source)));
        l=r.comp()->layer(id);AUREA_CHECK_NEAR(l->source_frame(FrameIndex{80}),source,.001);
        AUREA_CHECK(l->timeRemap.find_exact(FrameIndex{90})==kInvalidIndex);
    }
    AUREA_CHECK(!r.e.set_time_remap_value(id.pack(),50,std::numeric_limits<f32>::quiet_NaN()));
    AUREA_CHECK(!r.e.set_time_remap_value(id.pack(),19,100));
    AUREA_CHECK(r.e.set_time_remap_value(id.pack(),70,100));
    AUREA_CHECK(r.e.set_time_remap_value(id.pack(),100,0));
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{115}),50,.001);
    AUREA_CHECK(r.e.save_project("build/regression-2128-remap.aurea").ok());
    AUREA_CHECK(r.e.load_project("build/regression-2128-remap.aurea").ok());
    r.comp()->layers().for_each([&](LayerId,const Layer& x){AUREA_CHECK_NEAR(x.source_frame(FrameIndex{115}),50,.001);});
}

AUREA_TEST(Regression2128, SplitGetsSeparateRowsAndBothLeftTrimsKeepTheirSource) {
    Rig2128 r; const auto id=r.video();
    AUREA_CHECK(r.e.set_time_remap_value(id.pack(),0,0));
    AUREA_CHECK(r.e.set_time_remap_value(id.pack(),150,299));
    AUREA_CHECK(r.e.set_time_remap_value(id.pack(),299,0));
    const auto before=r.comp()->layer(id)->source_frame(FrameIndex{190});
    Command c;c.type=CommandType::LayerSplit;c.layer_split.layer=id;c.layer_split.at=FrameIndex{150};
    AUREA_CHECK(r.e.apply_command(c).ok());LayerId second;
    r.comp()->layers().for_each([&](LayerId key,const Layer& x){if(x.start.value==150)second=key;});
    AUREA_CHECK(second.valid());if(!second.valid())return;
    AUREA_CHECK(r.comp()->layer(id)->trackId!=r.comp()->layer(second)->trackId);
    AUREA_CHECK(r.e.edit_clip_time(second.pack(),0,180));
    AUREA_CHECK(r.e.edit_clip_time(id.pack(),0,20));
    AUREA_CHECK_EQ(r.comp()->layer(id)->start.value,20LL);
    AUREA_CHECK_EQ(r.comp()->layer(second)->start.value,180LL);
    AUREA_CHECK_NEAR(r.comp()->layer(second)->source_frame(FrameIndex{190}),before,.001);
    c.type=CommandType::Undo;AUREA_CHECK(r.e.apply_command(c).ok());
    AUREA_CHECK_EQ(r.comp()->layer(id)->start.value,0LL);
    AUREA_CHECK_EQ(r.comp()->layer(second)->start.value,180LL);
}

AUREA_TEST(Regression2128, TextSpacingCompatibilityAndPresetBlurQuality) {
    Rig2128 r;auto id=r.e.add_text("A\nB");AUREA_CHECK(id.ok());if(!id.ok())return;
    f32 style[21]{};style[20]=12345;
    AUREA_CHECK(r.e.query_text_style(*id,style,20));style[18]=2.25f;style[19]=7;
    AUREA_CHECK(r.e.set_text_style(*id,style,20));
    AUREA_CHECK(r.e.query_text_style(*id,style));AUREA_CHECK_NEAR(style[20],12345,0);
    AUREA_CHECK(r.e.set_text_style(*id,style));
    auto* l=r.comp()->layer(LayerId::unpack(*id));
    AUREA_CHECK_NEAR(l->text.lineHeight,2.25,.001);AUREA_CHECK_NEAR(l->text.tracking,7,.001);
    style[18]=std::numeric_limits<f32>::infinity();AUREA_CHECK(!r.e.set_text_style(*id,style,20));
    const auto samples=r.comp()->motion_blur().samples;
    AUREA_CHECK(r.e.apply_text_preset(*id,11));AUREA_CHECK(r.comp()->motion_blur().samples>=samples);
    AUREA_CHECK(r.e.save_project("build/regression-2128-text.aurea").ok());
    AUREA_CHECK(r.e.load_project("build/regression-2128-text.aurea").ok());
    r.comp()->layers().for_each([&](LayerId,const Layer& x){AUREA_CHECK_NEAR(x.text.lineHeight,2.25,.001);AUREA_CHECK_NEAR(x.text.tracking,7,.001);});
}

AUREA_TEST(Regression2128, AnimatorEffectRangeColorBlurAndRandomKeys) {
    Layer l;l.kind=LayerKind::Text;l.effects.push_back(animator2128(l));auto& fx=l.effects[0];
    fx.params[aePosition].constant=ParamValue::vec3(20,30,40);
    fx.params[aeEnd].constant=ParamValue::scalar(50);fx.params[aeBlur].constant=ParamValue::scalar(12);
    fx.params[aeFillOn].constant=ParamValue::boolean(true);fx.params[aeFill].constant=ParamValue::color(1,0,0,.5f);
    auto s=glyphs2128(l);
    for(u32 i=0;i<8;++i){AUREA_CHECK_NEAR(s[i].translate.x,i<4?20:0,.001);AUREA_CHECK_NEAR(s[i].blur,i<4?12:0,.001);AUREA_CHECK_NEAR(s[i].fillOpacity,i<4?.5:1,.001);}
    auto& tr=l.tracks.get_or_create(TrackProperty::EffectParam,fx.id,param_track_key(aePosition,0));
    tr.set(FrameIndex{0},0);tr.set(FrameIndex{30},60);for(auto& k:tr.keys)k.interp=Interpolation::Linear;
    AUREA_CHECK_NEAR(glyphs2128(l,15.5)[0].translate.x,31,.001);
    fx.params[aeRandom].constant=ParamValue::boolean(true);fx.params[aeSeed].constant=ParamValue::scalar(17);
    const auto a=glyphs2128(l,15);const auto b=glyphs2128(l,1);const auto again=glyphs2128(l,15);
    u32 selected=0;for(u32 i=0;i<8;++i){AUREA_CHECK_NEAR(a[i].translate.x,again[i].translate.x,.001);selected+=a[i].blur>0?1:0;}AUREA_CHECK_EQ(selected,4u);
    fx.enabled=false;s=glyphs2128(l);for(auto& x:s){AUREA_CHECK_NEAR(x.blur,0,0);AUREA_CHECK_NEAR(x.translate.x,0,0);}
}

AUREA_TEST(Regression2128, NullMotionTileSamplesParentsOwnKeyframes) {
    auto project=Project::create_new(320,180,30,"null");auto* c=project->timeline().composition(project->timeline().root());
    auto parent=c->add_layer(LayerKind::Null,"null");auto child=c->add_layer(LayerKind::Image,"image");
    auto* p=c->layer(parent);auto* l=c->layer(child);p->start=FrameIndex{10};p->end=FrameIndex{200};p->offset=FrameIndex{7};
    l->parent=parent;l->start=FrameIndex{30};l->end=FrameIndex{100};l->offset=FrameIndex{50};
    EffectRegistry reg;register_builtin_effects(reg);EffectInstance fx;fx.id=p->alloc_effect_id();fx.type=effect_type_id(effect_keys::kMotionTile);initialize_instance(fx,*reg.params(fx.type));p->effects.push_back(fx);
    auto& tr=p->tracks.get_or_create(TrackProperty::EffectParam,fx.id,param_track_key(1,0));tr.set(FrameIndex{7},50);tr.set(FrameIndex{67},200);for(auto& k:tr.keys)k.interp=Interpolation::Linear;
    LayerPlacement placement;EffectPlan plan;EffectGraph::plan(*l,reg,FrameIndex{70},1,placement,nullptr,plan,30,c);
    AUREA_CHECK_EQ(plan.evals.size(),usize{1});if(plan.evals.empty())return;
    AUREA_CHECK_EQ(plan.evals[0].localTime.value,47LL);AUREA_CHECK_NEAR(plan.values[plan.evals[0].valueOffset+1].v[0],150,.001);
    l->timeRemapEnabled=true;l->timeRemap.set(FrameIndex{70},0);
    EffectGraph::plan(*l,reg,FrameIndex{70},1,placement,nullptr,plan,30,c);AUREA_CHECK_NEAR(plan.values[1].v[0],150,.001);
    p->effects[0].enabled=false;EffectGraph::plan(*l,reg,FrameIndex{70},1,placement,nullptr,plan,30,c);AUREA_CHECK(plan.empty());
}

AUREA_TEST(Regression2128, ExtrudedLettersUseAnimatorGeometryAndColor) {
    const auto font=default_font();AUREA_CHECK(font!=nullptr);if(!font)return;
    scene3d::Text3DSpec spec;spec.content="ABCD";spec.separateGlyphs=true;auto built=scene3d::build_text3d(*font,spec);AUREA_CHECK(built.ok());if(!built.ok())return;
    Layer l;l.kind=LayerKind::Model3D;l.effects.push_back(animator2128(l));auto& fx=l.effects[0];
    fx.params[aeEnd].constant=ParamValue::scalar(50);fx.params[aePosition].constant=ParamValue::vec3(0,100,0);
    fx.params[aeFillOn].constant=ParamValue::boolean(true);fx.params[aeFill].constant=ParamValue::color(1,0,0,.5f);
    auto rest=built.asset->rest_world_matrices(),pose=rest;std::vector<f32> opacity;std::vector<Vec4> fill;
    scene3d::apply_text3d_animators(*built.asset,l,0,30,pose,opacity,&fill);
    for(usize i=0;i<pose.size();++i){AUREA_CHECK_NEAR(pose[i].col[3].y-rest[i].col[3].y,i<2?-1:0,.001);AUREA_CHECK_NEAR(opacity[i],i<2?.5:1,.001);AUREA_CHECK_NEAR(fill[i].w,i<2?1:0,.001);}
}
