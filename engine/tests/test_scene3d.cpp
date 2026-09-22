// Testes do import 3D (glTF/GLB → SceneAsset).
//
// Usam os modelos de amostra da Khronos em tests/data/gltf (fora do git; ver
// .gitignore). Sem a pasta, os testes avisam e passam — o CI sem os modelos
// não quebra, mas também não finge que testou.
#include "TestFramework.hpp"

#include "aurea/scene3d/Importer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

std::string data_path(const char* rel) { return std::string(AUREA_TEST_DATA_DIR) + "/gltf/" + rel; }

bool have(const char* rel) {
    std::FILE* f = std::fopen(data_path(rel).c_str(), "rb");
    if (!f) {
        std::printf("\n    (modelo de amostra ausente: %s — teste pulado)\n", rel);
        return false;
    }
    std::fclose(f);
    return true;
}

ImportResult load(const char* rel) {
    ImportOptions o;
    return import_gltf_file(data_path(rel), o);
}

const Material* material_named(const SceneAsset& a, const char* name) {
    for (const Material& m : a.materials) if (m.name == name) return &m;
    return nullptr;
}

} // namespace

AUREA_TEST(Scene3D, BoxImportsWithBoundsAndNormals) {
    if (!have("Box.glb")) return;
    ImportResult r = load("Box.glb");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.stats.meshes, 1u);
    AUREA_CHECK_EQ(a.stats.triangles, 12u);
    AUREA_CHECK(a.bounds.valid());
    // Caixa unitária centrada na origem (o nó raiz gira −90° em X).
    AUREA_CHECK_NEAR(a.bounds.extent().x, 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.bounds.extent().y, 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.bounds.extent().z, 1.0f, 1e-4f);
    const Primitive& p = a.meshes[0].primitives[0];
    AUREA_CHECK_EQ(p.normals.size(), p.positions.size());
    for (const Vec3& n : p.normals) AUREA_CHECK_NEAR(n.length(), 1.0f, 1e-4f);
}

AUREA_TEST(Scene3D, ExternalBufferAndTextureResolveNextToTheGltf) {
    if (!have("BoxTextured/BoxTextured.gltf")) return;
    ImportResult r = load("BoxTextured/BoxTextured.gltf");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.images.size(), static_cast<usize>(1));
    AUREA_CHECK(a.images[0].width > 0 && a.images[0].height > 0);
    AUREA_CHECK_EQ(a.images[0].rgba.size(), static_cast<usize>(a.images[0].width) * a.images[0].height * 4);
    AUREA_CHECK(a.materials[0].baseColorTex.valid());
    AUREA_CHECK(!a.meshes[0].primitives[0].uv0.empty());
}

AUREA_TEST(Scene3D, DamagedHelmetHasFullPbrSetWithTangents) {
    if (!have("DamagedHelmet.glb")) return;
    ImportResult r = load("DamagedHelmet.glb");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.materials.size(), static_cast<usize>(1));
    const Material& m = a.materials[0];
    AUREA_CHECK(m.baseColorTex.valid());
    AUREA_CHECK(m.metallicRoughnessTex.valid());
    AUREA_CHECK(m.normalTex.valid());
    AUREA_CHECK(m.occlusionTex.valid());
    AUREA_CHECK(m.emissiveTex.valid());
    AUREA_CHECK_EQ(a.stats.images, 5u);
    const Primitive& p = a.meshes[0].primitives[0];
    // O arquivo não traz TANGENT: geradas porque há mapa de normal.
    AUREA_CHECK_EQ(p.tangents.size(), p.positions.size());
    AUREA_CHECK(p.generatedTangents);
    for (const Vec4& t : p.tangents) AUREA_CHECK(t.w == 1.0f || t.w == -1.0f);
    AUREA_CHECK_EQ(a.stats.triangles, 15452u);   // 46356 índices
}

AUREA_TEST(Scene3D, FoxHasSkinAndThreeNamedClips) {
    if (!have("Fox.glb")) return;
    ImportResult r = load("Fox.glb");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.skins.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(a.skins[0].joints.size(), a.skins[0].inverseBind.size());
    AUREA_CHECK_EQ(a.animations.size(), static_cast<usize>(3));
    bool survey = false, walk = false, run = false;
    for (const Animation& an : a.animations) {
        survey = survey || an.name == "Survey";
        walk = walk || an.name == "Walk";
        run = run || an.name == "Run";
        AUREA_CHECK(an.duration > 0.0f);
        for (const AnimSampler& s : an.samplers) {
            for (usize k = 1; k < s.times.size(); ++k) AUREA_CHECK(s.times[k] >= s.times[k - 1]);
        }
    }
    AUREA_CHECK(survey && walk && run);
    const Primitive& p = a.meshes[0].primitives[0];
    AUREA_CHECK(p.skinned());
    for (const Vec4& w : p.weights) AUREA_CHECK_NEAR(w.x + w.y + w.z + w.w, 1.0f, 1e-3f);
}

