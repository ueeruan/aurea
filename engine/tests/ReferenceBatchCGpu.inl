// AE reference batch C: Depth Field, Smear (ADBE SCHMEAR), Glitchify and the
// Extrude registration. The AE frames live in build/reference/effects-20261115
// (batch-c/runtime and runtime/frames); the numbers below are the REA-observed
// contracts and values verified against those frames.
namespace batch_c_tests {
constexpr const char* kDepthField = "aurea.ai.depth_field_reference";
constexpr const char* kSmear = "aurea.distort.smear_mask_reference";

u32 index_of(const char* key, const char* id) {
    const auto* parameters = gpu().effects.params(effect_type_id(key));
    const u32 index = parameters ? parameters->find(id) : kInvalidIndex;
    AUREA_CHECK_MSG(index != kInvalidIndex, id);
    return index;
}

void add_rect_mask(Layer& layer, f32 x0, f32 y0, f32 x1, f32 y1) {
    Mask m;
    m.id = static_cast<u32>(layer.masks.size());
    m.operation = MaskOperation::None;
    for (Vec2 p : {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}}) m.points.push_back({p, {}, {}});
    layer.masks.push_back(std::move(m));
}
} // namespace batch_c_tests

// Native noise engine (REA 0x180067870/0x180067A50/0x180068500): the values
// below match the AE frame glitchify-amount0-show-noise (seed 0, t = 0,
// 256×144) byte for byte (batch-c/models/glitchify_noise.py).
AUREA_TEST(GlitchifyReference, NoiseEngineMatchesTheNativeShowNoiseFrame) {
    using namespace glitchify_reference;
    const auto p = permutation(0);
    AUREA_CHECK_EQ(p[0], 187u);
    AUREA_CHECK_EQ(p[1], 232u);
    AUREA_CHECK_EQ(p[5], 29u);
    auto mapped = [&](f32 x, f32 y) {
        f32 v = sample(p, x, y, 0, 144, 2, 10, 1.5f) * 256;
        if (v > 256 * .9f) v = v * .2f + 256 * .9f * .8f;
        if (v < 256 * .1f) v = v * .2f + 256 * .1f * .8f;
        return static_cast<i32>(std::clamp(v, 0.0f, 255.0f));
    };
    AUREA_CHECK_EQ(mapped(0, 0), 128);
    AUREA_CHECK_EQ(mapped(1, 0), 185);
    AUREA_CHECK_EQ(mapped(2, 0), 230);
    AUREA_CHECK_EQ(mapped(100, 60), 254);
    AUREA_CHECK_EQ(mapped(105, 60), 188);
    AUREA_CHECK_EQ(mapped(200, 130), 19);
    AUREA_CHECK_EQ(mapped(37, 90), 206);
    AUREA_CHECK_NEAR(sample(permutation(7), 50, 50, 3.5f, 144, 2, 10, 1.5f), 1.1800565f, 2e-4);
    const f32 w = wiggle(p, 0xd0a, 0.25f, 2, 144, 2, 10, 1.5f);
    AUREA_CHECK(w == 0.0f || w == 0.5f || w == -0.5f || w == 1.0f || w == -1.0f);
}

AUREA_TEST(GlitchifyReference, ContractUsesTheNativeControlsWithoutChangingLegacy) {
    using namespace glitchify_reference;
    const auto* params = gpu().effects.params(effect_type_id(kKey));
    AUREA_CHECK(params != nullptr);
    if (!params) return;
    AUREA_CHECK_EQ(params->count(), static_cast<u32>(kCount));
    AUREA_CHECK_EQ(kCount, 65u);
    AUREA_CHECK_NEAR(params->at(kAmount).defaultValue.v[0], 50, 1e-6);
    AUREA_CHECK_NEAR(params->at(kSpeed).defaultValue.v[0], 50, 1e-6);
    AUREA_CHECK_EQ(params->at(kTransitionChannel).defaultValue.as_enum(), 5u);     // Lightness
    AUREA_CHECK_EQ(params->at(kSplitChannel).defaultValue.as_enum(), 7u);          // Saturation
    AUREA_CHECK_EQ(params->at(kSliceMode).defaultValue.as_enum(), kTransferDivide);
    AUREA_CHECK_EQ(params->at(kImageMode).defaultValue.as_enum(), kTransferOverlay);
    AUREA_CHECK(!params->at(kCompressionEnabled).defaultValue.as_bool());
    AUREA_CHECK(params->at(kRepeatEdges).defaultValue.as_bool());
    const auto* legacy = gpu().effects.params(effect_type_id(effect_keys::kGlitchify));
    AUREA_CHECK(legacy && legacy->count() == 60u);
}

