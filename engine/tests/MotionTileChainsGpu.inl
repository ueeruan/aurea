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

// Beta 3's recording changes X from -101.2 to -88.4 degrees while the
// displayed tile wall stays unchanged. Exercise the editor's command path,
// rather than only mutating a Scene fixture, with our own asymmetric pixels.
AUREA_TEST(Gpu, MotionTileBeta3RotationCommandsChangeThePerspectiveAndReopen) {
    AUREA_REQUIRE_GPU();
    const std::string folder = "build/reference/beta3-20261013/motion-tile-tests/";
    std::filesystem::create_directories(folder);
    const ImagePixels pixels = reference_image(128, 128);
    Image8 source;
    source.width = source.height = 128;
    source.rgba = pixels.rgba;
    const std::string sourcePath = folder + "own-asymmetric-source.png";
    AUREA_CHECK(write_png(sourcePath, source));
    Scene3DRig rig(512, 288, true);
    const auto image = rig.e.import_image(pixels.rgba.data(), 128, 128, "Own tile texture", sourcePath.c_str());
    AUREA_CHECK(image.ok()); if (!image.ok()) return;
    const LayerId id = LayerId::unpack(*image);
    AUREA_CHECK(rig.e.set_layer_3d(*image, true));
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add = {id, effect_type_id(effect_keys::kMotionTile), kInvalidIndex};
    AUREA_CHECK(rig.e.apply_command(add).ok());
    auto* layer = rig.e.project()->timeline().composition(rig.e.project()->timeline().current())->layer(id);
    Command mirror;
    mirror.type = CommandType::EffectSetParam;
    mirror.effect_param = {id, EffectId{layer->effects.back().id, 0}, motion_tile::kMirror, 1.f};
    AUREA_CHECK(rig.e.apply_command(mirror).ok());
    auto atAngle = [&](f32 angle) {
        Command rotate;
        // Kotlin and Swift both use gestureKeyframe for this knob. The motor
        // writes the static X value when that axis is not animated.
        rotate.type = CommandType::KeyframeInsert;
        rotate.keyframe = {TrackRef{id, TrackProperty::RotationX, kInvalidIndex, 0}, FrameIndex{0}, angle,
                           kAutoKeyOnlyIfChanged | kAutoKeyAtPlayhead | kAutoKeyStaticWhenUnanimated};
        AUREA_CHECK(rig.e.apply_command(rotate).ok());
        return rig.capture(512);
    };
    const auto first = atAngle(-101.2f);
    const auto grazing = atAngle(-88.4f);
    const auto back = atAngle(-221.2f);
    auto changedPixels = [](const Image8& a, const Image8& b) {
        u32 changed = 0;
        for (usize p = 0; p + 3 < a.rgba.size() && p + 3 < b.rgba.size(); p += 4)
            changed += std::abs(int(a.rgba[p]) - int(b.rgba[p])) > 8
                || std::abs(int(a.rgba[p + 1]) - int(b.rgba[p + 1])) > 8
                || std::abs(int(a.rgba[p + 2]) - int(b.rgba[p + 2])) > 8;
        return changed;
    };
    const u32 firstChange = changedPixels(first, grazing), secondChange = changedPixels(grazing, back);
    std::printf("    beta3 tile X rotation: %u / %u changed pixels, coverage %.3f / %.3f / %.3f\n",
                firstChange, secondChange, coverage(first), coverage(grazing), coverage(back));
    AUREA_CHECK(firstChange > 1000u);
    AUREA_CHECK(secondChange > 1000u);
    AUREA_CHECK(back.rgba == rig.capture(512).rgba);
    const std::string projectPath = folder + "grazing-plane.aurea";
    AUREA_CHECK(rig.e.save_project(projectPath.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(projectPath.c_str()).ok());
    AUREA_CHECK(max_diff(back, rig.capture(512)) <= 2u);
    AUREA_CHECK(write_png(folder + "x-minus101.png", first));
    AUREA_CHECK(write_png(folder + "x-minus88.png", grazing));
    AUREA_CHECK(write_png(folder + "x-minus221.png", back));
}

// A tiled plane has one physical horizon. Repeating an already projected
// floor in screen coordinates would put texture on rays whose intersection
// with that plane is behind the camera. Check that domain independently of
// the tile shader's lookup, including the recording's grazing angle.
AUREA_TEST(Gpu, MotionTileBeta3GrazingWallDoesNotRepeatBehindCamera) {
    AUREA_REQUIRE_GPU();
    for (const f32 angle : {-101.2f, -88.4f, -221.2f}) {
        Scene scene(512, 288);
        const LayerId id = scene.image(reference_image(128, 128), 256, 144);
        scene.comp->layer(id)->threeD = true;
        scene.comp->layer(id)->transform.rotation.x = angle;
        scene.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant = ParamValue::boolean(true);
        const FloatImage pixels = scene.render();
        RenderSettings settings;
        FrameSnapshot snapshot;
        gpu().renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, &scene.media, &Scene::lookup, &scene,
                               settings, 1000000, 0, DecodeMode::Still, 1.f, snapshot);
        const RenderLayer* plane = nullptr;
        for (const auto& candidate : snapshot.layers)
            if (candidate.id == id && candidate.source.kind != LayerSource::Kind::Scene3D) plane = &candidate;
        AUREA_CHECK(plane != nullptr); if (!plane) return;
        const Mat4& m = plane->compFromLayer;
        u32 behind = 0, incorrectlyLit = 0;
        for (u32 y = 4; y + 4 < pixels.height; y += 3) for (u32 x = 4; x + 4 < pixels.width; x += 3) {
            const f64 qx = x + .5, qy = y + .5;
            const f64 a = m.col[0].x - qx * m.col[0].w, c = m.col[1].x - qx * m.col[1].w;
            const f64 b = m.col[0].y - qy * m.col[0].w, d = m.col[1].y - qy * m.col[1].w;
            const f64 tx = qx * m.col[3].w - m.col[3].x, ty = qy * m.col[3].w - m.col[3].y;
            const f64 determinant = a * d - b * c;
            if (std::fabs(determinant) < 1e-10) continue;
            const f64 px = (d * tx - c * ty) / determinant, py = (a * ty - b * tx) / determinant;
            const f64 w = m.col[0].w * px + m.col[1].w * py + m.col[3].w;
            if (w >= -.001) continue;
            ++behind;
            const auto* color = pixels.at(x, y);
            incorrectlyLit += color[0] > .005f || color[1] > .005f || color[2] > .005f;
        }
        std::printf("    beta3 tile X %.1f: behind-camera samples %u, erroneously repeated %u\n",
                    angle, behind, incorrectlyLit);
        if (angle == -101.2f || angle == -88.4f) AUREA_CHECK(behind > 1000u);
        AUREA_CHECK_EQ(incorrectlyLit, 0u);
    }
}

