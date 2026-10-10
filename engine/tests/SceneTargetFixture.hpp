#pragma once
#include "MaterialOverrideFixture.hpp"
#include "aurea/render/Renderer.hpp"

namespace aurea::test_fixtures::scene_target {
inline void add_scene(FrameSnapshot& snapshot, const std::shared_ptr<const scene3d::SceneAsset>& asset, bool shadow = true) {
    const u32 index = static_cast<u32>(snapshot.scenes.size());
    auto scene = material_override::frame(asset, shadow ? 6 : 0);
    scene.camera = scene3d::default_camera(snapshot.compWidth, snapshot.compHeight);
    const Mat4 scale = Mat4::scale(Vec3{snapshot.compWidth / 320.f, snapshot.compHeight / 180.f, 1});
    for (auto& instance : scene.instances) {
        instance.world = scale * instance.world;
        // Separate assets with scene index zero must keep independent GPU caches.
        instance.assetKey = static_cast<u64>(reinterpret_cast<std::uintptr_t>(instance.asset.get()));
    }
    scene.shadow.mapResolution = 4096;
    snapshot.scenes.push_back(std::move(scene));
    RenderLayer layer; layer.source.kind = LayerSource::Kind::Scene3D;
    layer.source.sceneGroup = index; layer.source.width = snapshot.compWidth; layer.source.height = snapshot.compHeight;
    snapshot.layers.push_back(std::move(layer)); snapshot.plans.emplace_back();
}
inline FrameSnapshot pixels() {
    FrameSnapshot snapshot; snapshot.compWidth = 320; snapshot.compHeight = 180;
    snapshot.background = Vec4{.03f, .06f, .12f, .35f};
    auto asset = material_override::asset(); asset->materials[0].alphaMode = scene3d::AlphaMode::Blend;
    asset->materials[0].baseColor.w = .55f; asset->materials[0].emissive = Vec3{.3f, .05f, .02f};
    asset->materials[0].emissiveStrength = 4;
    add_scene(snapshot, asset, false);
    snapshot.scenes[0].post.bloom = true; snapshot.scenes[0].post.bloomIntensity = .35f;
    snapshot.scenes[0].environment.exposure = .6f;
    for (const bool blue : {false, true}) {
        auto child = std::make_unique<FrameSnapshot>(); child->compWidth = blue ? 153 : 151; child->compHeight = blue ? 89 : 87;
        child->background = Vec4{}; add_scene(*child, material_override::asset(blue));
        child->scenes[0].environment.exposure = blue ? .8f : 1.2f;
        RenderLayer nested; nested.source.kind = LayerSource::Kind::Nested;
        nested.source.nestedIndex = static_cast<u32>(snapshot.nested.size());
        nested.source.width = child->compWidth; nested.source.height = child->compHeight;
        nested.compFromLayer = Mat4::translation(Vec3{blue ? 150.f : 3.f, blue ? 80.f : 4.f, 0});
        nested.opacity = .65f;
        snapshot.nested.push_back(std::move(child)); snapshot.layers.push_back(std::move(nested)); snapshot.plans.emplace_back();
    }
    return snapshot;
}
}
