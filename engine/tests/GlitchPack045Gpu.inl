// Pedidos de usuários da 0.4.5 (glitch): Glitchify completo (controles do
// plugin original, build/reference/glitchify/SPEC.md), Hyper Glitch, Block
// Glitch (estilo DaVinci) e Shift Channels (o do AE).

namespace glitch045 {

ImagePixels g045_pattern(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        const usize at = (static_cast<usize>(y) * w + x) * 4;
        px.rgba[at] = static_cast<u8>((x * 73 + y * 19) % 256);
        px.rgba[at + 1] = static_cast<u8>((x * 11 + y * 47) % 256);
        px.rgba[at + 2] = static_cast<u8>((x * 31 + y * 7) % 256);
    }
    return px;
}

f32 g045_diff(const FloatImage& a, const FloatImage& b) {
    f32 m = 0;
    for (usize i = 0; i < a.px.size() && i < b.px.size(); ++i) m = std::max(m, std::fabs(a.px[i] - b.px[i]));
    return m;
}

bool g045_finite(const FloatImage& a) {
    for (f32 v : a.px) if (!std::isfinite(v)) return false;
    return true;
}

u32 g045_param(const char* key, const char* id) {
    const ParameterRegistry* reg = gpu().effects.params(effect_type_id(key));
    if (!reg) return kInvalidIndex;
    for (u32 i = 0; i < reg->count(); ++i) if (std::strcmp(reg->at(i).id, id) == 0) return i;
    return kInvalidIndex;
}

} // namespace glitch045

AUREA_TEST(Glitch045Gpu, SignalIdentityIncludesIndependentSplitAndSyncControls) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto type = effect_type_id(effect_keys::kSignal);
    const Effect* effect = registry.find(type);
    const ParameterRegistry* params = registry.params(type);
    AUREA_CHECK(effect != nullptr && params != nullptr);
    if (!effect || !params) return;
    std::vector<ParamValue> values;
    for (u32 index = 0; index < params->count(); ++index) values.push_back(params->at(index).defaultValue);
    for (u32 index : {0u, 2u, 4u, 8u, 9u}) values[index] = ParamValue::scalar(0);
    EffectEval eval;
    eval.values = values.data();
    eval.count = static_cast<u32>(values.size());
    AUREA_CHECK(effect->is_identity(eval));
    values[9] = ParamValue::scalar(4);
    AUREA_CHECK(!effect->is_identity(eval));
    values[9] = ParamValue::scalar(0);
    values[8] = ParamValue::scalar(1);
    AUREA_CHECK(!effect->is_identity(eval));
    values[1] = ParamValue::scalar(0);
    AUREA_CHECK(effect->is_identity(eval));
    values[4] = ParamValue::scalar(10);
    AUREA_CHECK(!effect->is_identity(eval));
    values[7] = ParamValue::scalar(0);
    AUREA_CHECK(effect->is_identity(eval));
}

AUREA_TEST(Glitch045Gpu, SignalSplitRendersWhenBandNoiseAndDriftAreDisabled) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene scene(96, 64);
    const auto layer = scene.image(g045_pattern(96, 64), 48, 32);
    const FloatImage plain = scene.render(FrameIndex{12});
    auto& effect = scene.add_effect(layer, effect_keys::kSignal);
    for (u32 index : {0u, 2u, 4u, 8u, 9u}) effect.params[index].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(plain, scene.render(FrameIndex{12})) < 0.001f);
    effect.params[9].constant = ParamValue::scalar(12);
    const FloatImage split = scene.render(FrameIndex{12});
    AUREA_CHECK(g045_diff(plain, split) > 0.02f);
    AUREA_CHECK(g045_finite(split));
    (void)scene.render(FrameIndex{40});
    AUREA_CHECK(g045_diff(split, scene.render(FrameIndex{12})) < 0.001f);
    effect.params[7].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(plain, scene.render(FrameIndex{12})) < 0.001f);
}

AUREA_TEST(Glitch045Gpu, SignalDriftUsesLayerPixelsAtReducedPreviewResolution) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 64);
    ImagePixels pixels = uniform_image(128, 64, 255, 0, 0);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 64; x < 128; ++x) {
        const usize at = (static_cast<usize>(y) * 128 + x) * 4;
        pixels.rgba[at] = 0;
        pixels.rgba[at + 2] = 255;
    }
    const auto layer = scene.image(std::move(pixels), 64, 32);
    auto& effect = scene.add_effect(layer, effect_keys::kSignal);
    for (u32 index : {0u, 4u, 8u, 9u}) effect.params[index].constant = ParamValue::scalar(0);
    effect.params[2].constant = ParamValue::scalar(8);
    const FloatImage full = scene.render(FrameIndex{1});
    const FloatImage half = scene.render(FrameIndex{1}, 2);
    AUREA_CHECK(full.v(59, 32).z > 0.9f);
    AUREA_CHECK(half.v(29, 16).z > 0.9f);
    AUREA_CHECK(full.v(51, 32).x > 0.9f);
    AUREA_CHECK(half.v(25, 16).x > 0.9f);
}

AUREA_TEST(SignalReferenceGpu, ContractUsesObservedControlsWithoutChangingLegacyEffects) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* params = registry.params(effect_type_id(effect_keys::kSignalReference));
    AUREA_CHECK(params != nullptr);
    if (!params) return;
    AUREA_CHECK_EQ(params->count(), 35u);
    AUREA_CHECK_EQ(registry.params(effect_type_id(effect_keys::kSignal))->count(), 10u);
    AUREA_CHECK_EQ(registry.params(effect_type_id(effect_keys::kAnalogSignal))->count(), 16u);
    const struct { u32 index; const char* id; f32 value; } controls[] = {
        {0, "seed", 1}, {1, "signal_strength", 40}, {2, "amplification", 1},
        {3, "cutoff", 88}, {6, "hue_noise", 15}, {7, "chroma_loss", .24f},
        {13, "frequency", .62f}, {21, "tape_fade", 5}, {22, "tape_gamma", 3},
        {34, "scanline_blur", 1.5f},
    };
    for (const auto& control : controls) {
        AUREA_CHECK(std::strcmp(params->at(control.index).id, control.id) == 0);
        AUREA_CHECK_NEAR(params->at(control.index).defaultValue.v[0], control.value, 1e-6f);
    }
    AUREA_CHECK_NEAR(params->at(21).minValue, .01f, 1e-6f);
    AUREA_CHECK_NEAR(params->at(21).maxValue, 5, 1e-6f);
    AUREA_CHECK_NEAR(params->at(22).maxValue, 14, 1e-6f);
}