// Covers the horizon excluded by the old point-color oracle. A white periodic
// source has no sampling ambiguity; an opaque in-frustum ray must stay white.
// Alternating own 4-texel rows have an independent area average: integrate the
// inverse homography over the screen pixel, including the original bilinear
// source reconstruction, rather than expecting one arbitrary high-frequency
// point value. This is an own regression, not a claim about the unidentified APK.
AUREA_TEST(Gpu, MotionTileGrazingCoverageAndIntegratedStripeColor) {
    AUREA_REQUIRE_GPU();
    const std::string folder = "build/reference/motion-tile-finish-20261013/";
    std::filesystem::create_directories(folder);
    for (const bool stripes : {false, true}) for (const f32 angle : {-88.4f, -101.2f}) {
        Scene scene(512, 288);
        auto source = uniform_image(128, 128, 255, 255, 255);
        if (stripes) for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 128; ++x) {
            auto* value = source.rgba.data() + (y * 128 + x) * 4;
            value[0] = value[1] = value[2] = (y / 4) % 2 ? 255 : 0;
        }
        const auto id = scene.image(std::move(source), 256, 144);
        scene.comp->layer(id)->threeD = true;
        scene.comp->layer(id)->transform.rotation.x = angle;
        scene.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant = ParamValue::boolean(true);
        const auto actual = scene.render();
        FrameSnapshot snapshot;
        RenderSettings settings;
        gpu().renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, &scene.media, &Scene::lookup, &scene,
            settings, 9000001, 0, DecodeMode::Still, 1.f, snapshot);
        const RenderLayer* plane = nullptr;
        for (const auto& layer : snapshot.layers)
            if (layer.id == id && layer.source.kind != LayerSource::Kind::Scene3D) plane = &layer;
        AUREA_CHECK(plane != nullptr); if (!plane) return;
        const Mat4& m = plane->compFromLayer;
        struct Point { f64 x=0, y=0; bool valid=false; };
        auto ray = [&](f64 qx, f64 qy) {
            Point result;
            const f64 a=m.col[0].x-qx*m.col[0].w,c=m.col[1].x-qx*m.col[1].w;
            const f64 b=m.col[0].y-qy*m.col[0].w,d=m.col[1].y-qy*m.col[1].w;
            const f64 tx=qx*m.col[3].w-m.col[3].x,ty=qy*m.col[3].w-m.col[3].y;
            const f64 det=a*d-b*c;
            if (std::fabs(det)<1e-10) return result;
            result.x=(d*tx-c*ty)/det;result.y=(a*ty-b*tx)/det;
            // clip_from_comp changes only X/Y. The homogeneous Z/W used by
            // the Vulkan depth frustum are already present in compFromLayer.
            const Vec4 projected=m*Vec4{static_cast<f32>(result.x),static_cast<f32>(result.y),0,1};
            result.valid=std::isfinite(result.x)&&std::isfinite(result.y)&&projected.w>1e-5f
                &&projected.z>=0.f&&projected.z<=projected.w;
            return result;
        };
        u32 front=0, missing=0, integratedRows=0, badRows=0;
        f64 maxError=0;
        if (!stripes) {
            for (u32 y=2;y+2<288;++y) for (u32 x=2;x+2<512;x+=3) {
                if (!ray(x+.01,y+.01).valid || !ray(x+.99,y+.99).valid) continue;
                ++front;const auto* value=actual.at(x,y);
                missing+=value[0]<.98f||value[1]<.98f||value[2]<.98f;
            }
            AUREA_CHECK(front>1000u);
            AUREA_CHECK_EQ(missing,0u);
        } else {
            motion_tile::Params p;p.mirror=true;
            for (u32 y=2;y+2<288;++y) {
                const auto lo=ray(256.5,y+.0001),hi=ray(256.5,y+.9999);
                // Isolate filtering from the finite raster cutoff: both ends
                // must be inside the existing 24x fallback wall for this case.
                if (!lo.valid||!hi.valid||std::min(lo.y,hi.y)<-1472.||std::max(lo.y,hi.y)>1600.)continue;
                const f64 span=std::fabs(hi.y-lo.y);
                if (span<32.||span>2048.)continue;
                f64 average=0;
                for (u32 n=0;n<4096;++n) {
                    const auto pos=ray(256.5,y+(n+.5)/4096.);
                    const auto local=motion_tile::reference_lookup(p,{static_cast<f32>(pos.x/128.),static_cast<f32>(pos.y/128.)});
                    const f64 sy=std::clamp(static_cast<f64>(local.y),.5/128.,127.5/128.)*128.-.5;
                    const int first=static_cast<int>(std::floor(sy));const f64 frac=sy-first;
                    average+=(first/4)%2*(1-frac)+((std::min(first+1,127)/4)%2)*frac;
                }
                average/=4096.;++integratedRows;
                const f64 error=std::fabs(actual.at(256,y)[0]-average);
                maxError=std::max(maxError,error);badRows+=error>.12;
                std::printf("    tile stripe X%.1f row%u footprint%.1f expected%.4f actual%.4f error%.4f\n",
                    angle,y,span,average,actual.at(256,y)[0],error);
            }
            AUREA_CHECK(integratedRows>0u);
            AUREA_CHECK_EQ(badRows,0u);
        }
        std::printf("    grazing tile %s X%.1f front%u missing%u integratedRows%u badRows%u maxError%.4f\n",
            stripes?"stripes":"white",angle,front,missing,integratedRows,badRows,maxError);
        const std::string label=stripes?"stripes":"white";
        AUREA_CHECK(write_png(folder+label+"-"+std::to_string(angle)+".png",actual.encoded()));
    }
}

