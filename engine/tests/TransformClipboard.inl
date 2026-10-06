AUREA_TEST(TransformClipboard, CopiesAnimationWithoutReplacingMediaEffectsOrLayerTiming) {
    Engine engine; AUREA_CHECK(engine.initialize(headless_config()).ok());
    AUREA_CHECK(engine.new_project(640,360,30.,"transform clipboard").ok());
    auto source=engine.add_shape(0), target=engine.add_text("destination");
    AUREA_CHECK(source.ok() && target.ok()); if (!source.ok() || !target.ok()) return;
    auto* comp=engine.project()->timeline().composition(engine.project()->timeline().current());
    auto* a=comp->layer(LayerId::unpack(*source)); auto* b=comp->layer(LayerId::unpack(*target));
    a->transform.position={42,53,64}; a->transform.rotation={10,20,30}; a->transform.opacity=.7f;
    a->offset=FrameIndex{10}; b->start=FrameIndex{120}; b->end=FrameIndex{270}; b->offset=FrameIndex{40};
    Track position; position.property=TrackProperty::PositionX;
    Keyframe first; first.time=FrameIndex{10}; first.value=42; first.interp=Interpolation::Bezier;
    first.tangentOut=3; position.keys.push_back(first);
    auto second=first; second.time=FrameIndex{40}; second.value=82; position.keys.push_back(second);
    a->tracks.add(position);
    EffectInstance fx; fx.id=17; fx.type=effect_type_id("aurea.light.glow"); b->effects.push_back(fx);
    const auto before=b->transform;
    AUREA_CHECK(engine.copy_transform(*source));
    AUREA_CHECK((engine.clipboard_state() & 16u)!=0);
    const u64 targets[]{*target,*target};
    AUREA_CHECK_EQ(engine.paste_transform(targets,2),1u);
    AUREA_CHECK_NEAR(b->transform.position.x,42.f,.001f);
    AUREA_CHECK_NEAR(b->transform.rotation.y,20.f,.001f);
    AUREA_CHECK_NEAR(b->transform.opacity,.7f,.001f);
    AUREA_CHECK_EQ(b->text.content,std::string("destination"));
    AUREA_CHECK_EQ(b->effects.size(),usize{1}); AUREA_CHECK_EQ(b->effects[0].id,17u);
    AUREA_CHECK_EQ(b->start.value,120ll); AUREA_CHECK_EQ(b->end.value,270ll); AUREA_CHECK_EQ(b->offset.value,40ll);
    const auto* track=b->tracks.find(TrackProperty::PositionX); AUREA_CHECK(track!=nullptr);
    if(track) { AUREA_CHECK_EQ(track->keys.size(),usize{2}); AUREA_CHECK_EQ(track->keys[0].time.value,40ll);
        AUREA_CHECK_EQ(track->keys[1].time.value,70ll); AUREA_CHECK_NEAR(track->keys[0].tangentOut,3.f,.001f); }
    Command undo; undo.type=CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
    b=comp->layer(LayerId::unpack(*target)); AUREA_CHECK(b!=nullptr);
    if(b) { AUREA_CHECK_NEAR(b->transform.position.x,before.position.x,.001f);
        AUREA_CHECK(b->tracks.find(TrackProperty::PositionX)==nullptr);
        b->locked=true; AUREA_CHECK_EQ(engine.paste_transform(targets,2),0u); }
    engine.shutdown();
}
