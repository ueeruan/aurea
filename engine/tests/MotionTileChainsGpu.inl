// Motion Tile is a wall, including when another effect reads outside the
// original image. Use asymmetric image pixels: a solid cannot detect a lost
// mirror flag or a tile that has silently become a stretched edge.
AUREA_TEST(Gpu, MotionTileMirrorSurvivesAddingAnImageEffect) {
    AUREA_REQUIRE_GPU();
    for (int kind = 0; kind < 5; ++kind) {
        Scene s(256, 144);
        const LayerId id = s.image(reference_image(256, 144), 128, 72);
        auto& tile = s.add_effect(id, effect_keys::kMotionTile);
        tile.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
        tile.params[motion_tile::kTileHeight].constant.v[0] = 50.0f;
        tile.params[motion_tile::kMirror].constant.v[0] = 1.0f;
        (void)s.render(); // The effect is already visible before the addition.
        u32 seam = 64;
        if (kind == 0) {
            s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 4.0f;
        } else if (kind == 1) {
            auto& glow = s.add_effect(id, effect_keys::kGlow);
            glow.params[1].constant.v[0] = 8.0f;
            glow.params[2].constant.v[0] = 0.4f;
        } else if (kind == 2 || kind == 3) {
            auto& transform = s.add_effect(id, effect_keys::kTransform);
            transform.params[2].constant = ParamValue::vec2(50.0f, 50.0f);
            seam = 96;
            if (kind == 3)
                s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 2.0f;
        } else {
            s.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = -0.5f;
        }
        const FloatImage image = s.render();
        f32 worst = 0.0f, contrast = 0.0f;
        for (u32 y : {48u, 64u, 80u, 96u}) {
            for (u32 k = 3; k < 24; ++k) {
                for (u32 c = 0; c < 3; ++c) {
                    worst = std::max(worst, std::fabs(image.at(seam - k, y)[c] - image.at(seam + k - 1, y)[c]));
                    // Span the source gradient, rather than just its dark
                    // linear-light edge, to reject a flat/clamped output.
                    contrast = std::max(contrast, std::fabs(image.at(seam + 3, y)[c] - image.at(seam + 60, y)[c]));
                }
            }
        }
        std::printf("    image Motion Tile chain %d: mirror error %.4f, contrast %.4f\n", kind, worst, contrast);
        AUREA_CHECK(worst < 0.025f);
        AUREA_CHECK(contrast > 0.1f);
        AUREA_CHECK_NEAR(s.comp->layer(id)->effects[0].params[motion_tile::kMirror].constant.v[0], 1.0f, 1e-6f);
    }
}

AUREA_TEST(Gpu, MotionTileDefaultImageNeedsTheWallForLaterBlur) {
    AUREA_REQUIRE_GPU();
    for (int mirror = 0; mirror < 2; ++mirror) {
        Scene s(160, 90);
        const LayerId id = s.image(uniform_image(160, 90, 200, 200, 200), 80, 45);
        s.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant.v[0] = static_cast<f32>(mirror);
        s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 12.0f;
        const WallHoles holes = wall_holes(s.render(), srgb_decode(200.0f / 255.0f));
        std::printf("    full image then blur, mirror %d: %u dark pixels\n", mirror, holes.holes);
        AUREA_CHECK_EQ(holes.holes, 0u);
    }
}

AUREA_TEST(Gpu, MotionTileDefaultAfterShrinkingImageUsesTheTransformedSource) {
    AUREA_REQUIRE_GPU();
    for (int mirror = 0; mirror < 2; ++mirror) {
        Scene s(160, 90);
        const LayerId id = s.image(uniform_image(160, 90, 200, 200, 200), 80, 45);
        s.add_effect(id, effect_keys::kTransform).params[2].constant = ParamValue::vec2(50.0f, 50.0f);
        s.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant.v[0] = static_cast<f32>(mirror);
        const WallHoles holes = wall_holes(s.render(), srgb_decode(200.0f / 255.0f));
        std::printf("    shrunk image then tile, mirror %d: %u dark pixels\n", mirror, holes.holes);
        AUREA_CHECK_EQ(holes.holes, 0u);
    }
}

AUREA_TEST(Gpu, MotionTileMirroredWallKeepsBlurMarginAcrossAShrinkingTransform) {
    AUREA_REQUIRE_GPU();
    for (f32 scale : {10.0f, 25.0f}) {
        for (f32 rotation : {0.0f, 33.0f}) {
            Scene s(160, 90);
            const LayerId id = s.image(uniform_image(160, 90, 200, 200, 200), 80, 45);
            s.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant.v[0] = 1.0f;
            auto& transform = s.add_effect(id, effect_keys::kTransform);
            transform.params[2].constant = ParamValue::vec2(scale, scale);
            transform.params[3].constant.v[0] = rotation;
            s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 24.0f;
            const WallHoles holes = wall_holes(s.render(), srgb_decode(200.0f / 255.0f));
            std::printf("    mirrored tile, transform %.0f%% / %.0f degrees, blur: %u dark pixels, min %.4f\n",
                        scale, rotation, holes.holes, holes.darkest);
            AUREA_CHECK_EQ(holes.holes, 0u);
        }
    }
}

AUREA_TEST(Gpu, MotionTileMirrorAfterBlurKeepsItsPeriodWhenTheImageMovesOffscreen) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 128, 72);
    s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 4.0f;
    auto& tile = s.add_effect(id, effect_keys::kMotionTile);
    tile.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
    tile.params[motion_tile::kTileHeight].constant.v[0] = 50.0f;
    tile.params[motion_tile::kMirror].constant.v[0] = 1.0f;
    const FloatImage centered = s.render();
    // At 50%, two mirrored 64px tiles make one 128px period. Moving the
    // source by that whole period must not crop it or change the grid size.
    s.comp->layer(id)->transform.position.x -= 128.0f;
    const FloatImage moved = s.render();
    f32 worst = 0.0f;
    for (u32 y = 8; y + 8 < centered.height; ++y)
        for (u32 x = 8; x + 8 < centered.width; ++x)
            for (u32 c = 0; c < 3; ++c)
                worst = std::max(worst, std::fabs(centered.at(x, y)[c] - moved.at(x, y)[c]));
    std::printf("    blur before mirrored tile, one-period pan: maximum pixel error %.4f\n", worst);
    AUREA_CHECK(worst < 0.015f);
}