namespace {
Mat4 tile_test_plane_matrix(Scene& scene, LayerId id, f32* texelScale = nullptr) {
    FrameSnapshot snapshot;
    RenderSettings settings;
    gpu().renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, &scene.media, &Scene::lookup, &scene,
        settings, 9100001, 0, DecodeMode::Still, 1.f, snapshot);
    for (const auto& layer : snapshot.layers)
        if (layer.id == id && layer.source.kind != LayerSource::Kind::Scene3D) {
            if (texelScale) *texelScale = layer.texelScale;
            return layer.compFromLayer;
        }
    AUREA_CHECK_MSG(false, "own periodic plane missing from snapshot");
    return Mat4::identity();
}

struct TileTestRay { Vec2 pixel{}; bool valid = false; };
TileTestRay tile_test_ray(const Mat4& m, f64 x, f64 y) {
    const f64 a = m.col[0].x - x * m.col[0].w, b = m.col[1].x - x * m.col[1].w;
    const f64 c = m.col[0].y - y * m.col[0].w, d = m.col[1].y - y * m.col[1].w;
    const f64 tx = x * m.col[3].w - m.col[3].x, ty = y * m.col[3].w - m.col[3].y;
    const f64 determinant = a * d - b * c;
    if (std::fabs(determinant) < 1e-10) return {};
    TileTestRay result;
    result.pixel = {static_cast<f32>((d * tx - b * ty) / determinant),
                    static_cast<f32>((a * ty - c * tx) / determinant)};
    const Vec4 projected = m * Vec4{result.pixel.x, result.pixel.y, 0, 1};
    result.valid = std::isfinite(result.pixel.x) && std::isfinite(result.pixel.y) && projected.w > 1e-5f
        && projected.z >= 0 && projected.z <= projected.w;
    return result;
}

