// AE reference batch B: Card Wipe and Card Dance. Pixel comparisons against
// the After Effects 2027 frames run in build/reference/effects-20261115/batch-b
// with AUREA_BATCH_B_REVIEW set; the assertions here keep the observed contracts.
namespace batch_b_tests {
constexpr const char* kCardWipe = "aurea.transition.card_wipe_reference";
constexpr const char* kCardDance = "aurea.3d.card_dance_reference";

u32 index_of(const char* key, const char* id) {
    const auto* parameters = gpu().effects.params(effect_type_id(key));
    const u32 index = parameters ? parameters->find(id) : kInvalidIndex;
    AUREA_CHECK_MSG(index != kInvalidIndex, id);
    return index;
}

// The AE fixtures (batch-b/runtime/render-cards.jsx): eight 32 px colour bars
// and eight 32 px gray bars (sRGB levels below), both 256x144.
constexpr u8 kRampLevels[8] = {0, 64, 96, 128, 160, 192, 224, 255};

ImagePixels bars() {
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

ImagePixels ramp() {
    ImagePixels image;
    image.width = 256;
    image.height = 144;
    image.rgba.assign(256 * 144 * 4, 255);
    for (u32 y = 0; y < 144; ++y) for (u32 x = 0; x < 256; ++x)
        for (u32 c = 0; c < 3; ++c) image.rgba[(static_cast<usize>(y) * 256 + x) * 4 + c] = kRampLevels[x / 32];
    return image;
}

void save(const FloatImage& frame, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(frame.px.data()),
                 static_cast<std::streamsize>(frame.px.size() * sizeof(f32)));
    AUREA_CHECK(output.good());
}

struct Fixture {
    Scene scene{256, 144};
    LayerId layer{}, gradient{};
    Fixture() {
        scene.comp->set_transparent_background(true);
        gradient = scene.image(ramp(), 128, 72);
        scene.comp->layer(gradient)->visible = false;
        layer = scene.image(bars(), 128, 72);
    }
    void link(EffectInstance& effect, u32 index) const {
        effect.params[index].constant.ref = gradient.pack();
        effect.params[index].constant.v[0] = static_cast<f32>(gradient.index);
    }
};

void set(EffectInstance& effect, const char* key, const char* id, f32 value) {
    effect.params[index_of(key, id)].constant = ParamValue::scalar(value);
}
}

AUREA_TEST(CardReference, NativeControlContracts) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    const auto* wipe = registry.params(effect_type_id(batch_b_tests::kCardWipe));
    const auto* dance = registry.params(effect_type_id(batch_b_tests::kCardDance));
    AUREA_CHECK(wipe && dance);
    if (!wipe || !dance) return;
    // 13 wipe controls + camera (14) + lighting and material (9) + jitter (12).
    AUREA_CHECK_EQ(wipe->count(), 48u);
    AUREA_CHECK_NEAR(wipe->at(0).defaultValue.v[0], 25, 1e-6);
    AUREA_CHECK_NEAR(wipe->at(wipe->find("rows")).defaultValue.v[0], 9, 1e-6);
    AUREA_CHECK_NEAR(wipe->at(wipe->find("columns")).defaultValue.v[0], 12, 1e-6);
    AUREA_CHECK_EQ(wipe->at(wipe->find("flip_order")).enumCount, 9u);
    AUREA_CHECK_EQ(wipe->at(wipe->find("camera_order")).enumCount, 12u);
    AUREA_CHECK_EQ(wipe->find("camera_system"), 13u);
    AUREA_CHECK_EQ(wipe->find("light_type"), 27u);
    AUREA_CHECK_EQ(wipe->find("x_jitter"), 36u);
    AUREA_CHECK_NEAR(wipe->at(wipe->find("ambient_light")).defaultValue.v[0], .25, 1e-6);
    AUREA_CHECK_NEAR(wipe->at(wipe->find("diffuse_reflection")).defaultValue.v[0], .75, 1e-6);
    // 8 grid controls + 8 channels x (source, multiplier, offset) + camera + lighting.
    AUREA_CHECK_EQ(dance->count(), 55u);
    AUREA_CHECK_EQ(dance->at(dance->find("x_position_source")).enumCount, 19u);
    AUREA_CHECK_NEAR(dance->at(dance->find("x_rotation_multiplier")).defaultValue.v[0], 90, 1e-6);
    AUREA_CHECK_NEAR(dance->at(dance->find("x_scale_offset")).defaultValue.v[0], 1, 1e-6);
    AUREA_CHECK_EQ(dance->at(dance->find("transformation_order")).defaultValue.as_enum(), 5u);
    AUREA_CHECK_EQ(dance->find("camera_system"), 32u);
}

