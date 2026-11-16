// Behavioral regressions for the independently implemented PEDG control contract.
// These checks do not establish numeric equivalence to the external renderer.
namespace deep_glow_tests {
constexpr const char* key = "aurea.light.deep_glow";
void set(Scene& scene, LayerId layer, const char* id, f32 value) {
    const auto* parameters = gpu().effects.params(effect_type_id(key));
    const u32 index = parameters->find(id);
    AUREA_CHECK_MSG(index != kInvalidIndex, id);
    if (index != kInvalidIndex)
        scene.comp->layer(layer)->effects.back().params[index].constant = ParamValue::scalar(value);
}
}

AUREA_TEST(DeepGlowReference, NewDefaultsAndSavedModeAreDistinct) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* parameters = registry.params(effect_type_id(deep_glow_tests::key));
    AUREA_CHECK(parameters != nullptr);
    if (!parameters) return;
    AUREA_CHECK_EQ(parameters->count(), 63u);
    const u32 mode = parameters->find("render_mode");
    AUREA_CHECK_EQ(mode, 21u);
    AUREA_CHECK_EQ(parameters->find("ref_radius"), 22u);
    AUREA_CHECK_EQ(parameters->find("ref_bottom"), 62u);
    EffectInstance instance;
    initialize_instance(instance, *parameters);
    TrackSet tracks;
    AUREA_CHECK_EQ(instance.params[mode].constant.as_enum(), 1u);
    AUREA_CHECK((parameters->at(mode).flags & kParamHidden) != 0);
    instance.params.resize(mode);
    const auto savedMode = evaluate_param_f(tracks, instance, mode, parameters->at(mode), 0, nullptr);
    AUREA_CHECK_EQ(savedMode.as_enum(), 0u);
}

AUREA_TEST(DeepGlowReferenceGpu, RadiusZeroIsIdentityAndHaloIsFiniteDeterministic) {
    AUREA_REQUIRE_GPU();
    Scene scene(96, 64);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(reference_image(48, 32), 48, 32);
    const auto plain = scene.render();
    scene.add_effect(layer, deep_glow_tests::key);
    deep_glow_tests::set(scene, layer, "ref_radius", 0);
    deep_glow_tests::set(scene, layer, "ref_source_opacity", 0);
    AUREA_CHECK(image_diff(plain, scene.render()) < 1e-6f);
    deep_glow_tests::set(scene, layer, "ref_radius", 25);
    deep_glow_tests::set(scene, layer, "ref_dither_enabled", 0);
    const auto first = scene.render();
    const auto second = scene.render();
    for (const f32 value : first.px) AUREA_CHECK(std::isfinite(value));
    AUREA_CHECK(image_diff(first, second) < 1e-6f);
    AUREA_CHECK(image_diff(plain, first) > .001f);
    deep_glow_tests::set(scene, layer, "ref_threshold", 500);
    const auto rejected = scene.render();
    f32 maximum = 0;
    for (const f32 value : rejected.px) maximum = std::max(maximum, std::abs(value));
    AUREA_CHECK(maximum < 1e-6f);
}

