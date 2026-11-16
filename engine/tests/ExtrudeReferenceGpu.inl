namespace extrude_tests {
struct EvalFixture {
    EffectRegistry registry;
    EffectInstance instance;
    std::vector<ParamValue> values;
    LayerPlacement placement;
    Layer layer;
    EffectEval eval;
    EvalFixture() {
        extrude_reference::register_effect(registry);
        const auto type = effect_type_id(extrude_reference::kKey);
        initialize_instance(instance, *registry.params(type));
        for (const auto& parameter : instance.params) values.push_back(parameter.constant);
        placement.layerWidth = 100; placement.layerHeight = 100;
        placement.compWidth = 160; placement.compHeight = 160;
        eval.effect = registry.find(type); eval.instance = &instance;
        eval.values = values.data(); eval.count = static_cast<u32>(values.size());
        eval.placement = &placement; eval.layer = &layer;
        values[extrude_reference::kStrokeWidth] = ParamValue::scalar(0);
    }
};
}

AUREA_TEST(ExtrudeReference, ParametersAndRealDirectionalVanishingCameraGeometry) {
    using namespace extrude_reference;
    extrude_tests::EvalFixture fixture;
    AUREA_CHECK_EQ(fixture.values.size(), static_cast<usize>(kCount));
    AUREA_CHECK_NEAR(fixture.values[kVanishing].as_vec2().x, .8f, 1e-6);
    AUREA_CHECK_NEAR(fixture.values[kVanishing].as_vec2().y, .8f, 1e-6);
    vector::Contour ring;
    ring.pts = {{20, 20}, {80, 20}, {80, 80}, {20, 80}};
    SourcePaths source;
    source.rings.push_back(std::move(ring));
    std::vector<SourcePaths> sources{std::move(source)};
    std::vector<Vec4> directional, vanishing, camera;
    Rect bounds;
    AUREA_CHECK(make_geometry(fixture.eval, sources, directional, bounds).ok());
    AUREA_CHECK_EQ(directional.size(), 36u * 4u);
    AUREA_CHECK(bounds.w > 60 && bounds.h > 60);
    fixture.values[kMode] = ParamValue::scalar(1);
    AUREA_CHECK(make_geometry(fixture.eval, sources, vanishing, bounds).ok());
    AUREA_CHECK(!vanishing.empty() && directional != vanishing);
    fixture.values[kMode] = ParamValue::scalar(2);
    fixture.placement.sceneCameraValid = true;
    fixture.placement.sceneCompFromWorld.col[0].x = 1000;
    fixture.placement.sceneCompFromWorld.col[1].y = 1000;
    fixture.placement.sceneCompFromWorld.col[2].w = 1;
    fixture.placement.sceneCompFromWorld.col[3].w = 1000;
    AUREA_CHECK(make_geometry(fixture.eval, sources, camera, bounds).ok());
    AUREA_CHECK(!camera.empty() && camera != vanishing);
    for (const auto& vertex : camera) {
        AUREA_CHECK(std::isfinite(vertex.x) && std::isfinite(vertex.y));
        AUREA_CHECK(std::isfinite(vertex.z) && std::isfinite(vertex.w));
    }
}

AUREA_TEST(ExtrudeReference, SourcePathsFollowRasterOrigin) {
    using namespace extrude_reference;
    extrude_tests::EvalFixture fixture;
    fixture.layer.kind = LayerKind::Shape;
    fixture.layer.shape.shapeType = 0;
    fixture.layer.shape.bounds = {0, 0, 100, 100};
    fixture.placement.sourceShift = {25, 10};
    std::vector<SourcePaths> paths;
    AUREA_CHECK(source_paths(fixture.eval, paths).ok());
    AUREA_CHECK(!paths.empty());
    f32 minimumX = 1e30f, minimumY = 1e30f;
    for (const auto& source : paths) for (const auto& ring : source.rings) for (Vec2 point : ring.pts) {
        minimumX = std::min(minimumX, point.x);
        minimumY = std::min(minimumY, point.y);
    }
    AUREA_CHECK_NEAR(minimumX, 25, .001);
    AUREA_CHECK_NEAR(minimumY, 10, .001);
}