AUREA_TEST(CardReferenceGpu, WipeRestsFlipsAndShowsTheBackLayer) {
    AUREA_REQUIRE_GPU();
    using namespace batch_b_tests;
    Fixture f;
    const FloatImage plain = f.scene.render(FrameIndex{0}, 1, true);
    auto& effect = f.scene.add_effect(f.layer, kCardWipe);
    set(effect, kCardWipe, "completion", 0);
    // Default camera (2 widths, 70 mm) and light (distant, ambient .25 +
    // diffuse .75): the resting cards are the layer.
    const FloatImage rest = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(rest));
    AUREA_CHECK(image_diff(plain, rest) < .01f);
    AUREA_CHECK_NEAR(rest.v(40, 72).y, plain.v(40, 72).y, 2e-3);
    set(effect, kCardWipe, "completion", 50);
    const FloatImage half = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(half));
    AUREA_CHECK(image_diff(plain, half) > .01f);
    // Fully flipped about X with the layer as its own back: the back layer is
    // mapped upright, so the frame is the layer again.
    set(effect, kCardWipe, "completion", 100);
    const FloatImage done = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(image_diff(plain, done) < .01f);
    // Same seed, same frame; another seed moves the random timing.
    set(effect, kCardWipe, "completion", 50);
    set(effect, kCardWipe, "timing_randomness", .5f);
    const FloatImage random = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(image_diff(random, f.scene.render(FrameIndex{0}, 1, true)) < 1e-7f);
    set(effect, kCardWipe, "random_seed", 9);
    AUREA_CHECK(image_diff(random, f.scene.render(FrameIndex{0}, 1, true)) > 1e-4f);
}

AUREA_TEST(CardReferenceGpu, DanceReadsTheGradientPerCard) {
    AUREA_REQUIRE_GPU();
    using namespace batch_b_tests;
    Fixture f;
    const FloatImage plain = f.scene.render(FrameIndex{0}, 1, true);
    auto& effect = f.scene.add_effect(f.layer, kCardDance);
    f.link(effect, index_of(kCardDance, "gradient_layer_1"));
    // Every source None: the cards rest.
    const FloatImage rest = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(image_diff(plain, rest) < .01f);
    // Z position from Intensity 1: dark cards go back, bright cards come forward.
    effect.params[index_of(kCardDance, "z_position_source")].constant = ParamValue::scalar(1);
    const FloatImage depth = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(all_finite(depth));
    AUREA_CHECK(image_diff(plain, depth) > .005f);
    // Y rotation offset without a gradient source still turns every card.
    effect.params[index_of(kCardDance, "z_position_source")].constant = ParamValue::scalar(0);
    set(effect, kCardDance, "y_rotation_offset", 60);
    const FloatImage turned = f.scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(image_diff(plain, turned) > .01f);
}

