// AE reference batch A2: Time Displacement and the AE-faithful Posterize Time
// (both ask for the layer INPUT at other times through the effect's temporal
// checkout). Evidence: build/reference/effects-20261115/batch-a2/.
namespace batch_a2_tests {
constexpr const char* kTimeDisplacement = "aurea.time.time_displacement_reference";
constexpr const char* kPosterize = "aurea.time.posterize_reference";

/// A comp-sized map: left half `left`, right half `right` (8-bit sRGB).
ImagePixels split_map(u32 w, u32 h, u8 left, u8 right) {
    ImagePixels image;
    image.width = w;
    image.height = h;
    image.rgba.assign(static_cast<usize>(w) * h * 4, 255);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x)
        for (u32 c = 0; c < 3; ++c) image.rgba[(static_cast<usize>(y) * w + x) * 4 + c] = x < w / 2 ? left : right;
    return image;
}

f32 frame_diff(const FloatImage& a, const FloatImage& b) {
    f32 d = 0;
    for (usize k = 0; k < a.px.size() && k < b.px.size(); ++k) d = std::max(d, std::fabs(a.px[k] - b.px[k]));
    return d;
}
} // namespace batch_a2_tests

AUREA_TEST(TimeDisplacementReference, NativeControlsAndLegacyInstancesRemainSeparate) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* parameters = registry.params(effect_type_id(batch_a2_tests::kTimeDisplacement));
    const auto* legacy = registry.params(effect_type_id("aurea.time.time_displacement"));
    AUREA_CHECK(parameters && legacy);
    if (!parameters || !legacy) return;
    AUREA_CHECK_EQ(parameters->count(), 4u);
    AUREA_CHECK_EQ(parameters->find("map_layer"), 0u);
    AUREA_CHECK_NEAR(parameters->at(1).defaultValue.v[0], 1, 1e-6);
    AUREA_CHECK_NEAR(parameters->at(1).typed_min(), -3600, 1e-3);
    AUREA_CHECK_NEAR(parameters->at(1).typed_max(), 3600, 1e-3);
    AUREA_CHECK_NEAR(parameters->at(2).defaultValue.v[0], 60, 1e-6);
    AUREA_CHECK(parameters->at(3).defaultValue.as_bool());
    // The legacy key keeps its six controls and indices.
    AUREA_CHECK_EQ(legacy->count(), 6u);
    AUREA_CHECK_EQ(legacy->find("map_layer"), 1u);
}

// Native 8-bit path: black (L = 0) is now − 127/128·max, white (L = 255) now + max,
// 50% grey (L = 128) snaps back to now. Each pixel is COPIED from its level.
AUREA_TEST(TimeDisplacementReferenceGpu, MapLevelsCopyTheLayerInputFromOtherTimes) {
    AUREA_REQUIRE_GPU();
    using namespace batch_a2_tests;
    Scene s(kPtW, kPtH, 30.0);
    // Child square: x = 10 + 2·frame, 12 px wide, at y = 36.
    const LayerId id = precomp_temporal_square(s, true, false);
    const LayerId map = s.image(split_map(kPtW, kPtH, 0, 255), kPtW * .5f, kPtH * .5f);
    s.comp->layer(map)->visible = false;
    const LayerId grey = s.image(split_map(kPtW, kPtH, 128, 128), kPtW * .5f, kPtH * .5f);
    s.comp->layer(grey)->visible = false;
    EffectInstance& e = s.add_effect(id, kTimeDisplacement);
    e.params[0].constant.ref = map.pack();
    e.params[0].constant.v[0] = static_cast<f32>(map.index);
    e.params[1].constant = ParamValue::scalar(.5f);
    const FloatImage split = s.render(FrameIndex{30}, 1, true);
    // Left half from frame 15 (square at x = 40), right half from frame 45 (x = 100).
    AUREA_CHECK_MSG(split.v(40, 36).x > .9f, "black map pixels do not come from now - max");
    AUREA_CHECK_MSG(split.v(100, 36).x > .9f, "white map pixels do not come from now + max");
    AUREA_CHECK_MSG(split.v(70, 36).x < .1f, "the present square leaked into displaced pixels");
    AUREA_CHECK(split.v(75, 36).x < .1f);
    // Mid grey: the present frame.
    e.params[0].constant.ref = grey.pack();
    e.params[0].constant.v[0] = static_cast<f32>(grey.index);
    const FloatImage present = s.render(FrameIndex{30}, 1, true);
    AUREA_CHECK_MSG(present.v(70, 36).x > .9f, "50% grey must keep the present time");
    AUREA_CHECK(present.v(40, 36).x < .1f);
    // No map layer: the layer itself is the map (AE default). Transparent
    // pixels (L = 0) come from frame 15; the present square (white) from 45.
    e.params[0].constant.ref = 0;
    e.params[0].constant.v[0] = 0;
    const FloatImage self = s.render(FrameIndex{30}, 1, true);
    AUREA_CHECK_MSG(self.v(40, 36).x > .9f, "self map: transparent pixels must come from now - max");
    AUREA_CHECK_MSG(self.v(70, 36).x < .1f, "self map: the present square must come from now + max");
    // Times clamp to the layer: at frame 5, now - 0.5 s is frame 0 (x = 10).
    const FloatImage early = s.render(FrameIndex{5}, 1, true);
    AUREA_CHECK_MSG(early.v(10, 36).x > .9f, "times below the layer start must clamp to frame 0");
}

