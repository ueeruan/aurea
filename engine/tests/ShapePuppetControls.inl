// Real facade tracks, Renderer geometry, undo and project round-trips.
#include <algorithm>
#include "aurea/expr/Expression.hpp"
namespace {
void shape_controls_seek(Engine& engine, i64 frame) {
    Command seek; seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{frame}, 30.);
    AUREA_CHECK(engine.apply_command(seek).ok());
}
Layer* shape_controls_layer(Engine& engine, u64 id) {
    return engine.project()->timeline().composition(engine.project()->timeline().current())->layer(LayerId::unpack(id));
}
void shape_controls_compare_rendered_corners(Engine& engine, u64 id, i64 frame) {
    shape_controls_seek(engine, frame);
    bridge::LayerDetailPOD detail{}; AUREA_CHECK(engine.query_layer_detail(id, detail));
    AUREA_CHECK(detail.geomFlags & bridge::kGeomCornersValid);
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    FrameSnapshot snapshot; RenderSettings settings;
    engine.renderer().prepare(*comp, *engine.project(), FrameIndex{frame}, nullptr, nullptr, nullptr,
        settings, 1, 0, DecodeMode::Still, 1.f, snapshot);
    const auto found = std::find_if(snapshot.layers.begin(), snapshot.layers.end(), [&](const auto& layer) { return layer.id.pack() == id; });
    AUREA_CHECK(found != snapshot.layers.end()); if (found == snapshot.layers.end()) return;
    AUREA_CHECK_EQ(detail.sourceWidth, found->source.width); AUREA_CHECK_EQ(detail.sourceHeight, found->source.height);
    const f32 w = static_cast<f32>(found->source.width), h = static_cast<f32>(found->source.height);
    const f32 x[4] = {0,w,w,0}, y[4] = {0,0,h,h};
    for (u32 i = 0; i < 4; ++i) {
        const Vec4 point = found->compFromLayer * Vec4{x[i],y[i],0,1};
        AUREA_CHECK_NEAR(detail.corners[i*2], point.x / point.w, 1e-4f);
        AUREA_CHECK_NEAR(detail.corners[i*2+1], point.y / point.w, 1e-4f);
    }
}
}

AUREA_TEST(ShapeControls, DetailSamplesThePlayheadAndMatchesRenderedBounds) {
    Engine engine; auto cfg = headless_config(); cfg.backend = new aurea::test::MockBackend();
    AUREA_CHECK(engine.initialize(cfg).ok()); AUREA_CHECK(engine.new_project(320,240,30.,"shape bounds").ok());
    const auto added = engine.add_shape(0); AUREA_CHECK(added.ok()); if (!added) return;
    const u64 id = *added; auto* layer = shape_controls_layer(engine,id);
    layer->shape.bounds = Rect{0,0,100,80}; layer->transform.anchor = Vec3{50,40,0};
    layer->transform.position = Vec3{160,120,0}; layer->transform.rotation.z = 23;
    layer->start = FrameIndex{30}; layer->offset = FrameIndex{10}; layer->end = FrameIndex{120};
    const f32 first[] = {0,2,3,.2f,1,100.25f,80.5f};
    const f32 last[] = {0,18,7,.8f,5,300.75f,160.5f};
    for (u32 param = 1; param <= 6; ++param) {
        auto& track = layer->tracks.get_or_create(TrackProperty::ShapeParam,0,param);
        track.set(FrameIndex{0},first[param],Interpolation::Linear);
        track.set(FrameIndex{20},last[param],Interpolation::Linear);
    }
    for (i64 frame : {30,35,40}) {
        shape_controls_compare_rendered_corners(engine,id,frame);
        bridge::LayerDetailPOD detail{}; AUREA_CHECK(engine.query_layer_detail(id,detail));
        f32 values[Engine::kShapeParamFloats]{};
        AUREA_CHECK_EQ(engine.query_shape_params(id,values,Engine::kShapeParamFloats),Engine::kShapeParamFloats);
        AUREA_CHECK_EQ(detail.sourceWidth,static_cast<u32>(values[5]));
        AUREA_CHECK_EQ(detail.sourceHeight,static_cast<u32>(values[6]));
        AUREA_CHECK_NEAR(detail.shapeCorner,values[1],1e-6f);
        AUREA_CHECK_EQ(detail.shapeTypePoints >> 16,static_cast<u32>(values[2]));
        AUREA_CHECK_NEAR(detail.shapeInner,values[3],1e-6f);
        AUREA_CHECK_NEAR(detail.shapeStrokeWidth,values[4],1e-6f);
    }
    AUREA_CHECK_EQ(layer->shape.bounds.w,100.f); AUREA_CHECK_EQ(layer->shape.bounds.h,80.f);
    AUREA_CHECK_EQ(layer->transform.anchor.x,50.f); // querying never rewrites authored state
}

