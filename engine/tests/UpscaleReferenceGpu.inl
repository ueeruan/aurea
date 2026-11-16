// AE reference batch A2: Detail-preserving Upscale (ADBE Upscale). The pixel
// comparison against the AE 2027 frames (gray fixture, 200 %) runs with
// AUREA_UPSCALE_REVIEW=<dir>; the assertions keep the observed contracts.
// Evidence: build/reference/effects-20261115/batch-a2/upscale.md.
namespace upscale_tests {
constexpr const char* kUpscale = "aurea.distort.upscale_reference";

/// The AE "gray" fixture (render-references.jsx): eight 32 px bars, 8-bit
/// sRGB 32/64/128/192/255/128(50 % opacity), red, black in a 256x144 layer.
ImagePixels gray_bars() {
    ImagePixels image;
    image.width = 256;
    image.height = 144;
    image.rgba.assign(256 * 144 * 4, 255);
    static const u8 grey[8] = {32, 64, 128, 192, 255, 128, 255, 0};
    for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 256; ++x) {
        u8* p = &image.rgba[(static_cast<usize>(y) * 256 + x) * 4];
        p[0] = p[1] = p[2] = grey[x / 32];
        if (x / 32 == 6) p[1] = p[2] = 0;
        p[3] = x / 32 == 5 ? 128 : 255;
    }
    return image;
}
} // namespace upscale_tests

AUREA_TEST(UpscaleReference, NativeControls) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* parameters = registry.params(effect_type_id(upscale_tests::kUpscale));
    AUREA_CHECK(parameters);
    if (!parameters) return;
    AUREA_CHECK_EQ(parameters->count(), 4u);
    AUREA_CHECK_NEAR(parameters->at(0).defaultValue.v[0], 100, 1e-6);
    AUREA_CHECK_NEAR(parameters->at(0).typed_min(), 100, 1e-6);
    AUREA_CHECK_NEAR(parameters->at(0).typed_max(), 30000, 1e-3);
    AUREA_CHECK_NEAR(parameters->at(2).defaultValue.v[0], 50, 1e-6);
    AUREA_CHECK_EQ(parameters->at(3).enumCount, 2u);
    AUREA_CHECK_EQ(parameters->at(3).defaultValue.as_enum(), 1u);   // Bicubic
}

AUREA_TEST(UpscaleReferenceGpu, StepResponseMatchesTheObservedAeRow) {
    AUREA_REQUIRE_GPU();
    using namespace upscale_tests;
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(gray_bars(), 128, 72);
    const FloatImage plain = scene.render(FrameIndex{0}, 1, true);
    auto& effect = scene.add_effect(layer, kUpscale);
    // 100 %: the native effect returns its input untouched.
    AUREA_CHECK(image_diff(plain, scene.render(FrameIndex{0}, 1, true)) < 1e-6f);
    effect.params[0].constant = ParamValue::scalar(200);
    const FloatImage up = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(up));
    // AE 2027, row 72 (premultiplied linear): the 0.2156 -> 0.5269 edge at
    // comp x 63.5 and the opaque -> 50 % alpha edge at x 191.5.
    const f32 expected[][3] = {{60, .2126f, 1}, {62, .1775f, 1}, {63, .2455f, 1}, {64, .4991f, 1}, {65, .5657f, 1},
                               {66, .5425f, 1}, {191, .8193f, .8867f}, {192, .1753f, .6133f}, {193, .0526f, .4473f}};
    for (const auto& row : expected) {
        const auto px = up.v(static_cast<u32>(row[0]), 72);
        AUREA_CHECK_NEAR(px.x, row[1], .01f);   // fp16 work format: GPU vs AE max .0088
        AUREA_CHECK_NEAR(px.w, row[2], .004f);
    }
    // Detail does not change the image in AE 2027.
    effect.params[2].constant = ParamValue::scalar(0);
    AUREA_CHECK(image_diff(up, scene.render(FrameIndex{0}, 1, true)) < 1e-6f);
    // Detail-preserving alpha runs alpha through the same patch pipeline.
    effect.params[3].constant = ParamValue::scalar(0);
    AUREA_CHECK(all_finite(scene.render(FrameIndex{0}, 1, true)));
    // A larger scale takes more 1.5x steps and still stays finite.
    effect.params[3].constant = ParamValue::scalar(1);
    effect.params[0].constant = ParamValue::scalar(330);
    AUREA_CHECK(all_finite(scene.render(FrameIndex{0}, 1, true)));
}

AUREA_TEST(UpscaleReferenceGpu, RenderObservedReferenceCasesWhenRequested) {
    const char* outputPath = std::getenv("AUREA_UPSCALE_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    using namespace upscale_tests;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    const std::filesystem::path out(outputPath);
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(gray_bars(), 128, 72);
    auto save = [](const FloatImage& frame, const std::filesystem::path& path) {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(frame.px.data()), static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
    };
    save(scene.render(FrameIndex{0}, 1, true), out / "input-gray.rgba32f");
    auto& effect = scene.add_effect(layer, kUpscale);
    for (const f32 scale : {100.f, 200.f}) for (const f32 detail : {0.f, 50.f}) {
        effect.params[0].constant = ParamValue::scalar(scale);
        effect.params[2].constant = ParamValue::scalar(detail);
        save(scene.render(FrameIndex{0}, 1, true), out / ("upscale" + std::to_string(static_cast<int>(scale)) + "-detail" +
                                                          std::to_string(static_cast<int>(detail)) + ".rgba32f"));
    }
    // 2D case (no AE frame): the numpy model of batch-a2/upscale_model.py is
    // run on the saved input to check that the GPU passes follow it.
    Scene detail(256, 144);
    detail.comp->set_transparent_background(true);
    const auto small = detail.image(reference_image(64, 36), 128, 72);
    save(detail.render(FrameIndex{0}, 1, true), out / "input-reference64.rgba32f");
    auto& upscale = detail.add_effect(small, kUpscale);
    for (const f32 scale : {200.f, 330.f}) {
        upscale.params[0].constant = ParamValue::scalar(scale);
        save(detail.render(FrameIndex{0}, 1, true), out / ("reference64-s" + std::to_string(static_cast<int>(scale)) + ".rgba32f"));
    }
}
