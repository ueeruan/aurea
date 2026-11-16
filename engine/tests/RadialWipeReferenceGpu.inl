namespace radial_wipe_tests {
constexpr const char* key = "aurea.transition.radial_wipe_reference";
void set(Scene& scene, LayerId layer, const char* id, f32 value) {
    const auto* parameters = gpu().effects.params(effect_type_id(key));
    const u32 index = parameters->find(id);
    AUREA_CHECK_MSG(index != kInvalidIndex, id);
    if (index != kInvalidIndex)
        scene.comp->layer(layer)->effects.back().params[index].constant = ParamValue::scalar(value);
}
}

AUREA_TEST(RadialWipeReference, NativeParameterDefaultsAndLegacyInstancesRemainSeparate) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* parameters = registry.params(effect_type_id(radial_wipe_tests::key));
    const auto* legacy = registry.params(effect_type_id(effect_keys::kRadialWipe));
    AUREA_CHECK(parameters && legacy);
    if (!parameters || !legacy) return;
    AUREA_CHECK_EQ(parameters->count(), 5u);
    AUREA_CHECK_EQ(legacy->count(), 6u);
    AUREA_CHECK_NEAR(parameters->at(0).defaultValue.v[0], 0, 1e-6);
    AUREA_CHECK_NEAR(legacy->at(0).defaultValue.v[0], 50, 1e-6);
    AUREA_CHECK_EQ(parameters->at(3).enumCount, 3u);
    AUREA_CHECK_NEAR(parameters->at(4).typed_max(), 32000, 1e-6);
}

AUREA_TEST(RadialWipeReferenceGpu, ThreeDirectionsAndEndpointsPreservePremultiplication) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 64);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.solid(64, 64, {.8f, .4f, .2f, .5f}, 32, 32);
    const auto plain = scene.render();
    scene.add_effect(layer, radial_wipe_tests::key);
    AUREA_CHECK(image_diff(plain, scene.render()) < 1e-6f);
    radial_wipe_tests::set(scene, layer, "completion", 25);
    const auto checkPixel = [&](const FloatImage& frame, u32 x, u32 y, bool keep) {
        const f32* actual = frame.at(x, y);
        const f32* source = plain.at(x, y);
        for (u32 component = 0; component < 4; ++component)
            AUREA_CHECK_NEAR(actual[component], keep ? source[component] : 0.f, .005);
    };
    const auto clockwise = scene.render();
    checkPixel(clockwise, 48, 16, false);
    checkPixel(clockwise, 16, 16, true);
    checkPixel(clockwise, 48, 48, true);
    radial_wipe_tests::set(scene, layer, "direction", 1);
    const auto counterclockwise = scene.render();
    checkPixel(counterclockwise, 16, 16, false);
    checkPixel(counterclockwise, 48, 16, true);
    checkPixel(counterclockwise, 16, 48, true);
    radial_wipe_tests::set(scene, layer, "direction", 2);
    const auto both = scene.render();
    checkPixel(both, 32, 8, false);
    checkPixel(both, 8, 32, true);
    checkPixel(both, 56, 32, true);
    checkPixel(both, 32, 56, true);
    radial_wipe_tests::set(scene, layer, "completion", 75);
    const auto both75 = scene.render();
    checkPixel(both75, 32, 56, true);
    checkPixel(both75, 32, 8, false);
    checkPixel(both75, 8, 32, false);
    radial_wipe_tests::set(scene, layer, "completion", 100);
    const auto empty = scene.render();
    for (const f32 value : empty.px) AUREA_CHECK(std::abs(value) < 1e-6f);
}

