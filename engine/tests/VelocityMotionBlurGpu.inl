// 0.3.2 Export Engine 2.0, MotionBlurRenderer: the fast 3D motion blur path
// (MotionBlurSettings::vectorBlur — one centre scene + velocity buffer +
// reconstruction) against the brute-force shutter accumulation it replaces.
// Included in test_gpu.cpp (Vulkan host GPU).
namespace velocity_blur {

// A lit cube (24 vertices, flat faces) with an optional morph target.
std::shared_ptr<scene3d::SceneAsset> cube_asset(Vec4 color, bool morph, bool transparent = false) {
    auto asset = std::make_shared<scene3d::SceneAsset>();
    scene3d::Material m;
    m.baseColor = color; m.metallic = 0; m.roughness = .6f;
    m.emissive = Vec3{color.x, color.y, color.z}; m.emissiveStrength = .6f;
    m.alphaMode = transparent ? scene3d::AlphaMode::Blend : scene3d::AlphaMode::Opaque;
    asset->materials.push_back(m);
    scene3d::Primitive p; p.material = 0;
    const Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vec3 n : normals) {
        const Vec3 a = std::fabs(n.x) > .5f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 b = n.cross(a);
        const u32 base = static_cast<u32>(p.positions.size());
        for (const Vec2 c : {Vec2{-1, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{-1, 1}}) {
            p.positions.push_back(n + a * c.x + b * c.y);
            p.normals.push_back(n);
        }
        // (v1 − v0) × (v2 − v0) = 4·a × (n × a) = 4n: counter-clockwise seen
        // from outside, the glTF front face.
        p.indices.insert(p.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    p.bounds.min = Vec3{-1, -1, -1}; p.bounds.max = Vec3{1, 1, 1};
    scene3d::Mesh mesh;
    if (morph) {
        p.morphTargets.resize(1);
        p.morphTargets[0].positions.resize(p.positions.size());
        for (usize i = 0; i < p.positions.size(); ++i) p.morphTargets[0].positions[i] = p.positions[i] * .35f;
        mesh.morphWeights = {0};
    }
    mesh.primitives.push_back(std::move(p));
    mesh.bounds.min = Vec3{-1.35f, -1.35f, -1.35f}; mesh.bounds.max = Vec3{1.35f, 1.35f, 1.35f};
    asset->meshes.push_back(std::move(mesh));
    asset->bounds = asset->meshes[0].bounds;
    scene3d::Node node; node.mesh = 0; asset->nodes.push_back(node); asset->roots = {0};
    if (morph) {
        scene3d::Animation animation; animation.duration = 1;
        scene3d::AnimSampler weights; weights.times = {0, 1}; weights.values = {0, 1}; weights.components = 1;
        animation.samplers.push_back(std::move(weights));
        animation.channels.push_back({0, scene3d::AnimPath::Weights, 0});
        asset->animations.push_back(std::move(animation));
    }
    return asset;
}

struct Models {
    std::unordered_map<u64, std::shared_ptr<const scene3d::SceneAsset>> byId;
    static std::shared_ptr<const scene3d::SceneAsset> lookup(void* self, AssetId id) {
        auto& map = static_cast<Models*>(self)->byId;
        const auto found = map.find(id.pack());
        return found == map.end() ? nullptr : found->second;
    }
};

struct Rig {
    Scene scene;
    Models models;
    struct Guard {
        ~Guard() { gpu().renderer.set_model_lookup(nullptr, nullptr); gpu().renderer.release_project_resources(); }
    } guard;
    Rig(u32 w, u32 h) : scene(w, h) {
        scene.comp->set_background(Color{.04f, .05f, .07f, 1});
        gpu().renderer.set_model_lookup(&Models::lookup, &models);
        auto& mb = scene.comp->motion_blur();
        mb.enabled = true; mb.shutterAngle = 360; mb.shutterPhase = -180;
        mb.samples = 64; mb.adaptiveLimit = 128;
        scene.comp->post_process().bloom = false;
    }
    Layer* model(std::shared_ptr<scene3d::SceneAsset> asset, Vec3 position, f32 scale, const char* name) {
        Asset a; a.kind = AssetKind::Model3D;
        const AssetId id = scene.project.add_asset(std::move(a));
        models.byId[id.pack()] = std::move(asset);
        Layer* l = scene.comp->layer(scene.comp->add_layer(LayerKind::Model3D, name));
        l->model.scene = id; l->model.unitScale = scale;
        l->transform.position = position; l->threeD = true; l->motionBlur = true;
        return l;
    }
};

struct Frame {
    FloatImage image;
    bool velocity = false;
    usize samples = 0;
    u32 velocityGroups = 0;
    u32 reductions = 0;
    f64 wallMs = 0;
    f32 gpuMs = 0;
    u64 hash = 0;
};

Frame render(Rig& rig, i64 frame, bool vector, bool finalQuality, bool blur = true) {
    auto& g = gpu();
    auto& mb = rig.scene.comp->motion_blur();
    mb.vectorBlur = vector; mb.enabled = blur;
    const u32 w = rig.scene.comp->width(), h = rig.scene.comp->height();
    RenderSettings settings; settings.finalQuality = finalQuality; settings.dither = false; settings.gpuTimers = true;
    Frame out;
    FrameSnapshot snapshot;
    (void)g.renderer.take_reductions();
    const auto start = std::chrono::steady_clock::now();
    g.renderer.prepare(*rig.scene.comp, rig.scene.project, FrameIndex{frame}, nullptr, nullptr, nullptr,
        settings, 1, 0, DecodeMode::Still, 1, snapshot);
    AUREA_CHECK(!g.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u);
    if (snapshot.scenes.empty()) return out;
    out.velocity = snapshot.scenes[0].velocity.enabled;
    out.samples = snapshot.scenes[0].blurFrames.size();
    OffscreenTarget target{g.target(w, h), w, h}; FrameStats stats; RenderTimings timings;
    const Status result = g.renderer.render(snapshot, settings, &target, stats, timings);
    AUREA_CHECK(result.ok()); AUREA_CHECK(!g.renderer.take_incomplete());
    g.backend.wait_idle();
    out.wallMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::array<GpuTiming, 600> measured{};
    (void)g.backend.read_gpu_timings(measured.data(), static_cast<u32>(measured.size()), &out.gpuMs);
    out.velocityGroups = g.renderer.scene_stats().velocityGroups;
    out.reductions = g.renderer.take_reductions();
    std::vector<u16> half(usize(w) * h * 4);
    AUREA_CHECK(g.backend.read_texture(target.texture, half.data(), w * 8).ok());
    out.hash = 1469598103934665603ull;
    for (u16 v : half) { out.hash ^= v; out.hash *= 1099511628211ull; }
    out.image.width = w; out.image.height = h; out.image.px.resize(half.size());
    for (usize i = 0; i < half.size(); ++i) out.image.px[i] = half_to_float(half[i]);
    return out;
}

f32 luma(const FloatImage& im, u32 x, u32 y) { const f32* p = im.at(x, y); return .2126f * p[0] + .7152f * p[1] + .0722f * p[2]; }

// Mean |a − b| (RGB) over the pixels that any of the images changes against the background.
f64 region_error(const FloatImage& a, const FloatImage& b, const FloatImage& sharp, const FloatImage& reference, u32* count = nullptr) {
    f64 total = 0; u32 n = 0;
    const Vec4 bg = sharp.v(0, 0);
    for (u32 y = 0; y < a.height; ++y) for (u32 x = 0; x < a.width; ++x) {
        const Vec4 s = sharp.v(x, y), r = reference.v(x, y);
        const bool touched = std::fabs(s.x - bg.x) + std::fabs(s.y - bg.y) + std::fabs(s.z - bg.z) > .01f
                          || std::fabs(r.x - bg.x) + std::fabs(r.y - bg.y) + std::fabs(r.z - bg.z) > .01f;
        if (!touched) continue;
        for (int c = 0; c < 3; ++c) total += std::fabs(a.at(x, y)[c] - b.at(x, y)[c]);
        ++n;
    }
    if (count) *count = n;
    return n ? total / (3.0 * n) : 0.0;
}

// Pixels of row y that are a partial mix between the background and the object.
u32 partial_pixels(const FloatImage& im, u32 y) {
    f32 bg = luma(im, 0, y), peak = bg;
    for (u32 x = 0; x < im.width; ++x) peak = std::max(peak, luma(im, x, y));
    u32 n = 0;
    for (u32 x = 0; x < im.width; ++x) {
        const f32 t = (luma(im, x, y) - bg) / std::max(1e-4f, peak - bg);
        n += t > .08f && t < .92f;
    }
    return n;
}

f32 max_difference(const FloatImage& a, const FloatImage& b) {
    f32 m = 0;
    for (usize i = 0; i < a.px.size() && i < b.px.size(); ++i) m = std::max(m, std::fabs(a.px[i] - b.px[i]));
    return a.px.size() == b.px.size() ? m : 1e9f;
}

} // namespace velocity_blur

AUREA_TEST(MotionBlurGpu, VectorBlurRigidCubeMatchesAccumulation) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
#if !defined(AUREA_TEST_GLES)
    const u32 validationBefore = vk::Backend::validation_errors();
    struct ValidationCheck {
        u32 before;
        ~ValidationCheck() { AUREA_CHECK_EQ(vk::Backend::validation_errors(), before); }
    } validation{validationBefore};
#endif
    Rig rig(480, 270);
    Layer* cube = rig.model(cube_asset(Vec4{.9f, .55f, .2f, 1}, false), Vec3{240, 135, 0}, 38, "rigid cube");
    auto& px = cube->tracks.get_or_create(TrackProperty::PositionX);
    px.set(FrameIndex{0}, 120); px.set(FrameIndex{10}, 360);         // 24 px per frame
    auto& ry = cube->tracks.get_or_create(TrackProperty::RotationY);
    ry.set(FrameIndex{0}, 0); ry.set(FrameIndex{10}, 90);              // 9° per frame
    auto& rx = cube->tracks.get_or_create(TrackProperty::RotationX);
    rx.set(FrameIndex{0}, 20); rx.set(FrameIndex{10}, 20);
    const Frame sharp = render(rig, 5, false, true, false);
    const Frame reference = render(rig, 5, false, true);
    const Frame fast = render(rig, 5, true, true);
    const Frame preview = render(rig, 5, true, false);
    const Frame sharpPreview = render(rig, 5, false, false, false);
    AUREA_CHECK(!reference.velocity); AUREA_CHECK(reference.samples > 8);
    AUREA_CHECK(fast.velocity); AUREA_CHECK_EQ(fast.samples, 0u); AUREA_CHECK_EQ(fast.velocityGroups, 1u);
    AUREA_CHECK(preview.velocity); AUREA_CHECK_EQ(preview.velocityGroups, 1u);
    AUREA_CHECK_EQ(fast.reductions, 0u);
    u32 region = 0;
    const f64 fastError = region_error(fast.image, reference.image, sharp.image, reference.image, &region);
    const f64 sharpError = region_error(sharp.image, reference.image, sharp.image, reference.image);
    const u32 y = 135;
    const u32 partialSharp = partial_pixels(sharp.image, y), partialFast = partial_pixels(fast.image, y),
              partialReference = partial_pixels(reference.image, y);
    const f32 previewExport = max_difference(preview.image, fast.image);
    const f32 sharpPreviewExport = max_difference(sharpPreview.image, sharp.image);
    std::printf("\n    vector vs accumulation(%zu samples): mean |RGB| error %.5f over %u px (unblurred %.5f);"
                " partial px on row: sharp %u, vector %u, accumulation %u; preview/export max diff %.6f (unblurred %.6f)\n",
                reference.samples, fastError, region, sharpError, partialSharp, partialFast, partialReference,
                previewExport, sharpPreviewExport);
    AUREA_CHECK(region > 1000);
    AUREA_CHECK(fastError < sharpError * .4);       // much closer to the reference than no blur
    AUREA_CHECK(fastError < .03);
    AUREA_CHECK(partialFast >= partialSharp + 20);   // energy spread along the motion
    AUREA_CHECK(partialFast * 2 >= partialReference);
    // The vector stage has no preview/export switch (same selection, taps and
    // tiles at the same size). The base scene still differs a little between
    // preview and export (LOD hysteresis, environment wait); the blur only
    // carries that difference along, it adds no divergence of its own.
    AUREA_CHECK(previewExport <= 2.f * sharpPreviewExport + 1e-3f);
    // Deterministic: the same frame twice is bit-identical.
    const Frame again = render(rig, 5, true, true);
    AUREA_CHECK_EQ(again.hash, fast.hash);
    // An exposure that does not straddle the frame (phase 0: frame … frame + 1)
    // renders the single scene at the shutter centre, like the sample mean.
    rig.scene.comp->motion_blur().shutterPhase = 0;
    const Frame lateReference = render(rig, 5, false, true), late = render(rig, 5, true, true);
    const f64 lateError = region_error(late.image, lateReference.image, sharp.image, lateReference.image);
    const f64 lateSharp = region_error(sharp.image, lateReference.image, sharp.image, lateReference.image);
    std::printf("    phase 0: vector error %.5f (unblurred at the frame %.5f)\n", lateError, lateSharp);
    AUREA_CHECK(late.velocity);
    AUREA_CHECK(lateError < lateSharp * .4);
}

AUREA_TEST(MotionBlurGpu, VectorBlurStaticObjectMovingCameraBlurs) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    Rig rig(480, 270);
    rig.model(cube_asset(Vec4{.2f, .6f, .95f, 1}, false), Vec3{240, 135, 0}, 40, "static cube");
    Layer* camera = rig.scene.comp->layer(rig.scene.comp->add_layer(LayerKind::Camera, "dolly"));
    camera->transform.position = Vec3{240, 135, -600};
    auto& cx = camera->tracks.get_or_create(TrackProperty::PositionX);
    cx.set(FrameIndex{0}, 140); cx.set(FrameIndex{10}, 340);
    const Frame sharp = render(rig, 5, false, true, false);
    const Frame reference = render(rig, 5, false, true);
    const Frame fast = render(rig, 5, true, true);
    AUREA_CHECK(fast.velocity); AUREA_CHECK_EQ(fast.velocityGroups, 1u);
    const f64 fastError = region_error(fast.image, reference.image, sharp.image, reference.image);
    const f64 sharpError = region_error(sharp.image, reference.image, sharp.image, reference.image);
    const u32 partialSharp = partial_pixels(sharp.image, 135), partialFast = partial_pixels(fast.image, 135);
    std::printf("\n    camera-only motion: vector error %.5f (unblurred %.5f), partial px sharp %u vector %u\n",
                fastError, sharpError, partialSharp, partialFast);
    AUREA_CHECK(partialFast >= partialSharp + 10);
    AUREA_CHECK(fastError < sharpError * .5);
}

