AUREA_TEST(MediaCompletionGpu, DissolveUsesBothLayersAndTheirPremultipliedAlpha) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 64);
    scene.comp->set_transparent_background(true);
    const LayerId background = scene.image(uniform_image(128, 64, 0, 0, 255, 128), 64, 32);
    scene.comp->layer(background)->visible = false;
    const LayerId foreground = scene.image(uniform_image(96, 64, 255, 0, 0), 64, 32);
    const auto original = scene.render();
    auto& fx = scene.add_effect(foreground, "aurea.transition.blur_dissolve");
    fx_ref(fx, "background", background);
    AUREA_CHECK(sl_diff(scene.render(), original) < 1e-6f);
    fx_set(fx, "progress", 50.0f);
    const Vec4 mixed = scene.render().v(64, 32);
    AUREA_CHECK(std::fabs(mixed.x - 0.5f) < 0.003f);
    AUREA_CHECK(std::fabs(mixed.z - 64.0f / 255.0f) < 0.003f);
    AUREA_CHECK(std::fabs(mixed.w - (0.5f + 64.0f / 255.0f)) < 0.003f);
    fx_set(fx, "progress", 100.0f);
    const auto end = scene.render();
    const Vec4 border = end.v(4, 32);   // original foreground doesn't cover this pixel
    AUREA_CHECK(border.x < 1e-6f && std::fabs(border.z - 128.0f / 255.0f) < 0.003f);
    AUREA_CHECK(std::fabs(border.w - 128.0f / 255.0f) < 0.003f);
    fx_set(fx, "direction", 1.0f);
    AUREA_CHECK(sl_diff(scene.render(), original) < 1e-6f);
    fx_set(fx, "auto", 1.0f);
    AUREA_CHECK(sl_diff(scene.render(FrameIndex{0}), end) < 1e-6f);
    AUREA_CHECK(sl_diff(scene.render(FrameIndex{299}), original) < 1e-6f);
}

AUREA_TEST(MediaCompletionGpu, DissolveOffersDistinctBlurKernelsAndIndependentScales) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 64);
    scene.comp->set_transparent_background(true);
    auto stepImage = [](u32 channel) {
        ImagePixels image = uniform_image(128, 64, 0, 0, 0);
        for (u32 y = 0; y < 64; ++y)
            for (u32 x = 64; x < 128; ++x) image.rgba[(y * 128 + x) * 4 + channel] = 255;
        return image;
    };
    const LayerId background = scene.image(stepImage(2), 64, 32);
    scene.comp->layer(background)->visible = false;
    const LayerId foreground = scene.image(stepImage(0), 64, 32);
    auto& fx = scene.add_effect(foreground, "aurea.transition.blur_dissolve");
    fx_ref(fx, "background", background);
    fx_set(fx, "progress", 50.0f);
    const auto gaussian = scene.render();
    AUREA_CHECK(gaussian.v(60, 32).x > 0.025f && gaussian.v(60, 32).z > 0.025f);
    fx_set(fx, "filter", 1.0f);
    const auto box = scene.render();
    fx_set(fx, "filter", 2.0f);
    const auto triangle = scene.render();
    AUREA_CHECK(sl_diff(gaussian, box) > 0.01f && sl_diff(box, triangle) > 0.01f);
    fx_set(fx, "outgoing", 0.0f);
    const auto sharpForeground = scene.render();
    AUREA_CHECK(sharpForeground.v(60, 32).x < 0.001f && sharpForeground.v(60, 32).z > 0.025f);
    fx_set(fx, "incoming", 0.0f);
    AUREA_CHECK(scene.render().v(60, 32).z < 0.001f);
}

AUREA_TEST(MediaCompletionGpu, FilmGrainRespondsToProfileSeedAndMatteWithoutChangingAlpha) {
    AUREA_REQUIRE_GPU();
    Scene scene(96, 64);
    scene.comp->set_transparent_background(true);
    const LayerId matte = scene.image(uniform_image(96, 64, 0, 0, 0), 48, 32);
    scene.comp->layer(matte)->visible = false;
    const LayerId layer = scene.image(uniform_image(96, 64, 94, 94, 94, 156), 48, 32);
    const auto original = scene.render(FrameIndex{7});
    auto& fx = scene.add_effect(layer, "aurea.stylize.film_profile_grain");
    const auto digital = scene.render(FrameIndex{7});
    AUREA_CHECK(sl_finite(digital) && sl_diff(digital, original) > 0.01f);
    AUREA_CHECK(sl_diff(digital, scene.render(FrameIndex{7})) < 1e-6f);
    AUREA_CHECK(sl_diff(digital, scene.render(FrameIndex{8})) > 0.01f);
    for (usize i = 3; i < digital.px.size(); i += 4) AUREA_CHECK(digital.px[i] == original.px[i]);
    fx_set(fx, "animate", 0.0f);
    const auto fixed = scene.render(FrameIndex{7});
    AUREA_CHECK(sl_diff(fixed, scene.render(FrameIndex{8})) < 1e-6f);
    fx_set(fx, "seed", 84.0f);
    AUREA_CHECK(sl_diff(fixed, scene.render(FrameIndex{7})) > 0.01f);
    for (u32 profile = 1; profile < 4; ++profile) {
        fx_set(fx, "profile", static_cast<f32>(profile));
        AUREA_CHECK(sl_diff(fixed, scene.render(FrameIndex{7})) > 0.01f);
    }
    fx_ref(fx, "matte", matte);
    AUREA_CHECK(sl_diff(original, scene.render(FrameIndex{7})) < 1e-6f);
    fx_set(fx, "amount", 0.0f);
    AUREA_CHECK(sl_diff(original, scene.render(FrameIndex{7})) < 1e-6f);
}

