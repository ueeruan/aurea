// Testes do import 3D (glTF/GLB → SceneAsset).
//
// Usam os modelos de amostra da Khronos em tests/data/gltf (fora do git; ver
// .gitignore). Sem a pasta, os testes avisam e passam — o CI sem os modelos
// não quebra, mas também não finge que testou.
#include "TestFramework.hpp"

#include "aurea/scene3d/Importer.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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
    // Níveis de detalhe: dois, cada um de fato mais leve que o anterior.
    std::printf("    LOD: %u -> %zu -> %zu triangulos\n", p.triangle_count(),
                p.lods.size() > 0 ? p.lods[0].size() / 3 : 0, p.lods.size() > 1 ? p.lods[1].size() / 3 : 0);
    AUREA_CHECK_EQ(p.lods.size(), usize{2});
    if (p.lods.size() == 2) {
        AUREA_CHECK(p.lods[0].size() < p.indices.size() * 8 / 10);
        AUREA_CHECK(p.lods[1].size() < p.lods[0].size() * 8 / 10);
        for (u32 i : p.lods[1]) AUREA_CHECK(i < p.positions.size());
    }
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

AUREA_TEST(Scene3D, InvalidChannelsPreservePoseAndDoNotReusePreviousSamples) {
    SceneAsset asset;
    asset.nodes.resize(2);
    asset.roots = {0};
    asset.nodes[0].children = {1};
    asset.nodes[1].parent = 0;
    asset.nodes[1].translation = {2, 3, 4};
    asset.nodes[1].scale = {2, 3, 4};
    asset.nodes[1].morphWeights = {0.25f, 0.75f};
    asset.animations.resize(1);
    auto& clip = asset.animations[0];
    clip.duration = 1;
    clip.samplers.resize(5);
    clip.samplers[0].times = {0, 1};
    clip.samplers[0].values = {30, 0, 0, 50, 0, 0};
    clip.samplers[2].times = {0, 1};
    clip.samplers[2].values = {7, 8, 9}; // Truncated scale.
    clip.samplers[3].times = {0, 1};
    clip.samplers[3].components = 4;
    clip.samplers[3].values = {0, 0, 0, 1}; // Truncated rotation.
    clip.samplers[4].times = {0, 1};
    clip.samplers[4].components = 2;
    clip.samplers[4].interpolation = AnimInterp::CubicSpline;
    clip.samplers[4].values = {0, 0, 1, 0, 0, 0}; // Missing second cubic key.
    clip.channels = {{0, AnimPath::Translation, 0}, {1, AnimPath::Translation, 1},
                     {1, AnimPath::Scale, 2}, {1, AnimPath::Rotation, 3}, {1, AnimPath::Weights, 4}};
    Pose pose;
    for (f32 time : {0.5f, 1.0f, 0.0f, 0.5f}) {
        evaluate_pose(asset, 0, time, pose);
        const Mat4 expected = Mat4::translation({30 + 20 * time, 0, 0}) * asset.nodes[1].local_matrix();
        for (u32 col = 0; col < 4; ++col) for (u32 row = 0; row < 4; ++row)
            AUREA_CHECK_NEAR((&pose.nodeWorld[1].col[col].x)[row], (&expected.col[col].x)[row], 1e-6f);
        AUREA_CHECK_EQ(pose.morphWeights[1].size(), usize{2});
        AUREA_CHECK_EQ(pose.morphWeights[1][0], 0.25f);
        AUREA_CHECK_EQ(pose.morphWeights[1][1], 0.75f);
    }
}

AUREA_TEST(Scene3D, FbxConstantTakeRetainsItsTransform) {
    const auto result = import_scene_file(std::string(AUREA_TEST_DATA_DIR) + "/constant-take.fbx", ImportOptions{});
    AUREA_CHECK_MSG(result.ok(), result.detail.c_str());
    if (!result.ok()) return;
    const auto& asset = *result.asset;
    AUREA_CHECK_EQ(asset.animations.size(), usize{1});
    if (asset.animations.empty()) return;
    i32 node = -1;
    for (usize i = 0; i < asset.nodes.size(); ++i)
        if (asset.nodes[i].name == "AnimatedTriangle") node = static_cast<i32>(i);
    AUREA_CHECK(node >= 0);
    if (node < 0) return;
    AUREA_CHECK_NEAR(asset.nodes[node].translation.x, 0.0f, 1e-6f);
    Pose pose;
    for (f32 time : {0.0f, 0.5f, 1.0f, 0.0f}) {
        evaluate_pose(asset, 0, time, pose);
        AUREA_CHECK_NEAR(pose.nodeWorld[node].col[3].x, 5.0f, 1e-5f);
        AUREA_CHECK_NEAR(pose.nodeWorld[node].col[0].x, 1.0f, 1e-5f);
    }
    evaluate_pose(asset, -1, 0.5f, pose);
    AUREA_CHECK_NEAR(pose.nodeWorld[node].col[3].x, 0.0f, 1e-6f);
}