AUREA_TEST(GlitchifyReferenceGpu, ShowNoiseAndModulesAreDeterministic) {
    AUREA_REQUIRE_GPU();
    using namespace glitchify_reference;
    Scene scene(160, 96);
    scene.comp->set_transparent_background(true);
    auto pixels = reference_image(160, 96);
    const LayerId layer = scene.image(std::move(pixels), 80, 48);
    const auto plain = scene.render();
    auto& fx = scene.add_effect(layer, kKey);
    fx.params[kAmount].constant = ParamValue::scalar(100);
    fx.params[kPositionWiggle].constant = ParamValue::scalar(30);
    const auto glitched = scene.render(FrameIndex{7});
    AUREA_CHECK(image_diff(plain, glitched) > 1e-3f);
    for (const f32 v : glitched.px) AUREA_CHECK(std::isfinite(v));
    AUREA_CHECK(image_diff(glitched, scene.render(FrameIndex{7})) < 1e-6f);
    fx.params[kShowNoise].constant = ParamValue::scalar(1);
    const auto noise = scene.render();
    bool grey = true;
    for (usize i = 0; i + 3 < noise.px.size(); i += 4) {
        if (noise.px[i + 3] < .5f) continue;
        grey = grey && std::fabs(noise.px[i] - noise.px[i + 1]) < 1e-3f && std::fabs(noise.px[i + 1] - noise.px[i + 2]) < 1e-3f;
    }
    AUREA_CHECK(grey);
}

AUREA_TEST(DepthFieldReference, ContractAndIdentityWithoutRadius) {
    using namespace batch_c_tests;
    const auto* params = gpu().effects.params(effect_type_id(kDepthField));
    AUREA_CHECK(params != nullptr);
    if (!params) return;
    AUREA_CHECK_NEAR(params->at(index_of(kDepthField, "focal_bias")).defaultValue.v[0], 50, 1e-6);
    AUREA_CHECK_NEAR(params->at(index_of(kDepthField, "max_radius")).defaultValue.v[0], 0, 1e-6);
    Scene scene(96, 64);
    const LayerId layer = scene.image(reference_image(96, 64), 48, 32);
    const auto plain = scene.render();
    scene.add_effect(layer, kDepthField);
    AUREA_CHECK(image_diff(plain, scene.render()) < 1e-6f);   // radius 0: the native copies the input
}

AUREA_TEST(SmearReference, SolverMovesTheSourceInsideTheBoundaryOnly) {
    using namespace smear_reference;
    Inputs in;
    in.sourceVertices = {{40, 40}, {60, 40}, {60, 60}, {40, 60}};
    in.source = in.sourceVertices;
    in.boundary = {{10, 10}, {90, 10}, {90, 90}, {10, 90}};
    in.offset = {60, 50};
    in.percent = 1;
    in.elasticity = 3;
    const Grid grid = solve(in, 64);
    AUREA_CHECK(!grid.empty());
    if (grid.empty()) return;
    auto at = [&](Vec2 p) {
        const u32 x = static_cast<u32>(std::round((p.x - grid.area.x) / grid.cell));
        const u32 y = static_cast<u32>(std::round((p.y - grid.area.y) / grid.cell));
        return grid.displacement[static_cast<usize>(y) * grid.width + x];
    };
    // Inside the moved source the pixel comes from 10 px to the left.
    const Vec2 inside = at({60, 50});
    AUREA_CHECK_NEAR(inside.x, -10, grid.cell * 1.01);
    AUREA_CHECK_NEAR(inside.y, 0, grid.cell * 1.01);
    // Outside the boundary nothing moves.
    const Vec2 outside = at({grid.area.x, grid.area.y});
    AUREA_CHECK_NEAR(outside.x, 0, 1e-6);
    AUREA_CHECK_NEAR(outside.y, 0, 1e-6);
    // Between, the displacement relaxes (smaller than the source move).
    const Vec2 between = at({80, 50});
    AUREA_CHECK(between.x < 0 && between.x > -10);
    in.percent = 0;
    AUREA_CHECK(solve(in, 64).empty());
}