AUREA_TEST(SignalReferenceGpu, AnalogPipelineIsDeterministicAndItsStagesRespond) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene scene(128, 64);
    const auto layer = scene.image(g045_pattern(128, 64), 64, 32);
    auto& effect = scene.add_effect(layer, effect_keys::kSignalReference);
    const FloatImage base = scene.render(FrameIndex{30});
    AUREA_CHECK(g045_finite(base));
    (void)scene.render(FrameIndex{60});
    AUREA_CHECK(g045_diff(base, scene.render(FrameIndex{30})) < .001f);
    const struct { u32 index; f32 value; } controls[] = {
        {0, 2}, {2, 0}, {4, 40}, {5, 40}, {7, 1}, {9, 1}, {10, 2},
        {11, 1}, {23, 20}, {24, 5}, {26, 15}, {27, 0}, {28, 0}, {31, 0}, {34, 0},
    };
    for (const auto& control : controls) {
        const ParamValue before = effect.params[control.index].constant;
        effect.params[control.index].constant = ParamValue::scalar(control.value);
        const FloatImage changed = scene.render(FrameIndex{30});
        AUREA_CHECK(g045_finite(changed));
        AUREA_CHECK_MSG(g045_diff(base, changed) > .001f, "Signal control left the frame unchanged");
        effect.params[control.index].constant = before;
    }
}

AUREA_TEST(SignalReferenceGpu, TransmissionDistortionPreservesTheInputAlpha) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene scene(96, 64);
    scene.comp->set_transparent_background(true);
    ImagePixels pixels = g045_pattern(96, 64);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 96; ++x) {
        pixels.rgba[(static_cast<usize>(y) * 96 + x) * 4 + 3] = static_cast<u8>((x * 11 + y * 7) % 256);
    }
    const auto layer = scene.image(std::move(pixels), 48, 32);
    const FloatImage plain = scene.render(FrameIndex{30});
    auto& effect = scene.add_effect(layer, effect_keys::kSignalReference);
    effect.params[23].constant = ParamValue::scalar(15);
    effect.params[26].constant = ParamValue::scalar(10);
    effect.params[34].constant = ParamValue::scalar(0);
    const FloatImage changed = scene.render(FrameIndex{30});
    AUREA_CHECK(g045_finite(changed));
    for (usize index = 3; index < changed.px.size(); index += 4) {
        AUREA_CHECK_NEAR(changed.px[index], plain.px[index], .001f);
    }
}

AUREA_TEST(SignalReferenceGpu, ScanlineBlurMatchesObservedTwoDimensionalAlphaFalloff) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 64);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(uniform_image(64, 64, 255, 255, 255), 32, 32);
    (void)scene.add_effect(layer, effect_keys::kSignalReference);
    const FloatImage rendered = scene.render();
    AUREA_CHECK_NEAR(rendered.v(32, 0).w, 212.0f / 255.0f, .01f);
    AUREA_CHECK_NEAR(rendered.v(0, 32).w, 212.0f / 255.0f, .01f);
    AUREA_CHECK_NEAR(rendered.v(0, 0).w, 177.0f / 255.0f, .01f);
    AUREA_CHECK_NEAR(rendered.v(32, 32).w, 1, .001f);
}

AUREA_TEST(SignalReferenceGpu, RenderObservedReferenceCases) {
    const char* inputPath = std::getenv("AUREA_SIGNAL_REFERENCE_INPUT_RGBA");
    const char* outputPath = std::getenv("AUREA_SIGNAL_REFERENCE_REVIEW");
    if (!inputPath || !outputPath) return;
    AUREA_REQUIRE_GPU();
    ImagePixels input;
    const char* widthText = std::getenv("AUREA_SIGNAL_REFERENCE_WIDTH");
    const char* heightText = std::getenv("AUREA_SIGNAL_REFERENCE_HEIGHT");
    input.width = widthText ? static_cast<u32>(std::strtoul(widthText, nullptr, 10)) : 256u;
    input.height = heightText ? static_cast<u32>(std::strtoul(heightText, nullptr, 10)) : 144u;
    AUREA_CHECK(input.width > 0 && input.height > 0 && input.width <= 4096 && input.height <= 4096);
    if (!input.width || !input.height || input.width > 4096 || input.height > 4096) return;
    input.rgba.resize(static_cast<usize>(input.width) * input.height * 4);
    std::ifstream stream(inputPath, std::ios::binary);
    stream.read(reinterpret_cast<char*>(input.rgba.data()), static_cast<std::streamsize>(input.rgba.size()));
    AUREA_CHECK_EQ(stream.gcount(), static_cast<std::streamsize>(input.rgba.size()));
    if (!stream) return;
    const bool exactSize = stream.peek() == std::char_traits<char>::eof();
    AUREA_CHECK(exactSize);
    if (!exactSize) return;
    Scene scene(input.width, input.height);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(std::move(input), scene.comp->width() * .5f, scene.comp->height() * .5f);
    const std::string directory(outputPath);
    auto save = [&](const char* name, i64 frame) {
        const FloatImage rendered = scene.render(FrameIndex{frame}, 1, true);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        const std::filesystem::path stem = std::filesystem::path(directory) / std::filesystem::path(name).stem();
        std::ofstream floats(stem.string() + ".rgba32f", std::ios::binary);
        floats.write(reinterpret_cast<const char*>(rendered.px.data()),
            static_cast<std::streamsize>(rendered.px.size() * sizeof(f32)));
        AUREA_CHECK(floats.good());
        Image8 encoded;
        encoded.width = rendered.width;
        encoded.height = rendered.height;
        encoded.rgba.resize(rendered.px.size());
        for (usize index = 0; index < rendered.px.size(); index += 4) {
            const f32 alpha = std::clamp(rendered.px[index + 3], 0.0f, 1.0f);
            for (u32 channel = 0; channel < 3; ++channel) {
                const f32 color = alpha > 1e-6f ? rendered.px[index + channel] / alpha : 0;
                encoded.rgba[index + channel] = static_cast<u8>(std::lround(std::clamp(srgb_encode(color), 0.0f, 1.0f) * 255));
            }
            encoded.rgba[index + 3] = static_cast<u8>(std::lround(alpha * 255));
        }
        AUREA_CHECK(write_png(directory + "/" + name, encoded));
    };
    save("input-signal-fixture.png", 0);
    auto& effect = scene.add_effect(layer, effect_keys::kSignalReference);
    save("signal-default-frame0.png", 0);
    save("signal-default-frame1.png", 1);
    for (u32 index : {4u, 5u, 6u, 7u, 12u, 17u, 27u, 28u, 31u, 32u, 33u, 34u}) {
        effect.params[index].constant = ParamValue::scalar(0);
    }
    effect.params[1].constant = ParamValue::scalar(1);
    save("signal-minimal.png", 0);
}