namespace {
void model_projection_compare_snapshot_and_pick(Engine& engine, u64 id, i64 frame) {
    shape_controls_seek(engine,frame);
    bridge::LayerDetailPOD detail{}; AUREA_CHECK(engine.query_layer_detail(id,detail));
    AUREA_CHECK(detail.geomFlags & bridge::kGeomCornersValid);
    const auto* comp=engine.project()->timeline().composition(engine.project()->timeline().current());
    FrameSnapshot snapshot; RenderSettings settings;
    engine.renderer().prepare(*comp,*engine.project(),FrameIndex{frame},nullptr,nullptr,nullptr,
        settings,1,0,DecodeMode::Still,1.f,snapshot);
    const scene3d::SceneFrame* scene=nullptr; const scene3d::SceneInstance* instance=nullptr;
    for (const auto& group : snapshot.scenes) for (const auto& item : group.instances)
        if (item.layerKey==id) { scene=&group; instance=&item; }
    AUREA_CHECK(scene && instance && instance->asset); if (!scene || !instance || !instance->asset) return;
    const auto& camera=scene->camera;
    const Mat4 clip=camera.imageTransform * scene3d::reverse_z_perspective(camera.fovY,
        static_cast<f32>(comp->width())/static_cast<f32>(comp->height()),camera.nearZ) * camera.view * instance->world;
    auto project=[&](Vec3 p) {
        const Vec4 v=clip*Vec4{p.x,p.y,p.z,1}; AUREA_CHECK(v.w>1e-6f);
        return Vec2{(v.x/v.w*.5f+.5f)*comp->width(),(v.y/v.w*.5f+.5f)*comp->height()};
    };
    const scene3d::Aabb& bounds=instance->asset->bounds;
    f32 x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
    for (u32 i=0;i<8;++i) {
        const Vec2 p=project(Vec3{(i&1)?bounds.max.x:bounds.min.x,(i&2)?bounds.max.y:bounds.min.y,(i&4)?bounds.max.z:bounds.min.z});
        x0=std::min(x0,p.x); x1=std::max(x1,p.x); y0=std::min(y0,p.y); y1=std::max(y1,p.y);
    }
    const f32 expected[]={x0,y0,x1,y0,x1,y1,x0,y1};
    for (u32 i=0;i<8;++i) AUREA_CHECK_NEAR(detail.corners[i],expected[i],1e-3f);
    const Vec2 center=project(bounds.center());
    AUREA_CHECK_EQ(engine.scene_pick(center.x,center.y,0),id);
}
}