AUREA_TEST(ReferenceBatchBGpu, RenderObservedReferenceCasesWhenRequested) {
    const char* outputPath = std::getenv("AUREA_BATCH_B_REVIEW");
    if (!outputPath) return;
    AUREA_REQUIRE_GPU();
    using namespace batch_b_tests;
    std::error_code error;
    std::filesystem::create_directories(outputPath, error);
    AUREA_CHECK(!error);
    if (error) return;
    const std::filesystem::path out(outputPath);
    // Mirrors batch-b/runtime/cases.json (AE property indices -> Aurea ids).
    struct Value { const char* id; f32 x, y; };
    struct Case { const char* name; bool dance; f32 seconds; std::vector<Value> values; };
    const f32 W = 256, H = 144;
    const std::vector<Case> cases = {
        {"cw-c0", false, 0, {{"completion", 0, 0}}},
        {"cw-c25", false, 0, {}},
        {"cw-c50", false, 0, {{"completion", 50, 0}}},
        {"cw-c100", false, 0, {{"completion", 100, 0}}},
        {"cw-c50-y", false, 0, {{"completion", 50, 0}, {"flip_axis", 1, 0}}},
        {"cw-c50-order5", false, 0, {{"completion", 50, 0}, {"flip_order", 4, 0}}},
        {"cw-c50-grad", false, 0, {{"completion", 50, 0}, {"flip_order", 8, 0}, {"gradient_layer", -1, 0}}},
        {"cw-c50-random", false, 0, {{"completion", 50, 0}, {"flip_axis", 2, 0}, {"flip_direction", 2, 0},
                                     {"timing_randomness", .5f, 0}, {"random_seed", 7, 0}}},
        {"cw-jitter", false, .5f, {{"completion", 25, 0}, {"x_jitter", 2, 0}, {"x_jitter_speed", 3, 0},
                                   {"z_rotation_jitter", 30, 0}, {"z_rotation_jitter_speed", 2, 0}}},
        {"cw-spec", false, 0, {{"completion", 50, 0}, {"light_type", 0, 0}, {"specular_reflection", 1, 0},
                               {"highlight_sharpness", 20, 0}}},
        {"cw-camera", false, 0, {{"completion", 50, 0}, {"camera_rotation_y", 30, 0}, {"camera_z", 3, 0}}},
        {"cw-corner", false, 0, {{"completion", 25, 0}, {"camera_system", 1, 0},
                                 {"corner_upper_right", 230 / W, 20 / H}, {"corner_lower_right", 240 / W, 130 / H}}},
        {"cd-rest", true, 0, {}},
        {"cd-zpos", true, 0, {{"z_position_source", 1, 0}, {"z_position_multiplier", 1, 0}}},
        {"cd-xrot", true, 0, {{"x_rotation_source", 1, 0}, {"x_rotation_multiplier", 90, 0}}},
        {"cd-yrot", true, 0, {{"y_rotation_source", 1, 0}, {"y_rotation_multiplier", 45, 0}}},
        {"cd-zrot", true, 0, {{"z_rotation_source", 1, 0}, {"z_rotation_multiplier", 30, 0}}},
        {"cd-scale", true, 0, {{"x_scale_source", 1, 0}, {"x_scale_multiplier", .5f, 0}, {"x_scale_offset", 1, 0}}},
        {"cd-xpos", true, 0, {{"x_position_source", 1, 0}, {"x_position_multiplier", .2f, 0}}},
        {"cd-order1", true, 0, {{"transformation_order", 0, 0}, {"x_position_source", 1, 0},
                                {"x_position_multiplier", .2f, 0}, {"z_rotation_source", 1, 0},
                                {"z_rotation_multiplier", 30, 0}}},
        {"cd-hslope", true, 0, {{"y_rotation_source", 6, 0}, {"y_rotation_multiplier", 10, 0}}},
        {"cd-light", true, 0, {{"y_rotation_offset", 45, 0}, {"light_type", 0, 0}, {"specular_reflection", 1, 0},
                               {"highlight_sharpness", 20, 0}}},
    };
    {
        Fixture f;
        save(f.scene.render(FrameIndex{0}, 1, true), out / "input-bars.rgba32f");
    }
    for (const Case& c : cases) {
        Fixture f;
        const char* key = c.dance ? kCardDance : kCardWipe;
        auto& effect = f.scene.add_effect(f.layer, key);
        if (c.dance) f.link(effect, index_of(key, "gradient_layer_1"));
        for (const Value& v : c.values) {
            const u32 index = index_of(key, v.id);
            if (v.x < 0) { f.link(effect, index); continue; }
            const auto* spec = gpu().effects.params(effect_type_id(key));
            effect.params[index].constant = spec->at(index).type == ParamType::Point2D
                ? ParamValue::vec2(v.x, v.y) : ParamValue::scalar(v.x);
        }
        const FrameIndex frame{static_cast<i64>(std::lround(c.seconds * 30))};
        save(f.scene.render(frame, 1, true), out / (std::string(c.name) + ".rgba32f"));
    }
}