AUREA_TEST(AdobeReferenceGpu, ControlContractsPreserveLegacyEffects) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* sharpen = registry.params(effect_type_id(effect_keys::kSharpenReference));
    const auto* edges = registry.params(effect_type_id(effect_keys::kFindEdgesReference));
    const auto* wave = registry.params(effect_type_id(effect_keys::kWaveWarpReference));
    AUREA_CHECK(sharpen != nullptr && edges != nullptr && wave != nullptr);
    if (!sharpen || !edges || !wave) return;
    AUREA_CHECK_EQ(sharpen->count(), 1u);
    AUREA_CHECK_EQ(edges->count(), 2u);
    AUREA_CHECK_EQ(wave->count(), 8u);
    AUREA_CHECK_NEAR(wave->at(1).defaultValue.v[0], 10, .0001f);
    AUREA_CHECK_NEAR(wave->at(2).defaultValue.v[0], 40, .0001f);
    AUREA_CHECK_NEAR(wave->at(3).defaultValue.v[0], 90, .0001f);
    AUREA_CHECK_NEAR(wave->at(4).defaultValue.v[0], 1, .0001f);
    AUREA_CHECK_NEAR(sharpen->at(0).defaultValue.v[0], 0, .0001f);
    AUREA_CHECK_NEAR(sharpen->at(0).hardMax, 4000, .0001f);
    AUREA_CHECK_NEAR(edges->at(0).defaultValue.v[0], 0, .0001f);
    AUREA_CHECK_NEAR(edges->at(1).defaultValue.v[0], 0, .0001f);
    AUREA_CHECK_EQ(registry.params(effect_type_id(effect_keys::kSharpen))->count(), 1u);
    AUREA_CHECK_EQ(registry.params(effect_type_id(effect_keys::kFindEdges))->count(), 4u);
    AUREA_CHECK_EQ(registry.params(effect_type_id(effect_keys::kWaveWarp))->count(), 11u);
    const auto* blocks = registry.params(effect_type_id("aurea.transition.block_dissolve_reference"));
    AUREA_CHECK(blocks != nullptr);
    if (blocks) {
        AUREA_CHECK_EQ(blocks->count(), 5u);
        AUREA_CHECK_NEAR(blocks->at(0).defaultValue.v[0], 0, .0001f);
        AUREA_CHECK_NEAR(blocks->at(1).defaultValue.v[0], 1, .0001f);
        AUREA_CHECK_NEAR(blocks->at(2).defaultValue.v[0], 1, .0001f);
        AUREA_CHECK(blocks->at(4).defaultValue.as_bool());
    }
    const auto* turbulent = registry.params(effect_type_id("aurea.distort.turbulent_displace_reference"));
    AUREA_CHECK(turbulent != nullptr);
    if (turbulent) {
        AUREA_CHECK_EQ(turbulent->count(), 12u);
        AUREA_CHECK_NEAR(turbulent->at(1).defaultValue.v[0], 50, .0001f);
        AUREA_CHECK_NEAR(turbulent->at(2).defaultValue.v[0], 100, .0001f);
        AUREA_CHECK_NEAR(turbulent->at(4).defaultValue.v[0], 1, .0001f);
        AUREA_CHECK_NEAR(turbulent->at(9).defaultValue.v[0], 1, .0001f);
    }
}

AUREA_TEST(AdobeReferenceGpu, FindEdgesInversionAndOriginalBlendHaveObservedEndpoints) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 48);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(uniform_image(64, 48, 80, 120, 160), 32, 24);
    const FloatImage input = scene.render();
    auto& effect = scene.add_effect(layer, effect_keys::kFindEdgesReference);
    const Vec4 white = scene.render().v(32, 24);
    AUREA_CHECK_NEAR(white.x, 1, .002f);
    AUREA_CHECK_NEAR(white.y, 1, .002f);
    AUREA_CHECK_NEAR(white.z, 1, .002f);
    effect.params[0].constant = ParamValue::scalar(1);
    const Vec4 black = scene.render().v(32, 24);
    AUREA_CHECK_NEAR(black.x, 0, .002f);
    AUREA_CHECK_NEAR(black.y, 0, .002f);
    AUREA_CHECK_NEAR(black.z, 0, .002f);
    effect.params[1].constant = ParamValue::scalar(1);
    AUREA_CHECK(glitch045::g045_diff(input, scene.render()) < .001f);
}

AUREA_TEST(AdobeReferenceGpu, SharpenCrossKernelIsFiniteAndPreservesConstantFields) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 48);
    const auto layer = scene.image(uniform_image(64, 48, 255, 255, 255), 32, 24);
    const FloatImage input = scene.render();
    auto& effect = scene.add_effect(layer, effect_keys::kSharpenReference);
    for (f32 amount : {0.0f, 16.0f, 100.0f, 4000.0f}) {
        effect.params[0].constant = ParamValue::scalar(amount);
        const FloatImage rendered = scene.render();
        AUREA_CHECK(glitch045::g045_finite(rendered));
        // The native kernel reads zero beyond the layer and brightens its
        // outermost pixels. Constant-field invariance applies to the interior.
        for (u32 y = 1; y + 1 < rendered.height; ++y) for (u32 x = 1; x + 1 < rendered.width; ++x) {
            const Vec4 actual = rendered.v(x, y), expected = input.v(x, y);
            AUREA_CHECK_NEAR(actual.x, expected.x, .002f);
            AUREA_CHECK_NEAR(actual.y, expected.y, .002f);
            AUREA_CHECK_NEAR(actual.z, expected.z, .002f);
            AUREA_CHECK_NEAR(actual.w, expected.w, .002f);
        }
    }
}

