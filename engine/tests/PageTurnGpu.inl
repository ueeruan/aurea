namespace {
f32 page_image_difference(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    AUREA_CHECK_EQ(a.px.size(), b.px.size());
    for (usize i=0; i<std::min(a.px.size(), b.px.size()); ++i)
        largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
}

AUREA_TEST(PageTurnGpu, ZeroAndMixZeroAreIdentityAndFullTurnLeavesTheFrame) {
    AUREA_REQUIRE_GPU();
    Scene scene(160, 96);
    const auto layer = scene.image(reference_image(160, 96), 80, 48);
    const auto original = scene.render();
    auto& effect = scene.add_effect(layer, "aurea.distort.page_turn");
    effect.params[0].constant.v[0] = 0;
    AUREA_CHECK_NEAR(page_image_difference(original, scene.render()), 0, 0);
    effect.params[0].constant.v[0] = 50;
    effect.params[8].constant.v[0] = 0;
    AUREA_CHECK_NEAR(page_image_difference(original, scene.render()), 0, 0);
    effect.params[8].constant.v[0] = 100;
    effect.params[2].constant.v[0] = 8;
    const auto halfway = scene.render();
    AUREA_CHECK(page_image_difference(original, halfway) > .2f);
    for (const f32 direction : {0.f, 90.f, 180.f, 270.f}) {
        effect.params[0].constant.v[0] = 100;
        effect.params[1].constant.v[0] = direction;
        const auto turned = scene.render();
        AUREA_CHECK(turned.mean().x + turned.mean().y + turned.mean().z < .001f);
    }
}

AUREA_TEST(PageTurnGpu, FoldedBackReadsMirroredPixelsAndNeverFillsTransparentHoles) {
    AUREA_REQUIRE_GPU();
    Scene scene(160, 96);
    scene.comp->set_transparent_background(true);
    auto pixels = uniform_image(160, 96, 255, 0, 0);
    for (u32 y=0; y<96; ++y) for (u32 x=0; x<160; ++x) {
        u8* p = &pixels.rgba[(static_cast<usize>(y) * 160 + x) * 4];
        if (x >= 80) { p[0] = 0; p[2] = 255; }
        if (y >= 40 && y < 56) p[0] = p[1] = p[2] = p[3] = 0;
    }
    const auto layer = scene.image(std::move(pixels), 80, 48);
    auto& effect = scene.add_effect(layer, "aurea.distort.page_turn");
    effect.params[0].constant.v[0] = 50;
    effect.params[2].constant.v[0] = 8;
    effect.params[5].constant.v[0] = 0;
    effect.params[6].constant.v[0] = 0;
    const auto front = scene.render();
    AUREA_CHECK(near4(front.v(50, 20), Vec4{1, 0, 0, 1}, .005f));
    effect.params[6].constant.v[0] = 100;
    effect.params[7].constant = ParamValue::color(1, 1, 1, 0);
    const auto back = scene.render();
    AUREA_CHECK(near4(back.v(50, 20), Vec4{0, 0, 1, 1}, .005f));
    effect.params[7].constant = ParamValue::color(0, 1, 0, 1);
    const auto paper = scene.render();
    AUREA_CHECK(near4(paper.v(50, 20), Vec4{0, 1, 0, 1}, .005f));
    for (u32 x=0; x<160; ++x) AUREA_CHECK(near4(paper.v(x, 48), Vec4{}, .001f));
    effect.params[5].constant.v[0] = 100;
    AUREA_CHECK(scene.render().v(70, 20).y < paper.v(70, 20).y - .05f);
    effect.params[5].constant.v[0] = 0;
    effect.params[7].constant = ParamValue::color(.5f, .5f, .5f, 1);
    AUREA_CHECK_NEAR(scene.render().v(50, 20).x, Color::srgb_to_linear(.5f), .003f);
}

AUREA_TEST(PageTurnGpu, DiagonalSmallRadiusAndEffectChainsRemainFiniteAndDeterministic) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 108);
    const auto layer = scene.image(reference_image(96, 54), 96, 54);
    auto& tile = scene.add_effect(layer, effect_keys::kMotionTile);
    tile.params[motion_tile::kTileWidth].constant.v[0] = 50;
    tile.params[motion_tile::kTileHeight].constant.v[0] = 50;
    tile.params[motion_tile::kMirror].constant.v[0] = 1;
    scene.add_effect(layer, "aurea.distort.page_turn");
    auto& effect = scene.comp->layer(layer)->effects.back();
    effect.params[0].constant.v[0] = 35;
    effect.params[1].constant.v[0] = 37;
    effect.params[2].constant.v[0] = .5f;
    const auto a = scene.render(FrameIndex{12});
    const auto b = scene.render(FrameIndex{4});
    AUREA_CHECK_NEAR(page_image_difference(a, b), 0, .001f);
    u32 visible = 0, finite = 0;
    for (usize i=0; i<a.px.size(); i+=4) {
        visible += a.px[i] + a.px[i+1] + a.px[i+2] > .02f;
        finite += std::isfinite(a.px[i]) && std::isfinite(a.px[i+1]) && std::isfinite(a.px[i+2]) && std::isfinite(a.px[i+3]);
    }
    AUREA_CHECK(visible > 1000);
    AUREA_CHECK_EQ(finite, a.width * a.height);
    scene.add_effect(layer, effect_keys::kExposure).params[0].constant.v[0] = 1;
    const auto brighter = scene.render();
    AUREA_CHECK(brighter.mean().x > a.mean().x * 1.8f);
}