AUREA_TEST(MediaCompletionGpu, ColorPalettesRespectToneLevelsAndKeepAlpha) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 32);
    scene.comp->set_transparent_background(true);
    ImagePixels input = uniform_image(128, 32, 0, 0, 0, 128);
    for (u32 y = 0; y < input.height; ++y) {
        for (u32 x = 0; x < input.width; ++x) {
            const u8 code = static_cast<u8>((x / 32) * 85);
            for (u32 channel = 0; channel < 3; ++channel) input.rgba[(y * 128 + x) * 4 + channel] = code;
        }
    }
    const LayerId layer = scene.image(std::move(input), 64, 16);
    const auto original = scene.render();
    auto& duo = scene.add_effect(layer, "aurea.color.duotone");
    fx_set(duo, "shadow_color", 1.0f, 0.0f, 0.0f, 1.0f);
    fx_set(duo, "highlight_color", 0.0f, 0.0f, 1.0f, 1.0f);
    fx_set(duo, "softness", 20.0f);
    const auto mapped = scene.render();
    AUREA_CHECK(mapped.v(48, 16).x > 0.49f && mapped.v(80, 16).z > 0.49f);
    fx_set(duo, "threshold", 73.0f);
    AUREA_CHECK(scene.render().v(80, 16).x > scene.render().v(80, 16).z);
    fx_set(duo, "invert", 1.0f);
    AUREA_CHECK(scene.render().v(16, 16).z > 0.49f);
    fx_set(duo, "amount", 0.0f);
    AUREA_CHECK(sl_diff(scene.render(), original) < 1e-6f);

    auto& quad = scene.add_effect(layer, "aurea.color.quadtone");
    fx_set(quad, "shadow_color", 1, 0, 0, 1);
    fx_set(quad, "low_color", 0, 1, 0, 1);
    fx_set(quad, "high_color", 0, 0, 1, 1);
    fx_set(quad, "highlight_color", 1, 1, 0, 1);
    fx_set(quad, "low_level", 100.0f / 3.0f);
    fx_set(quad, "high_level", 200.0f / 3.0f);
    const auto ramp = scene.render();
    AUREA_CHECK(ramp.v(16, 16).x > 0.49f && ramp.v(16, 16).y < 0.005f);
    AUREA_CHECK(ramp.v(48, 16).y > 0.49f && ramp.v(48, 16).z < 0.005f);
    AUREA_CHECK(ramp.v(80, 16).z > 0.49f && ramp.v(80, 16).x < 0.005f);
    AUREA_CHECK(ramp.v(112, 16).x > 0.49f && ramp.v(112, 16).y > 0.49f);
    for (usize i = 3; i < ramp.px.size(); i += 4) AUREA_CHECK(ramp.px[i] == original.px[i]);
}

AUREA_TEST(MediaCompletionGpu, RackDefocusRacksBetweenTwoAperturesAndKeepsTheEndSharp) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 96);
    scene.comp->set_transparent_background(true);
    ImagePixels input = uniform_image(128, 96, 0, 0, 0);
    for (u32 y = 46; y <= 50; ++y)
        for (u32 x = 62; x <= 66; ++x)
            for (u32 channel = 0; channel < 3; ++channel) input.rgba[(y * 128 + x) * 4 + channel] = 255;
    const LayerId layer = scene.image(std::move(input), 64, 48);
    const auto original = scene.render();
    auto& rack = scene.add_effect(layer, "aurea.blur.rack_defocus");
    fx_set(rack, "focus_from", 50.0f);
    AUREA_CHECK(scene.render().v(0, 0).w > 0.999f);
    fx_set(rack, "focus_from", 20.0f);
    const auto start = scene.render();
    AUREA_CHECK(start.v(64, 48).x < 0.5f && start.v(70, 48).x > 0.0001f);
    AUREA_CHECK(start.v(3, 4).w > 0.99f);
    fx_set(rack, "progress", 50.0f);
    const auto half = scene.render();
    AUREA_CHECK(half.v(64, 48).x > start.v(64, 48).x);
    fx_set(rack, "bokeh", -100.0f);
    AUREA_CHECK(sl_diff(half, scene.render()) > 0.01f);
    fx_set(rack, "progress", 100.0f);
    AUREA_CHECK(sl_diff(scene.render(), original) < 1e-6f);
    fx_set(rack, "auto", 1.0f);
    AUREA_CHECK(sl_diff(scene.render(FrameIndex{0}), original) > 0.1f);
    AUREA_CHECK(sl_diff(scene.render(FrameIndex{299}), original) < 1e-6f);
}