// Decode and premultiply BEFORE bilinear interpolation, as required for a
// filtered transparent source. Hidden RGB cannot leak into the periodic wall.
Vec4 tile_test_source_sample(const ImagePixels& pixels, Vec2 uv) {
    const f32 x = std::clamp(uv.x * pixels.width - .5f, 0.f, static_cast<f32>(pixels.width - 1));
    const f32 y = std::clamp(uv.y * pixels.height - .5f, 0.f, static_cast<f32>(pixels.height - 1));
    const u32 x0 = static_cast<u32>(std::floor(x)), y0 = static_cast<u32>(std::floor(y));
    const u32 x1 = std::min(x0 + 1, pixels.width - 1), y1 = std::min(y0 + 1, pixels.height - 1);
    const f32 fx = x - x0, fy = y - y0;
    auto texel = [&](u32 px, u32 py) {
        const auto* p = pixels.rgba.data() + (py * pixels.width + px) * 4;
        const f32 alpha = p[3] / 255.f;
        return Vec4{srgb_decode(p[0] / 255.f) * alpha, srgb_decode(p[1] / 255.f) * alpha,
                    srgb_decode(p[2] / 255.f) * alpha, alpha};
    };
    return texel(x0, y0) * ((1 - fx) * (1 - fy)) + texel(x1, y0) * (fx * (1 - fy))
         + texel(x0, y1) * ((1 - fx) * fy) + texel(x1, y1) * (fx * fy);
}
}