AUREA_TEST(Scene3D, MorphTargetsAndWeightAnimation) {
    if (!have("AnimatedMorphCube.glb")) return;
    ImportResult r = load("AnimatedMorphCube.glb");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    const Primitive& p = a.meshes[0].primitives[0];
    AUREA_CHECK_EQ(p.morphTargets.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(p.morphTargets[0].positions.size(), p.positions.size());
    AUREA_CHECK_EQ(a.meshes[0].morphWeights.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(a.animations.size(), static_cast<usize>(1));
    bool weights = false;
    for (const AnimChannel& c : a.animations[0].channels) {
        if (c.path != AnimPath::Weights) continue;
        weights = true;
        AUREA_CHECK_EQ(a.animations[0].samplers[c.sampler].components, 2u);
    }
    AUREA_CHECK(weights);
}

AUREA_TEST(Scene3D, AlphaModesAreKept) {
    if (!have("AlphaBlendModeTest.glb")) return;
    ImportResult r = load("AlphaBlendModeTest.glb");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    const Material* blend = material_named(a, "MatBlend");
    const Material* mask = material_named(a, "MatCutoff25");
    const Material* opaque = material_named(a, "MatOpaque");
    AUREA_CHECK(blend && mask && opaque);
    if (!blend || !mask || !opaque) return;
    AUREA_CHECK(blend->alphaMode == AlphaMode::Blend);
    AUREA_CHECK(mask->alphaMode == AlphaMode::Mask);
    AUREA_CHECK(opaque->alphaMode == AlphaMode::Opaque);
    AUREA_CHECK_NEAR(mask->alphaCutoff, 0.25f, 1e-6f);
    const Material* def = material_named(a, "MatCutoffDefault");
    AUREA_CHECK(def && std::fabs(def->alphaCutoff - 0.5f) < 1e-6f);
}

AUREA_TEST(Scene3D, CamerasAreImported) {
    if (!have("Cameras/Cameras.gltf")) return;
    ImportResult r = load("Cameras/Cameras.gltf");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.cameras.size(), static_cast<usize>(2));
    bool persp = false, ortho = false;
    for (const CameraInfo& c : a.cameras) {
        persp = persp || c.perspective;
        ortho = ortho || !c.perspective;
    }
    AUREA_CHECK(persp && ortho);
}

AUREA_TEST(Scene3D, TruncatedGlbFailsWithSpecificError) {
    if (!have("Box.glb")) return;
    std::FILE* f = std::fopen(data_path("Box.glb").c_str(), "rb");
    std::vector<u8> bytes(4096);
    bytes.resize(std::fread(bytes.data(), 1, bytes.size(), f));
    std::fclose(f);
    bytes.resize(bytes.size() / 2);   // metade do arquivo
    ImportOptions o;
    ImportResult r = import_gltf_memory(bytes.data(), bytes.size(), "", o);
    AUREA_CHECK(!r.ok());
    AUREA_CHECK(r.error != ImportError::None);
    AUREA_CHECK(!r.detail.empty());

    const char junk[] = "isto nao e um modelo 3D de jeito nenhum";
    r = import_gltf_memory(reinterpret_cast<const u8*>(junk), sizeof(junk), "", o);
    AUREA_CHECK(r.error == ImportError::InvalidFormat);
}

AUREA_TEST(Scene3D, MissingExternalBufferIsReportedAsSuch) {
    if (!have("BoxTextured/BoxTextured.gltf")) return;
    std::FILE* f = std::fopen(data_path("BoxTextured/BoxTextured.gltf").c_str(), "rb");
    std::vector<u8> json(65536);
    json.resize(std::fread(json.data(), 1, json.size(), f));
    std::fclose(f);
    ImportOptions o;
    // Diretório base sem o .bin ao lado.
    ImportResult r = import_gltf_memory(json.data(), json.size(), "diretorio/que/nao/existe/", o);
    AUREA_CHECK(r.error == ImportError::MissingBuffer);
}

AUREA_TEST(Scene3D, CancelledImportStopsWithCancelled) {
    if (!have("DamagedHelmet.glb")) return;
    ImportProgress progress;
    progress.cancel = true;
    ImportOptions o;
    ImportResult r = import_gltf_file(data_path("DamagedHelmet.glb"), o, &progress);
    AUREA_CHECK(r.error == ImportError::Cancelled);
    AUREA_CHECK(r.asset == nullptr);
}

AUREA_TEST(Scene3D, TextureCapDownscalesAndWarns) {
    if (!have("DamagedHelmet.glb")) return;
    ImportOptions o;
    o.maxTextureSize = 512;
    ImportResult r = import_gltf_file(data_path("DamagedHelmet.glb"), o);
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    for (const Image& img : r.asset->images) {
        if (img.rgba.empty()) continue;
        AUREA_CHECK(img.width <= 512 && img.height <= 512);
    }
    AUREA_CHECK(!r.asset->warnings.empty());
}