AUREA_TEST(Scene3D, NonFiniteAnimationTimeCannotCorruptPose) {
    SceneAsset asset;
    asset.nodes.resize(1);
    asset.roots = {0};
    asset.nodes[0].translation = {2, 3, 4};
    asset.animations.resize(1);
    auto& clip = asset.animations[0];
    clip.duration = 1;
    clip.samplers.resize(1);
    clip.samplers[0].times = {0, 1};
    clip.samplers[0].values = {30, 0, 0, 50, 0, 0};
    clip.channels = {{0, AnimPath::Translation, 0}};
    Pose pose;
    for (f32 time : {std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity(),
                     -std::numeric_limits<f32>::infinity()}) {
        AUREA_CHECK_EQ(clip_time(clip, time), 0.0f);
        evaluate_pose(asset, 0, time, pose);
        AUREA_CHECK_EQ(pose.nodeWorld[0].col[3].x, 2.0f);
        AUREA_CHECK_EQ(pose.nodeWorld[0].col[3].y, 3.0f);
        AUREA_CHECK_EQ(pose.nodeWorld[0].col[3].z, 4.0f);
    }
}

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

// -----------------------------------------------------------------------------
// Texto 3D
// -----------------------------------------------------------------------------
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

AUREA_TEST(Scene3D, TriangulatesPolygonWithHoleExactly) {
    using namespace aurea;
    // Quadrado 4×4 com furo 2×2: área 12, 8 triângulos, nenhum dentro do furo.
    std::vector<std::vector<Vec2>> rings{{{0, 0}, {4, 0}, {4, 4}, {0, 4}}, {{1, 1}, {1, 3}, {3, 3}, {3, 1}}};
    std::vector<u32> idx;
    AUREA_CHECK(scene3d::triangulate_polygon(rings, idx));
    std::vector<Vec2> pts;
    for (const auto& r : rings) pts.insert(pts.end(), r.begin(), r.end());
    f64 area = 0;
    for (usize t = 0; t + 2 < idx.size(); t += 3) {
        const Vec2 a = pts[idx[t]], b = pts[idx[t + 1]], c = pts[idx[t + 2]];
        area += std::fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5;
        const Vec2 m{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
        AUREA_CHECK(!(m.x > 1 && m.x < 3 && m.y > 1 && m.y < 3));
    }
    std::printf("    quadrado com furo: %zu triangulos, area %.3f\n", idx.size() / 3, area);
    AUREA_CHECK(idx.size() / 3 == 8);
    AUREA_CHECK(std::fabs(area - 12.0) < 1e-6);
}

AUREA_TEST(Scene3D, Text3DMeshIsClosedAndFacesOutward) {
    using namespace aurea;
    const auto font = text::default_font();
    if (!font) return;
    scene3d::Text3DSpec spec;
    spec.content = "Aurea 80B\nog";
    spec.depth = 0.3f;
    spec.color = Vec4{1.0f, 0.5f, 0.0f, 1.0f};
    scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const scene3d::Primitive& p = r.asset->meshes[0].primitives[0];
    // Toda face aponta para o lado da própria normal (frente, fundo e laterais).
    u32 wrong = 0;
    f64 front = 0, back = 0;
    for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
        const Vec3 a = p.positions[p.indices[t]], b = p.positions[p.indices[t + 1]], c = p.positions[p.indices[t + 2]];
        const Vec3 g = (b - a).cross(c - a);
        const Vec3 n = p.normals[p.indices[t]] + p.normals[p.indices[t + 1]] + p.normals[p.indices[t + 2]];
        if (g.dot(n) < 0) ++wrong;
        if (n.z > 2.9f) front += g.length() * 0.5;
        if (n.z < -2.9f) back += g.length() * 0.5;
    }
    // Área da frente = área das letras pela regra do aninhamento (furos descontados).
    TextData td;
    td.content = spec.content;
    td.size = 100.0f;
    td.alignment = spec.alignment;
    std::vector<std::vector<Vec2>> cs;
    AUREA_CHECK(text::outline(*font, td, cs));
    f64 expected = 0;
    for (usize i = 0; i < cs.size(); ++i) {
        f64 a = 0;
        for (usize k = 0, j = cs[i].size() - 1; k < cs[i].size(); j = k++) a += cs[i][j].x * cs[i][k].y - cs[i][k].x * cs[i][j].y;
        a = std::fabs(a) * 0.5 / 10000.0;
        u32 depth = 0;
        for (usize j = 0; j < cs.size(); ++j) {
            if (j == i) continue;
            bool in = false;
            const Vec2 q = cs[i][0];
            for (usize k = 0, m = cs[j].size() - 1; k < cs[j].size(); m = k++)
                if (((cs[j][k].y > q.y) != (cs[j][m].y > q.y)) && (q.x < (cs[j][m].x - cs[j][k].x) * (q.y - cs[j][k].y) / (cs[j][m].y - cs[j][k].y) + cs[j][k].x)) in = !in;
            if (in) ++depth;
        }
        expected += (depth & 1u) ? -a : a;
    }
    const Vec3 ext = r.asset->bounds.extent();
    std::printf("    texto 3D: %u triangulos, %u vertices, caixa %.2f x %.2f x %.2f, frente %.4f (esperado %.4f), fundo %.4f, invertidas %u\n",
                p.triangle_count(), p.vertex_count(), ext.x, ext.y, ext.z, front, expected, back, wrong);
    AUREA_CHECK(wrong == 0);
    AUREA_CHECK(std::fabs(front - expected) < 0.01 * expected);
    AUREA_CHECK(std::fabs(back - expected) < 0.01 * expected);
    AUREA_CHECK(std::fabs(ext.z - 0.3f) < 1e-4f);
    AUREA_CHECK(r.asset->materials[0].baseColor.x > 0.99f && r.asset->materials[0].baseColor.z < 0.01f);
    // Receita ida e volta.
    scene3d::Text3DSpec back2;
    AUREA_CHECK(scene3d::decode_text3d(scene3d::encode_text3d(spec), back2));
    AUREA_CHECK(back2.content == spec.content && std::fabs(back2.depth - 0.3f) < 1e-4f && back2.alignment == 1);
}