AUREA_TEST(ModelProjectionControls, UnparentedAndParentedBoundsUseTheRenderedCameraAndWorldAtEachFrame) {
    for (bool parented : {false,true}) {
        Engine engine; auto cfg=headless_config(); cfg.backend=new aurea::test::MockBackend();
        AUREA_CHECK(engine.initialize(cfg).ok()); AUREA_CHECK(engine.new_project(320,240,30.,"model projection").ok());
        const auto model=engine.add_shape3d(0,"cube"),camera=engine.add_camera();
        AUREA_CHECK(model.ok() && camera.ok()); if (!model || !camera) continue;
        u64 parentId=0;
        if (parented) { const auto parent=engine.add_null(true); AUREA_CHECK(parent.ok()); if (!parent) continue; parentId=*parent; }
        auto* layer=shape_controls_layer(engine,*model); auto* lens=shape_controls_layer(engine,*camera);
        layer->transform.position=Vec3{178,105,25}; layer->transform.rotation=Vec3{17,-24,11};
        lens->transform.position.x+=12; lens->transform.position.y-=5; lens->transform.position.z+=17;
        lens->transform.rotation=Vec3{5,-7,13}; // tilt and Z rotation are intentional authored camera values
        if (parented) {
            auto* parent=shape_controls_layer(engine,parentId);
            parent->transform.position=Vec3{8,-4,15}; parent->transform.rotation=Vec3{7,12,9};
            parent->transform.scale=Vec3{1.05f,.95f,1}; layer->parent=LayerId::unpack(parentId);
        }
        for (const auto property : {TrackProperty::PositionZ,TrackProperty::RotationX,TrackProperty::RotationY}) {
            auto& track=layer->tracks.get_or_create(property);
            const f32 value=property==TrackProperty::PositionZ?25.f:(property==TrackProperty::RotationX?17.f:-24.f);
            track.set(FrameIndex{0},value,Interpolation::Linear); track.set(FrameIndex{30},value+20,Interpolation::Linear);
        }
        for (i64 frame : {0,30,15,0}) model_projection_compare_snapshot_and_pick(engine,*model,frame);
        shape_controls_seek(engine,15); bridge::LayerDetailPOD before{},after{};
        AUREA_CHECK(engine.query_layer_detail(*model,before));
        Command move; move.type=CommandType::LayerSetPosition; move.position={LayerId::unpack(*model),203,105,25};
        AUREA_CHECK(engine.apply_command(move).ok()); model_projection_compare_snapshot_and_pick(engine,*model,15);
        Command undo; undo.type=CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
        model_projection_compare_snapshot_and_pick(engine,*model,15); AUREA_CHECK(engine.query_layer_detail(*model,after));
        for(u32 i=0;i<8;++i) AUREA_CHECK_NEAR(before.corners[i],after.corners[i],1e-3f);
    }
}

