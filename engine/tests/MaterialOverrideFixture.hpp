#pragma once

#include "aurea/scene3d/SceneRenderer.hpp"
#include <array>

namespace aurea::test_fixtures::material_override {

inline std::shared_ptr<scene3d::SceneAsset> asset(bool blue = false) {
    using namespace scene3d;
    auto source = std::make_shared<SceneAsset>();
    Material material; material.baseColor = Vec4{.8f, .65f, .45f, 1};
    material.metallic = .15f; material.roughness = .55f; material.doubleSided = true;
    material.baseColorTex.image = 0;
    source->materials.push_back(material);
    Image image; image.width = image.height = 2;
    const std::array<u8, 4> pixel = blue ? std::array<u8, 4>{64, 96, 255, 255}
                                      : std::array<u8, 4>{255, 224, 128, 255};
    for (u32 i = 0; i < 4; ++i) image.rgba.insert(image.rgba.end(), pixel.begin(), pixel.end());
    source->images.push_back(std::move(image));
    Primitive primitive; primitive.material = 0;
    primitive.positions = {{-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0}, {-.5f, .5f, 0}};
    primitive.normals.assign(4, Vec3{0, 0, -1});
    primitive.uv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    primitive.indices = {0, 2, 1, 0, 3, 2};
    primitive.bounds.min = Vec3{-.5f, -.5f, 0}; primitive.bounds.max = Vec3{.5f, .5f, 0};
    Mesh mesh; mesh.bounds = primitive.bounds; mesh.primitives.push_back(std::move(primitive));
    source->bounds = mesh.bounds; source->meshes.push_back(std::move(mesh));
    Node node; node.mesh = 0; source->nodes.push_back(node); source->roots = {0};
    return source;
}

inline scene3d::SceneFrame frame(const std::shared_ptr<const scene3d::SceneAsset>& source,
                                u32 mode, bool cloneOrigins = false) {
    using namespace scene3d;
    SceneFrame result; result.camera = default_camera(320, 180); result.post.bloom = false;
    result.floor.mode = mode == 6 ? 1 : 0; result.floor.reflectivity = 0;
    SceneLight light; light.kind = LightKindGpu::Directional; light.direction = Vec3{-.4f, .7f, .8f};
    light.intensity = 2; light.castShadows = mode == 6; result.lights.push_back(light);
    for (u32 i = 0; i < 12; ++i) {
        SceneInstance instance;
        instance.asset = cloneOrigins ? std::make_shared<SceneAsset>(*source) : source;
        instance.assetKey = cloneOrigins ? 100 + i : 1;
        instance.world = Mat4::translation(Vec3{45.f + 46.f * (i % 6), 60.f + 55.f * (i / 6), 0})
                         * Mat4::scale(Vec3{32, 32, 32});
        instance.castShadows = true;
        if (mode) {
            MaterialOverride over; over.mask = 0x37;
            over.baseColor = source->materials[0].baseColor;
            over.metallic = source->materials[0].metallic; over.roughness = source->materials[0].roughness;
            if (mode == 2 || mode == 6) over.baseColor.x = .3f;
            if (mode == 3) over.baseColor.x = .2f + .25f * (i % 3);
            if (mode == 4) over.baseColor.x = i % 2 ? .3f : .7f;
            if (mode == 5) { over.mask |= 8; over.baseColor.w = .45f; }
            instance.materials.push_back(over);
        }
        result.instances.push_back(std::move(instance));
    }
    return result;
}

} // namespace aurea::test_fixtures::material_override
