// Community reports: exercise the synchronous facade used by both native UIs.
AUREA_TEST(Beta008Memory, SingleTintAt4KFitsTheDefaultConservativeBudget) {
    auto* backend = new aurea::test::MockBackend(); // Engine owns an injected backend.
    Engine e; auto config = headless_config(); config.backend = backend;
    config.memoryBudgetBytes = 384ull << 20;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(3840, 2160, 30, "4K tint budget").ok());
    const auto layer = e.add_shape(0); AUREA_CHECK(layer.ok()); if (!layer.ok()) return;
    Command effect; effect.type = CommandType::EffectAdd;
    effect.effect_add.layer = LayerId::unpack(*layer);
    effect.effect_add.effectType = e.effects().find_key("aurea.color.tint");
    effect.effect_add.index = 0xffffffffu;
    AUREA_CHECK(e.apply_command(effect).ok());
    TextureDesc desc; desc.width = 3840; desc.height = 2160;
    desc.format = SurfaceFormat::RGBA16F; desc.renderTarget = desc.sampled = true;
    const auto target = backend->create_texture(desc); AUREA_CHECK(target.ok()); if (!target.ok()) return;
    AUREA_CHECK(e.render_offscreen(*target, desc.width, desc.height).ok());
    backend->destroy_texture(*target); e.shutdown();
}
AUREA_TEST(Beta008, ImportedModelFitsOnlyItsSelectedScene) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "selected scene").ok());
    ModelImport request; request.path = std::string(AUREA_TEST_DATA_DIR) + "/beta008-selected-scene.gltf";
    const auto id = e.import_model(request); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    const auto* layer = comp->layer(LayerId::unpack(*id));
    const auto asset = e.model_asset(layer->model.scene.pack()); AUREA_CHECK(asset != nullptr); if (!asset) return;
    AUREA_CHECK(asset->nodes[0].inScene && asset->nodes[1].inScene && !asset->nodes[2].inScene);
    AUREA_CHECK_NEAR(asset->bounds.extent().x, 1, 1e-5f);
    AUREA_CHECK_NEAR(asset->bounds.extent().y, 1, 1e-5f);
    AUREA_CHECK_NEAR(layer->model.unitScale, 99, 1e-4f);
    AUREA_CHECK_NEAR(layer->model.pivot.x, 0, 1e-5f);
}
AUREA_TEST(Beta008, DirectionalLightUsesQueuedInsertionAndFreshKeyframeTime) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "light time").ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    const i64 end = comp->duration().value;
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{end}, 30);
    AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
    const auto id = e.add_light(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* layer = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK_EQ(layer->start.value, end); AUREA_CHECK(layer->contains_time(FrameIndex{end}));
    layer->tracks.get_or_create(TrackProperty::LightIntensity).set(FrameIndex{0}, 1);
    layer->tracks.get_or_create(TrackProperty::LightIntensity).set(FrameIndex{20}, 5);
    seek.seek.time = tick_at(FrameIndex{end + 10}, 30); AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
    f32 values[11]; AUREA_CHECK(e.query_light(*id, values, 11)); AUREA_CHECK_NEAR(values[1], 3, 1e-5f);
}
AUREA_TEST(Beta008, TextTextureKeepsBevelAndSurvivesEditingUndoAndSave) {
    const auto directory = std::filesystem::temp_directory_path() / ("aurea008_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto image = (directory / "custom.bmp").string();
    auto* file = std::fopen(image.c_str(), "wb"); AUREA_CHECK(file != nullptr); if (!file) return;
    // Own 2x2 24-bit BMP, padded rows. Match the product's supported decoders.
    u8 bitmap[70]{}; bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 70; bitmap[10] = 54;
    bitmap[14] = 40; bitmap[18] = 2; bitmap[22] = 2; bitmap[26] = 1; bitmap[28] = 24; bitmap[34] = 16;
    const u8 pixels[16] = {0,0,255,0,255,0,0,0,255,0,0,0,255,255,0,0};
    std::memcpy(bitmap + 54, pixels, sizeof(pixels)); std::fwrite(bitmap, 1, sizeof(bitmap), file); std::fclose(file);
    Engine e; auto config = headless_config(); config.documentsDirectory = directory.string();
    AUREA_CHECK(e.initialize(config).ok()); AUREA_CHECK(e.new_project(320, 180, 30, "texture").ok());
    scene3d::Text3DSpec spec; spec.content = "Aurea"; spec.bevel = true;
    const auto id = e.add_text3d(spec); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    AUREA_CHECK(e.set_text3d_texture(*id, image).ok());
    AUREA_CHECK(e.query_text3d(*id, spec)); AUREA_CHECK(spec.bevel);
    AUREA_CHECK(spec.texturePath.rfind("docs:", 0) == 0);
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    const auto assetId = comp->layer(LayerId::unpack(*id))->model.scene;
    auto asset = e.model_asset(assetId.pack()); AUREA_CHECK(asset != nullptr);
    if (asset) {
        AUREA_CHECK_EQ(asset->images.size(), 1u);
        if (!asset->images.empty()) AUREA_CHECK_EQ(asset->images[0].width, 2u);
        AUREA_CHECK_EQ(asset->materials[0].baseColorTex.image, 0);
    }
    spec.roughness = .7f; AUREA_CHECK(e.set_text3d(*id, spec).ok());
    const auto projectFile = (directory / "text.aurea").string(); AUREA_CHECK(e.save_project(projectFile.c_str()).ok());
    AUREA_CHECK(e.load_project(projectFile.c_str()).ok());
    AUREA_CHECK(e.query_text3d(*id, spec)); AUREA_CHECK(!spec.texturePath.empty() && spec.bevel);
    AUREA_CHECK_NEAR(spec.roughness, .7f, 1e-5f); AUREA_CHECK(e.model_missing_textures(*id).empty());
    AUREA_CHECK(e.set_text3d_texture(*id, "").ok());
    AUREA_CHECK(e.query_text3d(*id, spec)); AUREA_CHECK(spec.texturePath.empty());
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.query_text3d(*id, spec)); AUREA_CHECK(!spec.texturePath.empty());
    e.shutdown(); std::filesystem::remove_all(directory);
}
AUREA_TEST(Beta008, DuplicatedNestedGroupsHaveIndependentEditableContents) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "group copy").ok());
    auto& timeline = e.project()->timeline();
    const auto root = timeline.root();
    const auto inner = timeline.create_composition("inner", 320, 180, 30);
    const auto shape = timeline.composition(inner)->add_layer(LayerKind::Shape, "shape");
    timeline.composition(inner)->layer(shape)->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{20}, 45);
    const auto outer = timeline.create_composition("outer", 320, 180, 30);
    const auto nested = timeline.composition(outer)->add_layer(LayerKind::Composition, "nested");
    timeline.composition(outer)->layer(nested)->nested.composition = inner;
    const auto group = timeline.composition(root)->add_layer(LayerKind::Composition, "group");
    timeline.composition(root)->layer(group)->nested.composition = outer;
    Command duplicate; duplicate.type = CommandType::LayerDuplicate; duplicate.layer_ref.layer = group;
    AUREA_CHECK(e.apply_command(duplicate).ok());
    u64 selected = 0; AUREA_CHECK_EQ(e.get_selection(&selected, 1), 1u);
    const auto copy = LayerId::unpack(selected);
    const auto copiedOuter = timeline.composition(root)->layer(copy)->nested.composition;
    const auto copiedInner = timeline.composition(copiedOuter)->layer(nested)->nested.composition;
    AUREA_CHECK(copiedOuter != outer && copiedInner != inner);
    auto* copiedShape = timeline.composition(copiedInner)->layer(shape);
    copiedShape->name = "edited copy";
    copiedShape->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{20}, 120);
    timeline.composition(root)->layer(copy)->start = FrameIndex{60};
    AUREA_CHECK_EQ(timeline.composition(root)->layer(group)->start.value, 0ll);
    AUREA_CHECK_EQ(timeline.composition(inner)->layer(shape)->name, std::string("shape"));
    AUREA_CHECK_NEAR(timeline.composition(inner)->layer(shape)->tracks.sample_or(TrackProperty::RotationZ, FrameIndex{20}, 0), 45, 1e-5f);
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(timeline.composition(root)->layer(copy) == nullptr);
    undo.type = CommandType::Redo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(timeline.composition(root)->layer(copy)->nested.composition == copiedOuter);
    AUREA_CHECK_EQ(timeline.composition(copiedInner)->layer(shape)->name, std::string("edited copy"));
}