AUREA_TEST(MotionBlurGpu, VectorBlurPanningCameraBlursTheVisibleSky) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    Rig rig(480, 270);
    rig.scene.comp->environment().showBackground = true;
    rig.model(cube_asset(Vec4{.85f, .3f, .6f, 1}, false), Vec3{240, 135, 0}, 40, "static cube");
    Layer* camera = rig.scene.comp->layer(rig.scene.comp->add_layer(LayerKind::Camera, "pan"));
    camera->transform.position = Vec3{240, 135, -600};
    auto& ry = camera->tracks.get_or_create(TrackProperty::RotationY);
    ry.set(FrameIndex{0}, -10); ry.set(FrameIndex{10}, 10);
    const Frame sharp = render(rig, 5, false, true, false);
    const Frame reference = render(rig, 5, false, true);
    const Frame fast = render(rig, 5, true, true);
    const f64 fastError = region_error(fast.image, reference.image, sharp.image, reference.image);
    const f64 sharpError = region_error(sharp.image, reference.image, sharp.image, reference.image);
    std::printf("\n    camera pan with sky: velocity %u, vector error %.5f (unblurred %.5f)\n", fast.velocity, fastError, sharpError);
    AUREA_CHECK(fast.velocity); AUREA_CHECK_EQ(fast.velocityGroups, 1u);
    AUREA_CHECK(fastError < sharpError * .6);
}