AUREA_TEST(Gpu, MotionTilePeriodicControlsMatchLinearPremultipliedSource) {
    AUREA_REQUIRE_GPU();
    for (u32 kind = 0; kind < 6; ++kind) {
        Scene scene(384, 216);
        scene.comp->set_background(Color{0, 0, 0, 0});
        scene.comp->set_transparent_background(true);
        auto source = uniform_image(128, 96, 0, 0, 0);
        for (u32 y = 0; y < source.height; ++y) for (u32 x = 0; x < source.width; ++x) {
            auto* p = source.rgba.data() + (y * source.width + x) * 4;
            p[0] = static_cast<u8>(30 + x * 180 / 127); p[1] = static_cast<u8>(45 + y * 140 / 95);
            p[2] = static_cast<u8>(80 + (x + y) * 65 / 222); p[3] = static_cast<u8>(45 + (x + y) * 150 / 222);
        }
        const auto id = scene.image(source, 192, 108, 2.f);
        auto* layer = scene.comp->layer(id);
        layer->threeD = true; layer->transform.rotation.x = 22; layer->transform.opacity = .73f;
        if (kind == 5) scene.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = -1;
        auto& effect = scene.add_effect(id, effect_keys::kMotionTile);
        motion_tile::Params p;
        p.mirror = kind == 1 || kind == 2 || kind >= 4;
        if (kind == 2) { p.centerX = .2f; p.centerY = .85f; p.tileX = .9f; p.tileY = 1.3f; }
        if (kind == 3) p.phaseTurns = 1.75f;
        if (kind == 4) { p.phaseTurns = -.5f; p.horizontalPhase = true; }
        if (kind == 5) {
            p.tileX = .75f; p.tileY = .9f; p.centerX = 1.2f; p.centerY = -.1f;
            p.outputX = .65f; p.outputY = .7f;
            effect.params[motion_tile::kScale].constant.v[0] = 60;
        }
        effect.params[motion_tile::kCenter].constant = ParamValue::vec2(p.centerX, p.centerY);
        const f32 scale = kind == 5 ? .6f : 1.f;
        effect.params[motion_tile::kTileWidth].constant.v[0] = p.tileX * 100 / scale;
        effect.params[motion_tile::kTileHeight].constant.v[0] = p.tileY * 100 / scale;
        effect.params[motion_tile::kOutputWidth].constant.v[0] = p.outputX * 100;
        effect.params[motion_tile::kOutputHeight].constant.v[0] = p.outputY * 100;
        effect.params[motion_tile::kMirror].constant = ParamValue::boolean(p.mirror);
        effect.params[motion_tile::kPhase].constant.v[0] = p.phaseTurns * 360;
        effect.params[motion_tile::kHorizontalPhase].constant = ParamValue::boolean(p.horizontalPhase);
        const auto actual = scene.render(); const Mat4 m = tile_test_plane_matrix(scene, id);
        u32 compared = 0, outside = 0, leaked = 0; f32 worst = 0;
        for (u32 y = 3; y + 3 < 216; y += 3) for (u32 x = 3; x + 3 < 384; x += 3) {
            const auto ray = tile_test_ray(m, x + .5, y + .5);
            if (!ray.valid) continue;
            const f32 ndcX = 2 * (x + .5f) / 384 - 1, ndcY = 2 * (y + .5f) / 216 - 1;
            if (std::fabs(ndcX) > p.outputX || std::fabs(ndcY) > p.outputY) {
                ++outside; const Vec4 value = actual.v(x, y);
                leaked += value.x > .001f || value.y > .001f || value.z > .001f || value.w > .001f;
                continue;
            }
            const Vec2 uv = motion_tile::reference_lookup(p, {ray.pixel.x / 128, ray.pixel.y / 96});
            // Magnified smooth source: point sampling is well-defined away
            // from a tile edge; the separate horizon test integrates pixels.
            if (uv.x < .08f || uv.y < .08f || uv.x > .92f || uv.y > .92f) continue;
            Vec4 expected = tile_test_source_sample(source, uv) * .73f;
            if (kind == 5) { expected.x *= .5f; expected.y *= .5f; expected.z *= .5f; }
            const Vec4 got = actual.v(x, y);
            const f32 expectedChannels[]{expected.x, expected.y, expected.z, expected.w};
            const f32 actualChannels[]{got.x, got.y, got.z, got.w};
            for (u32 channel = 0; channel < 4; ++channel)
                worst = std::max(worst, std::fabs(actualChannels[channel] - expectedChannels[channel]));
            ++compared;
        }
        std::printf("    periodic controls %u: compared%u outside%u leaked%u maxRGBAerror%.5f\n",
                    kind, compared, outside, leaked, worst);
        AUREA_CHECK(compared > 300u); AUREA_CHECK(worst < .015f);
        if (kind == 5) { AUREA_CHECK(outside > 100u); AUREA_CHECK_EQ(leaked, 0u); }
    }
}

