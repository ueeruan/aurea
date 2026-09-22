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

#include "aurea/scene3d/Animation.hpp"
#include "aurea/scene3d/Environment.hpp"
#include <chrono>

AUREA_TEST(Scene3D, EnvironmentBuildIsFastAndSane) {
    const auto t0 = std::chrono::steady_clock::now();
    const EnvironmentMaps m = build_studio_environment(128);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n    (ambiente de estudio: %.0f ms)", ms);
    AUREA_CHECK_EQ(m.prefiltered.size, 128u);
    AUREA_CHECK_EQ(m.prefiltered.mips, 6u);   // 128..4
    AUREA_CHECK_EQ(m.irradiance.levels[0].size(), static_cast<usize>(32 * 32 * 6 * 4));
    // Irradiância de cima (+Y) mais clara que a de baixo (−Y): céu × chão.
    auto lum = [](const CubeData& c, u32 face) {
        const usize idx = (static_cast<usize>(face) * c.size * c.size + (c.size / 2) * c.size + c.size / 2) * 4;
        return half_to_float(c.levels[0][idx]) + half_to_float(c.levels[0][idx + 1]) + half_to_float(c.levels[0][idx + 2]);
    };
    AUREA_CHECK(lum(m.irradiance, 2) > lum(m.irradiance, 3) * 2.0f);
    // LUT: A+B ≤ ~1 (energia) e B cresce com rugosidade em ângulo rasante.
    for (usize i = 0; i + 1 < m.brdfLut.size(); i += 4) {
        const f32 a = half_to_float(m.brdfLut[i]), b = half_to_float(m.brdfLut[i + 1]);
        AUREA_CHECK(a >= 0.0f && b >= 0.0f && a + b <= 1.05f);
    }
    for (f32 v : {0.0f, 0.5f, 1.0f, 65504.0f, 1e-3f}) AUREA_CHECK_NEAR(half_to_float(float_to_half(v)), v, std::fmax(1e-3f, v * 1e-3f));
}


AUREA_TEST(Scene3D, SkinnedAnimationPoseFollowsTheClipTime) {
    if (!have("Fox.glb")) return;
    ImportResult r = load("Fox.glb");
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK(!a.animations.empty() && !a.skins.empty());
    const Animation& clip = a.animations[0];
    AUREA_CHECK(clip.duration > 0.1f);
    // Laço: o tempo da layer volta ao começo do clipe (e negativos também).
    AUREA_CHECK_NEAR(clip_time(clip, clip.duration + 0.25), 0.25f, 1e-4f);
    AUREA_CHECK_NEAR(clip_time(clip, -0.25), clip.duration - 0.25f, 1e-4f);

    Pose rest, p0, p1, p1b;
    evaluate_pose(a, -1, 0.0f, rest);
    evaluate_pose(a, 0, 0.0f, p0);
    evaluate_pose(a, 0, clip.duration * 0.5f, p1);
    evaluate_pose(a, 0, clip.duration * 0.5f, p1b);
    AUREA_CHECK_EQ(p1.jointMatrices.size(), a.skins[0].joints.size());
    AUREA_CHECK_EQ(p1.nodeWorld.size(), a.nodes.size());
    // Pose de repouso: junta × inversa de bind ≈ identidade (a malha fica onde foi modelada).
    f32 restErr = 0.0f;
    for (const Mat4& m : rest.jointMatrices) {
        for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) {
            restErr = std::max(restErr, std::fabs((&m.col[c].x)[k] - (c == k ? 1.0f : 0.0f)));
        }
    }
    AUREA_CHECK_MSG(restErr < 1e-3f, "pose de repouso deveria ser identidade");
    // A pose muda com o tempo, e o mesmo instante dá os mesmos bits (seek = export).
    f32 moved = 0.0f;
    for (usize j = 0; j < p0.jointMatrices.size(); ++j) {
        for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) {
            moved = std::max(moved, std::fabs((&p0.jointMatrices[j].col[c].x)[k] - (&p1.jointMatrices[j].col[c].x)[k]));
            AUREA_CHECK(std::isfinite((&p1.jointMatrices[j].col[c].x)[k]));
            AUREA_CHECK_EQ((&p1.jointMatrices[j].col[c].x)[k], (&p1b.jointMatrices[j].col[c].x)[k]);
        }
    }
    AUREA_CHECK(moved > 0.05f);
}

AUREA_TEST(Scene3D, ObjAndFbxImportThroughUfbx) {
    // OBJ escrito na hora: um quadrado (duas faces) com material .mtl vermelho.
    const std::string dir = std::string(AUREA_TEST_DATA_DIR) + "/../";
    const std::string obj = "aurea_teste_quad.obj", mtl = "aurea_teste_quad.mtl";
    {
        std::FILE* f = std::fopen(mtl.c_str(), "wb");
        std::fputs("newmtl vermelho\nKd 1.0 0.0 0.0\n", f);
        std::fclose(f);
        f = std::fopen(obj.c_str(), "wb");
        std::fputs("mtllib aurea_teste_quad.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\n"
                   "usemtl vermelho\nf 1//1 2//1 3//1 4//1\n", f);
        std::fclose(f);
    }
    ImportOptions o;
    ImportResult r = import_scene_file(obj, o);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (r.ok()) {
        const SceneAsset& a = *r.asset;
        AUREA_CHECK_EQ(a.stats.triangles, 2u);
        AUREA_CHECK_EQ(a.stats.vertices, 4u);   // os cantos iguais foram soldados
        AUREA_CHECK(!a.materials.empty());
        if (!a.materials.empty()) {
            AUREA_CHECK_NEAR(a.materials[0].baseColor.x, 1.0f, 1e-3f);
            AUREA_CHECK_NEAR(a.materials[0].baseColor.y, 0.0f, 1e-3f);
        }
        AUREA_CHECK_NEAR(a.bounds.extent().x, 1.0f, 1e-3f);
    }
    // Arquivo que não existe: erro específico, nunca "importado".
    ImportResult bad = import_scene_file("nao_existe.fbx", o);
    AUREA_CHECK(!bad.ok() && bad.error == ImportError::FileNotFound);
    // Lixo com extensão .fbx: formato inválido.
    {
        std::FILE* f = std::fopen("aurea_lixo.fbx", "wb");
        std::fputs("isto nao e um fbx", f);
        std::fclose(f);
    }
    ImportResult junk = import_scene_file("aurea_lixo.fbx", o);
    AUREA_CHECK(!junk.ok() && junk.error == ImportError::InvalidFormat);
    (void)dir;
}