AUREA_TEST(MotionBlurGpu, VectorBlurStaticForegroundEdgeStaysSharp) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    Rig rig(480, 270);
    // Background: a large green cube far behind, sweeping horizontally.
    Layer* back = rig.model(cube_asset(Vec4{.1f, .9f, .2f, 1}, false), Vec3{240, 135, 400}, 150, "moving background");
    auto& bx = back->tracks.get_or_create(TrackProperty::PositionX);
    bx.set(FrameIndex{0}, 140); bx.set(FrameIndex{10}, 340);
    // Foreground: a static red cube in front of it.
    Layer* front = rig.model(cube_asset(Vec4{.95f, .1f, .1f, 1}, false), Vec3{240, 135, -100}, 25, "static foreground");
    front->motionBlur = true;
    const Frame sharp = render(rig, 5, false, true, false);
    const Frame reference = render(rig, 5, false, true);
    const Frame fast = render(rig, 5, true, true);
    AUREA_CHECK(fast.velocity);
    // The foreground silhouette (red dominant in the sharp frame), eroded by one pixel.
    const FloatImage& s = sharp.image;
    auto red = [&](u32 x, u32 y) { const f32* p = s.at(x, y); return p[0] > .15f && p[0] > 2.5f * p[1]; };
    f32 worstFast = 0, worstReference = 0; u32 inside = 0;
    for (u32 y = 1; y + 1 < s.height; ++y) for (u32 x = 1; x + 1 < s.width; ++x) {
        if (!(red(x, y) && red(x - 1, y) && red(x + 1, y) && red(x, y - 1) && red(x, y + 1))) continue;
        ++inside;
        for (int c = 0; c < 3; ++c) {
            worstFast = std::max(worstFast, std::fabs(fast.image.at(x, y)[c] - s.at(x, y)[c]));
            worstReference = std::max(worstReference, std::fabs(reference.image.at(x, y)[c] - s.at(x, y)[c]));
        }
    }
    const u32 partialSharp = partial_pixels(sharp.image, 80), partialFast = partial_pixels(fast.image, 80);
    std::printf("\n    static foreground over moving background: %u px inside, max change vector %.5f, accumulation %.5f;"
                " background partial px (row 80) sharp %u vector %u\n", inside, worstFast, worstReference, partialSharp, partialFast);
    AUREA_CHECK(inside > 400);
    AUREA_CHECK(worstFast <= .02f);                  // no background bleed over the static object
    AUREA_CHECK(partialFast >= partialSharp + 10);   // while the background really blurs
}