AUREA_TEST(RadialWipeReferenceGpu, SpatialFeatherIsFiniteAndPreservesSourceColorRatios) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 64);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.solid(64, 64, {.8f, .4f, .2f, .5f}, 32, 32);
    const auto plain = scene.render();
    scene.add_effect(layer, radial_wipe_tests::key);
    radial_wipe_tests::set(scene, layer, "completion", 25);
    radial_wipe_tests::set(scene, layer, "feather", 12);
    const auto first = scene.render(FrameIndex{0}, 1, true);
    const auto repeated = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(first));
    AUREA_CHECK(image_diff(first, repeated) < 1e-6f);
    const Vec4 edge = first.v(32, 12);
    AUREA_CHECK(edge.w > .05f && edge.w < .45f);
    const Vec4 original = plain.v(32, 12);
    AUREA_CHECK_NEAR(edge.x / edge.w, original.x / original.w, .01);
    AUREA_CHECK_NEAR(edge.y / edge.w, original.y / original.w, .01);
    AUREA_CHECK_NEAR(edge.z / edge.w, original.z / original.w, .01);
    AUREA_CHECK(first.v(8, 56).w > .45f);
    const auto preview = scene.render(FrameIndex{0}, 2);
    AUREA_CHECK(all_finite(preview));
    AUREA_CHECK(preview.v(16, 6).w > .04f && preview.v(16, 6).w < .46f);
}

AUREA_TEST(RadialWipeReferenceGpu, RenderObservedReferenceCasesWhenRequested) {
    const char* inputPath = std::getenv("AUREA_ADOBE_REFERENCE_INPUT_RGBA");
    const char* outputPath = std::getenv("AUREA_ADOBE_REFERENCE_REVIEW");
    if (!inputPath || !outputPath) return;
    AUREA_REQUIRE_GPU();
    const char* widthText = std::getenv("AUREA_ADOBE_REFERENCE_WIDTH");
    const char* heightText = std::getenv("AUREA_ADOBE_REFERENCE_HEIGHT");
    ImagePixels input;
    input.width = widthText ? static_cast<u32>(std::strtoul(widthText, nullptr, 10)) : 256u;
    input.height = heightText ? static_cast<u32>(std::strtoul(heightText, nullptr, 10)) : 144u;
    AUREA_CHECK(input.width && input.height && input.width <= 4096 && input.height <= 4096);
    if (!input.width || !input.height || input.width > 4096 || input.height > 4096) return;
    input.rgba.resize(static_cast<usize>(input.width) * input.height * 4);
    std::ifstream source(inputPath, std::ios::binary);
    source.read(reinterpret_cast<char*>(input.rgba.data()), static_cast<std::streamsize>(input.rgba.size()));
    AUREA_CHECK_EQ(source.gcount(), static_cast<std::streamsize>(input.rgba.size()));
    if (source.gcount() != static_cast<std::streamsize>(input.rgba.size())) return;
    const bool exactSize = source.peek() == std::char_traits<char>::eof();
    AUREA_CHECK(exactSize);
    if (!exactSize) return;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    AUREA_CHECK(!error);
    if (error) return;
    Scene scene(input.width, input.height);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(std::move(input), scene.comp->width() * .5f, scene.comp->height() * .5f);
    auto& effect = scene.add_effect(layer, radial_wipe_tests::key);
    effect.params[1].constant = ParamValue::scalar(37);
    effect.params[2].constant = ParamValue::vec2(80.f / scene.comp->width(), 50.f / scene.comp->height());
    for (u32 direction = 0; direction < 3; ++direction) for (u32 completion : {25u, 75u}) {
        for (u32 feather : {0u, 20u, 100u}) {
            effect.params[0].constant = ParamValue::scalar(static_cast<f32>(completion));
            effect.params[3].constant = ParamValue::scalar(static_cast<f32>(direction));
            effect.params[4].constant = ParamValue::scalar(static_cast<f32>(feather));
            const auto frame = scene.render(FrameIndex{0}, 1, true);
            AUREA_CHECK(all_finite(frame));
            const std::string name = "radial-d" + std::to_string(direction + 1) + "-c" +
                std::to_string(completion) + "-f" + std::to_string(feather);
            const auto path = std::filesystem::path(outputPath) / (name + ".rgba32f");
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(frame.px.data()),
                static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
            AUREA_CHECK(output.good());
        }
    }
}
