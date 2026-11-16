#include "aurea/effects/FastBlurReference.hpp"

namespace {
f64 reference_box_value(const std::vector<fast_blur_reference::Segment> &segments, i32 distance) {
    for (const auto &segment : segments) {
        if (distance < segment.low || distance > segment.high)
            continue;
        const f64 position = segment.direction * (distance - segment.anchor) / segment.scale;
        return (segment.c * position + segment.b) * position + segment.a;
    }
    return 0;
}
ImagePixels fast_blur_square() {
    ImagePixels pixels = uniform_image(256, 144, 0, 0, 0, 0);
    for (u32 y = 64; y < 80; ++y)
        for (u32 x = 120; x < 136; ++x) {
            u8 *pixel = pixels.rgba.data() + (y * 256 + x) * 4;
            pixel[0] = pixel[1] = pixel[2] = pixel[3] = 255;
        }
    return pixels;
}
ImagePixels fast_blur_bars() {
    ImagePixels pixels = uniform_image(256, 144, 0, 0, 0, 255);
    constexpr u8 colors[8][3]{{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                              {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0}};
    for (u32 y = 0; y < 144; ++y)
        for (u32 x = 0; x < 256; ++x) {
            u8 *pixel = pixels.rgba.data() + (y * 256 + x) * 4;
            std::copy(std::begin(colors[x / 32]), std::end(colors[x / 32]), pixel);
        }
    return pixels;
}
} // namespace

AUREA_TEST(FastBlurMath, FractionalThreeBoxesMatchDiscreteConvolution) {
    for (f64 radius : {.001, .36900369, 1., 5. / 2.71, 10. / 2.71, 100. / 2.71}) {
        const u32 whole = static_cast<u32>(std::floor(radius));
        const f64 fraction = radius - whole;
        std::vector<f64> box(2 * whole + 3, 1 / (2 * radius + 1));
        box.front() = box.back() = fraction / (2 * radius + 1);
        std::vector<f64> expected{1};
        for (u32 pass = 0; pass < 3; ++pass) {
            std::vector<f64> next(expected.size() + box.size() - 1, 0);
            for (usize i = 0; i < expected.size(); ++i)
                for (usize j = 0; j < box.size(); ++j)
                    next[i + j] += expected[i] * box[j];
            expected = std::move(next);
        }
        const auto segments = fast_blur_reference::box_segments(radius);
        AUREA_CHECK(segments.size() <= fast_blur_reference::kMaxSegments);
        const i32 support = static_cast<i32>(expected.size() / 2);
        f64 mass = 0;
        for (i32 distance = -support; distance <= support; ++distance) {
            const f64 value = reference_box_value(segments, distance);
            AUREA_CHECK_NEAR(value, expected[distance + support], 1e-12);
            mass += value;
        }
        AUREA_CHECK_NEAR(mass, 1, 1e-12);
    }
}

AUREA_TEST(Gpu, FastBlurReferenceMatchesSavedNativeAlphaAndRgb) {
    AUREA_REQUIRE_GPU();
    const auto directory = std::filesystem::path(AUREA_GOLDEN_DIR).parent_path().parent_path().parent_path() /
                           "build/reference/effects-20261115/added/runtime/frames/decoded";
    if (!std::filesystem::exists(directory / "fast-blur-r1.rgba32f")) {
        std::printf("(native reference fixtures unavailable) ");
        return;
    }
    struct Case {
        const char *file;
        f32 radius;
        u32 dimensions;
        bool repeat, bars;
    };
    const Case cases[]{{"fast-blur-r0", 0, 0, true, false},   {"fast-blur-r1", 1, 0, true, false},
                       {"fast-blur-r2", 2, 0, true, false},   {"fast-blur-r5", 5, 0, true, false},
                       {"fast-blur-r10", 10, 0, true, false}, {"fast-blur-dim2", 5, 1, true, false},
                       {"fast-blur-dim3", 5, 2, true, false}, {"fast-blur-no-repeat", 10, 0, false, true}};
    for (const auto &sample : cases) {
        Scene scene(256, 144, 30);
        scene.comp->set_transparent_background(true);
        scene.comp->set_background(Color{0, 0, 0, 0});
        const LayerId id = scene.image(sample.bars ? fast_blur_bars() : fast_blur_square(), 128, 72);
        auto &effect = scene.add_effect(id, fast_blur_reference::kKey);
        effect.params[0].constant = ParamValue::scalar(sample.radius);
        effect.params[1].constant = ParamValue::scalar(static_cast<f32>(sample.dimensions));
        effect.params[2].constant = ParamValue::boolean(sample.repeat);
        const FloatImage result = scene.render(FrameIndex{0}, 1, true);
        std::vector<f32> native(result.px.size());
        std::ifstream stream(directory / (std::string(sample.file) + ".rgba32f"), std::ios::binary);
        stream.read(reinterpret_cast<char *>(native.data()),
                    static_cast<std::streamsize>(native.size() * sizeof(f32)));
        AUREA_CHECK_EQ(stream.gcount(), static_cast<std::streamsize>(native.size() * sizeof(f32)));
        f32 alpha = 0, rgb = 0;
        bool finite = true;
        for (usize index = 0; index < result.px.size(); ++index) {
            finite &= std::isfinite(result.px[index]);
            const f32 difference = std::abs(result.px[index] - native[index]);
            if (index % 4 == 3)
                alpha = std::max(alpha, difference);
            else
                rgb = std::max(rgb, difference);
        }
        std::printf("    %s: alpha max %.8f, RGB max %.8f\n", sample.file, alpha, rgb);
        AUREA_CHECK(finite);
        // Native PNG encoding quantizes RGB to 10/65535. Engine output uses
        // RGBA16F after full-precision filtering. These tolerances include both.
        AUREA_CHECK(alpha < .001f);
        AUREA_CHECK(rgb < .001f);
    }
}

AUREA_TEST(Gpu, FastBlurReferenceHugeRadiusPreservesHdrAndTransparentSupport) {
    AUREA_REQUIRE_GPU();
    Scene scene(17, 9, 30);
    scene.comp->set_transparent_background(true);
    scene.comp->set_background(Color{0, 0, 0, 0});
    const LayerId id = scene.solid(17, 9, Vec4{4, 2, 1, .5f}, 8.5f, 4.5f);
    const FloatImage baseline = scene.render(FrameIndex{0}, 1, true);
    auto &effect = scene.add_effect(id, fast_blur_reference::kKey);
    effect.params[0].constant = ParamValue::scalar(30000);
    const FloatImage repeated = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(max_diff(repeated, baseline) < .004f);
    effect.params[2].constant = ParamValue::boolean(false);
    const FloatImage transparent = scene.render(FrameIndex{0}, 1, true);
    const auto segments = fast_blur_reference::box_segments(30000. / 2.71);
    f64 horizontal = 0, vertical = 0;
    for (i32 x = 0; x < 17; ++x)
        horizontal += reference_box_value(segments, x - 8);
    for (i32 y = 0; y < 9; ++y)
        vertical += reference_box_value(segments, y - 4);
    AUREA_CHECK_NEAR(transparent.v(8, 4).w, baseline.v(8, 4).w * horizontal * vertical, 1e-7);
    AUREA_CHECK(transparent.v(8, 4).w > 0);
    for (f32 value : transparent.px)
        AUREA_CHECK(std::isfinite(value));
}

AUREA_TEST(Gpu, FastBlurReferenceRasterFallbackMatchesComputeAndKeepsOffViewportSource) {
    AUREA_REQUIRE_GPU();
    Scene scene(16, 9, 30);
    scene.comp->set_transparent_background(true);
    scene.comp->set_background(Color{0, 0, 0, 0});
    ImagePixels source = uniform_image(64, 9, 0, 0, 0, 0);
    for (u32 y = 0; y < 9; ++y)
        for (u32 x = 20; x < 26; ++x) {
            u8 *pixel = source.rgba.data() + (y * 64 + x) * 4;
            pixel[0] = pixel[1] = pixel[2] = pixel[3] = 255;
        }
    const LayerId id = scene.image(std::move(source), 32, 4.5f);
    auto &effect = scene.add_effect(id, fast_blur_reference::kKey);
    effect.params[0].constant = ParamValue::scalar(10);
    effect.params[1].constant = ParamValue::scalar(1);
    effect.params[2].constant = ParamValue::boolean(false);
    auto &shaders = gpu().renderer.shaders();
    AUREA_CHECK(shaders.pipeline(PipelineKey::compute_shader(ShaderId::effects_reference_blur_comp)).ok());
    const FloatImage computed = scene.render(FrameIndex{0}, 1, true);
    struct Restore {
        ShaderLibrary &shaders;
        ~Restore() {
            shaders.set_test_failing_shader(ShaderId::Count);
        }
    } restore{shaders};
    shaders.set_test_failing_shader(ShaderId::effects_reference_blur_comp);
    const FloatImage raster = scene.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(computed.v(15, 4).w > .02f);
    AUREA_CHECK(max_diff(computed, raster) < .001f);
}
