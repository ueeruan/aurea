// Own scenes reconstruct requested behavior. User videos/build2183 are from
// another app; these tests neither use its assets nor establish its internals.
namespace reference3d {

struct ColorCoverage { u32 visible = 0, chromatic = 0, red = 0, green = 0, blue = 0; };
struct PixelDifference { u32 maximum = 0, changed = 0; f64 mean = 0; };
PixelDifference difference(const Image8& a, const Image8& b) {
    PixelDifference result;
    AUREA_CHECK_EQ(a.rgba.size(), b.rgba.size());
    if (a.rgba.size() != b.rgba.size() || a.rgba.empty()) return result;
    f64 sum = 0;
    for (usize i = 0; i + 3 < a.rgba.size(); i += 4) {
        u32 pixelMaximum = 0;
        for (usize channel = 0; channel < 3; ++channel) {
            const auto delta = static_cast<u32>(std::abs(int(a.rgba[i + channel]) - int(b.rgba[i + channel])));
            pixelMaximum = std::max(pixelMaximum, delta); sum += delta;
        }
        result.maximum = std::max(result.maximum, pixelMaximum);
        result.changed += pixelMaximum > 3;
    }
    result.mean = sum / (a.rgba.size() / 4 * 3);
    return result;
}
ColorCoverage colors(const Image8& image) {
    ColorCoverage counts;
    for (usize i = 0; i + 3 < image.rgba.size(); i += 4) {
        const int r = image.rgba[i], g = image.rgba[i + 1], b = image.rgba[i + 2];
        const int high = std::max({r, g, b}), low = std::min({r, g, b});
        if (high <= 28) continue;
        ++counts.visible;
        counts.chromatic += high - low > 35;
        counts.red += r > g + 30 && r > b + 30;
        counts.green += g > r + 30 && g > b + 30;
        counts.blue += b > r + 30 && b > g + 30;
    }
    return counts;
}

std::filesystem::path folder() {
    const auto root = std::filesystem::path(AUREA_TEST_DATA_DIR).parent_path().parent_path().parent_path();
    const auto path = root / "build/reference/beta3-followup-3d-20261013/own-fixtures";
    std::error_code error; std::filesystem::create_directories(path, error);
    AUREA_CHECK(!error);
    return path;
}

Image8 texture() {
    Image8 image; image.width = image.height = 128;
    image.rgba.resize(128u * 128u * 4u);
    const u8 palette[4][3]{{240, 20, 30}, {20, 240, 40}, {25, 35, 240}, {235, 235, 235}};
    for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 128; ++x) {
        const u32 cell = (x / 16 + y / 16) % 4;
        u8* p = image.rgba.data() + (y * 128 + x) * 4;
        for (u32 c = 0; c < 3; ++c) p[c] = palette[cell][c];
        p[3] = 255;
    }
    return image;
}

Image8 capture(Scene3DRig& rig, i64 frame) {
    seek_frame(rig.e, frame);
    return rig.capture(256);
}

void configure_camera(Scene& scene) {
    const LayerId id = scene.comp->add_layer(LayerKind::Camera, "own analytic camera");
    Layer* layer = scene.comp->layer(id);
    layer->end = scene.comp->duration(); layer->threeD = true;
    layer->transform.position = {128, 128, -400}; layer->transform.anchor = {};
    layer->camera.fov = 2.0f * std::atan(128.0f / 400.0f) / kDeg2Rad;
    layer->camera.active = true;
}

Vec3 analytical_center(Vec3 local, f32 degrees) {
    const f32 angle = degrees * kDeg2Rad, c = std::cos(angle), s = std::sin(angle);
    return {128 + c * local.x + s * local.z, 128 + local.y, -s * local.x + c * local.z};
}

} // namespace reference3d

