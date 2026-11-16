// AE reference batch A: Radial Blur, Radial Shadow (and their AE import
// mapping lives in test_ae_import.cpp). Pixel comparisons against the After
// Effects frames run in build/reference/effects-20261115/batch-a with
// AUREA_BATCH_A_REVIEW set; the assertions here keep the observed contracts.
namespace batch_a_tests {
constexpr const char* kRadialBlur = "aurea.blur.radial_blur_reference";
constexpr const char* kRadialShadow = "aurea.stylize.radial_shadow_reference";

u32 index_of(const char* key, const char* id) {
    const auto* parameters = gpu().effects.params(effect_type_id(key));
    const u32 index = parameters ? parameters->find(id) : kInvalidIndex;
    AUREA_CHECK_MSG(index != kInvalidIndex, id);
    return index;
}

// The AE fixtures (render-references.jsx): eight 32 px bars and a 16 px white
// square in a 256x144 layer.
ImagePixels bars_fixture() {
    ImagePixels image;
    image.width = 256;
    image.height = 144;
    image.rgba.assign(256 * 144 * 4, 255);
    static const u8 colors[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                    {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
    for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 256; ++x)
        for (u32 c = 0; c < 3; ++c) image.rgba[(static_cast<usize>(y) * 256 + x) * 4 + c] = colors[x / 32][c];
    return image;
}

ImagePixels square_fixture() {
    ImagePixels image;
    image.width = 256;
    image.height = 144;
    image.rgba.assign(256 * 144 * 4, 0);
    for (u32 y = 64; y < 80; ++y) for (u32 x = 120; x < 136; ++x)
        for (u32 c = 0; c < 4; ++c) image.rgba[(static_cast<usize>(y) * 256 + x) * 4 + c] = 255;
    return image;
}

void save(const FloatImage& frame, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(frame.px.data()),
                 static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
    AUREA_CHECK(output.good());
}
}

AUREA_TEST(RadialBlurReference, NativeControlsAndLegacyInstancesRemainSeparate) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* parameters = registry.params(effect_type_id(batch_a_tests::kRadialBlur));
    const auto* legacy = registry.params(effect_type_id(effect_keys::kRadialBlur));
    AUREA_CHECK(parameters && legacy);
    if (!parameters || !legacy) return;
    AUREA_CHECK_EQ(parameters->count(), 5u);
    AUREA_CHECK_EQ(legacy->count(), 5u);
    AUREA_CHECK_NEAR(parameters->at(0).defaultValue.v[0], 10, 1e-6);
    AUREA_CHECK_NEAR(parameters->at(0).typed_max(), 1000, 1e-6);
    AUREA_CHECK_EQ(parameters->at(2).enumCount, 2u);
    AUREA_CHECK_EQ(parameters->at(3).enumCount, 2u);
    AUREA_CHECK_EQ(parameters->find("seed"), 4u);
    // The legacy key keeps its own first control (type), so old projects
    // still read the same indices.
    AUREA_CHECK_EQ(legacy->find("type"), 0u);
}

AUREA_TEST(RadialBlurReferenceGpu, SpinAndZoomAverageAroundTheObservedCenterGrid) {
    AUREA_REQUIRE_GPU();
    using namespace batch_a_tests;
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(bars_fixture(), 128, 72);
    const FloatImage plain = scene.render(FrameIndex{0}, 1, true);
    auto& effect = scene.add_effect(layer, kRadialBlur);
    const FloatImage spin = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(spin));
    // Bars are vertical: a spin blur mixes neighbouring bars away from the
    // centre row and leaves the centre column almost untouched.
    AUREA_CHECK(image_diff(plain, spin) > .002f);
    AUREA_CHECK_NEAR(spin.v(127, 72).w, 1, 1e-4);
    // Deterministic per-pixel phases: the same seed gives the same frame,
    // another seed moves the stratified samples.
    AUREA_CHECK(image_diff(spin, scene.render(FrameIndex{0}, 1, true)) < 1e-7f);
    effect.params[index_of(kRadialBlur, "seed")].constant = ParamValue::scalar(7);
    const FloatImage seeded = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(image_diff(spin, seeded) > 1e-5f);
    effect.params[index_of(kRadialBlur, "seed")].constant = ParamValue::scalar(0);
    effect.params[index_of(kRadialBlur, "type")].constant = ParamValue::scalar(1);
    const FloatImage zoom = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(zoom));
    AUREA_CHECK(image_diff(spin, zoom) > .002f);
    // Zoom keeps a radial line on itself: along the centre row the vertical
    // bars blur horizontally; along the centre column the colour stays.
    AUREA_CHECK_NEAR(zoom.v(127, 20).x, plain.v(127, 20).x, .02);
    // The integer part of the amount drives the geometry: below 1 the native
    // effect samples each pixel at itself.
    effect.params[index_of(kRadialBlur, "amount")].constant = ParamValue::scalar(.5f);
    AUREA_CHECK(image_diff(plain, scene.render(FrameIndex{0}, 1, true)) < 1e-6f);
}