AUREA_TEST(Gpu, MotionTilePeriodicPrefixColorAndBlurKeepTheirMirroredPeriod) {
    AUREA_REQUIRE_GPU();
    for (f32 angle : {0.f, -75.f, -88.4f}) {
        Scene scene(256, 144);
        auto source = reference_image(128, 72);
        for (usize pixel = 3; pixel < source.rgba.size(); pixel += 4) source.rgba[pixel] = 150;
        const auto id = scene.image(std::move(source), 128, 72);
        auto* layer = scene.comp->layer(id);
        layer->threeD = true; layer->transform.rotation.x = angle;
        scene.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = -.5f;
        scene.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 4;
        auto& tile = scene.add_effect(id, effect_keys::kMotionTile);
        tile.params[motion_tile::kTileWidth].constant.v[0] = 50;
        tile.params[motion_tile::kTileHeight].constant.v[0] = 50;
        tile.params[motion_tile::kMirror].constant = ParamValue::boolean(true);
        tile.params[motion_tile::kPhase].constant.v[0] = 180;
        const auto centered = scene.render();
        layer->transform.position.x -= 128; // two 64px mirrored cells
        const auto moved = scene.render(); f32 worst = 0; f64 energy = 0;
        for (usize channel = 0; channel < moved.px.size(); ++channel) {
            worst = std::max(worst, std::fabs(centered.px[channel] - moved.px[channel]));
            if (channel % 4 < 3) energy += centered.px[channel];
        }
        std::printf("    3D prefix color+blur X%.1f: periodic error%.5f energy%.2f\n", angle, worst, energy);
        AUREA_CHECK(energy > 100); AUREA_CHECK(worst < .015f);
    }
}

AUREA_TEST(Gpu, MotionTilePeriodicPyramidHasBoundedTransientCostAndPoolReuse) {
    AUREA_REQUIRE_GPU();
    Scene scene(256, 144);
    const auto id = scene.image(uniform_image(512, 256, 200, 130, 80), 128, 72);
    auto* layer = scene.comp->layer(id); layer->threeD = true;
    scene.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant = ParamValue::boolean(true);
    u64 peak = 0;
    for (const f32 angle : {0.f, -88.4f, -101.2f, 0.f}) {
        layer->transform.rotation.x = angle; (void)scene.render();
        const auto stats = gpu().renderer.graph_stats(); peak = std::max(peak, stats.transientBytes);
        const std::string dump = gpu().renderer.graph_dump();
        AUREA_CHECK(dump.find("scene-tile-prefilter") != std::string::npos);
        AUREA_CHECK(dump.find("motion-tile") == std::string::npos);
        AUREA_CHECK(stats.transientTextures < 30u);
        AUREA_CHECK(stats.transientBytes < (8ull << 20));
        std::printf("    periodic pyramid X%.1f: passes%u textures%u bytes%llu\n", angle,
                    stats.passesExecuted, stats.transientTextures, stats.transientBytes);
    }
    (void)scene.render(); (void)scene.render();
    const u64 created = gpu().renderer.pool_stats().createdTotal;
    for (u32 frame = 0; frame < 6; ++frame) (void)scene.render();
    std::printf("    periodic pyramid peak%llu reused pool created%llu ->%llu\n", peak,
                created, gpu().renderer.pool_stats().createdTotal);
    AUREA_CHECK_EQ(gpu().renderer.pool_stats().createdTotal, created);
}