AUREA_TEST(Reference3DGpu, TexturedCubeAndPyramidKeepMapsWithMotionBlurAndInsidePrecomp) {
    AUREA_REQUIRE_GPU();
    const auto directory = reference3d::folder();
    const std::string imagePath = (directory / "own-color-grid.png").string();
    AUREA_CHECK(write_png(imagePath, reference3d::texture()));
    for (const u32 kind : {0u, 4u}) {
        Scene3DRig rig(256, 256);
        const auto object = rig.e.add_shape3d(kind, "own textured reference");
        AUREA_CHECK(object.ok()); if (!object.ok()) return;
        const f32 white[4]{1, 1, 1, 1};
        AUREA_CHECK(rig.e.set_shape3d_part_style(*object, -1, white, imagePath.c_str()).ok());
        Composition* composition = current_comp(rig.e);
        auto& blur = composition->motion_blur(); blur.enabled = true; blur.samples = 8;
        blur.previewSamples = 8; blur.shutterAngle = 180; blur.shutterPhase = -90;
        Layer* layer = composition->layer(LayerId::unpack(*object));
        layer->transform.rotation = {12, 25, 0};
        const Image8 still = reference3d::capture(rig, 10);
        AUREA_CHECK(rig.e.set_motion_blur(*object, true));
        const Image8 staticBlur = reference3d::capture(rig, 10);
        AUREA_CHECK_EQ(still.rgba.size(), staticBlur.rgba.size());
        u32 maximum = 0;
        if (still.rgba.size() == staticBlur.rgba.size()) for (usize i = 0; i < still.rgba.size(); ++i)
            maximum = std::max(maximum, static_cast<u32>(std::abs(int(still.rgba[i]) - int(staticBlur.rgba[i]))));
        AUREA_CHECK(maximum <= 3);
        layer = current_comp(rig.e)->layer(LayerId::unpack(*object));
        auto& rotation = layer->tracks.get_or_create(TrackProperty::RotationY);
        (void)rotation.set(FrameIndex{0}, 15.0f, Interpolation::Linear);
        (void)rotation.set(FrameIndex{60}, 135.0f, Interpolation::Linear);
        std::vector<Image8> directImages;
        for (const bool precomp : {false, true}) {
            Composition* content = current_comp(rig.e);
            if (precomp) {
                const MotionBlurSettings nestedBlur = current_comp(rig.e)->motion_blur();
                const u64 ids[]{*object};
                const auto group = rig.e.precompose(ids, 1, "own textured nested scene");
                AUREA_CHECK(group.ok()); if (!group.ok()) return;
                const Layer* nested = current_comp(rig.e)->layer(LayerId::unpack(*group));
                content = rig.e.project()->timeline().composition(nested->nested.composition);
                content->motion_blur() = nestedBlur;
            }
            usize imageIndex = 0;
            for (const i64 frame : {0LL, 15LL, 30LL, 45LL, 59LL}) {
                const Image8 image = reference3d::capture(rig, frame);
                if (precomp) {
                    const auto delta = reference3d::difference(image, directImages[imageIndex]);
                    std::printf("    direct/nested shape=%u frame=%lld max=%u mean=%.6f changed=%u\n",
                        kind, static_cast<long long>(frame), delta.maximum, delta.mean, delta.changed);
                    AUREA_CHECK(delta.maximum <= 3);
                    AUREA_CHECK(delta.mean < .01);
                } else directImages.push_back(image);
                ++imageIndex;
                if (frame == 30) {
                    content->layer(LayerId::unpack(*object))->motionBlur = false;
                    const Image8 sharp = reference3d::capture(rig, frame);
                    content->layer(LayerId::unpack(*object))->motionBlur = true;
                    const auto delta = reference3d::difference(image, sharp);
                    std::printf("    animated blur shape=%u precomp=%d max=%u mean=%.6f changed=%u\n",
                        kind, precomp, delta.maximum, delta.mean, delta.changed);
                    AUREA_CHECK(delta.maximum > 3);
                    AUREA_CHECK(delta.changed > 30);
                    AUREA_CHECK(delta.mean > .01);
                }
                const auto coverage = reference3d::colors(image);
                std::printf("    own shape=%u precomp=%d frame=%lld: visible=%u chromatic=%u R/G/B=%u/%u/%u\n",
                    kind, precomp, static_cast<long long>(frame), coverage.visible, coverage.chromatic,
                    coverage.red, coverage.green, coverage.blue);
                AUREA_CHECK(coverage.visible > 300);
                AUREA_CHECK(coverage.chromatic > coverage.visible / 3);
                AUREA_CHECK(coverage.red > 20 && coverage.green > 20 && coverage.blue > 20);
                const std::string name = "shape-" + std::to_string(kind) + (precomp ? "-nested-" : "-direct-") + std::to_string(frame) + ".png";
                AUREA_CHECK(write_png((directory / name).string(), image));
            }
        }
    }
}