AUREA_TEST(RadialShadowReference, NativeControlsAndLegacyInstancesRemainSeparate) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* parameters = registry.params(effect_type_id(batch_a_tests::kRadialShadow));
    const auto* legacy = registry.params(effect_type_id("aurea.stylize.radial_shadow"));
    AUREA_CHECK(parameters && legacy);
    if (!parameters || !legacy) return;
    AUREA_CHECK_EQ(parameters->count(), 9u);
    AUREA_CHECK_EQ(legacy->count(), 9u);
    AUREA_CHECK_NEAR(parameters->at(2).defaultValue.v[0], .25, 1e-6);
    AUREA_CHECK_NEAR(parameters->at(6).defaultValue.v[0], 100, 1e-6);
    AUREA_CHECK_NEAR(parameters->at(6).typed_max(), 200, 1e-6);
    AUREA_CHECK(!parameters->at(8).defaultValue.as_bool());
    AUREA_CHECK(legacy->at(8).defaultValue.as_bool());
}

AUREA_TEST(RadialShadowReferenceGpu, ProjectionFromTheLightMatchesTheObservedSquare) {
    AUREA_REQUIRE_GPU();
    using namespace batch_a_tests;
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(square_fixture(), 128, 72);
    auto& effect = scene.add_effect(layer, kRadialShadow);
    effect.params[0].constant = ParamValue::color(1, 0, 0, 1);
    effect.params[1].constant = ParamValue::scalar(100);
    effect.params[2].constant = ParamValue::vec2(64.f / 256, 36.f / 144);
    effect.params[7].constant = ParamValue::boolean(true);
    const FloatImage frame = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(frame));
    // AE: the 16 px square at (120, 64) lit from (64, 36) with distance 10
    // covers x 126..143 and y 67..84; edge alphas are 8-bit (204, 229, 184).
    for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 256; ++x) {
        const bool inside = x >= 126 && x <= 143 && y >= 67 && y <= 84;
        if (!inside) AUREA_CHECK_NEAR(frame.v(x, y).w, 0, 1e-6);
    }
    AUREA_CHECK_NEAR(frame.v(130, 75).w, 1, 1e-6);
    AUREA_CHECK_NEAR(frame.v(130, 75).x, 1, 1e-5);
    AUREA_CHECK_NEAR(frame.v(130, 75).y, 0, 1e-6);
    AUREA_CHECK_NEAR(frame.v(126, 75).w, 204.f / 255, 1e-3);
    AUREA_CHECK_NEAR(frame.v(130, 67).w, 229.f / 255, 1e-3);
    AUREA_CHECK_NEAR(frame.v(126, 67).w, 184.f / 255, 1e-3);
    // Composite: the layer stays in front of its own shadow.
    effect.params[7].constant = ParamValue::boolean(false);
    const FloatImage composite = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK_NEAR(composite.v(128, 72).x, 1, 1e-4);
    AUREA_CHECK_NEAR(composite.v(128, 72).y, 1, 1e-4);
    // Without "Resize Layer" the shadow stays inside the layer box; with it a
    // long projection leaves the box.
    effect.params[3].constant = ParamValue::scalar(70);
    effect.params[2].constant = ParamValue::vec2(.5f, .5f);
    effect.params[8].constant = ParamValue::boolean(true);
    const FloatImage resized = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(resized));
    effect.params[4].constant = ParamValue::scalar(20);
    effect.params[5].constant = ParamValue::scalar(1);
    const FloatImage glass = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(glass));
    AUREA_CHECK(image_diff(resized, glass) > 1e-4f);
}