AUREA_TEST(AdobeReferenceGpu, DropShadowUsesObservedBearingAndIntegerWorldOffset) {
    AUREA_REQUIRE_GPU();
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(uniform_image(16, 16, 255, 255, 255), 128, 72);
    auto& effect = scene.add_effect(layer, "aurea.stylize.drop_shadow_reference");
    effect.params[0].constant = ParamValue::color(1, 1, 1, 1);
    effect.params[1].constant = ParamValue::scalar(100);
    effect.params[3].constant = ParamValue::scalar(5);
    effect.params[4].constant = ParamValue::scalar(0);
    effect.params[5].constant = ParamValue::boolean(true);
    struct Case { f32 direction; u32 left; u32 top; };
    for (const Case sample : {Case{0, 120, 59}, Case{90, 125, 64}, Case{135, 123, 67}}) {
        effect.params[2].constant = ParamValue::scalar(sample.direction);
        const auto frame = scene.render(FrameIndex{0}, 1, true);
        for (u32 y = 0; y < frame.height; ++y) for (u32 x = 0; x < frame.width; ++x) {
            const f32 expected = x >= sample.left && x < sample.left + 16 &&
                y >= sample.top && y < sample.top + 16 ? 1.f : 0.f;
            AUREA_CHECK_NEAR(frame.v(x, y).w, expected, .002f);
        }
    }
    effect.params[3].constant = ParamValue::scalar(0);
    effect.params[4].constant = ParamValue::scalar(2);
    const FloatImage softened = scene.render(FrameIndex{0}, 1, true);
    // Three fractional boxes reach three pixels even when softness is two.
    AUREA_CHECK_NEAR(softened.v(117, 72).w, .00957f, .00003f);
}

AUREA_TEST(AdobeReferenceGpu, ExportObservedShadowCasesWhenRequested) {
    const char* outputPath = std::getenv("AUREA_ADOBE_REFERENCE_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    std::error_code directoryError;
    std::filesystem::create_directories(outputPath, directoryError);
    AUREA_CHECK(!directoryError);
    if (directoryError) return;
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(uniform_image(16, 16, 255, 255, 255), 128, 72);
    auto& effect = scene.add_effect(layer, "aurea.stylize.drop_shadow_reference");
    effect.params[0].constant = ParamValue::color(1, 1, 1, 1);
    effect.params[1].constant = ParamValue::scalar(100);
    effect.params[5].constant = ParamValue::boolean(true);
    auto save = [&](const std::string& name) {
        const FloatImage frame = scene.render(FrameIndex{0}, 1, true);
        std::ofstream stream(std::filesystem::path(outputPath) / (name + ".rgba32f"), std::ios::binary);
        stream.write(reinterpret_cast<const char*>(frame.px.data()),
                     static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
        AUREA_CHECK(stream.good());
        for (f32 value : frame.px) AUREA_CHECK(std::isfinite(value));
    };
    effect.params[3].constant = ParamValue::scalar(5);
    for (u32 angle : {0u, 90u, 135u}) {
        effect.params[2].constant = ParamValue::scalar(static_cast<f32>(angle));
        save("drop-shadow-angle" + std::to_string(angle));
    }
    effect.params[3].constant = ParamValue::scalar(0);
    for (u32 softness : {2u, 5u, 10u, 30000u}) {
        effect.params[4].constant = ParamValue::scalar(static_cast<f32>(softness));
        save("drop-shadow-soft" + std::to_string(softness));
    }
}

AUREA_TEST(AdobeReferenceGpu, WaveWarpWaveformsAreDeterministicAndCycleBySeconds) {
    AUREA_REQUIRE_GPU();
    Scene scene(96, 64);
    const auto layer = scene.image(glitch045::g045_pattern(96, 64), 48, 32);
    const FloatImage input = scene.render();
    auto& effect = scene.add_effect(layer, effect_keys::kWaveWarpReference);
    const FloatImage sine = scene.render();
    AUREA_CHECK(glitch045::g045_diff(input, sine) > .02f);
    AUREA_CHECK(glitch045::g045_diff(sine, scene.render(FrameIndex{30})) < .002f);
    for (u32 wave = 1; wave < 9; ++wave) {
        effect.params[0].constant = ParamValue::scalar(static_cast<f32>(wave));
        const FloatImage changed = scene.render();
        AUREA_CHECK(glitch045::g045_finite(changed));
        AUREA_CHECK(glitch045::g045_diff(sine, changed) > .002f);
        (void)scene.render(FrameIndex{15});
        AUREA_CHECK(glitch045::g045_diff(changed, scene.render()) < .001f);
    }
    effect.params[1].constant = ParamValue::scalar(0);
    AUREA_CHECK(glitch045::g045_diff(input, scene.render()) < .001f);
}

AUREA_TEST(AdobeReferenceGpu, BlockDissolvePermutationIsNestedAndIndependentOfTime) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 48);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(uniform_image(64, 48, 255, 255, 255), 32, 24);
    const FloatImage input = scene.render();
    auto& effect = scene.add_effect(layer, "aurea.transition.block_dissolve_reference");
    AUREA_CHECK(glitch045::g045_diff(input, scene.render()) < .001f);
    effect.params[0].constant = ParamValue::scalar(25);
    effect.params[4].constant = ParamValue::scalar(0);
    const FloatImage first = scene.render();
    effect.params[0].constant = ParamValue::scalar(50);
    const FloatImage second = scene.render();
    AUREA_CHECK(glitch045::g045_finite(second));
    AUREA_CHECK(glitch045::g045_diff(second, scene.render(FrameIndex{60})) < .001f);
    for (usize index = 3; index < first.px.size(); index += 4) AUREA_CHECK(second.px[index] <= first.px[index] + .001f);
    effect.params[0].constant = ParamValue::scalar(100);
    const FloatImage removed = scene.render();
    for (usize index = 3; index < removed.px.size(); index += 4) AUREA_CHECK_NEAR(removed.px[index], 0, .001f);
    effect.params[0].constant = ParamValue::scalar(50);
    effect.params[1].constant = ParamValue::scalar(16);
    effect.params[2].constant = ParamValue::scalar(12);
    effect.params[4].constant = ParamValue::scalar(1);
    const FloatImage soft = scene.render();
    bool fractional = false;
    for (usize index = 3; index < soft.px.size(); index += 4) fractional |= soft.px[index] > .01f && soft.px[index] < .99f;
    AUREA_CHECK(fractional);
    effect.params[3].constant = ParamValue::scalar(10);
    const FloatImage feathered = scene.render();
    AUREA_CHECK(glitch045::g045_finite(feathered));
    AUREA_CHECK(glitch045::g045_diff(soft, feathered) > .002f);
}