AUREA_TEST(SmearReferenceGpu, MasksDriveTheDistortion) {
    AUREA_REQUIRE_GPU();
    using namespace batch_c_tests;
    Scene scene(96, 96);
    const LayerId id = scene.image(reference_image(96, 96), 48, 48);
    const auto plain = scene.render();
    auto& fx = scene.add_effect(id, kSmear);
    fx.params[smear_reference::kPercent].constant = ParamValue::scalar(100);
    fx.params[smear_reference::kMaskOffset].constant = ParamValue::vec2(.6f, .5f);
    AUREA_CHECK(image_diff(plain, scene.render()) < 1e-6f);   // no masks: nothing moves
    add_rect_mask(*scene.comp->layer(id), 36, 36, 60, 60);
    add_rect_mask(*scene.comp->layer(id), 8, 8, 88, 88);
    fx.params[smear_reference::kSourceMask].constant = ParamValue::scalar(1);
    fx.params[smear_reference::kBoundaryMask].constant = ParamValue::scalar(2);
    const auto smeared = scene.render();
    AUREA_CHECK(image_diff(plain, smeared) > 1e-3f);
    for (const f32 v : smeared.px) AUREA_CHECK(std::isfinite(v));
}

// The GPU noise engine equals the CPU rule (and so the AE frame): Show noise
// engine on a 256×144 layer at t = 0, seed 0. The bytes are sRGB-encoded (a
// standard 8 bpc AE project), so the linear output is their sRGB decode.
AUREA_TEST(GlitchifyReferenceGpu, ShowNoiseMatchesTheNativeBytes) {
    AUREA_REQUIRE_GPU();
    using namespace glitchify_reference;
    Scene scene(256, 144);
    const LayerId layer = scene.image(reference_image(256, 144), 128, 72);
    auto& fx = scene.add_effect(layer, kKey);
    fx.params[kShowNoise].constant = ParamValue::scalar(1);
    const auto noise = scene.render();
    const struct { u32 x, y, byte; } expected[] = {{0, 0, 128}, {1, 0, 185}, {2, 0, 230}, {100, 60, 254},
                                                   {105, 60, 188}, {200, 130, 19}, {37, 90, 206}};
    for (const auto& e : expected) {
        const Vec4 c = noise.v(e.x, e.y);
        const f32 s = static_cast<f32>(e.byte) / 255.0f;
        const f32 linear = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
        const f32 tolerance = std::max(4.5e-3f, 1.5f * (linear - [&] {   // ±1 byte in the encoded domain
            const f32 lower = std::max(0.0f, s - 1.0f / 255.0f);
            return lower <= 0.04045f ? lower / 12.92f : std::pow((lower + 0.055f) / 1.055f, 2.4f);
        }()));
        AUREA_CHECK_NEAR(c.x, linear, tolerance);
        AUREA_CHECK_NEAR(c.w, 1, 1e-3);
    }
}

// Image Glitch layers are transparent outside what they glitch (AE 26.5 photo
// frames, build/reference/glitchify-ae/iso): the Divide slice only touches
// its band (top = trunc(Position·(H − 1)) without shift, height = trunc(H·a)),
// Glitch Block only its copied cells, and the default stack keeps the image's
// tone (no wash-out from dividing the whole frame by itself).
AUREA_TEST(GlitchifyReferenceGpu, ImageGlitchOnlyTouchesItsBandAndBlocks) {
    AUREA_REQUIRE_GPU();
    using namespace glitchify_reference;
    constexpr u32 W = 160, H = 96;
    auto setup = [&](Scene& scene) -> EffectInstance& {
        const LayerId id = scene.image(reference_image(W, H), W * 0.5f, H * 0.5f);
        return scene.add_effect(id, kKey);
    };
    Scene plainScene(W, H);
    (void)plainScene.image(reference_image(W, H), W * 0.5f, H * 0.5f);
    const auto plain = plainScene.render(FrameIndex{30});
    auto row_diff = [&](const FloatImage& a, u32 y) {
        f32 d = 0;
        for (u32 x = 0; x < W; ++x) for (u32 c = 0; c < 3; ++c)
            d = std::max(d, std::fabs(a.px[(static_cast<usize>(y) * W + x) * 4 + c] - plain.px[(static_cast<usize>(y) * W + x) * 4 + c]));
        return d;
    };
    {
        Scene scene(W, H);
        auto& fx = setup(scene);
        fx.params[kChannelEnabled].constant = ParamValue::scalar(0);
        fx.params[kColorEnabled].constant = ParamValue::scalar(0);
        fx.params[kPixelStreak].constant = ParamValue::scalar(0);
        fx.params[kBlockAmount].constant = ParamValue::scalar(0);
        fx.params[kSliceShift].constant = ParamValue::scalar(0);
        const auto sliced = scene.render(FrameIndex{30});
        const u32 top = static_cast<u32>((H - 1) / 2), height = static_cast<u32>(H * 0.25f);
        f32 inside = 0, outside = 0;
        for (u32 y = 0; y < H; ++y) {
            if (y >= top && y < top + height) inside = std::max(inside, row_diff(sliced, y));
            else outside = std::max(outside, row_diff(sliced, y));
        }
        AUREA_CHECK(outside < 2e-3f);
        AUREA_CHECK(inside > 1e-2f);
    }
    {
        Scene scene(W, H);
        auto& fx = setup(scene);
        fx.params[kChannelEnabled].constant = ParamValue::scalar(0);
        fx.params[kColorEnabled].constant = ParamValue::scalar(0);
        fx.params[kPixelStreak].constant = ParamValue::scalar(0);
        fx.params[kSliceAmount].constant = ParamValue::scalar(0);
        fx.params[kBlockAmount].constant = ParamValue::scalar(0);
        AUREA_CHECK(image_diff(plain, scene.render(FrameIndex{30})) < 2e-3f);   // no block chosen: identity
    }
    {
        Scene scene(W, H);
        (void)setup(scene);
        const auto glitched = scene.render(FrameIndex{30});
        f64 a = 0, b = 0;
        for (usize i = 0; i < plain.px.size(); i += 4) for (usize c = 0; c < 3; ++c) { a += plain.px[i + c]; b += glitched.px[i + c]; }
        AUREA_CHECK(b < a * 1.35);   // the old linear stack brightened the frame far more
    }
}