AUREA_TEST(ExtrudeReference, OrthographicCameraAcceptsUnitWWithLargerNearDistance) {
    using namespace extrude_reference;
    extrude_tests::EvalFixture fixture;
    fixture.values[kMode] = ParamValue::scalar(2);
    fixture.placement.sceneCameraValid = true;
    fixture.placement.sceneNearZ = 2;
    fixture.placement.sceneCompFromWorld.col[2].z = -.001f;
    fixture.placement.sceneCompFromWorld.col[3].z = .5f;
    vector::Contour ring;
    ring.pts = {{20, 20}, {80, 20}, {80, 80}, {20, 80}};
    SourcePaths source;
    source.rings.push_back(std::move(ring));
    std::vector<Vec4> vertices;
    Rect bounds;
    AUREA_CHECK(make_geometry(fixture.eval, {source}, vertices, bounds).ok());
    AUREA_CHECK_EQ(vertices.size(), 36u * 4u);
    f32 nearest = 0, farthest = 1;
    for (usize index = 0; index < vertices.size(); index += 4) {
        const Vec4 point = vertices[index];
        AUREA_CHECK(std::isfinite(point.x) && std::isfinite(point.y));
        AUREA_CHECK_NEAR(point.w, 1, 1e-6);
        nearest = std::max(nearest, point.z);
        farthest = std::min(farthest, point.z);
    }
    AUREA_CHECK_NEAR(nearest, .5f, 1e-6);
    AUREA_CHECK_NEAR(farthest, .4f, 1e-6);
}

AUREA_TEST(ExtrudeReference, AnchorShiftFollowsLayerRotationAndScale) {
    using namespace extrude_reference;
    extrude_tests::EvalFixture fixture;
    fixture.values[kMode] = ParamValue::scalar(2);
    fixture.placement.sceneCameraValid = true;
    fixture.placement.layerWorldValid = true;
    fixture.placement.layerWorld = Mat4::from_quat(Quat::from_axis_angle({0, 0, 1}, 90 * kDeg2Rad)) *
        Mat4::scale({2, 3, 1});
    vector::Contour ring;
    ring.pts = {{20, 20}, {80, 20}, {80, 80}, {20, 80}};
    SourcePaths source;
    source.rings.push_back(std::move(ring));
    std::vector<Vec4> centered, shifted;
    Rect bounds;
    AUREA_CHECK(make_geometry(fixture.eval, {source}, centered, bounds).ok());
    fixture.values[kAnchor] = ParamValue::vec2(.4f, .5f);
    AUREA_CHECK(make_geometry(fixture.eval, {source}, shifted, bounds).ok());
    AUREA_CHECK(!centered.empty() && centered.size() == shifted.size());
    for (usize index = 0; index < std::min(centered.size(), shifted.size()); index += 4) {
        AUREA_CHECK_NEAR(shifted[index].x - centered[index].x, 0, .001);
        AUREA_CHECK_NEAR(shifted[index].y - centered[index].y, 20, .001);
    }
}

AUREA_TEST(ExtrudeReferenceGpu, DirectionalVanishingChangePixelsAndRadiusFreeSourceCompositionIsFinite) {
    AUREA_REQUIRE_GPU();
    if (!gpu().effects.find(effect_type_id(extrude_reference::kKey)))
        extrude_reference::register_effect(gpu().effects);
    Scene scene(160, 128);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.solid(40, 32, {1, .25f, .05f, 1}, 80, 64);
    const auto plain = scene.render();
    auto& effect = scene.add_effect(layer, extrude_reference::kKey);
    effect.params[extrude_reference::kBackLength].constant = ParamValue::scalar(24);
    const auto directional = scene.render();
    AUREA_CHECK(image_diff(plain, directional) > .01f);
    effect.params[extrude_reference::kMode].constant = ParamValue::scalar(1);
    const auto vanishing = scene.render();
    AUREA_CHECK(image_diff(vanishing, directional) > .01f);
    for (const f32 value : vanishing.px) AUREA_CHECK(std::isfinite(value));
    AUREA_CHECK(image_diff(vanishing, scene.render()) < 1e-6f);
}
