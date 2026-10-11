#pragma once

#include "MaterialOverrideFixture.hpp"

namespace aurea::test_fixtures::morph_sharing {

// Two primitives exercise nonzero index/vertex offsets. Independent origins
// provide an unshared reference without an alternative deformation algorithm.
inline std::shared_ptr<scene3d::SceneAsset> asset(u32 mode) {
    using namespace scene3d;
    auto source = material_override::asset();
    Material second = source->materials[0]; second.baseColor = Vec4{.3f, .65f, .9f, 1};
    source->materials.push_back(second);
    auto& mesh = source->meshes[0];
    Primitive part = mesh.primitives[0]; part.material = 1;
    part.bounds = Aabb{};
    for (auto& position : part.positions) {
        position = position * .6f + Vec3{.08f, -.06f, -.05f}; part.bounds.add(position);
    }
    mesh.primitives.push_back(std::move(part));
    mesh.morphWeights = {0, 0, .35f, .6f, 0, 0};
    for (auto& primitive : mesh.primitives) {
        primitive.morphTargets.resize(6);
        primitive.morphTargets[2].positions.assign(primitive.positions.size(), Vec3{.2f, .03f, 0});
        primitive.morphTargets[3].positions.assign(primitive.positions.size(), Vec3{-.05f, .08f, 0});
        primitive.morphTargets[2].normals.assign(primitive.positions.size(), Vec3{0, .1f, 0});
        if (mode == 6) {
            primitive.joints.assign(primitive.positions.size() * 4, 0);
            primitive.weights.assign(primitive.positions.size(), Vec4{1, 0, 0, 0});
        }
    }
    if (mode == 3) for (auto& material : source->materials) {
        material.alphaMode = AlphaMode::Blend; material.baseColor.w = .45f;
    }
    if (mode == 5) {
        Node other = source->nodes[0]; other.translation = Vec3{.25f, .12f, .05f};
        source->nodes.push_back(std::move(other)); source->roots.push_back(1);
    }
    if (mode == 6) {
        source->nodes[0].skin = 0; source->nodes.emplace_back(); source->roots.push_back(1);
        Skin skin; skin.joints = {1}; skin.inverseBind = {Mat4::identity()};
        source->skins.push_back(std::move(skin));
    }
    return source;
}

inline scene3d::SceneFrame frame(const std::shared_ptr<const scene3d::SceneAsset>& source,
                                u32 mode, bool cloneOrigins = false, bool floor = false) {
    using namespace scene3d;
    auto result = material_override::frame(source, 0, cloneOrigins);
    result.floor.mode = floor ? 1 : 0; result.floor.reflectivity = floor ? .8f : 0;
    result.floor.contactShadow = floor ? .8f : 0; result.floor.roughness = .02f;
    result.lights[0].castShadows = true;
    for (u32 i = 0; i < result.instances.size(); ++i) {
        auto& instance = result.instances[i];
        instance.nodeWorld.assign(source->nodes.size(), Mat4::identity());
        if (mode == 5) instance.nodeWorld[1] = source->nodes[1].local_matrix();
        auto weights = source->meshes[0].morphWeights;
        weights[0] = i % 2 ? -0.f : 0.f;
        if (mode == 1) weights[2] = i % 2 ? -.6f : .35f;
        if (mode == 2) weights[2] = -.5f + .1f * i;
        instance.morphWeights.assign(source->nodes.size(), weights);
        if (mode == 4) {
            MaterialOverride over; over.materialIndex = 0; over.mask = 1;
            over.baseColor = source->materials[0].baseColor;
            over.baseColor.x = .2f + .25f * (i % 3);
            instance.materials.push_back(over);
        }
        if (mode == 6) {
            instance.skinJointOffset = {0};
            instance.jointMatrices = {Mat4::translation(Vec3{0, .05f * (i % 3), 0})};
        }
        // The mirrored copies need their own normal matrix, even though the
        // deformed primitive streams can be shared. Two-sided material keeps
        // the pipeline the same so this also exercises one mixed-sign batch.
        if (i % 2) instance.world = instance.world * Mat4::scale(Vec3{-1, 1, 1});
    }
    return result;
}

inline u32 unique_weights(u32 mode) { return mode == 1 ? 2 : mode == 2 ? 12 : 1; }
inline u32 color_draws(u32 mode) { return mode == 3 || mode == 6 ? 24 : mode == 4 ? 4 : 2 * unique_weights(mode); }
inline u32 shadow_draws(u32 mode) { return mode == 3 ? 0 : mode == 6 ? 24 : 2 * unique_weights(mode); }

} // namespace aurea::test_fixtures::morph_sharing