AUREA_TEST(DeepGlowReferenceGpu, InputMaskReadsHiddenLayerAlphaLuminanceAndInvert) {
    AUREA_REQUIRE_GPU();
    Scene scene(96, 64);
    scene.comp->set_transparent_background(true);
    ImagePixels maskPixels;
    maskPixels.width = 96;
    maskPixels.height = 64;
    maskPixels.rgba.resize(96 * 64 * 4);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 96; ++x) {
        const u32 band = x / 24;
        const usize offset = (static_cast<usize>(y) * 96 + x) * 4;
        const u8 color = band == 1 ? 0 : 255;
        maskPixels.rgba[offset] = color;
        maskPixels.rgba[offset + 1] = color;
        maskPixels.rgba[offset + 2] = color;
        maskPixels.rgba[offset + 3] = band == 2 ? 128 : band == 3 ? 0 : 255;
    }
    const auto mask = scene.image(std::move(maskPixels), 48, 32);
    // Hidden layers are still gathered when an effect references their real ID.
    scene.comp->layer(mask)->visible = false;
    const auto host = scene.solid(96, 64, {1, 1, 1, 1}, 48, 32);
    auto& effect = scene.add_effect(host, deep_glow_tests::key);
    deep_glow_tests::set(scene, host, "ref_radius", 5);
    deep_glow_tests::set(scene, host, "ref_view", 0);
    deep_glow_tests::set(scene, host, "ref_source_opacity", 0);
    deep_glow_tests::set(scene, host, "ref_dither_enabled", 0);
    const auto check = [&](const FloatImage& frame, const std::array<f32, 4>& coverage) {
        AUREA_CHECK(all_finite(frame));
        for (u32 band = 0; band < 4; ++band) {
            const Vec4 pixel = frame.v(band * 24 + 12, 32);
            AUREA_CHECK_NEAR(pixel.x, coverage[band], .015);
            AUREA_CHECK_NEAR(pixel.y, coverage[band], .015);
            AUREA_CHECK_NEAR(pixel.z, coverage[band], .015);
            AUREA_CHECK_NEAR(pixel.w, 1.f, .015);
        }
    };
    check(scene.render(FrameIndex{0}, 1, true), {1, 1, 1, 1});
    const auto* parameters = gpu().effects.params(effect_type_id(deep_glow_tests::key));
    const u32 maskIndex = parameters->find("ref_mask_layer");
    AUREA_CHECK(maskIndex != kInvalidIndex);
    if (maskIndex == kInvalidIndex) return;
    effect.params[maskIndex].constant.ref = mask.pack();
    constexpr f32 halfAlpha = 128.f / 255.f;
    check(scene.render(FrameIndex{0}, 1, true), {1, 1, halfAlpha, 0});
    deep_glow_tests::set(scene, host, "ref_mask_mode", 1);
    check(scene.render(FrameIndex{0}, 1, true), {1, 0, halfAlpha, 0});
    deep_glow_tests::set(scene, host, "ref_mask_invert", 1);
    check(scene.render(FrameIndex{0}, 1, true), {0, 1, 1 - halfAlpha, 1});
    deep_glow_tests::set(scene, host, "ref_mask_mode", 0);
    check(scene.render(FrameIndex{0}, 1, true), {0, 0, 1 - halfAlpha, 1});
}

AUREA_TEST(DeepGlowReferenceGpu, InputMaskUsesCompositionCoordinatesForTransformedHost) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 96);
    scene.comp->set_transparent_background(true);
    ImagePixels maskPixels;
    maskPixels.width = 128;
    maskPixels.height = 96;
    maskPixels.rgba.resize(128 * 96 * 4, 255);
    for (u32 y = 44; y < 96; ++y) for (u32 x = 0; x < 128; ++x)
        maskPixels.rgba[(static_cast<usize>(y) * 128 + x) * 4 + 3] = 0;
    const auto mask = scene.image(std::move(maskPixels), 64, 48);
    scene.comp->layer(mask)->visible = false;
    const auto host = scene.solid(48, 24, {1, 1, 1, 1}, 80, 44);
    scene.comp->layer(host)->transform.scale = {1.5f, .75f, 1};
    scene.comp->layer(host)->transform.rotation.z = 90;
    auto& effect = scene.add_effect(host, deep_glow_tests::key);
    deep_glow_tests::set(scene, host, "ref_radius", 5);
    deep_glow_tests::set(scene, host, "ref_view", 0);
    deep_glow_tests::set(scene, host, "ref_source_opacity", 0);
    deep_glow_tests::set(scene, host, "ref_dither_enabled", 0);
    const auto* parameters = gpu().effects.params(effect_type_id(deep_glow_tests::key));
    const u32 maskIndex = parameters->find("ref_mask_layer");
    AUREA_CHECK(maskIndex != kInvalidIndex);
    if (maskIndex == kInvalidIndex) return;
    effect.params[maskIndex].constant.ref = mask.pack();
    const auto masked = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(masked));
    // The rotated/scaled host spans y=8..80. The mask boundary stays at comp y=44.
    AUREA_CHECK_NEAR(masked.v(80, 20).x, 1.f, .015);
    AUREA_CHECK_NEAR(masked.v(80, 68).x, 0.f, .015);
    deep_glow_tests::set(scene, host, "ref_mask_invert", 1);
    const auto inverted = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(inverted));
    AUREA_CHECK_NEAR(inverted.v(80, 20).x, 0.f, .015);
    AUREA_CHECK_NEAR(inverted.v(80, 68).x, 1.f, .015);
}