AUREA_TEST(Beta008, DeletingNullKeepsChildLocalValuesAndAuthoredKeys) {
    for (bool threeD : {false, true}) for (bool withGrand : {false, true}) {
        Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(320, 180, 30, "null deletion").ok());
        const auto grand = e.add_null(threeD), parent = e.add_null(threeD), child = e.add_shape(0);
        AUREA_CHECK(grand.ok() && parent.ok() && child.ok()); if (!grand.ok() || !parent.ok() || !child.ok()) return;
        auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
        auto* p = comp->layer(LayerId::unpack(*parent));
        if (withGrand) p->parent = LayerId::unpack(*grand);
        p->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{0}, 60);
        p->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{30}, 720);
        auto* c = comp->layer(LayerId::unpack(*child));
        c->threeD = threeD; c->parent = LayerId::unpack(*parent);
        c->transform.position = {12, 24, 36}; c->transform.rotation = {10, 20, 30}; c->transform.scale = {.5f, .75f, 1.5f};
        c->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 12);
        c->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{45}, 90);
        Command erase; erase.type = CommandType::LayerDelete; erase.layer_ref.layer = LayerId::unpack(*parent);
        AUREA_CHECK(e.apply_command(erase).ok()); c = comp->layer(LayerId::unpack(*child));
        AUREA_CHECK(c->parent == (withGrand ? LayerId::unpack(*grand) : LayerId{}));
        AUREA_CHECK_NEAR(c->transform.position.x, 12, 1e-5f);
        AUREA_CHECK_NEAR(c->transform.position.z, 36, 1e-5f);
        AUREA_CHECK_NEAR(c->transform.rotation.z, 30, 1e-5f);
        AUREA_CHECK_NEAR(c->transform.scale.y, .75f, 1e-5f);
        AUREA_CHECK_EQ(c->tracks.find(TrackProperty::PositionX)->keys.size(), 2u);
        AUREA_CHECK_NEAR(c->tracks.sample_or(TrackProperty::PositionX, FrameIndex{45}, 0), 90, 1e-5f);
        AUREA_CHECK(c->tracks.find(TrackProperty::RotationZ) == nullptr);
        Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
        AUREA_CHECK(comp->layer(LayerId::unpack(*child))->parent == LayerId::unpack(*parent));
        undo.type = CommandType::Redo; AUREA_CHECK(e.apply_command(undo).ok());
        AUREA_CHECK(comp->layer(LayerId::unpack(*parent)) == nullptr);
    }
}