// AE holds only the input (source + effects ABOVE Posterize Time); the layer
// transform and the effects below keep running at the comp rate.
AUREA_TEST(PosterizeTimeReferenceGpu, HoldsOnlyTheInputNotTheTransformOrLaterEffects) {
    AUREA_REQUIRE_GPU();
    using namespace batch_a2_tests;
    {
        Scene s(128, 72, 30.0);
        const LayerId id = s.solid(16, 16, Vec4{1, 1, 1, 1}, 20, 36);
        Track& x = s.comp->layer(id)->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0}, 20, Interpolation::Linear);
        x.set(FrameIndex{30}, 110, Interpolation::Linear);
        EffectInstance& e = s.add_effect(id, kPosterize);
        e.params[0].constant = ParamValue::scalar(10.0f);
        AUREA_CHECK_MSG(frame_diff(s.render(FrameIndex{0}), s.render(FrameIndex{1})) > .5f,
                        "Posterize Time must not hold the layer transform");
    }
    {
        Scene s(kPtW, kPtH, 30.0);
        const LayerId id = precomp_temporal_square(s, true, false);
        Layer* layer = s.comp->layer(id);
        // Effect ABOVE: exposure animated 0 → -3 stops over frames 0..3 (held).
        EffectInstance& above = s.add_effect(id, effect_keys::kExposure);
        const u32 aboveId = above.id;
        Track& stops = layer->tracks.get_or_create(TrackProperty::EffectParam, aboveId, param_track_key(0, 0));
        stops.set(FrameIndex{0}, 0, Interpolation::Linear);
        stops.set(FrameIndex{3}, -3, Interpolation::Linear);
        EffectInstance& posterize = s.add_effect(id, kPosterize);
        posterize.params[0].constant = ParamValue::scalar(10.0f);
        const FloatImage f0 = s.render(FrameIndex{0}, 1, true);
        const FloatImage f2 = s.render(FrameIndex{2}, 1, true);
        // Frames 0..2 show frame 0: the square at x = 10, full brightness.
        AUREA_CHECK(frame_diff(f0, f2) < .01f);
        AUREA_CHECK_NEAR(f2.v(10, 36).x, 1, .02);
        AUREA_CHECK(f2.v(14, 36).x > .9f);
        // Frame 3 is a new hold (frame 3 content, -3 stops).
        const FloatImage f3 = s.render(FrameIndex{3}, 1, true);
        AUREA_CHECK_NEAR(f3.v(16, 36).x, .125f, .02);
        // Effect BELOW: animated exposure after Posterize keeps running.
        EffectInstance& below = s.add_effect(id, effect_keys::kExposure);
        Track& late = s.comp->layer(id)->tracks.get_or_create(TrackProperty::EffectParam, below.id, param_track_key(0, 0));
        late.set(FrameIndex{0}, 0, Interpolation::Linear);
        late.set(FrameIndex{2}, -2, Interpolation::Linear);
        const FloatImage g0 = s.render(FrameIndex{0}, 1, true);
        const FloatImage g2 = s.render(FrameIndex{2}, 1, true);
        AUREA_CHECK_NEAR(g0.v(10, 36).x, 1, .02);
        AUREA_CHECK_NEAR(g2.v(10, 36).x, .25f, .02);
    }
}