AUREA_TEST(AdobeReferenceGpu, TurbulenceLatticeCyclesAndSeedAndTypesRespond) {
    AUREA_REQUIRE_GPU();
    Scene scene(96, 64);
    const auto layer = scene.image(glitch045::g045_pattern(96, 64), 48, 32);
    const FloatImage input = scene.render();
    auto& effect = scene.add_effect(layer, "aurea.distort.turbulent_displace_reference");
    const FloatImage first = scene.render();
    AUREA_CHECK(glitch045::g045_diff(first, input) > .002f);
    AUREA_CHECK(glitch045::g045_finite(first));
    effect.params[6].constant = ParamValue::scalar(1);
    const FloatImage cycle = scene.render();
    effect.params[5].constant = ParamValue::scalar(360);
    AUREA_CHECK(glitch045::g045_diff(cycle, scene.render()) < .001f);
    effect.params[5].constant = ParamValue::scalar(0);
    effect.params[8].constant = ParamValue::scalar(1);
    AUREA_CHECK(glitch045::g045_diff(cycle, scene.render()) > .002f);
    effect.params[8].constant = ParamValue::scalar(0);
    for (u32 type = 1; type < 9; ++type) {
        effect.params[0].constant = ParamValue::scalar(static_cast<f32>(type));
        const FloatImage changed = scene.render();
        AUREA_CHECK(glitch045::g045_finite(changed));
        AUREA_CHECK(glitch045::g045_diff(cycle, changed) > .002f);
    }
    effect.params[1].constant = ParamValue::scalar(0);
    AUREA_CHECK(glitch045::g045_diff(input, scene.render()) < .001f);
}

AUREA_TEST(AdobeReferenceGpu, ExportObservedNoiseLattice) {
    const char* outputPath = std::getenv("AUREA_ADOBE_LATTICE_REVIEW");
    if (!outputPath) return;
    struct Recorder final : EffectResources {
        std::vector<Vec4> texels;
        TextureHandle curve_lut(const CurveData&) noexcept override { return {}; }
        TextureHandle data_texture(u64, const Vec4* values, u32 width, u32 height) noexcept override {
            texels.assign(values, values + static_cast<usize>(width) * height);
            return {};
        }
    } recorder;
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto type = effect_type_id("aurea.distort.turbulent_displace_reference");
    const Effect* effect = registry.find(type);
    const ParameterRegistry* params = registry.params(type);
    AUREA_CHECK(effect != nullptr && params != nullptr);
    if (!effect || !params) return;
    std::vector<ParamValue> values;
    for (u32 index = 0; index < params->count(); ++index) values.push_back(params->at(index).defaultValue);
    EffectEval eval;
    eval.values = values.data();
    eval.count = static_cast<u32>(values.size());
    eval.resources = &recorder;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    auto save = [&](const char* name) {
        effect->resolve_resources(eval);
        AUREA_CHECK_EQ(recorder.texels.size(), 4096u);
        const auto path = std::filesystem::path(outputPath) / (std::string(name) + ".lattice32f");
        std::ofstream output(path, std::ios::binary);
        for (const Vec4& texel : recorder.texels) output.write(reinterpret_cast<const char*>(&texel.x), sizeof(f32));
        AUREA_CHECK(output.good());
    };
    save("turbulent-default");
    values[5] = ParamValue::scalar(90);
    save("turbulent-evolution90");
    values[5] = ParamValue::scalar(0);
    values[8] = ParamValue::scalar(1);
    save("turbulent-seed1");
}

AUREA_TEST(AdobeReferenceGpu, BlockDissolveValidatesGridBeforeAllocatingOrUploading) {
    struct Resources final : EffectResources {
        u32 uploads = 0;
        TextureHandle curve_lut(const CurveData&) noexcept override { return {}; }
        TextureHandle image_texture(u64, const u8*, u32, u32, bool) noexcept override {
            ++uploads;
            return {};
        }
    } resources;
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto type = effect_type_id("aurea.transition.block_dissolve_reference");
    const Effect* effect = registry.find(type);
    const ParameterRegistry* parameters = registry.params(type);
    AUREA_CHECK(effect && parameters);
    if (!effect || !parameters) return;
    std::vector<ParamValue> values;
    for (u32 index = 0; index < parameters->count(); ++index) values.push_back(parameters->at(index).defaultValue);
    values[0] = ParamValue::scalar(50);
    LayerPlacement placement;
    placement.layerWidth = UINT32_MAX;
    placement.layerHeight = 100;
    EffectEval eval;
    eval.values = values.data(); eval.count = static_cast<u32>(values.size());
    eval.placement = &placement; eval.resources = &resources;
    effect->resolve_resources(eval);
    AUREA_CHECK(eval.auxInfo.w < 0);
    AUREA_CHECK_EQ(resources.uploads, 0u);
    placement.layerWidth = placement.layerHeight = 1000000;
    effect->resolve_resources(eval);
    AUREA_CHECK(eval.auxInfo.w < 0);
    AUREA_CHECK_EQ(resources.uploads, 0u);
    values[0] = ParamValue::scalar(.000001f);
    AUREA_CHECK(effect->is_identity(eval));
}