// Writes Aurea renders of the batch-c/runtime/render-batch-c.jsx Smear cases
// (AUREA_BATCH_C_REVIEW = output folder); compare-batch-c.py diffs them with
// the decoded AE 2027 frames. Returns without rendering when unset.
AUREA_TEST(SmearReferenceGpu, RenderObservedReferenceCases) {
    const char* outputPath = std::getenv("AUREA_BATCH_C_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    using namespace batch_c_tests;
    const std::filesystem::path out(outputPath);
    std::error_code error;
    std::filesystem::create_directories(out, error);
    auto fixture = [](bool checker) {
        ImagePixels image;
        image.width = 256;
        image.height = 144;
        image.rgba.assign(256 * 144 * 4, 255);
        static const u8 colors[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                        {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
        for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 256; ++x) for (u32 c = 0; c < 3; ++c)
            image.rgba[(static_cast<usize>(y) * 256 + x) * 4 + c] =
                checker ? (((x / 16 + y / 16) % 2) ? 255 : 0) : colors[x / 32][c];
        return image;
    };
    auto save = [](const FloatImage& frame, const std::filesystem::path& path) {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(frame.px.data()), static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
    };
    struct Case { const char* name; bool checker; int masks; Vec2 offset; f32 rotation, scale, percent; u32 elasticity, interpolation; };
    const Case cases[] = {
        {"smear-identity", true, 0, {128, 72}, 0, 1, 0, 0, 1},
        {"smear-move", true, 0, {152, 80}, 0, 1, 100, 0, 1},
        {"smear-move-p50", true, 0, {152, 80}, 0, 1, 50, 0, 1},
        {"smear-rot30", true, 0, {128, 72}, 30, 1, 100, 0, 1},
        {"smear-scale15", true, 0, {128, 72}, 0, 1.5f, 100, 0, 1},
        {"smear-move-e5", true, 0, {152, 80}, 0, 1, 100, 4, 1},
        {"smear-move-e9", true, 0, {152, 80}, 0, 1, 100, 8, 1},
        {"smear-move-i1", true, 0, {152, 80}, 0, 1, 100, 0, 0},
        {"smear-move-i3", true, 0, {152, 80}, 0, 1, 100, 0, 2},
        {"smear-ellipse-move", false, 1, {150, 62}, 0, 1, 100, 0, 1},
        {"smear-noboundary-move", true, 2, {152, 80}, 0, 1, 100, 0, 1},
    };
    for (const Case& c : cases) {
        Scene scene(256, 144);
        scene.comp->set_transparent_background(true);
        const LayerId id = scene.image(fixture(c.checker), 128, 72);
        Layer& layer = *scene.comp->layer(id);
        if (c.masks == 1) {
            Mask m;
            m.id = 0;
            m.operation = MaskOperation::None;
            const f32 k = 0.5522847498f * 24;
            m.points = {{{112, 48}, {-k, 0}, {k, 0}}, {{136, 72}, {0, -k}, {0, k}},
                        {{112, 96}, {k, 0}, {-k, 0}}, {{88, 72}, {0, k}, {0, -k}}};
            layer.masks.push_back(std::move(m));
            add_rect_mask(layer, 40, 16, 216, 128);
        } else {
            add_rect_mask(layer, 96, 48, 160, 96);
            if (c.masks == 0) add_rect_mask(layer, 32, 16, 224, 128);
        }
        auto& fx = scene.add_effect(id, kSmear);
        fx.params[smear_reference::kSourceMask].constant = ParamValue::scalar(1);
        fx.params[smear_reference::kBoundaryMask].constant = ParamValue::scalar(c.masks == 2 ? 0.f : 2.f);
        fx.params[smear_reference::kMaskOffset].constant = ParamValue::vec2(c.offset.x / 256, c.offset.y / 144);
        fx.params[smear_reference::kMaskRotation].constant = ParamValue::scalar(c.rotation);
        fx.params[smear_reference::kMaskScale].constant = ParamValue::scalar(c.scale);
        fx.params[smear_reference::kPercent].constant = ParamValue::scalar(c.percent);
        fx.params[smear_reference::kElasticity].constant = ParamValue::scalar(static_cast<f32>(c.elasticity));
        fx.params[smear_reference::kInterpolation].constant = ParamValue::scalar(static_cast<f32>(c.interpolation));
        save(scene.render(FrameIndex{0}, 1, true), out / (std::string(c.name) + ".rgba32f"));
    }
}

// Glitchify review: the three AE frames in runtime/frames (signal fixture,
// 256×144, t = 0). AUREA_BATCH_C_SIGNAL_FIXTURE = runtime/signal-fixture.rgba.
AUREA_TEST(GlitchifyReferenceGpu, RenderObservedReferenceCases) {
    const char* outputPath = std::getenv("AUREA_BATCH_C_REVIEW");
    const char* fixturePath = std::getenv("AUREA_BATCH_C_SIGNAL_FIXTURE");
    if (!outputPath || !fixturePath) return;
    AUREA_REQUIRE_GPU();
    using namespace glitchify_reference;
    ImagePixels image;
    image.width = 256;
    image.height = 144;
    image.rgba.resize(256 * 144 * 4);
    std::ifstream in(fixturePath, std::ios::binary);
    in.read(reinterpret_cast<char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    AUREA_CHECK(in.good());
    if (!in.good()) return;
    const std::filesystem::path out(outputPath);
    auto save = [](const FloatImage& frame, const std::filesystem::path& path) {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(frame.px.data()), static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
    };
    struct Case { const char* name; f32 amount, completion; bool noise; u32 disable; };
    for (const Case c : {Case{"glitchify-default", 50, 0, false, kInvalidIndex}, Case{"glitchify-amount0-completion100", 0, 100, false, kInvalidIndex},
                         Case{"glitchify-amount0-show-noise", 0, 0, true, kInvalidIndex},
                         Case{"glitchify-default-nochannel", 50, 0, false, kChannelEnabled},
                         Case{"glitchify-default-nocolor", 50, 0, false, kColorEnabled},
                         Case{"glitchify-default-noimage", 50, 0, false, kImageEnabled}}) {
        Scene scene(256, 144);
        scene.comp->set_transparent_background(true);
        const LayerId id = scene.image(image, 128, 72);
        auto& fx = scene.add_effect(id, kKey);
        fx.params[kAmount].constant = ParamValue::scalar(c.amount);
        fx.params[kCompletion].constant = ParamValue::scalar(c.completion);
        fx.params[kShowNoise].constant = ParamValue::scalar(c.noise ? 1.f : 0.f);
        if (c.disable != kInvalidIndex) fx.params[c.disable].constant = ParamValue::scalar(0);
        save(scene.render(FrameIndex{0}, 1, true), out / (std::string(c.name) + ".rgba32f"));
    }
}

// Glitchify review on a photo (8 bpc sRGB AE comp "Aurea - Glitchify iso",
// 607×607, 30 fps, plugin defaults): each module alone and the full stack at
// 1 s and 2.5 s. AUREA_GLITCHIFY_PHOTO = raw RGBA8 fixture (607×607),
// AUREA_GLITCHIFY_REVIEW = output folder (linear premultiplied RGBA32F).
AUREA_TEST(GlitchifyReferenceGpu, RenderAePhotoCases) {
    const char* outputPath = std::getenv("AUREA_GLITCHIFY_REVIEW");
    const char* fixturePath = std::getenv("AUREA_GLITCHIFY_PHOTO");
    if (!outputPath || !fixturePath) return;
    AUREA_REQUIRE_GPU();
    using namespace glitchify_reference;
    constexpr u32 kSide = 607;
    ImagePixels image;
    image.width = kSide;
    image.height = kSide;
    image.rgba.resize(static_cast<usize>(kSide) * kSide * 4);
    std::ifstream in(fixturePath, std::ios::binary);
    in.read(reinterpret_cast<char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    AUREA_CHECK(in.good());
    if (!in.good()) return;
    const std::filesystem::path out(outputPath);
    struct Override { u32 index; f32 value; };
    struct Case { const char* name; std::vector<Override> set; };
    const Case cases[] = {
        {"full", {}},
        {"none", {{kChannelEnabled, 0}, {kColorEnabled, 0}, {kImageEnabled, 0}}},
        {"split", {{kColorEnabled, 0}, {kImageEnabled, 0}, {kChannelScale, 0}}},
        {"cscale", {{kColorEnabled, 0}, {kImageEnabled, 0}, {kSplitHorizontal, 0}}},
        {"channel", {{kColorEnabled, 0}, {kImageEnabled, 0}}},
        {"color", {{kChannelEnabled, 0}, {kImageEnabled, 0}}},
        {"streak", {{kChannelEnabled, 0}, {kColorEnabled, 0}, {kSliceAmount, 0}, {kBlockAmount, 0}}},
        {"slice", {{kChannelEnabled, 0}, {kColorEnabled, 0}, {kPixelStreak, 0}, {kBlockAmount, 0}}},
        {"block", {{kChannelEnabled, 0}, {kColorEnabled, 0}, {kPixelStreak, 0}, {kSliceAmount, 0}}},
        {"noise", {{kShowNoise, 1}}},
    };
    for (const Case& c : cases) {
        for (const i64 frame : {i64{30}, i64{75}}) {
            Scene scene(kSide, kSide, 30.0);
            const LayerId id = scene.image(image, kSide * 0.5f, kSide * 0.5f);
            auto& fx = scene.add_effect(id, kKey);
            for (const Override& o : c.set) fx.params[o.index].constant = ParamValue::scalar(o.value);
            const FloatImage result = scene.render(FrameIndex{frame}, 1, true);
            const std::string name = std::string(c.name) + (frame == 30 ? "-t1" : "-t2_5") + ".rgba32f";
            std::ofstream output(out / name, std::ios::binary);
            output.write(reinterpret_cast<const char*>(result.px.data()), static_cast<std::streamsize>(result.px.size() * sizeof(f32)));
        }
    }
}

// Depth of Field review: AE 2027 renders the bars fixture with a uniform depth
// (no 3D channel) at the full Maximum Radius; here Near Z = Far Z = 0 gives
// the same uniform depth on top of the AI map.
AUREA_TEST(DepthFieldReferenceGpu, RenderObservedReferenceCases) {
    const char* outputPath = std::getenv("AUREA_BATCH_C_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    using namespace batch_c_tests;
    ImagePixels image;
    image.width = 256;
    image.height = 144;
    image.rgba.assign(256 * 144 * 4, 255);
    static const u8 colors[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                    {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
    for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 256; ++x) for (u32 c = 0; c < 3; ++c)
        image.rgba[(static_cast<usize>(y) * 256 + x) * 4 + c] = colors[x / 32][c];
    Scene scene(256, 144);
    scene.comp->set_transparent_background(true);
    const LayerId id = scene.image(std::move(image), 128, 72);
    auto& fx = scene.add_effect(id, kDepthField);
    fx.params[index_of(kDepthField, "max_radius")].constant = ParamValue::scalar(10);
    fx.params[index_of(kDepthField, "near_z")].constant = ParamValue::scalar(0);
    fx.params[index_of(kDepthField, "far_z")].constant = ParamValue::scalar(0);
    const FloatImage frame = scene.render(FrameIndex{0}, 1, true);
    std::ofstream output(std::filesystem::path(outputPath) / "depth-field-r10.rgba32f", std::ios::binary);
    output.write(reinterpret_cast<const char*>(frame.px.data()), static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
}
