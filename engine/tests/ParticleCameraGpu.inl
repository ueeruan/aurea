// A composition camera translates the same 3D emitter whether it is attached
// to a particle layer or inserted in the effect stack of an ordinary layer.
namespace {
struct ParticleCameraMeasure {
    f64 energy = 0, x = 0, y = 0;
};

ParticleCameraMeasure particle_camera_measure(const FloatImage& image, u32 left = 0, u32 right = ~0u,
                                             bool greenOnly = false) {
    ParticleCameraMeasure out;
    for (u32 y = 0; y < image.height; ++y) for (u32 x = left; x < std::min(right, image.width); ++x) {
        const usize at = (usize(y) * image.width + x) * 4;
        const f64 value = greenOnly ? image.px[at + 1] : image.px[at] + image.px[at + 1] + image.px[at + 2];
        out.energy += value;
        out.x += value * (x + .5);
        out.y += value * (y + .5);
    }
    if (out.energy > 0) { out.x /= out.energy; out.y /= out.energy; }
    return out;
}

void particle_camera_point(EffectInstance& fx) {
    auto set = [&](u32 p, f32 value) { fx.params[p].constant = ParamValue::scalar(value); };
    for (u32 p : {particular::kVelocity, particular::kGravity, particular::kWindX,
                  particular::kWindY, particular::kWindZ, particular::kTurbulence,
                  particular::kEmitterW, particular::kEmitterH, particular::kEmitterD,
                  particular::kFadeIn, particular::kFadeOut, particular::kSizeRandom,
                  particular::kStretch, particular::kLifeRandom}) set(p, 0);
    set(particular::kRate, 30); set(particular::kPreRoll, 1000);
    set(particular::kLife, 30000); set(particular::kSize, 12);
    set(particular::kSizeEnd, 100); set(particular::kSeed, 71);
}

LayerId particle_camera_add(Composition& comp, f32 pan = 35.f, i64 start = 10, i64 end = 20) {
    const auto id = comp.add_layer(LayerKind::Camera, "camera");
    auto* camera = comp.layer(id);
    camera->start = FrameIndex{start}; camera->end = FrameIndex{end};
    camera->camera.active = true; camera->camera.fov = 40;
    camera->transform.position = {comp.width() * .5f + pan, comp.height() * .5f,
        -static_cast<f32>(comp.height()) * .5f / std::tan(20.f * kDeg2Rad)};
    camera->transform.anchor = {};
    return id;
}
}

AUREA_TEST(ParticleCameraGpu, CatalogEmitterUsesTheActiveCompositionCamera) {
    AUREA_REQUIRE_GPU();
    for (bool dedicated : {false, true}) {
        Scene scene(320, 180);
        // A fully transparent shape is discarded before the effect stack.
        // Hide this opaque source with the emitter's Show source=false instead.
        const LayerId id = scene.solid(320, 180, {1, 1, 1, 1}, 160, 90);
        if (dedicated) {
            Layer* layer = scene.comp->layer(id);
            layer->kind = LayerKind::ParticleSystem;
            layer->particles.emitterType = static_cast<u32>(ParticleEmitter::Particular);
        }
        auto& fx = scene.add_effect(id, effect_keys::kParticular);
        particle_camera_point(fx);
        const LayerId cameraId = scene.comp->add_layer(LayerKind::Camera, "camera");
        Layer* camera = scene.comp->layer(cameraId);
        camera->start = FrameIndex{0}; camera->end = FrameIndex{30};
        camera->camera.active = true; camera->camera.fov = 40;
        camera->transform.position = {160, 90, -90.f / std::tan(20.f * kDeg2Rad)};
        camera->transform.anchor = {};
        const FloatImage before = scene.render(FrameIndex{15}, 1, true);
        camera->transform.position.x += 35;
        const FloatImage after = scene.render(FrameIndex{15}, 1, true);
        const auto a = particle_camera_measure(before), b = particle_camera_measure(after);
        std::printf("    %s emitter camera pan: centroid %.3f -> %.3f, energy %.3f/%.3f\n",
                    dedicated ? "dedicated" : "catalog", a.x, b.x, a.energy, b.energy);
        AUREA_CHECK(a.energy > 10); AUREA_CHECK(b.energy > 10);
        AUREA_CHECK_NEAR(b.x - a.x, -35., 1.0);
        AUREA_CHECK_NEAR(b.y, a.y, .1);
        if (const char* path = std::getenv("AUREA_FX_DUMP"); path && *path) {
            const std::string name = dedicated ? "particle-dedicated" : "particle-catalog";
            (void)write_png(std::string(path) + "/" + name + "-before.png", before.encoded());
            (void)write_png(std::string(path) + "/" + name + "-after.png", after.encoded());
        }
    }
}