AUREA_TEST(ShapeControls, DeleteFinalSizeKeyPreservesValueGeometryUndoAndReopen) {
    Engine engine; AUREA_CHECK(engine.initialize(headless_config()).ok());
    AUREA_CHECK(engine.new_project(320,240,30.,"shape delete").ok());
    const auto added = engine.add_shape(0); AUREA_CHECK(added.ok()); if (!added) return;
    const u64 id = *added;
    AUREA_CHECK(engine.set_shape_param(id,5,100,false)); AUREA_CHECK(engine.ensure_shape_param_key(id,5));
    AUREA_CHECK(engine.set_shape_param(id,5,333,false));
    auto* layer = shape_controls_layer(engine,id); const f32 height = layer->shape.bounds.h;
    layer->threeD=true;
    auto& opacity = layer->tracks.get_or_create(TrackProperty::Opacity); opacity.set(FrameIndex{0},.5f);
    shape_controls_seek(engine,10);
    bridge::LayerDetailPOD before{},after{}; AUREA_CHECK(engine.query_layer_detail(id,before));
    AUREA_CHECK_EQ(engine.scene_pick(25,120,0),id);
    Command erase; erase.type = CommandType::KeyframeDelete;
    erase.keyframe.track = TrackRef{LayerId::unpack(id),TrackProperty::ShapeParam,0,5}; erase.keyframe.time = FrameIndex{0};
    // Toggle and timeline deletion must preserve the same size and center.
    shape_controls_seek(engine,0); AUREA_CHECK(engine.toggle_shape_param_key(id,5)); shape_controls_seek(engine,10);
    AUREA_CHECK(engine.query_layer_detail(id,after));
    for (u32 i=0;i<8;++i) AUREA_CHECK_NEAR(before.corners[i],after.corners[i],1e-5f);
    AUREA_CHECK_EQ(engine.scene_pick(25,120,0),id);
    Command restore; restore.type = CommandType::Undo; AUREA_CHECK(engine.apply_command(restore).ok());
    AUREA_CHECK(engine.apply_command(erase).ok());
    f32 values[Engine::kShapeParamFloats]{}; engine.query_shape_params(id,values,Engine::kShapeParamFloats);
    AUREA_CHECK_EQ(values[5],333.f); AUREA_CHECK_EQ(static_cast<u32>(values[15]) & (1u<<5),0u);
    AUREA_CHECK(engine.query_layer_detail(id,after));
    for (u32 i=0;i<8;++i) AUREA_CHECK_NEAR(before.corners[i],after.corners[i],1e-5f);
    AUREA_CHECK_EQ(engine.scene_pick(25,120,0),id);
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->shape.bounds.h,height);
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->tracks.find(TrackProperty::Opacity)->keys.size(),1u);
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->shape.bounds.w,100.f);
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->tracks.find(TrackProperty::ShapeParam,0,5)->keys.size(),1u);
    undo.type = CommandType::Redo; AUREA_CHECK(engine.apply_command(undo).ok());
    const auto path = std::filesystem::temp_directory_path()/"aurea-shape-final-key-test.aurea";
    AUREA_CHECK(engine.save_project(path.string().c_str()).ok());
    AUREA_CHECK(engine.set_shape_param(id,5,50,false)); AUREA_CHECK(engine.load_project(path.string().c_str()).ok());
    engine.query_shape_params(id,values,Engine::kShapeParamFloats); AUREA_CHECK_EQ(values[5],333.f);
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->tracks.find(TrackProperty::Opacity)->keys.size(),1u);
    std::error_code ignored; std::filesystem::remove(path,ignored);
}

AUREA_TEST(ShapeControls, SelectionDeleteFreezesTheCurrentLocalPoseInsteadOfAnEndpoint) {
    Engine engine; AUREA_CHECK(engine.initialize(headless_config()).ok());
    AUREA_CHECK(engine.new_project(320,240,30.,"shape selection").ok());
    const auto added = engine.add_shape(0); AUREA_CHECK(added.ok()); if (!added) return;
    const u64 id=*added; auto* layer=shape_controls_layer(engine,id);
    layer->shape.bounds.w=100; layer->transform.anchor.x=50;
    layer->start=FrameIndex{30}; layer->offset=FrameIndex{10}; layer->end=FrameIndex{120};
    auto& width=layer->tracks.get_or_create(TrackProperty::ShapeParam,0,5);
    width.set(FrameIndex{0},100,Interpolation::Linear); width.set(FrameIndex{20},500,Interpolation::Linear);
    shape_controls_seek(engine,30); // current local frame10, evaluated width300
    const i64 refs[]={35,0,5,0, 35,0,5,20};
    AUREA_CHECK_EQ(engine.edit_keyframe_selection(id,refs,2,0,true),2u);
    f32 values[Engine::kShapeParamFloats]{}; engine.query_shape_params(id,values,Engine::kShapeParamFloats);
    AUREA_CHECK_EQ(values[5],300.f); AUREA_CHECK_EQ(shape_controls_layer(engine,id)->transform.anchor.x,50.f);
    Command undo; undo.type=CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->tracks.find(TrackProperty::ShapeParam,0,5)->keys.size(),2u);
    AUREA_CHECK_EQ(shape_controls_layer(engine,id)->shape.bounds.w,100.f);
}