// Renders the batch-a2 oracle setups (batch-a2/oracle.jsx: scroll.png sliding left
// 4 px per frame in a 256x144 precomp, ramp maps) when AUREA_BATCH_A2_PTTD_REVIEW
// names an output folder; batch-a2/pt-td/compare.py compares them with AE 2027.
AUREA_TEST(ReferenceBatchA2Gpu, RenderPosterizeAndTimeDisplacementOracleCasesWhenRequested) {
    const char* outputPath = std::getenv("AUREA_BATCH_A2_PTTD_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    using namespace batch_a2_tests;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    if (error) return;
    const std::filesystem::path out(outputPath);
    auto scroll_pixels = [] {
        static const u8 bars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                      {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
        ImagePixels image;
        image.width = 768;
        image.height = 144;
        image.rgba.assign(768 * 144 * 4, 255);
        for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 768; ++x)
            for (u32 c = 0; c < 3; ++c) image.rgba[(static_cast<usize>(y) * 768 + x) * 4 + c] = bars[(x / 32) % 8][c];
        return image;
    };
    auto ramp_pixels = [](u32 w, u32 h, u32 k) {
        ImagePixels image;
        image.width = w;
        image.height = h;
        image.rgba.assign(static_cast<usize>(w) * h * 4, 255);
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x)
            for (u32 c = 0; c < 3; ++c) image.rgba[(static_cast<usize>(y) * w + x) * 4 + c] = static_cast<u8>(std::min(255u, k * x));
        return image;
    };
    auto save = [](const FloatImage& frame, const std::filesystem::path& path) {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(frame.px.data()), static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
    };
    // The moving source as a precomp layer of the 256x144 case comp.
    auto source = [&](Scene& s) {
        const CompositionId childId = s.project.timeline().create_composition("scroll-src", 256, 144, 30.0);
        Composition* child = s.project.timeline().composition(childId);
        child->set_duration(FrameIndex{300});
        child->set_background(Color{0, 0, 0, 0});
        child->set_transparent_background(true);
        Asset a;
        a.kind = AssetKind::Image;
        const AssetId aid = s.project.add_asset(std::move(a));
        s.images[aid.pack()] = scroll_pixels();
        const LayerId img = child->add_layer(LayerKind::Image, "scroll");
        Layer* l = child->layer(img);
        l->source = aid;
        l->transform.anchor = Vec3{384, 72, 0};
        Track& x = l->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0}, 384, Interpolation::Linear);
        x.set(FrameIndex{60}, 144, Interpolation::Linear);
        l->transform.position = Vec3{384, 72, 0};
        const LayerId n = s.comp->add_layer(LayerKind::Composition, "scroll-src");
        Layer* nl = s.comp->layer(n);
        nl->nested.composition = childId;
        nl->transform.anchor = Vec3{128, 72, 0};
        nl->transform.position = Vec3{128, 72, 0};
        return n;
    };
    struct Td { const char* name; i64 frame; f32 res, max; bool small, stretch, nomap; };
    const Td td[] = {{"td-default-f30", 30, 60, 1, false, true, false}, {"td-default-f10", 10, 60, 1, false, true, false},
                     {"td-res30-f30", 30, 30, 1, false, true, false}, {"td-res10-f30", 30, 10, 1, false, true, false},
                     {"td-max05-f20", 20, 60, .5f, false, true, false}, {"td-neg-f30", 30, 60, -1, false, true, false},
                     {"td-small-stretch-f30", 30, 60, 1, true, true, false}, {"td-small-nostretch-f30", 30, 60, 1, true, false, false},
                     {"td-nomap-f30", 30, 60, 1, false, true, true}, {"td-res7-f17", 17, 7, .8f, false, true, false}};
    for (const Td& c : td) {
        Scene s(256, 144, 30.0);
        s.comp->set_transparent_background(true);
        const LayerId layer = source(s);
        LayerId map{};
        if (!c.nomap) {
            map = c.small ? s.image(ramp_pixels(128, 72, 2), 128, 72) : s.image(ramp_pixels(256, 144, 1), 128, 72);
            s.comp->layer(map)->visible = false;
        }
        EffectInstance& e = s.add_effect(layer, kTimeDisplacement);
        if (!c.nomap) { e.params[0].constant.ref = map.pack(); e.params[0].constant.v[0] = static_cast<f32>(map.index); }
        e.params[1].constant = ParamValue::scalar(c.max);
        e.params[2].constant = ParamValue::scalar(c.res);
        e.params[3].constant = ParamValue::boolean(c.stretch);
        save(s.render(FrameIndex{c.frame}, 1, true), out / (std::string(c.name) + ".rgba32f"));
    }
    struct Pt { const char* name; f32 rate; i64 frame; bool move; i64 start; };
    const Pt pt[] = {{"pt-r10-f5", 10, 5, false, 0}, {"pt-r10-f7", 10, 7, false, 0}, {"pt-r7-f5", 7, 5, false, 0},
                     {"pt-r7-f9", 7, 9, false, 0}, {"pt-r12-f8", 12, 8, false, 0}, {"pt-r12-f6", 12, 6, false, 0},
                     {"pt-r10-f5-move", 10, 5, true, 0}, {"pt-r10-f7-late", 10, 7, false, 2}};
    for (const Pt& c : pt) {
        Scene s(256, 144, 30.0);
        s.comp->set_transparent_background(true);
        const LayerId layer = source(s);
        Layer* l = s.comp->layer(layer);
        if (c.start) { l->start.value += c.start; l->end.value += c.start; }
        if (c.move) {
            Track& y = l->tracks.get_or_create(TrackProperty::PositionY);
            y.set(FrameIndex{0}, 72, Interpolation::Linear);
            y.set(FrameIndex{60}, 132, Interpolation::Linear);
        }
        EffectInstance& e = s.add_effect(layer, kPosterize);
        e.params[0].constant = ParamValue::scalar(c.rate);
        save(s.render(FrameIndex{c.frame}, 1, true), out / (std::string(c.name) + ".rgba32f"));
    }
}
