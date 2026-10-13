AUREA_TEST(ClipTime, CurrentSourceGetterSupportsManualRemapOffsetFreezeAndReverseWithoutEffect) {
    TimeRig r(cfg_with_audio());
    r.L()->start = FrameIndex{10}; r.L()->end = FrameIndex{310}; r.L()->offset = FrameIndex{30};
    AUREA_CHECK(r.L()->effects.empty());
    AUREA_CHECK(r.e.set_time_remap(r.layer.pack(), true));
    AUREA_CHECK(r.e.set_time_remap_value(r.layer.pack(), 30, 0));
    AUREA_CHECK(r.e.set_time_remap_value(r.layer.pack(), 60, 90));
    AUREA_CHECK(r.e.set_time_remap_value(r.layer.pack(), 90, 90));
    AUREA_CHECK(r.e.set_time_remap_value(r.layer.pack(), 120, 30));
    for (const auto& sample : {std::pair<i64, f64>{25, 45}, {60, 90}, {85, 60}}) {
        Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{sample.first}, 30.0);
        AUREA_CHECK(r.e.apply_command(seek).ok());
        AUREA_CHECK_NEAR(r.e.query_time_remap_current_source(r.layer.pack()), sample.second, 1e-5);
    }
    AUREA_CHECK(r.L()->effects.empty());
    AUREA_CHECK(std::isnan(r.e.query_time_remap_current_source(~u64{0})));
}

AUREA_TEST(ClipTime, CurrentSourceGetterUsesCoreBezierAndMatchesEffectAliasInSourceFrames) {
    TimeRig r(cfg_with_audio()); RemapPanel panel(r);
    panel.at(0, 0); panel.at(60, 4);
    Command ease; ease.type = CommandType::KeyframeSetInterpolation;
    ease.keyframe_interp.track = TrackRef{r.layer, TrackProperty::TimeRemap, kInvalidIndex, 0};
    ease.keyframe_interp.time = FrameIndex{0}; ease.keyframe_interp.interp = Interpolation::Bezier;
    ease.keyframe_interp.bx1 = .42f; ease.keyframe_interp.by1 = 0;
    ease.keyframe_interp.bx2 = 1; ease.keyframe_interp.by2 = 1;
    AUREA_CHECK(r.e.apply_command(ease).ok()); panel.seek(30);
    const f64 expected = 120 * keyframe_ease(r.L()->timeRemap.keys[0], .5f);
    const f64 current = r.e.query_time_remap_current_source(r.layer.pack());
    AUREA_CHECK_NEAR(current, expected, .0001);
    AUREA_CHECK(current < 55);
    bridge::EffectParamRow rows[4]{}; char blob[512]{};
    AUREA_CHECK(r.e.query_effect_params(r.layer.pack(), panel.effect, rows, 4, blob, sizeof(blob)) >= 1);
    AUREA_CHECK_NEAR(current, rows[0].value[0] * 30, .0001);
}