AUREA_TEST(ShapeControls, FinalWidthDeleteAndLaterResizePreserveCustomStaticKeyedAndExpressionAnchors) {
    for (u32 anchorMode : {0u,1u,2u}) for (u32 deletion : {0u,1u,2u}) {
        Engine engine; auto cfg=headless_config(); cfg.backend=new aurea::test::MockBackend();
        AUREA_CHECK(engine.initialize(cfg).ok()); AUREA_CHECK(engine.new_project(320,240,30.,"shape anchor").ok());
        const auto added=engine.add_shape(0); AUREA_CHECK(added.ok()); if(!added)continue;
        const u64 id=*added;
        AUREA_CHECK(engine.set_shape_param(id,5,100,false)); AUREA_CHECK(engine.ensure_shape_param_key(id,5));
        AUREA_CHECK(engine.set_shape_param(id,5,333,false));
        auto* layer=shape_controls_layer(engine,id);
        layer->transform.anchor=Vec3{23,31,0}; layer->transform.rotation.z=31;
        layer->transform.scale=Vec3{1.1f,.9f,1};
        auto& anchor=layer->tracks.get_or_create(TrackProperty::AnchorX); anchor.staticValue=19;
        if(anchorMode==1) { anchor.set(FrameIndex{0},37,Interpolation::Linear); anchor.set(FrameIndex{20},61,Interpolation::Linear); }
        if(anchorMode==2) anchor.expression=expr::compile("37");
        const Track authored=anchor;
        shape_controls_compare_rendered_corners(engine,id,10);
        bridge::LayerDetailPOD before{},after{}; AUREA_CHECK(engine.query_layer_detail(id,before));
        if(deletion==0) {
            Command erase; erase.type=CommandType::KeyframeDelete;
            erase.keyframe.track={LayerId::unpack(id),TrackProperty::ShapeParam,0,5}; erase.keyframe.time=FrameIndex{0};
            AUREA_CHECK(engine.apply_command(erase).ok());
        } else if(deletion==1) {
            const i64 refs[]={35,0,5,0}; AUREA_CHECK_EQ(engine.edit_keyframe_selection(id,refs,1,0,true),1u);
        } else {
            shape_controls_seek(engine,0); AUREA_CHECK(engine.toggle_shape_param_key(id,5)); shape_controls_seek(engine,10);
        }
        shape_controls_compare_rendered_corners(engine,id,10);
        AUREA_CHECK(engine.query_layer_detail(id,after));
        for(u32 i=0;i<8;++i) AUREA_CHECK_NEAR(before.corners[i],after.corners[i],1e-4f);
        layer=shape_controls_layer(engine,id);
        AUREA_CHECK_EQ(layer->transform.anchor.x,23.f); AUREA_CHECK_EQ(layer->transform.anchor.y,31.f);
        const Track* kept=layer->tracks.find(TrackProperty::AnchorX);
        AUREA_CHECK(kept && kept->keys.size()==authored.keys.size()); if(!kept)continue;
        AUREA_CHECK_EQ(kept->staticValue,authored.staticValue); AUREA_CHECK(kept->expression==authored.expression);
        for(u32 i=0;i<kept->keys.size();++i) AUREA_CHECK_EQ(kept->keys[i].value,authored.keys[i].value);
        Command undo; undo.type=CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
        shape_controls_compare_rendered_corners(engine,id,10); AUREA_CHECK(engine.query_layer_detail(id,after));
        for(u32 i=0;i<8;++i) AUREA_CHECK_NEAR(before.corners[i],after.corners[i],1e-4f);
        undo.type=CommandType::Redo; AUREA_CHECK(engine.apply_command(undo).ok());
        const auto path=std::filesystem::temp_directory_path()/"aurea-shape-anchor-delete-test.aurea";
        AUREA_CHECK(engine.save_project(path.string().c_str()).ok()); AUREA_CHECK(engine.load_project(path.string().c_str()).ok());
        shape_controls_compare_rendered_corners(engine,id,10); AUREA_CHECK(engine.query_layer_detail(id,after));
        for(u32 i=0;i<8;++i) AUREA_CHECK_NEAR(before.corners[i],after.corners[i],1e-4f);
        layer=shape_controls_layer(engine,id);
        const Track* frozen=layer->tracks.find(TrackProperty::ShapeParam,0,5);
        AUREA_CHECK(frozen && frozen->keys.empty()); // serialized empty tracks retain the centered source convention
        const f32 centerX=(after.corners[0]+after.corners[4])*.5f,centerY=(after.corners[1]+after.corners[5])*.5f;
        AUREA_CHECK(engine.set_shape_param(id,5,445,false)); shape_controls_compare_rendered_corners(engine,id,10);
        AUREA_CHECK(engine.query_layer_detail(id,after));
        AUREA_CHECK_NEAR((after.corners[0]+after.corners[4])*.5f,centerX,1e-4f);
        AUREA_CHECK_NEAR((after.corners[1]+after.corners[5])*.5f,centerY,1e-4f);
        AUREA_CHECK_EQ(shape_controls_layer(engine,id)->transform.anchor.x,23.f);
        std::error_code ignored; std::filesystem::remove(path,ignored);
    }
}