AUREA_TEST(AdobeReferenceGpu, RenderObservedReferenceCases) {
    const char* inputPath = std::getenv("AUREA_ADOBE_REFERENCE_INPUT_RGBA");
    const char* outputPath = std::getenv("AUREA_ADOBE_REFERENCE_REVIEW");
    if (!inputPath || !outputPath) return;
    AUREA_REQUIRE_GPU();
    ImagePixels input;
    const char* widthText = std::getenv("AUREA_ADOBE_REFERENCE_WIDTH");
    const char* heightText = std::getenv("AUREA_ADOBE_REFERENCE_HEIGHT");
    input.width = widthText ? static_cast<u32>(std::strtoul(widthText, nullptr, 10)) : 256u;
    input.height = heightText ? static_cast<u32>(std::strtoul(heightText, nullptr, 10)) : 144u;
    AUREA_CHECK(input.width > 0 && input.height > 0 && input.width <= 4096 && input.height <= 4096);
    if (!input.width || !input.height || input.width > 4096 || input.height > 4096) return;
    input.rgba.resize(static_cast<usize>(input.width) * input.height * 4);
    std::ifstream stream(inputPath, std::ios::binary);
    stream.read(reinterpret_cast<char*>(input.rgba.data()), static_cast<std::streamsize>(input.rgba.size()));
    AUREA_CHECK_EQ(stream.gcount(), static_cast<std::streamsize>(input.rgba.size()));
    if (!stream) return;
    const bool exactSize = stream.peek() == std::char_traits<char>::eof();
    AUREA_CHECK(exactSize);
    if (!exactSize) return;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    for (const char* key : {effect_keys::kSharpenReference, effect_keys::kFindEdgesReference, effect_keys::kWaveWarpReference,
            "aurea.transition.block_dissolve_reference", "aurea.distort.turbulent_displace_reference"}) {
        Scene scene(input.width, input.height);
        scene.comp->set_transparent_background(true);
        const auto layer = scene.image(input, input.width * .5f, input.height * .5f);
        auto save = [&](const char* name, i64 frame = 0) {
            const FloatImage rendered = scene.render(FrameIndex{frame}, 1, true);
            const auto path = std::filesystem::path(outputPath) / (std::string(name) + ".rgba32f");
            std::ofstream floats(path, std::ios::binary);
            floats.write(reinterpret_cast<const char*>(rendered.px.data()),
                static_cast<std::streamsize>(rendered.px.size() * sizeof(f32)));
            AUREA_CHECK(floats.good());
        };
        save("input-adobe-fixture");
        auto& effect = scene.add_effect(layer, key);
        if (key == effect_keys::kSharpenReference) {
            effect.params[0].constant = ParamValue::scalar(16);
            save("sharpen-amount16");
            effect.params[0].constant = ParamValue::scalar(100);
            save("sharpen-amount100");
        } else if (key == effect_keys::kFindEdgesReference) {
            save("find-edges-default");
            effect.params[0].constant = ParamValue::scalar(1);
            save("find-edges-invert");
            effect.params[0].constant = ParamValue::scalar(0);
            effect.params[1].constant = ParamValue::scalar(.5f);
            save("find-edges-blend50");
        } else if (key == effect_keys::kWaveWarpReference) {
            save("wave-warp-default-frame0");
            save("wave-warp-default-frame15", 15);
            effect.params[7].constant = ParamValue::scalar(1);
            save("wave-warp-antialias2");
            effect.params[7].constant = ParamValue::scalar(2);
            save("wave-warp-antialias3");
            effect.params[7].constant = ParamValue::scalar(0);
            effect.params[0].constant = ParamValue::scalar(1);
            save("wave-warp-square");
            effect.params[0].constant = ParamValue::scalar(0);
            effect.params[5].constant = ParamValue::scalar(3);
            save("wave-warp-pin-left");
            effect.params[5].constant = ParamValue::scalar(1);
            save("wave-warp-pin-all");
            effect.params[5].constant = ParamValue::scalar(0);
            effect.params[6].constant = ParamValue::scalar(90);
            save("wave-warp-phase90");
            effect.params[1].constant = ParamValue::scalar(0);
            save("wave-warp-height0");
        } else if (std::strcmp(key, "aurea.transition.block_dissolve_reference") == 0) {
            save("block-dissolve-completion0");
            effect.params[0].constant = ParamValue::scalar(50);
            effect.params[1].constant = ParamValue::scalar(16);
            effect.params[2].constant = ParamValue::scalar(16);
            save("block-dissolve-completion50-block16-soft");
            effect.params[4].constant = ParamValue::scalar(0);
            save("block-dissolve-completion50-block16-hard");
            effect.params[4].constant = ParamValue::scalar(1);
            effect.params[3].constant = ParamValue::scalar(10);
            save("block-dissolve-completion50-block16-feather10");
            effect.params[0].constant = ParamValue::scalar(100);
            save("block-dissolve-completion100");
        } else {
            save("turbulent-displace-default");
            effect.params[0].constant = ParamValue::scalar(2);
            save("turbulent-displace-twist");
            effect.params[0].constant = ParamValue::scalar(3);
            save("turbulent-displace-smooth");
            effect.params[0].constant = ParamValue::scalar(0);
            effect.params[4].constant = ParamValue::scalar(2.5f);
            save("turbulent-displace-complexity2_5");
            effect.params[4].constant = ParamValue::scalar(1);
            effect.params[6].constant = ParamValue::scalar(1);
            effect.params[5].constant = ParamValue::scalar(360);
            save("turbulent-displace-cycle1-evolution360");
            effect.params[5].constant = ParamValue::scalar(0);
            effect.params[6].constant = ParamValue::scalar(0);
            effect.params[8].constant = ParamValue::scalar(1);
            save("turbulent-displace-seed1");
        }
    }
}

