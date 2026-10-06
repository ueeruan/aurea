AUREA_TEST(EffectCompatibility, WaveDirectionAppendPreservesSavedEnumsAndAnimation) {
    Engine engine;
    AUREA_CHECK(engine.initialize(headless_config()).ok());
    AUREA_CHECK(engine.new_project(160, 90, 30, "wave compatibility").ok());
    const auto added = engine.add_shape(0);
    AUREA_CHECK(added.ok());
    if (!added.ok()) return;
    const auto id = LayerId::unpack(*added);
    auto layer = [&]() { return engine.project()->timeline().composition(engine.project()->timeline().current())->layer(id); };
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = id;
    add.effect_add.effectType = effect_type_id(effect_keys::kWaveWarp);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(engine.apply_command(add).ok());
    auto& effect = layer()->effects.back();
    AUREA_CHECK_EQ(effect.params.size(), 10u);
    effect.params.resize(9); // The complete previous layout, not a malformed effect.
    effect.params[4].constant = ParamValue::scalar(2); // Diagonal remains enum 2.
    effect.params[3].constant = ParamValue::scalar(73);
    const u32 effectId = effect.id;
    layer()->tracks.get_or_create(TrackProperty::EffectParam, effectId, param_track_key(3, 0)).set(FrameIndex{12}, 145);
    std::filesystem::create_directories("build/reference/legacy-comparison-20261005");
    const char* path = "build/reference/legacy-comparison-20261005/wave-compatibility.aurea";
    AUREA_CHECK(engine.save_project(path).ok());
    AUREA_CHECK(engine.load_project(path).ok());
    auto& reopened = layer()->effects.back();
    AUREA_CHECK_EQ(reopened.params.size(), 10u);
    AUREA_CHECK_NEAR(reopened.params[4].constant.v[0], 2, 0);
    AUREA_CHECK_NEAR(reopened.params[9].constant.v[0], 0, 0);
    const Track* phase = layer()->tracks.find(TrackProperty::EffectParam, effectId, param_track_key(3, 0));
    AUREA_CHECK(phase && phase->keys.size() == 1 && phase->keys[0].value == 145);
    // The new mode and its independent track survive a second save/reopen.
    reopened.params[4].constant = ParamValue::scalar(4);
    reopened.params[9].constant = ParamValue::scalar(37);
    layer()->tracks.get_or_create(TrackProperty::EffectParam, effectId, param_track_key(9, 0)).set(FrameIndex{24}, 91);
    AUREA_CHECK(engine.save_project(path).ok());
    AUREA_CHECK(engine.load_project(path).ok());
    AUREA_CHECK_NEAR(layer()->effects.back().params[4].constant.v[0], 4, 0);
    AUREA_CHECK_NEAR(layer()->effects.back().params[9].constant.v[0], 37, 0);
    const Track* angle = layer()->tracks.find(TrackProperty::EffectParam, effectId, param_track_key(9, 0));
    AUREA_CHECK(angle && angle->keys.size() == 1 && angle->keys[0].value == 91);
}