AUREA_TEST(PuppetControls, ActivePinsExposeIndependentNativeXYTracksPerEffectInstance) {
    Engine engine; AUREA_CHECK(engine.initialize(headless_config()).ok());
    AUREA_CHECK(engine.new_project(320,240,30.,"puppet keys").ok());
    const auto added=engine.add_shape(0); AUREA_CHECK(added.ok()); if(!added)return;
    const u64 id=*added;
    for (u32 i=0;i<2;++i) {
        Command add; add.type=CommandType::EffectAdd; add.effect_add.layer=LayerId::unpack(id);
        add.effect_add.effectType=effect_type_id("aurea.distort.puppet"); add.effect_add.index=kInvalidIndex;
        AUREA_CHECK(engine.apply_command(add).ok());
    }
    auto* layer=shape_controls_layer(engine,id); AUREA_CHECK_EQ(layer->effects.size(),2u); if(layer->effects.size()!=2)return;
    const u32 first=layer->effects[0].id,second=layer->effects[1].id;
    AUREA_CHECK(first!=second);
    AUREA_CHECK_EQ(engine.puppet_add_pin(id,first,.2f,.3f),0); AUREA_CHECK_EQ(engine.puppet_add_pin(id,second,.1f,.4f),0);
    shape_controls_seek(engine,30);
    AUREA_CHECK(engine.puppet_move_pin(id,first,0,.8f,.7f,true,false));
    AUREA_CHECK(engine.puppet_move_pin(id,second,0,.9f,.6f,true,false));
    bridge::KeyframeRow rows[16]{}; const u32 count=engine.query_keyframes(id,rows,16); AUREA_CHECK_EQ(count,8u);
    for(u32 i=0;i<count;++i) {
        AUREA_CHECK_EQ(rows[i].property,31u); AUREA_CHECK(rows[i].effectIndex==first||rows[i].effectIndex==second);
        AUREA_CHECK(rows[i].paramIndex==20u||rows[i].paramIndex==21u); AUREA_CHECK_EQ(rows[i].timelineFlags,0u);
    }
    shape_controls_seek(engine,15); f32 pins[64]{};
    AUREA_CHECK_EQ(engine.query_puppet(id,first,pins,64),4u);
    AUREA_CHECK_NEAR(pins[1],.5f,1e-5f); AUREA_CHECK_NEAR(pins[2],.5f,1e-5f);
    AUREA_CHECK(engine.puppet_remove_pin(id,first,0)); AUREA_CHECK_EQ(engine.query_puppet(id,first,pins,64),0u);
    AUREA_CHECK_EQ(engine.query_keyframes(id,rows,16),4u);
    for(u32 i=0;i<4;++i) AUREA_CHECK_EQ(rows[i].effectIndex,second);
    Command undo; undo.type=CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
    AUREA_CHECK_EQ(engine.query_puppet(id,first,pins,64),4u); AUREA_CHECK_EQ(engine.query_keyframes(id,rows,16),8u);
}