AUREA_TEST(Glitch045Gpu, GlitchifyTransitionAndNoiseWorkWithZeroMasterAmount) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene scene(96, 64);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(g045_pattern(96, 64), 48, 32);
    const FloatImage plain = scene.render();
    auto& effect = scene.add_effect(layer, effect_keys::kGlitchify);
    effect.params[17].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(plain, scene.render()) < .001f);
    effect.params[29].constant = ParamValue::scalar(100);
    const FloatImage removed = scene.render();
    for (usize index = 3; index < removed.px.size(); index += 4) {
        AUREA_CHECK_NEAR(removed.px[index], 0, .001f);
    }
    effect.params[29].constant = ParamValue::scalar(0);
    effect.params[58].constant = ParamValue::scalar(1);
    const FloatImage noise = scene.render();
    AUREA_CHECK(g045_diff(plain, noise) > .1f);
    for (u32 y = 0; y < noise.height; ++y) for (u32 x = 0; x < noise.width; ++x) {
        const Vec4 color = noise.v(x, y);
        AUREA_CHECK_NEAR(color.x, color.y, .001f);
        AUREA_CHECK_NEAR(color.y, color.z, .001f);
        AUREA_CHECK_NEAR(color.w, 1, .001f);
    }
}

// Os controles novos do Glitchify ficam DEPOIS dos antigos (projetos salvos
// continuam lendo os mesmos índices) e o padrão de cada um não muda o quadro:
// com todos eles nos valores neutros explícitos o quadro é o mesmo.
AUREA_TEST(Glitch045Gpu, GlitchifyNewControlsAreAppendedWithNeutralDefaults) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    AUREA_CHECK_EQ(g045_param(effect_keys::kGlitchify, "streak"), 34u);
    AUREA_CHECK_EQ(g045_param(effect_keys::kGlitchify, "transition_channel"), 35u);
    AUREA_CHECK_EQ(g045_param(effect_keys::kGlitchify, "wiggle_steps"), 59u);
    Scene s(96, 64);
    const auto id = s.image(g045_pattern(96, 64), 48, 32);
    auto& fx = s.add_effect(id, effect_keys::kGlitchify);
    fx.params[50].constant = ParamValue::scalar(24);
    fx.params[51].constant = ParamValue::scalar(16);
    const FloatImage base = s.render(FrameIndex{12});
    // Mexer e voltar ao padrão: o mesmo quadro.
    fx.params[44].constant = ParamValue::scalar(60);   // fatia
    fx.params[48].constant = ParamValue::scalar(80);   // blocos trocados
    const FloatImage changed = s.render(FrameIndex{12});
    fx.params[44].constant = ParamValue::scalar(0);
    fx.params[48].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(base, changed) > 0.02f);
    AUREA_CHECK(g045_diff(base, s.render(FrameIndex{12})) < 0.001f);
}

// Cada módulo novo muda o quadro, sem NaN, e o resultado é o mesmo ao voltar
// ao mesmo instante (determinístico pela semente); outra semente muda.
AUREA_TEST(Glitch045Gpu, GlitchifyNewModulesChangeTheFrameDeterministically) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(128, 96);
    const auto id = s.image(g045_pattern(128, 96), 64, 48);
    auto& fx = s.add_effect(id, effect_keys::kGlitchify);
    // Neutro: só os módulos novos atuam.
    for (u32 p : {1u, 2u, 3u, 9u, 13u, 14u, 15u, 16u}) fx.params[p].constant.v[0] = 0;
    fx.params[50].constant = ParamValue::scalar(24);   // bloco trocado menor que a camada
    fx.params[51].constant = ParamValue::scalar(16);
    const FloatImage base = s.render(FrameIndex{20});
    struct Probe { u32 index; f32 value; const char* name; };
    const Probe probes[] = {
        {44, 70, "fatia"}, {48, 90, "blocos trocados"}, {41, 20, "corrupcao"}, {53, 90, "compressao"},
        {54, 80, "pontilhado"}, {58, 1, "mostrar ruido"},
    };
    for (const Probe& probe : probes) {
        const ParamValue before = fx.params[probe.index].constant;
        fx.params[probe.index].constant = ParamValue::scalar(probe.value);
        if (probe.index == 41) fx.params[43].constant = ParamValue::scalar(100);   // opacidade da corrupção
        const FloatImage a = s.render(FrameIndex{20});
        (void)s.render(FrameIndex{40});
        const FloatImage again = s.render(FrameIndex{20});
        const f32 d = g045_diff(base, a);
        std::printf("\n    glitchify %s: dif %.3f ", probe.name, d);
        AUREA_CHECK(d > 0.02f);
        AUREA_CHECK(g045_finite(a));
        AUREA_CHECK(g045_diff(a, again) < 0.001f);
        fx.params[probe.index].constant = before;
        if (probe.index == 41) fx.params[43].constant = ParamValue::scalar(25);
    }
    // Separação de UM componente (verde-magenta) e escala por luma.
    fx.params[3].constant = ParamValue::scalar(12);
    fx.params[38].constant = ParamValue::scalar(2);
    const FloatImage green = s.render(FrameIndex{20});
    fx.params[38].constant = ParamValue::scalar(0);
    const FloatImage rgb = s.render(FrameIndex{20});
    AUREA_CHECK(g045_diff(green, rgb) > 0.02f);
    fx.params[3].constant = ParamValue::scalar(0);
    fx.params[11].constant = ParamValue::scalar(25);
    const FloatImage uniformScale = s.render(FrameIndex{20});
    fx.params[40].constant = ParamValue::scalar(1);
    const FloatImage lumaScale = s.render(FrameIndex{20});
    AUREA_CHECK(g045_diff(uniformScale, lumaScale) > 0.02f);
    fx.params[11].constant = ParamValue::scalar(0);
    fx.params[40].constant = ParamValue::scalar(0);
    // Semente: blocos trocados com outra semente saem em outro lugar.
    fx.params[48].constant = ParamValue::scalar(90);
    const FloatImage seedA = s.render(FrameIndex{20});
    fx.params[5].constant = ParamValue::scalar(321);
    const FloatImage seedB = s.render(FrameIndex{20});
    AUREA_CHECK(g045_diff(seedA, seedB) > 0.02f);
}