AUREA_TEST(MotionBlurGpu, VectorBlurFallsBackToAccumulationForDeformingAndTransparent) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    for (u32 variant = 0; variant < 2; ++variant) {
        Rig rig(320, 180);
        const bool morph = variant == 0;
        Layer* l = rig.model(cube_asset(Vec4{.8f, .8f, .3f, morph ? 1.f : .5f}, morph, !morph), Vec3{160, 90, 0}, 30,
                             morph ? "morph cube" : "glass cube");
        if (morph) l->model.animationClip = 0;
        auto& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(FrameIndex{0}, 90); px.set(FrameIndex{30}, 240);
        const Frame accumulation = render(rig, 14, false, true);
        const Frame requested = render(rig, 14, true, true);
        std::printf("\n    %s: vector requested -> velocity %u, samples %zu (accumulation %zu), hashes %016llx/%016llx\n",
                    morph ? "morph" : "transparent", requested.velocity, requested.samples, accumulation.samples,
                    static_cast<unsigned long long>(requested.hash), static_cast<unsigned long long>(accumulation.hash));
        AUREA_CHECK(!requested.velocity);
        AUREA_CHECK_EQ(requested.velocityGroups, 0u);
        AUREA_CHECK(requested.samples > 1);
        AUREA_CHECK_EQ(requested.samples, accumulation.samples);
        // The fallback IS the accumulation: bit-identical pixels.
        AUREA_CHECK_EQ(requested.hash, accumulation.hash);
    }
}