AUREA_TEST(ParticleCameraGpu, CameraCutsPreserveParentPlacementAndFollowingEffects) {
    AUREA_REQUIRE_GPU();
    Scene scene(320, 180);
    const LayerId id = scene.solid(80, 60, {1, 1, 1, 1}, 170, 90);
    particle_camera_point(scene.add_effect(id, effect_keys::kParticular));
    const auto parent = scene.comp->add_layer(LayerKind::Null, "parent");
    scene.comp->layer(parent)->transform.position = {20, 0, 0};
    scene.comp->layer(parent)->transform.anchor = {};
    scene.comp->layer(id)->parent = parent;
    const auto noCamera = particle_camera_measure(scene.render(FrameIndex{15}, 1, true));
    AUREA_CHECK_NEAR(noCamera.x, 190., .1);
    particle_camera_add(*scene.comp);
    for (bool transform : {false, true}) {
        if (transform) {
            auto& effect = scene.add_effect(id, effect_keys::kTransform);
            effect.params[1].constant = ParamValue::vec2(.75f, .5f); // +20 local pixels, applied once
        }
        for (i64 frame : {9, 10, 15, 20, 15, 20, 9}) {
            const auto image = scene.render(FrameIndex{frame}, 1, true);
            const auto point = particle_camera_measure(image);
            const f64 expected = 190. + (transform ? 20. : 0.) - (frame >= 10 && frame < 20 ? 35. : 0.);
            std::printf("    camera cut %lld transform%d: %.3f expected %.3f\n", frame, transform, point.x, expected);
            AUREA_CHECK(point.energy > 10);
            AUREA_CHECK_NEAR(point.x, expected, .15);
            AUREA_CHECK_NEAR(point.y, 90., .15);
        }
    }
}

AUREA_TEST(ParticleCameraGpu, CameraMovesParticlesWithoutMovingTheVisibleSource) {
    AUREA_REQUIRE_GPU();
    Scene scene(320, 180);
    const auto id = scene.solid(80, 60, {1, 0, 0, 1}, 170, 90);
    auto& effect = scene.add_effect(id, effect_keys::kParticular);
    particle_camera_point(effect);
    effect.params[particular::kShowSource].constant = ParamValue::boolean(true);
    particle_camera_add(*scene.comp);
    for (i64 frame : {9, 15, 20}) {
        const auto image = scene.render(FrameIndex{frame}, 1, true);
        const auto point = particle_camera_measure(image, 0, ~0u, true);
        AUREA_CHECK(point.energy > 10);
        AUREA_CHECK_NEAR(point.x, frame == 15 ? 135. : 170., .2);
        AUREA_CHECK(near4(image.v(200, 70), Vec4{1, 0, 0, 1}, .002f));
        AUREA_CHECK(near4(image.v(100, 70), Vec4{0, 0, 0, 1}, .002f));
    }
}

AUREA_TEST(ParticleCameraGpu, SharedPrecompInstancesKeepIndependentCameraIntervals) {
    AUREA_REQUIRE_GPU();
    Scene scene(320, 180);
    const auto source = scene.solid(160, 180, {1, 1, 1, 1}, 80, 90);
    particle_camera_point(scene.add_effect(source, effect_keys::kParticular));
    auto& timeline = scene.project.timeline();
    const auto childId = timeline.create_composition("particles", 160, 180, 30.);
    scene.comp = timeline.composition(timeline.root());
    auto* child = timeline.composition(childId);
    child->set_transparent_background(true);
    const auto inner = child->add_layer(LayerKind::Shape, "emitter");
    *child->layer(inner) = *scene.comp->layer(source);
    scene.comp->remove_layer(source);
    particle_camera_add(*child, 30);
    for (int i = 0; i < 2; ++i) {
        const auto id = scene.comp->add_layer(LayerKind::Composition, "instance");
        auto* layer = scene.comp->layer(id);
        layer->nested.composition = childId;
        layer->offset = FrameIndex{i * 15};
        layer->transform.anchor = {80, 90, 0};
        layer->transform.position = {80.f + i * 160.f, 90, 0};
    }
    for (i64 frame : {0, 5, 15, 20, 15, 0}) {
        const auto image = scene.render(FrameIndex{frame}, 1, true);
        for (u32 i = 0; i < 2; ++i) {
            const auto point = particle_camera_measure(image, i * 160, (i + 1) * 160);
            const i64 local = frame + i * 15;
            const f64 expected = 80. + i * 160. - (local >= 10 && local < 20 ? 30. : 0.);
            AUREA_CHECK(point.energy > 10);
            AUREA_CHECK_NEAR(point.x, expected, .2);
            AUREA_CHECK_NEAR(point.y, 90., .2);
        }
    }
}

AUREA_TEST(ParticleCameraGpu, GroupCameraPassThroughControlsTheEmitterOnlyWhenEnabled) {
    AUREA_REQUIRE_GPU();
    for (bool through : {false, true}) {
        Scene scene(320, 180);
        const auto source = scene.solid(320, 180, {1, 1, 1, 1}, 160, 90);
        particle_camera_point(scene.add_effect(source, effect_keys::kParticular));
        auto& timeline = scene.project.timeline();
        const auto childId = timeline.create_composition("particles", 320, 180, 30.);
        scene.comp = timeline.composition(timeline.root());
        auto* child = timeline.composition(childId);
        child->set_transparent_background(true);
        const auto inner = child->add_layer(LayerKind::Shape, "emitter");
        *child->layer(inner) = *scene.comp->layer(source);
        scene.comp->remove_layer(source);
        const auto group = scene.comp->add_layer(LayerKind::Composition, "group");
        auto* layer = scene.comp->layer(group);
        layer->nested.composition = childId;
        layer->nested.cameraPassThrough = through;
        layer->transform.anchor = layer->transform.position = {160, 90, 0};
        particle_camera_add(*scene.comp);
        for (i64 frame : {9, 15, 20}) {
            const auto point = particle_camera_measure(scene.render(FrameIndex{frame}, 1, true));
            AUREA_CHECK(point.energy > 10);
            AUREA_CHECK_NEAR(point.x, through && frame == 15 ? 125. : 160., .2);
            AUREA_CHECK_NEAR(point.y, 90., .2);
        }
    }
}