// Transição pelo canal (claridade): metade do caminho some os pixels escuros
// e mantém os claros; em 100 % tudo some.
AUREA_TEST(Glitch045Gpu, GlitchifyTransitionByChannelRemovesDarkPixelsFirst) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(64, 32);
    s.comp->set_transparent_background(true);
    ImagePixels px = uniform_image(64, 32, 0, 0, 0);
    for (u32 y = 0; y < 32; ++y) for (u32 x = 32; x < 64; ++x)
        for (u32 c = 0; c < 3; ++c) px.rgba[(static_cast<usize>(y) * 64 + x) * 4 + c] = 255;
    const auto id = s.image(std::move(px), 32, 16);
    auto& fx = s.add_effect(id, effect_keys::kGlitchify);
    for (u32 p : {1u, 2u, 3u, 9u, 13u, 14u, 15u, 16u}) fx.params[p].constant.v[0] = 0;
    fx.params[35].constant = ParamValue::scalar(6);    // claridade
    fx.params[29].constant = ParamValue::scalar(50);
    fx.params[30].constant = ParamValue::scalar(10);
    const FloatImage half = s.render(FrameIndex{3});
    AUREA_CHECK(half.v(8, 16).w < 0.01f);
    AUREA_CHECK(half.v(56, 16).w > 0.99f);
    fx.params[29].constant = ParamValue::scalar(100);
    const FloatImage gone = s.render(FrameIndex{3});
    for (usize i = 3; i < gone.px.size(); i += 4) AUREA_CHECK_NEAR(gone.px[i], 0.0f, 0.001f);
}

// Hyper Glitch: o padrão muda o quadro; em segundos (24 e 60 qps dão o mesmo
// quadro no mesmo instante); determinístico; intensidade 0 = intacto; a
// semente e a intensidade (animáveis) mudam o resultado.
AUREA_TEST(Glitch045Gpu, HyperGlitchIsHeavyDeterministicAndSeeded) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    std::vector<FloatImage> atOneSecond;
    for (int fps : {24, 60}) {
        Scene s(128, 96, fps);
        const auto id = s.image(g045_pattern(128, 96), 64, 48);
        const FloatImage plain = s.render(FrameIndex{fps});
        auto& fx = s.add_effect(id, effect_keys::kHyperGlitch);
        const FloatImage a = s.render(FrameIndex{fps});
        (void)s.render(FrameIndex{fps * 2});
        const FloatImage again = s.render(FrameIndex{fps});
        AUREA_CHECK(g045_diff(plain, a) > 0.1f);
        AUREA_CHECK(g045_finite(a));
        AUREA_CHECK(g045_diff(a, again) < 0.001f);
        atOneSecond.push_back(a);
        fx.params[1].constant = ParamValue::scalar(4242);
        AUREA_CHECK(g045_diff(a, s.render(FrameIndex{fps})) > 0.02f);
        fx.params[0].constant = ParamValue::scalar(0);
        AUREA_CHECK(g045_diff(plain, s.render(FrameIndex{fps})) < 0.001f);
    }
    AUREA_CHECK(g045_diff(atOneSecond[0], atOneSecond[1]) < 0.001f);
}

// Block Glitch: blocos inteiros deslocados (o padrão muda o quadro), a
// direção vertical dá outro resultado, quantidade 0 = intacto, determinístico.
AUREA_TEST(Glitch045Gpu, BlockGlitchDisplacesBlocksDeterministically) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(160, 96);
    const auto id = s.image(g045_pattern(160, 96), 80, 48);
    const FloatImage plain = s.render(FrameIndex{10});
    auto& fx = s.add_effect(id, effect_keys::kBlockGlitch);
    fx.params[0].constant = ParamValue::scalar(80);
    const FloatImage h = s.render(FrameIndex{10});
    (void)s.render(FrameIndex{33});
    AUREA_CHECK(g045_diff(h, s.render(FrameIndex{10})) < 0.001f);
    AUREA_CHECK(g045_diff(plain, h) > 0.1f);
    AUREA_CHECK(g045_finite(h));
    fx.params[8].constant = ParamValue::scalar(1);
    AUREA_CHECK(g045_diff(h, s.render(FrameIndex{10})) > 0.02f);
    fx.params[9].constant = ParamValue::scalar(77);
    const FloatImage otherSeed = s.render(FrameIndex{10});
    fx.params[9].constant = ParamValue::scalar(3);
    AUREA_CHECK(g045_diff(otherSeed, s.render(FrameIndex{10})) > 0.02f);
    fx.params[0].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(plain, s.render(FrameIndex{10})) < 0.001f);
}

// Shift Channels: o padrão (cada canal do próprio) não muda nada; vermelho do
// verde etc. troca exatamente; "cheio"/"desligado" e a luminância.
AUREA_TEST(Glitch045Gpu, ShiftChannelsTakesEachChannelFromTheChosenSource) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(32, 32);
    s.comp->set_transparent_background(true);
    const auto id = s.image(uniform_image(32, 32, 200, 100, 30), 16, 16);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, effect_keys::kShiftChannels);
    AUREA_CHECK(g045_diff(plain, s.render()) < 0.001f);
    const Vec4 o = plain.v(16, 16);
    fx.params[1].constant = ParamValue::scalar(2);   // vermelho ← verde
    fx.params[2].constant = ParamValue::scalar(3);   // verde ← azul
    fx.params[3].constant = ParamValue::scalar(1);   // azul ← vermelho
    const Vec4 r = s.render().v(16, 16);
    AUREA_CHECK_NEAR(r.x, o.y, 0.01f);
    AUREA_CHECK_NEAR(r.y, o.z, 0.01f);
    AUREA_CHECK_NEAR(r.z, o.x, 0.01f);
    fx.params[1].constant = ParamValue::scalar(8);   // cheio
    fx.params[2].constant = ParamValue::scalar(9);   // desligado
    fx.params[3].constant = ParamValue::scalar(4);   // luminância
    const Vec4 f = s.render().v(16, 16);
    AUREA_CHECK_NEAR(f.x, 1.0f, 0.01f);
    AUREA_CHECK_NEAR(f.y, 0.0f, 0.01f);
    AUREA_CHECK(f.z > o.z && f.z < o.x);
    fx.params[0].constant = ParamValue::scalar(9);   // alfa desligado
    AUREA_CHECK_NEAR(s.render().v(16, 16).w, 0.0f, 0.01f);
}