AUREA_TEST(Reference3DGpu, NullRotationPreservesEveryChildLocalZAndProjectedCenter) {
    AUREA_REQUIRE_GPU();
    Scene scene(256, 256); reference3d::configure_camera(scene);
    const LayerId parent = scene.comp->add_layer(LayerKind::Null, "own 3D parent");
    Layer* null = scene.comp->layer(parent); null->threeD = true;
    null->transform.anchor = {}; null->transform.position = {128, 128, 0};
    const Vec3 centers[3]{{-22, -28, -60}, {0, 0, 0}, {24, 29, 70}};
    const Vec4 palette[3]{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    LayerId children[3];
    for (u32 n = 0; n < 3; ++n) {
        children[n] = scene.solid(18, 18, palette[n], centers[n].x, centers[n].y);
        Layer* child = scene.comp->layer(children[n]); child->threeD = true;
        child->parent = parent; child->transform.position = centers[n];
        child->transform.rotation.z = static_cast<f32>(n) * 17;
    }
    for (const f32 angle : {0.0f, 35.0f, 75.0f, 145.0f}) {
        null = scene.comp->layer(parent); null->transform.rotation.y = angle;
        const FloatImage image = scene.render(FrameIndex{0}, 1, true);
        for (u32 n = 0; n < 3; ++n) {
            const Vec3 expected = reference3d::analytical_center(centers[n], angle);
            const Mat4 world = layer_world_3d(*scene.comp, *scene.comp->layer(children[n]), FrameIndex{0});
            const Vec4 actual = world * Vec4{9, 9, 0, 1};
            AUREA_CHECK_NEAR(actual.x, expected.x, .0002f);
            AUREA_CHECK_NEAR(actual.y, expected.y, .0002f);
            AUREA_CHECK_NEAR(actual.z, expected.z, .0002f);
            f64 energy = 0, x = 0, y = 0;
            for (u32 yy = 0; yy < image.height; ++yy) for (u32 xx = 0; xx < image.width; ++xx) {
                const Vec4 color = image.v(xx, yy);
                const f32 values[3]{color.x, color.y, color.z};
                const f64 weight = std::max(0.0f, values[n] - std::max(values[(n + 1) % 3], values[(n + 2) % 3]));
                energy += weight; x += weight * (xx + .5); y += weight * (yy + .5);
            }
            const f64 projectedX = 128 + (expected.x - 128) * 400 / (400 + expected.z);
            const f64 projectedY = 128 + (expected.y - 128) * 400 / (400 + expected.z);
            std::printf("    own nullY=%.0f child=%u localZ=%.0f world=(%.2f,%.2f,%.2f) projected=(%.2f,%.2f) observed=(%.2f,%.2f) energy=%.2f\n",
                angle, n, centers[n].z, actual.x, actual.y, actual.z, projectedX, projectedY,
                x / std::max(energy, 1e-9), y / std::max(energy, 1e-9), energy);
            AUREA_CHECK(energy > 15);
            if (energy > 15) {
                AUREA_CHECK_NEAR(x / energy, projectedX, 1.2);
                AUREA_CHECK_NEAR(y / energy, projectedY, 1.2);
            }
        }
    }
}

AUREA_TEST(Reference3DGpu, NullRotatedDepthOrdersPlanesByWorldZWithAndWithoutBlur) {
    AUREA_REQUIRE_GPU();
    Scene scene(256, 256); reference3d::configure_camera(scene);
    const LayerId parent = scene.comp->add_layer(LayerKind::Null, "own depth parent");
    Layer* null = scene.comp->layer(parent); null->threeD = true;
    null->transform.position = {128, 128, 0}; null->transform.anchor = {};
    for (u32 n = 0; n < 2; ++n) {
        const LayerId id = scene.solid(112, 112, n ? Vec4{0, 0, 1, 1} : Vec4{1, 0, 0, 1}, 0, 0);
        Layer* child = scene.comp->layer(id); child->parent = parent; child->threeD = true;
        child->transform.position = {0, 0, n ? 40.0f : -40.0f}; child->motionBlur = true;
    }
    auto& blur = scene.comp->motion_blur(); blur.samples = blur.previewSamples = 8;
    blur.shutterAngle = 180; blur.shutterPhase = -90;
    for (const f32 angle : {0.0f, 35.0f, 145.0f, 180.0f}) {
        for (const bool blurred : {false, true}) {
            null = scene.comp->layer(parent);
            auto& rotation = null->tracks.get_or_create(TrackProperty::RotationY);
            (void)rotation.set(FrameIndex{0}, angle - 10.0f, Interpolation::Linear);
            (void)rotation.set(FrameIndex{20}, angle + 10.0f, Interpolation::Linear);
            blur.enabled = blurred;
            const Vec4 center = scene.render(FrameIndex{10}, 1, true).v(128, 128);
            const bool redInFront = std::cos(angle * kDeg2Rad) > 0;
            std::printf("    own depth nullY=%.0f blur=%d center=(%.3f,%.3f,%.3f), expected=%s\n",
                angle, blurred, center.x, center.y, center.z, redInFront ? "red" : "blue");
            AUREA_CHECK(redInFront ? center.x > .9f && center.z < .01f : center.z > .9f && center.x < .01f);
        }
    }
}