// -----------------------------------------------------------------------------
// KTX2 (KHR_texture_basisu)
// -----------------------------------------------------------------------------
namespace {
void put32(std::vector<aurea::u8>& v, usize at, aurea::u32 x) { for (int i = 0; i < 4; ++i) v[at + i] = static_cast<aurea::u8>(x >> (8 * i)); }
void put64(std::vector<aurea::u8>& v, usize at, aurea::u64 x) { for (int i = 0; i < 8; ++i) v[at + i] = static_cast<aurea::u8>(x >> (8 * i)); }

/// Bloco UASTC 4x4 de cor sólida (modo 8: código 0x17 em 5 bits, depois R, G, B, A).
void uastc_solid(aurea::u8* blk, aurea::u8 r, aurea::u8 g, aurea::u8 b, aurea::u8 a) {
    std::memset(blk, 0, 16);
    const aurea::u64 bits = 0x17ull | (aurea::u64(r) << 5) | (aurea::u64(g) << 13) | (aurea::u64(b) << 21) | (aurea::u64(a) << 29);
    for (int i = 0; i < 8; ++i) blk[i] = static_cast<aurea::u8>(bits >> (8 * i));
}

/// KTX2 8×8 UASTC (sRGB, RGBA) com 4 blocos: vermelho, verde / azul, branco meio transparente.
/// `zstd` = nível supercomprimido num quadro Zstd de bloco cru (válido pela especificação).
std::vector<aurea::u8> make_ktx2(bool zstd) {
    using aurea::u8;
    std::vector<u8> blocks(64);
    uastc_solid(&blocks[0], 255, 0, 0, 255);
    uastc_solid(&blocks[16], 0, 255, 0, 255);
    uastc_solid(&blocks[32], 0, 0, 255, 255);
    uastc_solid(&blocks[48], 255, 255, 255, 128);
    std::vector<u8> level = blocks;
    if (zstd) {
        level = {0x28, 0xB5, 0x2F, 0xFD, 0x20, 64, 0x01, 0x02, 0x00};   // magia, FHD (segmento único), tamanho, bloco cru final de 64
        level.insert(level.end(), blocks.begin(), blocks.end());
    }
    const usize dfdAt = 104, dfdLen = 44, dataAt = 160;
    std::vector<u8> f(dataAt + level.size(), 0);
    const u8 id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    std::memcpy(f.data(), id, 12);
    put32(f, 12, 0);                 // vkFormat indefinido (Basis)
    put32(f, 16, 1);                 // typeSize
    put32(f, 20, 8);                 // largura
    put32(f, 24, 8);                 // altura
    put32(f, 36, 1);                 // faces
    put32(f, 40, 1);                 // níveis
    put32(f, 44, zstd ? 2 : 0);      // supercompressão: nenhuma / Zstd
    put32(f, 48, dfdAt);
    put32(f, 52, dfdLen);
    put64(f, 80, dataAt);            // índice do nível 0
    put64(f, 88, level.size());
    put64(f, 96, blocks.size());
    put32(f, dfdAt, dfdLen);
    put32(f, dfdAt + 4, 0);                          // Khronos, bloco básico
    put32(f, dfdAt + 8, 2u | (40u << 16));           // versão 2, 40 bytes
    f[dfdAt + 12] = 166;                             // modelo UASTC
    f[dfdAt + 13] = 1;                               // BT.709
    f[dfdAt + 14] = 2;                               // sRGB
    f[dfdAt + 16] = 3; f[dfdAt + 17] = 3;            // bloco 4×4
    f[dfdAt + 20] = 16;                              // 16 bytes por bloco
    put32(f, dfdAt + 28, 127u << 16 | (3u << 24));   // amostra: 128 bits, canal RGBA
    put32(f, dfdAt + 40, 0xFFFFFFFFu);
    std::memcpy(&f[dataAt], level.data(), level.size());
    return f;
}
} // namespace

