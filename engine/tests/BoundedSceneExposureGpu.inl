// Compare the identical exposure on a real GPU, crossing batch boundaries with
// signed morph weights, a camera track, a sharp instance, HDR/post and reflection.
AUREA_TEST(MotionBlurGpu, BoundedSceneExposureMatchesCompleteShutterPixels) {
    AUREA_REQUIRE_GPU();
    Scene scene(320, 180);
    scene.comp->set_background(Color{.05f, .1f, .15f, .65f});
    auto source = std::make_shared<scene3d::SceneAsset>();
    for (u32 material = 0; material < 2; ++material) {
        scene3d::Material m;
        m.baseColor = material ? Vec4{.12f, .45f, .8f, 1} : Vec4{.95f, .2f, .08f, .6f};
        m.metallic = .15f; m.roughness = .3f; m.doubleSided = true;
        m.unlit = material == 0; m.alphaMode = material ? scene3d::AlphaMode::Opaque : scene3d::AlphaMode::Blend;
        if (material) { m.emissive = Vec3{.1f, .5f, 1}; m.emissiveStrength = 5; }
        source->materials.push_back(std::move(m));
    }
    scene3d::Mesh mesh; mesh.morphWeights = {0};
    for (u32 part = 0; part < 2; ++part) {
        scene3d::Primitive p; p.material = part;
        p.positions.resize(2048); p.normals.assign(2048, Vec3{0, 0, 1});
        const f32 left = part ? .1f : -.9f;
        for (u32 i = 0; i < 2048; ++i)
            p.positions[i] = Vec3{left + (i % 4 == 1 || i % 4 == 2 ? .7f : 0.f),
                i % 4 >= 2 ? .6f : -.6f, part ? 0.f : -0.1f};
        p.indices = {0, 1, 2, 0, 2, 3};
        p.bounds.min = Vec3{left, -.6f, -.1f}; p.bounds.max = Vec3{left + .7f, .6f, 0};
        p.morphTargets.resize(1); p.morphTargets[0].positions.assign(2048, Vec3{.25f, -.1f, .06f});
        mesh.primitives.push_back(std::move(p));
    }
    mesh.bounds.min = Vec3{-.9f, -.6f, -.1f}; mesh.bounds.max = Vec3{.8f, .6f, 0};
    source->meshes.push_back(std::move(mesh)); source->bounds = source->meshes[0].bounds;
    scene3d::Node node; node.mesh = 0; source->nodes.push_back(std::move(node)); source->roots = {0};
    scene3d::Animation animation; animation.duration = 1;
    scene3d::AnimSampler weights; weights.times = {0, 1}; weights.values = {-1, 1}; weights.components = 1;
    animation.samplers.push_back(std::move(weights)); animation.channels.push_back({0, scene3d::AnimPath::Weights, 0});
    source->animations.push_back(std::move(animation));
    std::shared_ptr<const scene3d::SceneAsset> model = std::move(source);
    auto& g = gpu();
    struct LookupGuard {
        Renderer& renderer;
        ~LookupGuard() { renderer.set_model_lookup(nullptr, nullptr); renderer.release_project_resources(); }
    } lookup{g.renderer};
    g.renderer.set_model_lookup([](void* context, AssetId) {
        return *static_cast<std::shared_ptr<const scene3d::SceneAsset>*>(context);
    }, &model);
    Asset asset; asset.kind = AssetKind::Model3D;
    const auto assetId = scene.project.add_asset(std::move(asset));
    for (u32 object = 0; object < 2; ++object) {
        const auto id = scene.comp->add_layer(LayerKind::Model3D, object ? "sharp model" : "morph exposure");
        auto* layer = scene.comp->layer(id); layer->model.scene = assetId;
        layer->model.animationClip = object ? -1 : 0;
        layer->model.unitScale = object ? 30 : 60;
        layer->transform.position = object ? Vec3{242, 68, -20} : Vec3{132, 103, 0};
        layer->threeD = true; layer->motionBlur = !object;
        if (!object) {
            layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 90);
            layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 175);
        }
    }
    const auto cameraId = scene.comp->add_layer(LayerKind::Camera, "shutter camera");
    auto* camera = scene.comp->layer(cameraId); camera->transform.position = Vec3{160, 90, -450};
    camera->tracks.get_or_create(TrackProperty::FocalLength).set(FrameIndex{0}, 28);
    camera->tracks.get_or_create(TrackProperty::FocalLength).set(FrameIndex{30}, 36);
    scene.comp->motion_blur().enabled = true;
    scene.comp->motion_blur().samples = scene.comp->motion_blur().adaptiveLimit = 16;
    scene.comp->motion_blur().shutterAngle = 360;
    scene.comp->post_process().quality3d = static_cast<u32>(Scene3DQuality::Low);
    scene.comp->post_process().bloom = true;
    scene.comp->post_process().bloomIntensity = .45f;
    scene.comp->post_process().exposure = .45f;

    std::vector<f64> expectedOffsets;
    auto render = [&](bool bounded, f32 phase, bool floor) {
        scene.comp->motion_blur().shutterPhase = phase;
        scene.comp->floor().mode = floor ? 1 : 0;
        scene.comp->floor().reflectivity = .7f; scene.comp->floor().roughness = .2f;
        RenderSettings settings; settings.finalQuality = true; settings.dither = false;
        settings.boundedSceneExposure = bounded; settings.sceneExposureBatchBudgetBytes = 768ull << 10;
        FrameSnapshot snapshot;
        g.renderer.prepare(*scene.comp, scene.project, FrameIndex{14}, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1, snapshot);
        AUREA_CHECK(!g.renderer.take_incomplete());
        AUREA_CHECK_EQ(snapshot.scenes.size(), 1u);
        if (snapshot.scenes.empty()) return FloatImage{};
        const auto& samples = snapshot.scenes[0].blurFrames;
        AUREA_CHECK_EQ(samples.size(), 16u);
        if (bounded) {
            AUREA_CHECK_EQ(snapshot.sceneExposureBatches.size(), 1u);
            for (usize i = 0; i < samples.size() && i < expectedOffsets.size(); ++i)
                AUREA_CHECK_NEAR(samples[i].subFrame, expectedOffsets[i], 1e-12);
        } else {
            AUREA_CHECK(snapshot.sceneExposureBatches.empty()); expectedOffsets.clear();
            for (const auto& sample : samples) expectedOffsets.push_back(sample.subFrame);
        }
        OffscreenTarget target{g.target(320, 180), 320, 180}; FrameStats stats; RenderTimings timings;
        const Status result = g.renderer.render(snapshot, settings, &target, stats, timings);
        if (!result.ok()) std::printf("\n    bounded=%u status %d %.*s batches=%u\n", bounded, result.raw(),
            static_cast<int>(result.detail().size()), result.detail().data(), timings.sceneExposureBatches);
        AUREA_CHECK(result.ok()); AUREA_CHECK(!g.renderer.take_incomplete());
        AUREA_CHECK(bounded ? timings.sceneExposureBatches > 1 : timings.sceneExposureBatches == 0);
        if (!result.ok()) return FloatImage{}; // Do not compare stale baseline pixels after a failed render.
        g.backend.wait_idle();
        std::vector<u16> half(320u * 180u * 4u);
        AUREA_CHECK(g.backend.read_texture(target.texture, half.data(), 320 * 8).ok());
        FloatImage image; image.width = 320; image.height = 180; image.px.resize(half.size());
        for (usize i = 0; i < half.size(); ++i) image.px[i] = half_to_float(half[i]);
        return image;
    };
    for (const bool floor : {false, true}) for (const f32 phase : {-360.f, 0.f}) {
        const auto reference = render(false, phase, floor);
        const auto bounded = render(true, phase, floor);
        AUREA_CHECK_EQ(reference.px.size(), bounded.px.size());
        f32 maximum = 0, brightest = 0; f64 energy = 0;
        for (usize i = 0; i < reference.px.size() && i < bounded.px.size(); ++i) {
            AUREA_CHECK(std::isfinite(bounded.px[i]));
            maximum = std::max(maximum, std::fabs(reference.px[i] - bounded.px[i]));
            if (i % 4 != 3) { energy += bounded.px[i]; brightest = std::max(brightest, bounded.px[i]); }
        }
        std::printf("    bounded 3D floor%u phase%.0f: max RGBA error %.6f, energy %.3f\n", floor, phase, maximum, energy);
        AUREA_CHECK(energy > 100); AUREA_CHECK(brightest > .3f); AUREA_CHECK(maximum <= .001f);
    }
}