AUREA_TEST(Beta008, ShapeKeyReadsQueuedSeekAndPreservesExistingKeyValue) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "shape queued key").ok());
    const auto id = e.add_shape(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* c = e.project()->timeline().composition(e.project()->timeline().root());
    c->layer(LayerId::unpack(*id))->start = FrameIndex{12};
    AUREA_CHECK(e.set_shape_param(*id, 5, 180, false));
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{42}, 30);
    AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
    AUREA_CHECK(e.ensure_shape_param_key(*id, 5));
    auto* track = c->layer(LayerId::unpack(*id))->tracks.find(TrackProperty::ShapeParam, 0, 5);
    AUREA_CHECK(track != nullptr); if (!track) return;
    AUREA_CHECK_EQ(track->keys.size(), 1u); AUREA_CHECK_EQ(track->keys[0].time.value, 30ll);
    AUREA_CHECK(e.set_shape_param(*id, 5, 240, false));
    AUREA_CHECK(e.ensure_shape_param_key(*id, 5)); AUREA_CHECK(e.ensure_shape_param_key(*id, 5));
    track = c->layer(LayerId::unpack(*id))->tracks.find(TrackProperty::ShapeParam, 0, 5);
    AUREA_CHECK_EQ(track->keys.size(), 1u); AUREA_CHECK_NEAR(track->keys[0].value, 240, 1e-5f);
}

AUREA_TEST(Beta008, FirstPuppetKeyAfterSeekPreservesInitialPoseAndInterpolates) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "puppet first key").ok());
    const auto id = e.add_shape(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    Command add; add.type = CommandType::EffectAdd;
    add.effect_add = {LayerId::unpack(*id), effect_type_id("aurea.distort.puppet"), kInvalidIndex};
    AUREA_CHECK(e.apply_command(add).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    const u32 fx = comp->layer(LayerId::unpack(*id))->effects.back().id;
    AUREA_CHECK_EQ(e.puppet_add_pin(*id, fx, .25f, .5f), 0);
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{30}, 30);
    AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
    AUREA_CHECK(e.puppet_move_pin(*id, fx, 0, .75f, .2f, true, false));
    f32 pins[Engine::kPuppetPinFloats];
    for (i64 f : {0, 15, 30}) {
        seek.seek.time = tick_at(FrameIndex{f}, 30); AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
        AUREA_CHECK_EQ(e.query_puppet(*id, fx, pins, Engine::kPuppetPinFloats), Engine::kPuppetPinFloats);
        AUREA_CHECK_NEAR(pins[1], .25f + .5f * f / 30.f, 1e-5f);
        AUREA_CHECK_NEAR(pins[2], .5f - .3f * f / 30.f, 1e-5f);
    }
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    e.query_puppet(*id, fx, pins, Engine::kPuppetPinFloats); AUREA_CHECK_NEAR(pins[1], .25f, 1e-5f);
    undo.type = CommandType::Redo; AUREA_CHECK(e.apply_command(undo).ok());
    e.query_puppet(*id, fx, pins, Engine::kPuppetPinFloats); AUREA_CHECK_NEAR(pins[1], .75f, 1e-5f);
}