AUREA_TEST(ReferenceBatchAGpu, RenderObservedReferenceCasesWhenRequested) {
    const char* outputPath = std::getenv("AUREA_BATCH_A_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    using namespace batch_a_tests;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    AUREA_CHECK(!error);
    if (error) return;
    const std::filesystem::path out(outputPath);
    {
        Scene scene(256, 144);
        scene.comp->set_transparent_background(true);
        const auto layer = scene.image(bars_fixture(), 128, 72);
        save(scene.render(FrameIndex{0}, 1, true), out / "input-bars.rgba32f");
        auto& effect = scene.add_effect(layer, kRadialBlur);
        effect.params[1].constant = ParamValue::vec2(.5f, .5f);
        for (u32 type = 1; type <= 2; ++type) for (u32 quality = 1; quality <= 2; ++quality) {
            effect.params[2].constant = ParamValue::scalar(static_cast<f32>(type - 1));
            effect.params[3].constant = ParamValue::scalar(static_cast<f32>(quality - 1));
            save(scene.render(FrameIndex{0}, 1, true),
                 out / ("radial-blur-type" + std::to_string(type) + "-q" + std::to_string(quality) + ".rgba32f"));
        }
    }
    {
        Scene scene(256, 144);
        scene.comp->set_transparent_background(true);
        const auto layer = scene.image(square_fixture(), 128, 72);
        save(scene.render(FrameIndex{0}, 1, true), out / "input-square.rgba32f");
        auto& effect = scene.add_effect(layer, kRadialShadow);
        effect.params[0].constant = ParamValue::color(1, 0, 0, 1);
        effect.params[1].constant = ParamValue::scalar(100);
        effect.params[2].constant = ParamValue::vec2(64.f / 256, 36.f / 144);
        effect.params[7].constant = ParamValue::boolean(true);
        for (u32 influence = 0; influence <= 2; ++influence) {
            effect.params[6].constant = ParamValue::scalar(static_cast<f32>(influence * 100));
            save(scene.render(FrameIndex{0}, 1, true),
                 out / ("radial-shadow-influence" + std::to_string(influence) + ".rgba32f"));
        }
    }
}

// Posterize Time reference: the native effect holds LAYER time
// trunc(trunc(t / P) * P); a layer that starts later is held from its own start.
AUREA_TEST(PosterizeTimeReferenceGpu, HoldsFromTheLayersOwnTimeZero) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 72, 30.0);
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.frameCount = 120;
    const LayerId id = s.video(cfg, 64, 36, 0.5f);
    const auto max_diff = [](const FloatImage& a, const FloatImage& b) {
        f32 d = 0;
        for (usize k = 0; k < a.px.size() && k < b.px.size(); ++k) d = std::max(d, std::fabs(a.px[k] - b.px[k]));
        return d;
    };
    EffectInstance& e = s.add_effect(id, "aurea.time.posterize_reference");
    e.params[0].constant = ParamValue::scalar(10.0f);
    AUREA_CHECK(max_diff(s.render(FrameIndex{0}), s.render(FrameIndex{2})) < .01f);
    AUREA_CHECK(max_diff(s.render(FrameIndex{2}), s.render(FrameIndex{3})) > .01f);
    Layer* layer = s.comp->layer(id);
    layer->start.value += 1;
    layer->end.value += 1;
    // Comp frames 1..3 are layer frames 0..2 (held); comp frame 4 is layer frame 3.
    AUREA_CHECK(max_diff(s.render(FrameIndex{1}), s.render(FrameIndex{3})) < .01f);
    AUREA_CHECK(max_diff(s.render(FrameIndex{3}), s.render(FrameIndex{4})) > .01f);
    // A rate above the comp rate does not hold anything.
    e.params[0].constant = ParamValue::scalar(60.0f);
    AUREA_CHECK(max_diff(s.render(FrameIndex{3}), s.render(FrameIndex{4})) > .01f);
}