// Regression guard: vectorBlur = false is today's accumulation. The printed
// hash was compared with the pre-0.3.2 engine on the same GPU (see report).
AUREA_TEST(MotionBlurGpu, VectorBlurOffKeepsAccumulationBitIdentical) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    Rig rig(320, 180);
    Layer* cube = rig.model(cube_asset(Vec4{.9f, .55f, .2f, 1}, false), Vec3{160, 90, 0}, 26, "rigid cube");
    auto& px = cube->tracks.get_or_create(TrackProperty::PositionX);
    px.set(FrameIndex{0}, 80); px.set(FrameIndex{10}, 240);
    auto& ry = cube->tracks.get_or_create(TrackProperty::RotationY);
    ry.set(FrameIndex{0}, 0); ry.set(FrameIndex{10}, 90);
    const Frame before = render(rig, 5, false, true);
    const Frame fast = render(rig, 5, true, true);
    const Frame after = render(rig, 5, false, true);
    std::printf("\n    accumulation hash %016llx (%zu samples), after a vector frame %016llx\n",
                static_cast<unsigned long long>(before.hash), before.samples, static_cast<unsigned long long>(after.hash));
    AUREA_CHECK(!before.velocity); AUREA_CHECK(before.samples > 1);
    AUREA_CHECK(fast.velocity);
    AUREA_CHECK_EQ(before.hash, after.hash);
    AUREA_CHECK_EQ(before.samples, after.samples);
}