// Optional native-oracle frame output. Input is straight-alpha sRGB RGBA8,
// output is the renderer's premultiplied linear RGBA32F readback.
// Cases are text lines: name ref_radius=25 ref_view=0 ref_threshold=50.
AUREA_TEST(DeepGlowReferenceGpu, OracleFramesWhenRequested) {
    const char* inputPath = std::getenv("AUREA_DEEPGLOW_ORACLE_RGBA8");
    const char* outputPath = std::getenv("AUREA_DEEPGLOW_ORACLE_OUT");
    if (!inputPath || !outputPath) return;
    AUREA_REQUIRE_GPU();
    const char* widthText = std::getenv("AUREA_DEEPGLOW_ORACLE_WIDTH");
    const char* heightText = std::getenv("AUREA_DEEPGLOW_ORACLE_HEIGHT");
    const u32 width = widthText ? static_cast<u32>(std::strtoul(widthText, nullptr, 10)) : 256u;
    const u32 height = heightText ? static_cast<u32>(std::strtoul(heightText, nullptr, 10)) : 144u;
    AUREA_CHECK(width > 0 && height > 0 && width <= 8192 && height <= 8192);
    if (!width || !height || width > 8192 || height > 8192) return;
    ImagePixels input;
    input.width = width; input.height = height;
    input.rgba.resize(static_cast<usize>(width) * height * 4);
    std::ifstream source(inputPath, std::ios::binary);
    AUREA_CHECK(source.good());
    source.read(reinterpret_cast<char*>(input.rgba.data()), static_cast<std::streamsize>(input.rgba.size()));
    AUREA_CHECK_EQ(source.gcount(), static_cast<std::streamsize>(input.rgba.size()));
    if (source.gcount() != static_cast<std::streamsize>(input.rgba.size())) return;
    const bool exactSize = source.peek() == std::char_traits<char>::eof();
    AUREA_CHECK(exactSize);
    if (!exactSize) return;
    std::vector<std::string> cases;
    if (const char* casesPath = std::getenv("AUREA_DEEPGLOW_ORACLE_CASES")) {
        std::ifstream list(casesPath);
        AUREA_CHECK(list.good());
        for (std::string line; std::getline(list, line);) {
            if (!line.empty() && line.front() != '#') cases.push_back(std::move(line));
        }
    } else {
        for (const u32 radius : {5u, 25u, 100u, 500u})
            cases.push_back("deepglow-halo-r" + std::to_string(radius) + " ref_radius=" + std::to_string(radius));
        for (const u32 threshold : {0u, 50u, 100u}) for (const u32 smoothness : {0u, 50u, 100u}) {
            cases.push_back("deepglow-input-r25-t" + std::to_string(threshold) + "-s" + std::to_string(smoothness) +
                " ref_radius=25 ref_view=0 ref_threshold=" + std::to_string(threshold) +
                " ref_threshold_smooth=" + std::to_string(smoothness));
        }
    }
    AUREA_CHECK(!cases.empty());
    std::error_code directoryError;
    std::filesystem::create_directories(outputPath, directoryError);
    AUREA_CHECK(!directoryError);
    if (directoryError) return;
    Scene scene(width, height);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(std::move(input), width * .5f, height * .5f);
    const auto baseline = scene.render(FrameIndex{0}, 1, true);
    const auto write = [&](const std::string& name, const FloatImage& frame) {
        const auto file = std::filesystem::path(outputPath) / (name + ".rgba32f");
        std::ofstream stream(file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(frame.px.data()),
            static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
        AUREA_CHECK(stream.good());
        for (const f32 value : frame.px) AUREA_CHECK(std::isfinite(value));
    };
    write("input-color-fixture", baseline);
    auto& effect = scene.add_effect(layer, deep_glow_tests::key);
    const auto* parameters = gpu().effects.params(effect_type_id(deep_glow_tests::key));
    for (const std::string& line : cases) {
        initialize_instance(effect, *parameters);
        deep_glow_tests::set(scene, layer, "ref_source_opacity", 0);
        deep_glow_tests::set(scene, layer, "ref_dither_enabled", 0);
        std::istringstream fields(line);
        std::string name;
        fields >> name;
        const bool safeName = !name.empty() && name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == std::string::npos;
        AUREA_CHECK(safeName);
        if (!safeName) continue;
        for (std::string setting; fields >> setting;) {
            const usize separator = setting.find('=');
            AUREA_CHECK(separator != std::string::npos);
            if (separator == std::string::npos) continue;
            const auto id = setting.substr(0, separator);
            const u32 index = parameters->find(id.c_str());
            AUREA_CHECK_MSG(index != kInvalidIndex, id.c_str());
            if (index == kInvalidIndex) continue;
            char* end = nullptr;
            const f32 value = std::strtof(setting.c_str() + separator + 1, &end);
            AUREA_CHECK(end && *end == '\0' && std::isfinite(value));
            if (end && *end == '\0' && std::isfinite(value)) effect.params[index].constant = ParamValue::scalar(value);
        }
        const auto frame = scene.render(FrameIndex{0}, 1, true);
        write(name, frame);
        std::printf("    Deep Glow oracle %s %ux%u RGBA32F\n", name.c_str(), frame.width, frame.height);
    }
}
