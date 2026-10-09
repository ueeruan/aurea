// Shared rules invoked by the Kotlin and Swift editing controls.
AUREA_TEST(ContentBounds, TrimmedKeysAreHiddenWithoutLosingAnimationAndUndoRestoresThem) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 240, 30, "trimmed keys").ok());
    const auto id = e.add_shape(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(LayerId::unpack(*id));
    auto& track = layer->tracks.get_or_create(TrackProperty::PositionX);
    for (i64 frame : {0, 30, 59, 60, 90}) track.set(FrameIndex{frame}, static_cast<f32>(frame));
    AUREA_CHECK(e.edit_clip_time(*id, 1, 60));
    bridge::KeyframeRow rows[8]{};
    AUREA_CHECK_EQ(e.query_keyframes(*id, rows, 8), 5u);
    for (u32 i = 0; i < 5; ++i)
        AUREA_CHECK_EQ((rows[i].timelineFlags & bridge::kKeyframeTimelineHidden) != 0, rows[i].time > 60);
    AUREA_CHECK_NEAR(layer->tracks.sample_or(TrackProperty::PositionX, FrameIndex{45}, 0), 45, 1e-5f);
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_keyframes(*id, rows, 8), 5u);
    for (u32 i = 0; i < 5; ++i) AUREA_CHECK_EQ(rows[i].timelineFlags, 0u);
    AUREA_CHECK(e.edit_clip_time(*id, 0, 30));
    AUREA_CHECK_EQ(e.query_keyframes(*id, rows, 8), 5u);
    for (u32 i = 0; i < 5; ++i)
        AUREA_CHECK_EQ((rows[i].timelineFlags & bridge::kKeyframeTimelineHidden) != 0, rows[i].time < 30);
}

AUREA_TEST(ContentBounds, DraggedAnimationEndpointStaysVisibleAtClipEndAndUndoRestoresIt) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 240, 30, "end key").ok());
    const auto id = e.add_shape(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(LayerId::unpack(*id));
    // Exercise local-time conversion too: local 70 maps to timeline end 90.
    layer->start = FrameIndex{30}; layer->end = FrameIndex{90}; layer->offset = FrameIndex{10};
    auto& track = layer->tracks.get_or_create(TrackProperty::PositionX);
    track.set(FrameIndex{20}, 20); track.set(FrameIndex{71}, 71);
    const i64 picked[] = {0, -1, 0, 20};
    AUREA_CHECK_EQ(e.edit_keyframe_selection(*id, picked, 1, 50, false), 1u);
    bridge::KeyframeRow rows[4]{};
    AUREA_CHECK_EQ(e.query_keyframes(*id, rows, 4), 2u);
    AUREA_CHECK_EQ(rows[0].time, 70);
    AUREA_CHECK_EQ(rows[0].timelineFlags, 0u); // exactly at end remains selectable
    AUREA_CHECK_EQ(rows[1].time, 71);
    AUREA_CHECK_EQ(rows[1].timelineFlags, bridge::kKeyframeTimelineHidden);
    const i64 endpoint[] = {0, -1, 0, 70};
    AUREA_CHECK_EQ(e.edit_keyframe_selection(*id, endpoint, 1, -1, false), 1u);
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_keyframes(*id, rows, 4), 2u);
    AUREA_CHECK_EQ(rows[0].time, 70); AUREA_CHECK_EQ(rows[0].timelineFlags, 0u);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_keyframes(*id, rows, 4), 2u);
    AUREA_CHECK_EQ(rows[0].time, 20); AUREA_CHECK_EQ(rows[0].timelineFlags, 0u);
}

AUREA_TEST(ContentBounds, SeekScrubStepAndShorterContentStayInsideLayers) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 240, 30, "bounded timeline").ok());
    e.set_content_bounded_playback(true);
    Command seek; seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{900}, 30);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 0ll);
    const auto first = e.add_shape(0), last = e.add_shape(1);
    AUREA_CHECK(first.ok() && last.ok()); if (!first.ok() || !last.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    comp->layer(LayerId::unpack(*first))->start = FrameIndex{30};
    comp->layer(LayerId::unpack(*first))->end = FrameIndex{90};
    comp->layer(LayerId::unpack(*last))->start = FrameIndex{120};
    comp->layer(LayerId::unpack(*last))->end = FrameIndex{180};
    comp->layer(LayerId::unpack(*last))->visible = false;
    AUREA_CHECK_EQ(e.query_navigation_end(), 180ll);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 180ll);
    seek.seek.time = tick_at(FrameIndex{105}, 30); AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 105ll); // gaps remain navigable
    Command step; step.type = CommandType::PlaybackStep; step.step.frames = 1000;
    AUREA_CHECK(e.apply_command(step).ok()); AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 180ll);
    step.step.frames = -1000; AUREA_CHECK(e.apply_command(step).ok()); AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 0ll);
    Command begin; begin.type = CommandType::PlaybackScrubBegin; AUREA_CHECK(e.apply_command(begin).ok());
    seek.type = CommandType::PlaybackScrub; seek.seek.time = tick_at(FrameIndex{999}, 30);
    AUREA_CHECK(e.apply_command(seek).ok()); AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 180ll);
    begin.type = CommandType::PlaybackScrubEnd; AUREA_CHECK(e.apply_command(begin).ok());
    AUREA_CHECK(e.edit_clip_time(*last, 1, 150));
    seek.type = CommandType::PlaybackSeek; AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 150ll);
    e.set_content_bounded_playback(false); AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, 999ll); // approved UI keeps its policy
}

AUREA_TEST(ContentBounds, PlaybackAndLoopRespectTheEnd) {
    PlaybackController controller;
    controller.set_navigation_end(FrameIndex{90}); controller.configure(30, FrameIndex{90});
    controller.seek(FrameIndex{200}, 0); AUREA_CHECK_EQ(controller.current().value, 90ll);
    controller.play(0); controller.update(4000000000ull);
    AUREA_CHECK(!controller.playing()); AUREA_CHECK_EQ(controller.current().value, 89ll);
    controller.set_loop(true); controller.play(5000000000ull); controller.update(9000000000ull);
    AUREA_CHECK(controller.playing()); AUREA_CHECK_EQ(controller.current().value, 30ll);
}

AUREA_TEST(ContentBounds, ThreeDSwitchPreservesAnimationAndIsUndoable) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 240, 30, "3D switch").ok());
    const auto id = e.add_shape(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(LayerId::unpack(*id));
    layer->transform.position.x = 27;
    layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{15}, 37);
    AUREA_CHECK(e.set_layer_3d(*id, true)); AUREA_CHECK(e.set_layer_3d(*id, false));
    AUREA_CHECK(!layer->threeD); AUREA_CHECK_NEAR(layer->transform.position.x, 27, 1e-5f);
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    layer = comp->layer(LayerId::unpack(*id)); AUREA_CHECK(layer->threeD);
    AUREA_CHECK_NEAR(layer->tracks.sample_or(TrackProperty::PositionX, FrameIndex{15}, 0), 37, 1e-5f);
    undo.type = CommandType::Redo; AUREA_CHECK(e.apply_command(undo).ok());
    layer = comp->layer(LayerId::unpack(*id)); AUREA_CHECK(!layer->threeD);
    layer->locked = true; AUREA_CHECK(!e.set_layer_3d(*id, true)); AUREA_CHECK(!layer->threeD);
}
