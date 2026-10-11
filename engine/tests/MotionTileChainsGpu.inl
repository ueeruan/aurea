// Motion Tile is a wall, including when another effect reads outside the
// original image. Use asymmetric image pixels: a solid cannot detect a lost
// mirror flag or a tile that has silently become a stretched edge.
AUREA_TEST(Gpu, MotionTileWallSurvivesSqueezeAtStrongAndRotatedSettings) {
    AUREA_REQUIRE_GPU();
    for (f32 strength : {-90.0f, 25.0f, 75.0f, 95.0f}) {
        for (f32 axis : {0.0f, 45.0f, 90.0f}) {
            Scene s(160, 90);
            const LayerId id = s.image(uniform_image(160, 90, 200, 200, 200), 80, 45);
            s.comp->layer(id)->transform.scale = Vec3{.25f, .25f, 1};
            s.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant.v[0] = 1;
            auto& squeeze = s.add_effect(id, "aurea.distort.squeeze");
            squeeze.params[0].constant.v[0] = strength;
            squeeze.params[2].constant.v[0] = axis;
            const WallHoles holes = wall_holes(s.render(), srgb_decode(200.0f / 255.0f));
            std::printf("    tile + squeeze %.0f / %.0f: %u holes, min %.4f\n", strength, axis, holes.holes, holes.darkest);
            AUREA_CHECK_EQ(holes.holes, 0u);
        }
    }
}

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

// Beta 2140: "projetos antigos com o Motion Tile bugado não consertam; só os
// criados nas versões novas". O projeto antigo trazia o "Esticar bordas"
// ligado (slot oculto 6): a parede virava a borda esticada da layer — sem
// cópias, preto em volta, espelho perdido — e sem controle para desligar.
// Aberto hoje, o projeto antigo (9 slots, build <= 2125; ou 10 slots, já
// convertido e salvo pelas 2126-2139) desenha IGUAL ao mesmo ajuste feito
// num projeto novo, e os ajustes da pessoa (ladrilho, espelho) ficam.
AUREA_TEST(Gpu, MotionTileOldProjectRendersLikeTheSameSetupCreatedNew) {
    AUREA_REQUIRE_GPU();
    std::error_code ec;
    std::filesystem::create_directories("build/prompt03", ec);
    auto layerOf = [](Engine& e, LayerId id) {
        return e.project()->timeline().composition(e.project()->timeline().current())->layer(id);
    };
    auto addTile = [](Engine& e, LayerId& id) {
        const auto text = e.add_text("Aurea");
        if (!text.ok()) return false;
        id = LayerId::unpack(*text);
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add.layer = id;
        add.effect_add.effectType = effect_type_id(effect_keys::kMotionTile);
        add.effect_add.index = kInvalidIndex;
        return e.apply_command(add).ok();
    };

    // O ajuste feito hoje pela tela: ladrilho 34%, espelhado.
    Scene3DRig fresh(256, 144);
    LayerId freshId;
    AUREA_CHECK(addTile(fresh.e, freshId));
    if (!layerOf(fresh.e, freshId)) return;
    const u32 freshFx = layerOf(fresh.e, freshId)->effects.back().id;
    const std::pair<u32, f32> sets[] = {
        {motion_tile::kTileWidth, 34.0f}, {motion_tile::kTileHeight, 34.0f}, {motion_tile::kMirror, 1.0f}};
    for (const auto& [param, value] : sets) {
        Command set;
        set.type = CommandType::EffectSetParam;
        set.effect_param.layer = freshId;
        set.effect_param.effect = EffectId{freshFx, 0};
        set.effect_param.paramIndex = param;
        set.effect_param.value = value;
        AUREA_CHECK(fresh.e.apply_command(set).ok());
    }
    const Image8 expected = fresh.capture(256);
    const f32 tiled = coverage(expected);

    for (const u32 slots : {motion_tile::kLegacyParamCount, motion_tile::kLegacyParamCount + 1}) {
        Scene3DRig old(256, 144);
        LayerId oldId;
        AUREA_CHECK(addTile(old.e, oldId));
        Layer* l = layerOf(old.e, oldId);
        if (!l) return;
        EffectInstance& fx = l->effects.back();
        fx.params.resize(slots);
        fx.params[motion_tile::kTileWidth].constant.v[0] = 34.0f;
        fx.params[motion_tile::kTileHeight].constant.v[0] = 34.0f;
        fx.params[motion_tile::kMirror].constant = ParamValue::boolean(true);
        fx.params[motion_tile::kLegacyClamp].constant = ParamValue::boolean(true);
        l->tracks.get_or_create(TrackProperty::EffectParam, fx.id, param_track_key(motion_tile::kLegacyClamp, 0))
            .set(FrameIndex{12}, 1.0f);
        const u32 fxId = fx.id;
        const std::string path = "build/prompt03/old-motion-tile-clamp-" + std::to_string(slots) + ".aurea";
        AUREA_CHECK(old.e.save_project(path.c_str()).ok());
        AUREA_CHECK(old.e.load_project(path.c_str()).ok());

        const Layer* back = layerOf(old.e, oldId);
        AUREA_CHECK(back != nullptr);
        if (!back) return;
        const EffectInstance& loaded = back->effects.back();
        AUREA_CHECK_EQ(loaded.params.size(), static_cast<usize>(motion_tile::kScale + 1));
        AUREA_CHECK(!loaded.params[motion_tile::kLegacyClamp].constant.as_bool());
        AUREA_CHECK(back->tracks.find(TrackProperty::EffectParam, fxId,
                                      param_track_key(motion_tile::kLegacyClamp, 0)) == nullptr);
        AUREA_CHECK(loaded.params[motion_tile::kMirror].constant.as_bool());
        AUREA_CHECK_NEAR(loaded.params[motion_tile::kTileWidth].constant.v[0], 34.0f, 1e-4f);

        const Image8 got = old.capture(256);
        AUREA_CHECK_EQ(got.width, expected.width);
        AUREA_CHECK_EQ(got.height, expected.height);
        if (got.width != expected.width || got.height != expected.height) return;
        u32 worst = 0;
        for (u32 y = 0; y < got.height; ++y)
            for (u32 x = 0; x < got.width; ++x)
                for (u32 c = 0; c < 3; ++c)
                    worst = std::max<u32>(worst, static_cast<u32>(std::abs(int(got.at(x, y)[c]) - int(expected.at(x, y)[c]))));
        std::printf("    old project (%u slots, clamp on) vs new: max diff %u, coverage %.3f vs %.3f\n",
                    slots, worst, coverage(got), tiled);
        AUREA_CHECK(worst <= 2u);
    }
}