AUREA_TEST(Scene3D, Ktx2UastcTranscodesRawAndZstd) {
    using namespace aurea;
    for (bool z : {false, true}) {
        const std::vector<u8> f = make_ktx2(z);
        AUREA_CHECK(scene3d::is_ktx2(f.data(), f.size()));
        scene3d::Image img;
        AUREA_CHECK(scene3d::decode_ktx2(f.data(), f.size(), img));
        if (img.rgba.size() != 8 * 8 * 4) continue;
        auto px = [&](u32 x, u32 y) { const u8* p = &img.rgba[(y * 8 + x) * 4]; return std::array<int, 4>{p[0], p[1], p[2], p[3]}; };
        std::printf("    ktx2 %s: %ux%u, cantos (%d,%d,%d) (%d,%d,%d) (%d,%d,%d) (%d,%d,%d,a%d), alfa %d\n", z ? "zstd" : "cru", img.width,
                    img.height, px(1, 1)[0], px(1, 1)[1], px(1, 1)[2], px(6, 1)[0], px(6, 1)[1], px(6, 1)[2], px(1, 6)[0], px(1, 6)[1],
                    px(1, 6)[2], px(6, 6)[0], px(6, 6)[1], px(6, 6)[2], px(6, 6)[3], img.hasAlpha ? 1 : 0);
        AUREA_CHECK((px(1, 1) == std::array<int, 4>{255, 0, 0, 255}));
        AUREA_CHECK((px(6, 1) == std::array<int, 4>{0, 255, 0, 255}));
        AUREA_CHECK((px(1, 6) == std::array<int, 4>{0, 0, 255, 255}));
        AUREA_CHECK((px(6, 6) == std::array<int, 4>{255, 255, 255, 128}));
        AUREA_CHECK(img.hasAlpha);
    }
    // Arquivo truncado: recusa, sem ler fora.
    std::vector<u8> bad = make_ktx2(false);
    bad.resize(120);
    scene3d::Image img;
    AUREA_CHECK(!scene3d::decode_ktx2(bad.data(), bad.size(), img));
}

AUREA_TEST(Scene3D, GltfWithKtx2OnlyTextureImports) {
    using namespace aurea;
    const std::string dir = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/";
    const std::vector<u8> k = make_ktx2(true);
    {
        std::FILE* f = std::fopen((dir + "aurea_teste_tex.ktx2").c_str(), "wb");
        AUREA_CHECK(f != nullptr);
        if (!f) return;
        std::fwrite(k.data(), 1, k.size(), f);
        std::fclose(f);
    }
    const char* json = R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_texture_basisu"],"extensionsRequired":["KHR_texture_basisu"],
"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}],
"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
"images":[{"uri":"aurea_teste_tex.ktx2","mimeType":"image/ktx2"}],
"textures":[{"extensions":{"KHR_texture_basisu":{"source":0}}}],
"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    const std::string path = dir + "aurea_teste_ktx2.gltf";
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite(json, 1, std::strlen(json), f);
        std::fclose(f);
    }
    scene3d::ImportOptions o;
    scene3d::ImportResult r = scene3d::import_gltf_file(path, o);
    std::printf("    glTF com KTX2: %s (%s)\n", r.ok() ? "importou" : "falhou", r.detail.c_str());
    AUREA_CHECK(r.ok());
    if (r.ok()) {
        AUREA_CHECK(r.asset->images.size() == 1 && r.asset->images[0].width == 8);
        AUREA_CHECK(r.asset->materials[0].baseColorTex.image == 0);
        AUREA_CHECK(!r.asset->images[0].rgba.empty() && r.asset->images[0].rgba[0] == 255 && r.asset->images[0].rgba[1] == 0);
    }
    std::remove(path.c_str());
    std::remove((dir + "aurea_teste_tex.ktx2").c_str());
}