AUREA_TEST(Gpu, MotionTilePeriodicPlaneDepthIntersectsAnOwnOpaqueModel) {
    AUREA_REQUIRE_GPU();
    u32 centerBlue[3]{};
    for (u32 kind = 0; kind < 3; ++kind) {
        Scene3DRig rig(320, 180);
        ModelImport model; model.path = write_triangle_gltf(true, 1.f, 0.f, 0.f);
        const auto modelId = rig.e.import_model(model);
        const auto reference = rig.capture(320);
        const auto imageId = rig.e.import_image(uniform_image(64, 64, 0, 80, 255).rgba.data(), 64, 64,
                                               "Own periodic blue", nullptr);
        AUREA_CHECK(modelId.ok() && imageId.ok()); if (!modelId.ok() || !imageId.ok()) return;
        auto* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        const LayerId id = LayerId::unpack(*imageId); auto* layer = comp->layer(id);
        layer->threeD = true; layer->transform.position = {160, 90, kind == 0 ? 100.f : kind == 1 ? -100.f : 0.f};
        layer->transform.rotation.y = kind == 2 ? 60.f : 0.f;
        push_effect(rig, id, effect_keys::kMotionTile);
        const auto image = rig.capture(320); u32 blue = 0, red = 0;
        for (u32 y = 75; y < 105; ++y) for (u32 x = 130; x < 190; ++x) {
            // The own model is a triangle, not a rectangle. Only its opaque
            // reference silhouette has a depth-occlusion expectation.
            if (reference.at(x, y)[0] <= 220) continue;
            const auto* pixel = image.at(x, y);
            blue += pixel[2] > 220 && pixel[0] < 10; red += pixel[0] > 220 && pixel[2] < 10;
        }
        centerBlue[kind] = blue;
        std::printf("    periodic plane/model %u: blue%u red%u\n", kind, blue, red);
        if (kind == 0) { AUREA_CHECK_EQ(blue, 0u); AUREA_CHECK(red > 1000u); }
        if (kind == 1) { AUREA_CHECK(blue > 1000u); AUREA_CHECK_EQ(red, 0u); }
        if (kind == 2) { AUREA_CHECK(blue > 100u); AUREA_CHECK(red > 100u); }
    }
    AUREA_CHECK(centerBlue[2] < centerBlue[1]);
}

AUREA_TEST(Gpu, MotionTilePeriodicPlanePreservesTheAcceptsLightsSetting) {
    AUREA_REQUIRE_GPU();
    Scene scene(128, 128);
    const auto id = scene.image(uniform_image(96, 96, 255, 255, 255), 64, 64);
    auto* plane = scene.comp->layer(id); plane->threeD = true;
    scene.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant = ParamValue::boolean(true);
    const auto lightId = scene.comp->add_layer(LayerKind::Light, "Own point light");
    auto* light = scene.comp->layer(lightId); light->threeD = true;
    light->end = scene.comp->duration(); light->light.kind = LightKind::Point;
    light->light.intensity = 1; light->light.range = 0;
    light->transform.position = {64, 64, -100};
    const f32 refused = scene.render().v(64, 64).x;
    plane->acceptsLights = true;
    const f32 face = scene.render().v(64, 64).x;
    plane->transform.rotation.y = 70;
    const f32 angled = scene.render().v(64, 64).x;
    plane->transform.rotation.y = 0; light->transform.position.z = 100;
    const f32 behind = scene.render().v(64, 64).x;
    std::printf("    periodic accepts lights: refused%.4f face%.4f angled%.4f behind%.4f\n", refused, face, angled, behind);
    AUREA_CHECK_NEAR(refused, 1.f, .005f); AUREA_CHECK_NEAR(face, 1.f, .01f);
    AUREA_CHECK(angled > .1f && angled < .6f); AUREA_CHECK(behind < .005f);
}

AUREA_TEST(Gpu, MotionTilePeriodicMinificationIntegratesTheUnmirroredTileSeam) {
    AUREA_REQUIRE_GPU();
    for (const bool mirror : {false, true}) {
        Scene scene(96, 96);
        auto source = uniform_image(128, 128, 0, 0, 0);
        for (u32 y = 0; y < 128; ++y) for (u32 x = 64; x < 128; ++x) {
            auto* p = source.rgba.data() + (y * 128 + x) * 4; p[0] = p[1] = p[2] = 255;
        }
        const auto id = scene.image(source, 48.5f, 48, .125f);
        scene.comp->layer(id)->threeD = true;
        scene.add_effect(id, effect_keys::kMotionTile).params[motion_tile::kMirror].constant = ParamValue::boolean(mirror);
        const auto actual = scene.render(); f32 texelScale = 0;
        const Mat4 m = tile_test_plane_matrix(scene, id, &texelScale);
        std::printf("    seam graph mirror%d:\n%s\n", mirror, gpu().renderer.graph_dump().c_str());
        const auto center = tile_test_ray(m, 40.5, 48.5), next = tile_test_ray(m, 41.5, 48.5);
        const f32 logicalFootprint = std::fabs(next.pixel.x - center.pixel.x);
        std::printf("    seam density%.9f base%.1fx%.1f logicalFootprint%.9f workingFootprint%.9f\n",
            texelScale, source.width * texelScale, source.height * texelScale, logicalFootprint, logicalFootprint * texelScale);
        motion_tile::Params params; params.mirror = mirror;
        f64 expected = 0;
        for (u32 sample = 0; sample < 4096; ++sample) {
            const auto ray = tile_test_ray(m, 40 + (sample + .5) / 4096, 48.5);
            AUREA_CHECK(ray.valid);
            expected += tile_test_source_sample(source, motion_tile::reference_lookup(params,
                {ray.pixel.x / 128, ray.pixel.y / 128})).x;
        }
        expected /= 4096;
        const f64 error = std::fabs(expected - actual.at(40, 48)[0]);
        std::printf("    isotropic seam mirror%d: expected%.5f actual%.5f error%.5f\n",
                    mirror, expected, actual.at(40, 48)[0], error);
        AUREA_CHECK(error < .03);
    }
}

