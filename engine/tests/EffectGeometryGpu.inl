// A uniform source makes missing coverage and projected cropping measurable.
AUREA_TEST(EffectGeometryGpu, MotionTileOutputWindowFollowsTheFrameUnderPerspective) {
    AUREA_REQUIRE_GPU();
    for (bool threeD : {false, true}) {
        Scene scene(160, 90);
        const LayerId id = scene.image(uniform_image(160, 90, 220, 180, 100), 80, 45, 2.0f);
        Layer* layer = scene.comp->layer(id);
        layer->threeD = threeD;
        if (threeD) layer->transform.rotation = {15.0f, 28.0f, 0.0f};
        auto& tile = scene.add_effect(id, effect_keys::kMotionTile);
        tile.params[motion_tile::kTileWidth].constant.v[0] = 50;
        tile.params[motion_tile::kTileHeight].constant.v[0] = 50;
        tile.params[motion_tile::kMirror].constant.v[0] = 1;
        const FloatImage full = scene.render();
        tile.params[motion_tile::kOutputWidth].constant.v[0] = 50;
        tile.params[motion_tile::kOutputHeight].constant.v[0] = 50;
        const FloatImage cropped = scene.render();
        u32 fullOutside = 0, croppedOutside = 0, retainedInside = 0;
        for (u32 y = 4; y + 4 < full.height; ++y) {
            for (u32 x = 4; x + 4 < full.width; ++x) {
                const bool outside = x < 36 || x >= 124 || y < 19 || y >= 71;
                if (outside) {
                    if (full.at(x, y)[0] > 0.05f) ++fullOutside;
                    if (cropped.at(x, y)[0] > 0.01f) ++croppedOutside;
                } else if (x >= 44 && x < 116 && y >= 27 && y < 63) {
                    if (cropped.at(x, y)[0] > 0.05f) ++retainedInside;
                }
            }
        }
        std::printf("    tile output 50%% (%s): full outside=%u cropped outside=%u interior=%u\n",
                    threeD ? "perspective" : "2D", fullOutside, croppedOutside, retainedInside);
        AUREA_CHECK(fullOutside > 4000u);
        AUREA_CHECK_EQ(croppedOutside, 0u);
        AUREA_CHECK(retainedInside > 2000u);
    }
}

AUREA_TEST(EffectGeometryGpu, MotionTileWindowHandlesNearPlaneAndBehindCamera) {
    AUREA_REQUIRE_GPU();
    Scene scene(160, 90);
    const auto id = scene.image(uniform_image(160, 90, 220, 180, 100), 80, 45, 2);
    auto* layer = scene.comp->layer(id);
    layer->threeD = true;
    auto& tile = scene.add_effect(id, effect_keys::kMotionTile);
    tile.params[motion_tile::kTileWidth].constant.v[0] = 50;
    tile.params[motion_tile::kTileHeight].constant.v[0] = 50;
    tile.params[motion_tile::kMirror].constant.v[0] = 1;
    // Default camera is at z=-1.2*height, with positive Z forward.
    for (const f32 z : {-220.0f, -107.5f, -100.0f}) {
        layer->transform.position.z = z;
        for (const f32 output : {100.0f, 50.0f}) {
            tile.params[motion_tile::kOutputWidth].constant.v[0] = output;
            tile.params[motion_tile::kOutputHeight].constant.v[0] = output;
            const auto pixels = scene.render();
            u32 lit=0, outside=0, finite=0;
            for (u32 y=0;y<pixels.height;++y) for(u32 x=0;x<pixels.width;++x) {
                const auto* p=pixels.at(x,y);
                finite += std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
                lit += p[0]>.01f;
                if(x<36 || x>=124 || y<19 || y>=71) outside += p[0]>.01f;
            }
            AUREA_CHECK_EQ(finite,pixels.width*pixels.height);
            if(z < -107.0f) AUREA_CHECK_EQ(lit,0u);
            else {
                AUREA_CHECK(lit>1000u);
                if(output==50) AUREA_CHECK_EQ(outside,0u);
            }
        }
    }
}

AUREA_TEST(EffectGeometryGpu, WaveAngleIsContinuousPeriodicAndKeepsPreviousDirections) {
    AUREA_REQUIRE_GPU();
    Scene scene(160, 90);
    const auto id = scene.image(reference_image(160, 90), 80, 45);
    auto& wave = scene.add_effect(id, effect_keys::kWaveWarp);
    AUREA_CHECK_EQ(wave.params.size(), 10u);
    if (wave.params.size() != 10) return;
    wave.params[0].constant.v[0] = 11;
    wave.params[1].constant.v[0] = 43;
    wave.params[3].constant.v[0] = 28;
    wave.params[6].constant.v[0] = 0; // Repeat the independently generated pattern.
    auto difference = [](const FloatImage& a, const FloatImage& b) {
        f32 maximum = 0; u32 changed = 0;
        for (u32 y = 0; y < a.height; ++y) for (u32 x = 0; x < a.width; ++x)
            for (u32 c = 0; c < 3; ++c) {
                const f32 delta = std::fabs(a.at(x,y)[c] - b.at(x,y)[c]);
                maximum = std::max(maximum, delta); changed += delta > .01f;
            }
        return std::make_pair(maximum, changed);
    };
    std::array<FloatImage,4> previous;
    for (u32 mode=0; mode<4; ++mode) {
        wave.params[4].constant.v[0] = static_cast<f32>(mode);
        wave.params[9].constant.v[0] = 0;
        previous[mode] = scene.render();
        wave.params[9].constant.v[0] = 37;
        AUREA_CHECK_NEAR(difference(previous[mode], scene.render()).first, 0, 0);
    }
    wave.params[4].constant.v[0] = 4;
    wave.params[9].constant.v[0] = 0;
    AUREA_CHECK_NEAR(difference(previous[0], scene.render()).first, 0, .001f);
    wave.params[9].constant.v[0] = 37;
    const auto rotated = scene.render();
    for (const auto& old : previous) AUREA_CHECK(difference(old, rotated).second > 1000u);
    wave.params[9].constant.v[0] = -323;
    AUREA_CHECK_NEAR(difference(rotated, scene.render()).first, 0, .001f);
    wave.params[9].constant.v[0] = 38;
    const auto adjacent = difference(rotated, scene.render());
    AUREA_CHECK(adjacent.second > 100u);
    wave.params[9].constant.v[0] = 37;
    wave.params[2].constant.v[0] = 2;
    AUREA_CHECK(difference(rotated, scene.render(FrameIndex{5})).second > 1000u);
}
