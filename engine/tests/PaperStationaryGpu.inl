// Independent content invariants: bending a remote part of a sheet must not
// low-pass stationary text strokes or alter its transparent pixels.
AUREA_TEST(PaperStationaryGpu, RemoteDeformationsPreserveFinePrintAndAlpha) {
    AUREA_REQUIRE_GPU();
    const char* keys[]{"aurea.distort.bender", "aurea.distort.bend",
                       "aurea.distort.curl", "aurea.distort.page_turn"};
    for (u32 mode = 0; mode < 4; ++mode) for (bool alphaPattern : {false, true}) {
        Scene scene(192, 128);
        scene.comp->set_transparent_background(true);
        auto pixels = uniform_image(192, 128, 0, 0, 0);
        for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 192; ++x) {
            auto* p = pixels.rgba.data() + (usize(y) * 192 + x) * 4;
            // One-pixel black/white type-like strokes. The other case makes
            // those same strokes transparent, independently testing coverage.
            const u8 ink = ((x + y) & 1) ? 255 : 0;
            p[0] = p[1] = p[2] = alphaPattern ? 255 : ink;
            p[3] = alphaPattern ? ink : 255;
            if (p[3] == 0) p[0] = p[1] = p[2] = 0;
        }
        const auto id = scene.image(std::move(pixels), 96, 64);
        const auto original = scene.render();
        auto& fx = scene.add_effect(id, keys[mode]);
        if (mode == 0) {
            fx.params[0].constant.v[0] = 20;
            fx.params[1].constant = ParamValue::vec2(.25f, .5f);
            fx.params[2].constant = ParamValue::vec2(.75f, .5f);
        } else if (mode == 1 || mode == 2) {
            fx.params[0].constant.v[0] = 60;
            if (mode == 2) fx.params[1].constant.v[0] = 20;
        } else {
            // Fold origin at x=152, silhouette at x=160. This keeps the output
            // texel grid aligned so a failure is not a fractional crop effect.
            fx.params[0].constant.v[0] = 100.f * 40.f / (192.f + 3.14159265358979323846f * 8.f);
            fx.params[2].constant.v[0] = 8;
        }
        for (bool finalQuality : {false, true}) {
            const auto result = scene.render(FrameIndex{0}, 1, finalQuality);
            f32 stationaryError = 0, overallChange = 0;
            for (usize i = 0; i < original.px.size(); ++i)
                overallChange = std::max(overallChange, std::fabs(original.px[i] - result.px[i]));
            // Far to the left of both the bend and every returned/back sheet.
            for (u32 y = 24; y < 104; ++y) for (u32 x = 8; x < 32; ++x)
                for (u32 c = 0; c < 4; ++c)
                    stationaryError = std::max(stationaryError,
                        std::fabs(original.at(x, y)[c] - result.at(x, y)[c]));
            std::printf("    %s alpha=%d final=%d stationary max %.6f; active change %.6f\n",
                        keys[mode], alphaPattern, finalQuality, stationaryError, overallChange);
            AUREA_CHECK(overallChange > .1f); // Reject a bypassed/neutral effect.
            AUREA_CHECK_NEAR(stationaryError, 0, .002f);
        }
    }
}

AUREA_TEST(PaperStationaryGpu, FractionalCurlBoundaryDoesNotResampleStationaryPrint) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 128);
    auto pixels = uniform_image(192, 128, 0, 0, 0);
    for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 192; ++x) {
        auto* p = pixels.rgba.data() + (usize(y) * 192 + x) * 4;
        p[0] = p[1] = p[2] = (x & 1) ? 255 : 0;
    }
    const auto id = scene.image(std::move(pixels), 96, 64);
    const auto original = scene.render();
    auto& fx = scene.add_effect(id, "aurea.distort.page_turn");
    fx.params[0].constant.v[0] = 25;
    fx.params[2].constant.v[0] = 8;
    for (bool finalQuality : {false, true}) {
        const auto result = scene.render(FrameIndex{0}, 1, finalQuality);
        f32 stationaryError = 0, overallChange = 0;
        for (usize i = 0; i < original.px.size(); ++i)
            overallChange = std::max(overallChange, std::fabs(original.px[i] - result.px[i]));
        for (u32 y = 24; y < 104; ++y) for (u32 x = 8; x < 32; ++x)
            for (u32 c = 0; c < 4; ++c)
                stationaryError = std::max(stationaryError,
                    std::fabs(original.at(x, y)[c] - result.at(x, y)[c]));
        std::printf("    page fractional crop final=%d stationary max %.6f; active change %.6f\n",
                    finalQuality, stationaryError, overallChange);
        AUREA_CHECK(overallChange > .1f);
        AUREA_CHECK_NEAR(stationaryError, 0, .002f);
    }
}