// MOTION BLUR COM MOTION TILE (relato: "se tiver motion tile, o motion blur
// não funfa"). Um Motion Tile terminal num plano 3D não é rasterizado como
// parede: o quad cobre o quadro inteiro e cada raio é resolvido pela inversa
// (NDC → camada) gravada no plano periódico. Enquanto o obturador reusava a
// inversa do instante do quadro, as K amostras desenhavam a parede na MESMA
// pose e a média saía idêntica ao quadro sem desfoque.
AUREA_TEST(Gpu, MotionTileOnAPlaneStillSmearsOnTheShutter) {
    AUREA_REQUIRE_GPU();
    Scene scene(320, 180);
    const auto id = scene.image(reference_image(96, 54), 100, 90);
    auto* layer = scene.comp->layer(id);
    layer->threeD = true;
    // 20 px por quadro, como o teste 2D do desfoque.
    Track& px = layer->tracks.get_or_create(TrackProperty::PositionX);
    px.set(layer->local_time(FrameIndex{0}), 100.f);
    px.set(layer->local_time(FrameIndex{10}), 300.f);
    auto& tile = scene.add_effect(id, effect_keys::kMotionTile);
    tile.params[motion_tile::kTileWidth].constant.v[0] = 40;
    tile.params[motion_tile::kTileHeight].constant.v[0] = 40;
    tile.params[motion_tile::kMirror].constant = ParamValue::boolean(true);
    auto& mb = scene.comp->motion_blur();
    mb.enabled = true; mb.samples = 16; mb.previewSamples = 16;
    mb.shutterAngle = 360; mb.shutterPhase = -90; mb.adaptiveLimit = 128;
    const FrameIndex at{5};
    const FloatImage sharp = scene.render(at);
    layer->motionBlur = true;
    const FloatImage blurred = scene.render(at);
    f32 worst = 0; u32 changed = 0; f64 energySharp = 0, energyBlurred = 0;
    for (usize i = 0; i < sharp.px.size(); ++i) {
        worst = std::max(worst, std::fabs(sharp.px[i] - blurred.px[i]));
        if (i % 4 < 3) { energySharp += sharp.px[i]; energyBlurred += blurred.px[i]; }
    }
    for (usize i = 0; i + 3 < sharp.px.size(); i += 4) {
        bool diff = false;
        for (u32 k = 0; k < 3; ++k) diff |= std::fabs(sharp.px[i + k] - blurred.px[i + k]) > .004f;
        changed += diff ? 1u : 0u;
    }
    std::printf("    plano com Motion Tile: %u pixels mudam, máximo %.4f, energia %.3f\n",
                changed, worst, energyBlurred / std::max(1e-6, energySharp));
    AUREA_CHECK(worst > .05f);           // a parede anda dentro do obturador
    AUREA_CHECK(changed > 300u);
    AUREA_CHECK(energyBlurred / std::max(1e-6, energySharp) > .7);
    AUREA_CHECK(energyBlurred / std::max(1e-6, energySharp) < 1.3);
    // Desligado, nada muda: o desfoque não é um filtro fixo na imagem.
    layer->motionBlur = false;
    const FloatImage again = scene.render(at);
    f32 repeat = 0;
    for (usize i = 0; i < sharp.px.size(); ++i) repeat = std::max(repeat, std::fabs(sharp.px[i] - again.px[i]));
    AUREA_CHECK(repeat < .002f);
}
