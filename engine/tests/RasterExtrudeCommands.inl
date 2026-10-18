#include <fstream>
AUREA_TEST(RasterExtrude, NativeSchemaAndUndoKeep3dActivationAtomic) {
    EffectRegistry registry;register_builtin_effects(registry);
    const auto* parameters=registry.params(effect_type_id(effect_keys::kRasterExtrude));
    AUREA_CHECK(parameters&&parameters->count()==4);if(!parameters)return;
    AUREA_CHECK_NEAR(parameters->at(0).defaultValue.v[0],32,.001f);
    AUREA_CHECK_NEAR(parameters->at(0).maxValue,512,.001f);AUREA_CHECK_NEAR(parameters->at(0).typed_max(),4096,.001f);
    AUREA_CHECK_NEAR(parameters->at(2).defaultValue.v[0],.7f,.001f);AUREA_CHECK_EQ(parameters->at(3).enumCount,3u);
    const auto* stroke=registry.params(effect_type_id(effect_keys::kStrokeOutline));
    AUREA_CHECK(stroke&&stroke->count()==6);AUREA_CHECK_NEAR(stroke->at(5).defaultValue.v[0],0,.001f);
    const auto root=std::filesystem::path(AUREA_TEST_DATA_DIR).parent_path().parent_path().parent_path();
    const auto folder=root/"build/validation/raster-extrude-20261013";
    std::filesystem::create_directories(folder);
    std::ofstream catalog(folder/"native-catalog.json");
    catalog<<"{\"effects\":[";
    bool firstEffect=true;
    for(const char* key:{effect_keys::kRasterExtrude,effect_keys::kStrokeOutline}) {
        if(!firstEffect)catalog<<',';firstEffect=false;
        catalog<<"{\"key\":\""<<key<<"\",\"params\":[";
        const auto* specs=registry.params(effect_type_id(key));
        for(u32 index=0;index<specs->count();++index) {
            if(index)catalog<<',';const auto& p=specs->at(index);
            catalog<<"{\"index\":"<<index<<",\"id\":\""<<p.id<<"\",\"default\":["<<p.defaultValue.v[0]<<','<<p.defaultValue.v[1]<<','<<p.defaultValue.v[2]<<','<<p.defaultValue.v[3]
                <<"],\"min\":"<<p.minValue<<",\"max\":"<<p.maxValue<<",\"typedMax\":"<<p.typed_max()<<",\"enumCount\":"<<p.enumCount<<'}';
        }
        catalog<<"]}";
    }
    catalog<<"]}\n";AUREA_CHECK(catalog.good());
    for(LayerKind kind:{LayerKind::Text,LayerKind::Shape,LayerKind::Image,LayerKind::Video,LayerKind::Composition}) {
        Engine engine;AUREA_CHECK(engine.initialize(headless_config()).ok());AUREA_CHECK(engine.new_project(192,144,30,nullptr).ok());
        Command create;create.type=CommandType::LayerCreate;create.layer_create.kind=kind;AUREA_CHECK(engine.apply_command(create).ok());
        const LayerId id=first_layer(engine);
        Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=id;add.effect_add.effectType=effect_type_id(effect_keys::kRasterExtrude);add.effect_add.index=kInvalidIndex;
        AUREA_CHECK(engine.apply_command(add).ok());AUREA_CHECK(layer_of(engine,id)->threeD);
        Command history;history.type=CommandType::Undo;AUREA_CHECK(engine.apply_command(history).ok());
        AUREA_CHECK(!layer_of(engine,id)->threeD);AUREA_CHECK(layer_of(engine,id)->effects.empty());
        history.type=CommandType::Redo;AUREA_CHECK(engine.apply_command(history).ok());AUREA_CHECK(layer_of(engine,id)->threeD);AUREA_CHECK_EQ(layer_of(engine,id)->effects.size(),1u);
        engine.shutdown();
    }
}
AUREA_TEST(RasterExtrude, StrokeDepthActivationAndUnsupportedTargetsAreValidatedBySharedCommands) {
    Engine engine;AUREA_CHECK(engine.initialize(headless_config()).ok());AUREA_CHECK(engine.new_project(192,144,30,nullptr).ok());
    const auto added=engine.add_shape(0);AUREA_CHECK(added.ok());if(!added.ok())return;
    const LayerId id=LayerId::unpack(*added);
    Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=id;add.effect_add.effectType=effect_type_id(effect_keys::kStrokeOutline);add.effect_add.index=kInvalidIndex;
    AUREA_CHECK(engine.apply_command(add).ok());AUREA_CHECK(!layer_of(engine,id)->threeD);
    Command set;set.type=CommandType::EffectSetParam;set.effect_param.layer=id;set.effect_param.effect=EffectId{layer_of(engine,id)->effects[0].id,0};set.effect_param.paramIndex=5;set.effect_param.value=32;
    AUREA_CHECK(engine.apply_command(set).ok());AUREA_CHECK(layer_of(engine,id)->threeD);
    Command undo;undo.type=CommandType::Undo;AUREA_CHECK(engine.apply_command(undo).ok());AUREA_CHECK(!layer_of(engine,id)->threeD);
    AUREA_CHECK_NEAR(layer_of(engine,id)->effects[0].params[5].constant.v[0],0,.001f);
    Command key;key.type=CommandType::KeyframeInsert;
    key.keyframe.track={id,TrackProperty::EffectParam,layer_of(engine,id)->effects[0].id,param_track_key(5,0)};
    key.keyframe.time=FrameIndex{12};key.keyframe.value=32;
    AUREA_CHECK(engine.apply_command(key).ok());AUREA_CHECK(layer_of(engine,id)->threeD);
    AUREA_CHECK(engine.apply_command(undo).ok());AUREA_CHECK(!layer_of(engine,id)->threeD);
    Command create;create.type=CommandType::LayerCreate;create.layer_create.kind=LayerKind::Null;AUREA_CHECK(engine.apply_command(create).ok());
    const Composition* comp=engine.project()->timeline().composition(engine.project()->timeline().current());
    LayerId nullId;comp->layers().for_each([&](LayerId candidate,const Layer& layer){if(layer.kind==LayerKind::Null)nullId=candidate;});
    add.effect_add.layer=nullId;add.effect_add.effectType=effect_type_id(effect_keys::kRasterExtrude);
    AUREA_CHECK(engine.apply_command(add).code()==Errc::InvalidArgument);
    add.effect_add.effectType=effect_type_id(effect_keys::kStrokeOutline);
    AUREA_CHECK(engine.apply_command(add).ok());
    bridge::EffectParamRow rows[8]{};char blob[2048]{};
    const u32 hiddenCount=engine.query_effect_params(nullId.pack(),layer_of(engine,nullId)->effects[0].id,rows,8,blob,sizeof(blob));
    AUREA_CHECK_EQ(hiddenCount,6u);AUREA_CHECK((rows[5].flags&kParamHidden)!=0);
    for(u32 i=0;i<5;++i)AUREA_CHECK((rows[i].flags&kParamHidden)==0);
    const u32 visibleCount=engine.query_effect_params(id.pack(),layer_of(engine,id)->effects[0].id,rows,8,blob,sizeof(blob));
    AUREA_CHECK_EQ(visibleCount,6u);AUREA_CHECK((rows[5].flags&kParamHidden)==0);
    engine.shutdown();
}
AUREA_TEST(RasterExtrude, LegacyStrokeProjectGetsOnlyAnAppendOnlyZeroDepthSlot) {
    Engine engine;AUREA_CHECK(engine.initialize(headless_config()).ok());AUREA_CHECK(engine.new_project(192,144,30,nullptr).ok());
    const auto added=engine.add_shape(0);AUREA_CHECK(added.ok());if(!added.ok())return;
    const LayerId id=LayerId::unpack(*added);
    Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=id;add.effect_add.effectType=effect_type_id(effect_keys::kStrokeOutline);add.effect_add.index=kInvalidIndex;
    AUREA_CHECK(engine.apply_command(add).ok());
    auto layer=[&](){return engine.project()->timeline().composition(engine.project()->timeline().current())->layer(id);};
    layer()->effects[0].params.resize(5);layer()->effects[0].params[0].constant.v[0]=17;
    const u32 effectId=layer()->effects[0].id;
    layer()->tracks.get_or_create(TrackProperty::EffectParam,effectId,param_track_key(4,0)).set(FrameIndex{12},67);
    const auto folder=std::filesystem::path(AUREA_TEST_DATA_DIR).parent_path().parent_path().parent_path()/"build/validation/raster-extrude-20261013";
    std::filesystem::create_directories(folder);const auto path=(folder/"legacy-stroke-own.aurea").string();
    AUREA_CHECK(engine.save_project(path.c_str()).ok());AUREA_CHECK(engine.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(layer()->effects[0].params.size(),6u);AUREA_CHECK(!layer()->threeD);
    AUREA_CHECK_NEAR(layer()->effects[0].params[0].constant.v[0],17,.001f);AUREA_CHECK_NEAR(layer()->effects[0].params[5].constant.v[0],0,.001f);
    const auto* track=layer()->tracks.find(TrackProperty::EffectParam,effectId,param_track_key(4,0));
    AUREA_CHECK(track&&track->keys.size()==1);if(track)AUREA_CHECK_NEAR(track->keys[0].value,67,.001f);
    engine.shutdown();
}