AUREA_TEST(MotionBlurGpu, VectorBlurPerformance1080p) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    Rig rig(1920, 1080);
    auto& mb = rig.scene.comp->motion_blur();
    mb.samples = 32; mb.adaptiveLimit = 32;
    for (u32 i = 0; i < 6; ++i) {
        Layer* cube = rig.model(cube_asset(Vec4{.3f + .1f * i, .5f, .9f - .1f * i, 1}, false),
                                Vec3{300.f + 260.f * i, 540, 0}, 80, "cube");
        auto& px = cube->tracks.get_or_create(TrackProperty::PositionX);
        px.set(FrameIndex{0}, 200.f + 260.f * i); px.set(FrameIndex{10}, 200.f + 260.f * i + 600.f);   // 60 px per frame
        auto& ry = cube->tracks.get_or_create(TrackProperty::RotationY);
        ry.set(FrameIndex{0}, 0); ry.set(FrameIndex{10}, 60);
    }
    rig.scene.comp->floor().mode = 1;   // a visible floor (static camera: still vector-eligible)
    Frame fast{}, accumulation{};
    f64 fastWall = 1e9, accumulationWall = 1e9; f32 fastGpu = 1e9f, accumulationGpu = 1e9f;
    for (u32 run = 0; run < 3; ++run) {
        fast = render(rig, 5, true, true);
        accumulation = render(rig, 5, false, true);
        fastWall = std::min(fastWall, fast.wallMs); accumulationWall = std::min(accumulationWall, accumulation.wallMs);
        if (fast.gpuMs > 0) fastGpu = std::min(fastGpu, fast.gpuMs);
        if (accumulation.gpuMs > 0) accumulationGpu = std::min(accumulationGpu, accumulation.gpuMs);
    }
    std::printf("\n    1080p, 6 rigid cubes + floor: vector %.2f ms wall (GPU %.2f ms) vs accumulation %zu samples %.2f ms wall (GPU %.2f ms)\n",
                fastWall, fastGpu, accumulation.samples, accumulationWall, accumulationGpu);
    AUREA_CHECK(fast.velocity); AUREA_CHECK_EQ(accumulation.samples, 32u);
    AUREA_CHECK(fastWall < accumulationWall);
}
