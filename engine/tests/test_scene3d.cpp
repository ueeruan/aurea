// Testes do import 3D (glTF/GLB → SceneAsset).
//
// Usam os modelos de amostra da Khronos em tests/data/gltf (fora do git; ver
// .gitignore). Sem a pasta, os testes avisam e passam — o CI sem os modelos
// não quebra, mas também não finge que testou.
#include "TestFramework.hpp"
#include "ImageIO.hpp"

#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/StudioEnvironment.hpp"

#include <algorithm>
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

namespace {
/// Equiretangular sintético: fundo constante + um "sol" (disco de poucos texels).
std::vector<f32> synthetic_equirect(u32 w, u32 h, f32 base, f32 sun, u32 sunX, u32 sunY, u32 sunR) {
    std::vector<f32> px(static_cast<usize>(w) * h * 3, base);
    if (sun <= 0.0f) return px;
    for (u32 y = sunY > sunR ? sunY - sunR : 0; y <= sunY + sunR && y < h; ++y) {
        for (u32 x = sunX - sunR; x <= sunX + sunR; ++x) {
            const i32 dx = static_cast<i32>(x) - static_cast<i32>(sunX), dy = static_cast<i32>(y) - static_cast<i32>(sunY);
            if (static_cast<u32>(dx * dx + dy * dy) > sunR * sunR) continue;
            f32* p = &px[(static_cast<usize>(y) * w + x) * 3];
            p[0] = p[1] = p[2] = sun;
        }
    }
    return px;
}

/// Σ radiância × ângulo sólido (canal R) de um nível de cubo RGBA16F.
f64 cube_energy(const CubeData& c, u32 level, f32 minus = 0.0f) {
    const u32 s = std::max(1u, c.size >> level);
    f64 e = 0.0;
    for (u32 f = 0; f < 6; ++f) {
        for (u32 y = 0; y < s; ++y) {
            for (u32 x = 0; x < s; ++x) {
                const f64 u = 2.0 * (x + 0.5) / s - 1.0, v = 2.0 * (y + 0.5) / s - 1.0;
                const f64 sa = 4.0 / (static_cast<f64>(s) * s) / std::pow(1.0 + u * u + v * v, 1.5);
                const usize i = ((static_cast<usize>(f) * s + y) * s + x) * 4;
                e += (half_to_float(c.levels[level][i]) - minus) * sa;
            }
        }
    }
    return e;
}

f64 equirect_energy(const std::vector<f32>& px, u32 w, u32 h, f32 minus = 0.0f) {
    f64 e = 0.0;
    for (u32 y = 0; y < h; ++y) {
        const f64 sa = (2.0 * 3.14159265358979 / w) * (3.14159265358979 / h) * std::sin((y + 0.5) * 3.14159265358979 / h);
        for (u32 x = 0; x < w; ++x) e += (px[(static_cast<usize>(y) * w + x) * 3] - minus) * sa;
    }
    return e;
}

/// Maior razão texel / média dos 4 vizinhos (dentro de cada face): um
/// vaga-lume é um texel isolado muito acima dos vizinhos.
f32 firefly_ratio(const CubeData& c, u32 level) {
    const u32 s = std::max(1u, c.size >> level);
    auto at = [&](u32 f, u32 x, u32 y) { return half_to_float(c.levels[level][((static_cast<usize>(f) * s + y) * s + x) * 4]); };
    f32 worst = 1.0f;
    for (u32 f = 0; f < 6; ++f) {
        for (u32 y = 1; y + 1 < s; ++y) {
            for (u32 x = 1; x + 1 < s; ++x) {
                const f32 n = 0.25f * (at(f, x - 1, y) + at(f, x + 1, y) + at(f, x, y - 1) + at(f, x, y + 1));
                if (n > 1e-4f) worst = std::max(worst, at(f, x, y) / n);
            }
        }
    }
    return worst;
}
} // namespace

AUREA_TEST(Scene3D, EnvironmentConstantRadianceIsPreservedEverywhere) {
    // Radiância constante → cubo, todos os mips do especular, o fundo e a
    // irradiância (radiância difusa = E/π) têm de devolver a mesma constante.
    const std::vector<f32> eq = synthetic_equirect(1024, 512, 0.7f, 0.0f, 0, 0, 0);
    const EnvironmentQuality q{128u, 256u, 0u};
    const EnvironmentMaps m = build_environment_from_equirect(eq.data(), 1024, 512, q);
    AUREA_CHECK_EQ(m.prefiltered.size, 128u);
    AUREA_CHECK_EQ(m.background.size, 256u);
    AUREA_CHECK_EQ(m.background.mips, 9u);   // 256..1
    f32 worst = 0.0f;
    auto scan = [&](const CubeData& c) {
        for (u32 l = 0; l < c.mips; ++l) {
            for (usize i = 0; i < c.levels[l].size(); i += 4) {
                for (usize k = 0; k < 3; ++k) worst = std::max(worst, std::fabs(half_to_float(c.levels[l][i + k]) - 0.7f));
            }
        }
    };
    scan(m.prefiltered);
    scan(m.background);
    scan(m.irradiance);
    std::printf("\n    (radiancia constante 0,7: maior erro %.5f)", static_cast<double>(worst));
    AUREA_CHECK(worst < 0.7f * 0.01f);
    // O estúdio com fundo pedido também gera a cadeia inteira.
    const EnvironmentMaps st = build_studio_environment(EnvironmentQuality{64u, 128u, 0u});
    AUREA_CHECK_EQ(st.background.size, 128u);
    AUREA_CHECK_EQ(st.background.mips, 8u);
    AUREA_CHECK_EQ(st.prefiltered.size, 64u);
    AUREA_CHECK_EQ(build_studio_environment(EnvironmentQuality{64u, 0u, 0u}).background.size, 0u);
}

AUREA_TEST(Scene3D, EnvironmentTinySunKeepsItsEnergyWithoutFireflies) {
    // Um sol de raio 3 texels (5000) num 4K: a conversão antiga (uma amostra
    // bilinear por texel de 128²) o perdia ou o fazia piscar com o giro.
    const u32 W = 4096, H = 2048;
    const f32 base = 0.02f;
    const std::vector<f32> eq = synthetic_equirect(W, H, base, 5000.0f, 1000, 700, 3);
    const f64 sunEq = equirect_energy(eq, W, H, base);
    const EnvironmentMaps m = build_environment_from_equirect(eq.data(), W, H, EnvironmentQuality::final_quality());
    AUREA_CHECK_EQ(m.prefiltered.size, 512u);
    AUREA_CHECK_EQ(m.background.size, 1024u);
    const f64 sunBg = cube_energy(m.background, 0, base), sunSpec0 = cube_energy(m.prefiltered, 0, base);
    std::printf("\n    (energia do sol: equiret %.4f, fundo 1024 %.4f, especular mip0 %.4f)", sunEq, sunBg, sunSpec0);
    AUREA_CHECK_NEAR(sunBg / sunEq, 1.0, 0.05);
    AUREA_CHECK_NEAR(sunSpec0 / sunEq, 1.0, 0.05);
    // Mips ásperos: energia preservada (±5%: lóbulo normalizado pela soma de
    // N·L e mips pesados pelo ângulo sólido) e nenhum vaga-lume isolado.
    for (u32 l = 1; l < m.prefiltered.mips; ++l) {
        const f64 e = cube_energy(m.prefiltered, l, base);
        const f32 ff = firefly_ratio(m.prefiltered, l);
        std::printf("\n    (mip %u, rugosidade %.2f: energia %.3f do original, vaga-lume %.2f)", l,
                    static_cast<double>(specular_roughness_at(static_cast<f32>(l), m.prefiltered.mips)), e / sunEq,
                    static_cast<double>(ff));
        AUREA_CHECK_NEAR(e / sunEq, 1.0, 0.05);
        if (l >= 3) AUREA_CHECK(ff < 2.0f);
    }
    // Deslocar o sol um texel não pode fazer o reflexo "piscar": a energia
    // no mip 0 do especular quase não muda.
    const std::vector<f32> eq2 = synthetic_equirect(W, H, base, 5000.0f, 1001, 700, 3);
    const EnvironmentMaps m1 = build_environment_from_equirect(eq.data(), W, H, EnvironmentQuality{256u, 0u, 0u});
    const EnvironmentMaps m2 = build_environment_from_equirect(eq2.data(), W, H, EnvironmentQuality{256u, 0u, 0u});
    const f64 a = cube_energy(m1.prefiltered, 0, base), b = cube_energy(m2.prefiltered, 0, base);
    std::printf("\n    (sol deslocado 1 texel: %.4f -> %.4f)", a, b);
    AUREA_CHECK_NEAR(a / b, 1.0, 0.03);
}

AUREA_TEST(Scene3D, EnvironmentBackgroundLodGrowsWithBlur) {
    const f32 fov = 45.0f * 3.14159265f / 180.0f;
    const BackgroundSampling sharp = background_sampling(0.0f, fov, 1080, 1024, 8);
    AUREA_CHECK_EQ(sharp.gradScale, 1.0f);
    AUREA_CHECK_EQ(sharp.specBlend, 0.0f);
    BackgroundSampling prev = sharp;
    for (int i = 1; i <= 20; ++i) {
        const BackgroundSampling s = background_sampling(static_cast<f32>(i) / 20.0f, fov, 1080, 1024, 8);
        AUREA_CHECK(s.gradScale >= prev.gradScale);
        AUREA_CHECK(s.specLod >= prev.specLod);
        AUREA_CHECK(s.specBlend >= prev.specBlend);
        prev = s;
    }
    AUREA_CHECK(prev.gradScale > 64.0f);
    AUREA_CHECK_NEAR(prev.specLod, 7.0f, 1e-4f);   // desfoque 1 = o mip mais áspero
    AUREA_CHECK_EQ(prev.specBlend, 1.0f);
    // NaN / fora da faixa não quebram o céu.
    AUREA_CHECK_EQ(background_sampling(std::numeric_limits<f32>::quiet_NaN(), fov, 1080, 1024, 8).gradScale, 1.0f);
    // A curva do LOD especular e a inversa (a da geração) casam.
    for (u32 l = 0; l < 8; ++l) {
        AUREA_CHECK_NEAR(specular_lod(specular_roughness_at(static_cast<f32>(l), 8), 8), static_cast<f32>(l), 1e-3f);
    }
    AUREA_CHECK_NEAR(specular_lod(1.0f, 8), 7.0f, 1e-5f);
    AUREA_CHECK_EQ(specular_lod(0.0f, 8), 0.0f);
}

AUREA_TEST(Scene3D, EnvironmentLdrPanoramaIsLinearizedAsSrgb) {
    const test::Image8 img{3, 1, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255}};
    const std::string path = "env_ldr_srgb.png";
    std::vector<u8> bytes;
    AUREA_CHECK(test::write_png(path, img));
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        bytes.resize(static_cast<usize>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        bytes.resize(std::fread(bytes.data(), 1, bytes.size(), f));
        std::fclose(f);
    }
    const auto px = decode_hdri(bytes.data(), bytes.size());
    AUREA_CHECK(px && px->ldr && px->width == 3);
    if (!px) return;
    AUREA_CHECK_NEAR(px->rgb[0], 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(px->rgb[3], 0.21586f, 1e-4f);   // sRGB 128 → linear (a gama 2,2 dava 0,2195)
    AUREA_CHECK_NEAR(px->rgb[6], 1.0f, 1e-6f);
    const auto boosted = decode_hdri(bytes.data(), bytes.size(), 4.0f);
    AUREA_CHECK(boosted && std::fabs(boosted->rgb[6] - 4.0f) < 1e-5f);
}

AUREA_TEST(Scene3D, EnvironmentHdriBuildTimes) {
    const std::vector<f32> eq = synthetic_equirect(4096, 2048, 0.5f, 2000.0f, 1000, 700, 3);
    struct Tier { const char* name; EnvironmentQuality q; };
    const Tier tiers[] = {{"preview sem fundo, 2 threads", EnvironmentQuality{256u, 0u, 2u}},
                          {"preview (2 threads)", EnvironmentQuality::preview()},
                          {"final sem fundo", EnvironmentQuality{512u, 0u, 0u}},
                          {"final", EnvironmentQuality::final_quality()},
                          {"final 1 thread", EnvironmentQuality{512u, 1024u, 1u}}};
    for (const Tier& t : tiers) {
        const auto t0 = std::chrono::steady_clock::now();
        const EnvironmentMaps m = build_environment_from_equirect(eq.data(), 4096, 2048, t.q);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        usize bytes = m.irradiance.levels[0].size() * 2 + m.brdfLut.size() * 2;
        for (const auto& l : m.prefiltered.levels) bytes += l.size() * 2;
        for (const auto& l : m.background.levels) bytes += l.size() * 2;
        std::printf("\n    (hdri 4096x2048, %s: %.0f ms, especular %u/%u mips, fundo %u/%u mips, %.1f MB)", t.name, ms,
                    m.prefiltered.size, m.prefiltered.mips, m.background.size, m.background.mips,
                    static_cast<double>(bytes) / 1048576.0);
    }
    for (const EnvironmentQuality q : {EnvironmentQuality{256u, 0u, 0u}, EnvironmentQuality{512u, 0u, 0u}}) {
        const auto t0 = std::chrono::steady_clock::now();
        (void)build_studio_environment(q);
        std::printf("\n    (estudio %u: %.0f ms)", q.specularSize,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
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
// Texturas escolhidas junto com o modelo (FBX/OBJ)
// -----------------------------------------------------------------------------
#include "ModelTextureFixtures.hpp"

AUREA_TEST(Scene3D, UfbxTexturesFromPickedFilesResolveByName) {
    namespace fx = aurea::test_fixtures;
    ImportOptions o;
    // OBJ: sem .mtl na pasta, o material inteiro falta — e a UI sabe o nome.
    {
        const std::string folder = fx::fresh_model_folder("aurea_teste_texturas_obj");
        const std::string obj = fx::write_textured_obj(folder);
        ImportResult r = import_scene_file(obj, o);
        AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
        if (r.ok()) {
            AUREA_CHECK_EQ(r.asset->missingTextures.size(), 1u);
            if (!r.asset->missingTextures.empty()) AUREA_CHECK(r.asset->missingTextures[0] == "Original.mtl");
        }
        // O .mtl escolhido (pasta gravada "Materiais/" ignorada), sem a imagem.
        fx::write_picked_mtl(folder, "Tijolo.png");
        r = import_scene_file(obj, o);
        AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
        if (r.ok()) {
            AUREA_CHECK(!r.asset->materials.empty() && !r.asset->materials[0].baseColorTex.valid());
            AUREA_CHECK_EQ(r.asset->missingTextures.size(), 1u);
            if (!r.asset->missingTextures.empty()) AUREA_CHECK(r.asset->missingTextures[0] == "Tijolo.png");
        }
        // A imagem escolhida depois: resolve pelo nome (sem "C:\Artista\texturas\").
        AUREA_CHECK(fx::write_solid_png(folder + "Tijolo.png", 0, 255, 0));
        r = import_scene_file(obj, o);
        AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
        if (r.ok()) {
            AUREA_CHECK(r.asset->missingTextures.empty());
            AUREA_CHECK_EQ(r.asset->images.size(), 1u);
            AUREA_CHECK(!r.asset->materials.empty() && r.asset->materials[0].baseColorTex.valid());
        }
    }
    // FBX com textura externa em "textures\Tijolo.png": a imagem escolhida
    // fica ao lado do modelo.
    {
        const std::string folder = fx::fresh_model_folder("aurea_teste_texturas_fbx");
        const std::string fbx = fx::write_textured_fbx(folder, "Tijolo.png");
        ImportResult r = import_scene_file(fbx, o);
        AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
        if (r.ok()) {
            AUREA_CHECK(!r.asset->materials.empty() && !r.asset->materials[0].baseColorTex.valid());
            AUREA_CHECK_EQ(r.asset->missingTextures.size(), 1u);
            if (!r.asset->missingTextures.empty()) AUREA_CHECK(r.asset->missingTextures[0] == "Tijolo.png");
        }
        AUREA_CHECK(fx::write_solid_png(folder + "Tijolo.png", 0, 255, 0));
        r = import_scene_file(fbx, o);
        AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
        if (r.ok()) {
            AUREA_CHECK(r.asset->missingTextures.empty());
            AUREA_CHECK_EQ(r.asset->images.size(), 1u);
            AUREA_CHECK(!r.asset->materials.empty() && r.asset->materials[0].baseColorTex.valid());
        }
    }
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

// Estúdios procedurais (v32): valores finitos, energia plausível por preset e
// a faixa dinâmica que desenha o reflexo (faixas >> fundo no escuro; sol HDR).
AUREA_TEST(Scene3D, StudioEnvironmentsHaveFiniteEnergy) {
    AUREA_CHECK(studio_preset_from_name("estudio_escuro") == 1u);
    AUREA_CHECK(studio_preset_from_name("estudio_produto") == 2u);
    AUREA_CHECK(studio_preset_from_name("ceu_sol") == 3u);
    AUREA_CHECK(studio_preset_from_name("nada") == 0u);
    AUREA_CHECK(studio_preset_of_key(studio_environment_key(2)) == 2u);
    AUREA_CHECK(studio_preset_of_key(0x0000000100000001ull) == 0u);
    AUREA_CHECK(!generate_studio_hdri(0) && !generate_studio_hdri(9));
    for (u32 preset = 1; preset < kStudioPresetCount; ++preset) {
        const auto px = generate_studio_hdri(preset, 512);
        AUREA_CHECK(px && px->width == 512 && px->height == 256);
        if (!px) continue;
        bool finite = true;
        f64 sum = 0.0, wsum = 0.0, upper = 0.0, upperW = 0.0;
        f32 peak = 0.0f;
        std::vector<f32> lum;
        lum.reserve(static_cast<usize>(px->width) * px->height);
        for (u32 y = 0; y < px->height; ++y) {
            const f64 w = std::sin((y + 0.5) / px->height * 3.14159265358979);   // área do texel
            for (u32 x = 0; x < px->width; ++x) {
                const f32* c = &px->rgb[(static_cast<usize>(y) * px->width + x) * 3];
                for (int k = 0; k < 3; ++k) finite = finite && std::isfinite(c[k]) && c[k] >= 0.0f;
                const f32 l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                sum += l * w;
                wsum += w;
                if (y < px->height / 2) { upper += l * w; upperW += w; }
                peak = std::max(peak, l);
                lum.push_back(l);
            }
        }
        std::nth_element(lum.begin(), lum.begin() + lum.size() / 2, lum.end());
        const f32 median = lum[lum.size() / 2];
        const f64 mean = sum / wsum;
        std::printf("\n    %s: media %.3f mediana %.4f pico %.1f", studio_preset_name(preset), mean, median, peak);
        AUREA_CHECK_MSG(finite, studio_preset_name(preset));
        AUREA_CHECK(mean > 0.05 && mean < 5.0);
        if (preset == 1) {
            // Escuro: fundo quase preto, faixas várias ordens acima.
            AUREA_CHECK(median < 0.03f);
            AUREA_CHECK(peak > 8.0f && peak > 300.0f * median);
        } else if (preset == 2) {
            // Produto: ciclorama cinza médio visível, luz macia (sem pico duro).
            AUREA_CHECK(median > 0.3f && median < 0.9f);
            AUREA_CHECK(peak < 20.0f);
        } else {
            // Céu: hemisfério de cima azul-claro; o sol, HDR de verdade.
            AUREA_CHECK(upper / upperW > 0.3);
            AUREA_CHECK(peak > 1000.0f && peak < 65000.0f);   // cabe em fp16
        }
        // O mesmo caminho de um HDRI importado: mapas finitos.
        const EnvironmentMaps maps = build_environment_from_equirect(px->rgb.data(), px->width, px->height,
                                                                     EnvironmentQuality{64u, 0u, 0u});
        AUREA_CHECK(maps.irradiance.size > 0 && !maps.irradiance.levels.empty());
        bool mapsFinite = !maps.irradiance.levels.empty();
        for (u16 h : maps.irradiance.levels[0]) mapsFinite = mapsFinite && std::isfinite(half_to_float(h));
        AUREA_CHECK_MSG(mapsFinite, studio_preset_name(preset));
    }
    // Determinístico e compartilhado.
    AUREA_CHECK(studio_hdri(1) && studio_hdri(1).get() == studio_hdri(1).get());
}

// -----------------------------------------------------------------------------
// FBX com personagem: textura EMBUTIDA, vários takes e esqueleto com herança
// de escala do Maya ("Segment Scale Compensate") — o caso que deixava braço e
// perna esticados.
// -----------------------------------------------------------------------------
#include "ufbx.h"

namespace {

std::string base64(const std::vector<u8>& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (usize i = 0; i < in.size(); i += 3) {
        const u32 n = (u32{in[i]} << 16) | (i + 1 < in.size() ? u32{in[i + 1]} << 8 : 0u)
                    | (i + 2 < in.size() ? u32{in[i + 2]} : 0u);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < in.size() ? k[(n >> 6) & 63] : '=';
        out += i + 2 < in.size() ? k[n & 63] : '=';
    }
    return out;
}

/// Curva FBX linear de 2 chaves (tempos em KTime).
std::string fbx_curve(int id, const char* name, const char* t1, f32 v0, f32 v1) {
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "\tAnimationCurve: %d, \"AnimCurve::%s\", \"\" {\n\t\tDefault: %g\n\t\tKeyVer: 4008\n"
                  "\t\tKeyTime: *2 {\n\t\t\ta: 0,%s\n\t\t}\n\t\tKeyValueFloat: *2 {\n\t\t\ta: %g,%g\n\t\t}\n"
                  "\t\tKeyAttrFlags: *1 {\n\t\t\ta: 24836\n\t\t}\n\t\tKeyAttrDataFloat: *4 {\n\t\t\ta: 0,0,0,0\n\t\t}\n"
                  "\t\tKeyAttrRefCount: *1 {\n\t\t\ta: 2\n\t\t}\n\t}\n",
                  id, name, static_cast<double>(v0), t1, static_cast<double>(v0), static_cast<double>(v1));
    return buf;
}

std::string fbx_curve_node(int id, const char* name, f32 def) {
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "\tAnimationCurveNode: %d, \"AnimCurveNode::%s\", \"\" {\n\t\tProperties70:  {\n"
                  "\t\t\tP: \"d|X\", \"Number\", \"\", \"A\",%g\n\t\t\tP: \"d|Y\", \"Number\", \"\", \"A\",%g\n"
                  "\t\t\tP: \"d|Z\", \"Number\", \"\", \"A\",%g\n\t\t}\n\t}\n",
                  id, name, static_cast<double>(def), static_cast<double>(def), static_cast<double>(def));
    return buf;
}

/// FBX 7.4 ASCII em centímetros: faixa de 5 vértices com skin em dois ossos.
///  - "Quadril" (escala 2) e "Joelho" filho com InheritType 2 (Rrs: ignora a
///    escala do pai — o Segment Scale Compensate do Maya).
///  - vértices 0,1 no Quadril, 2,3 no Joelho, 4 SEM PESO (fica no nó da malha).
///  - nó da malha deslocado (0,0,1); bind = repouso (Transform/TransformLink
///    dos clusters coerentes com a hierarquia avaliada do jeito do FBX).
///  - textura difusa só embutida (Video/Content, PNG vermelho); o caminho
///    gravado é de outra máquina e não existe. Cor difusa gravada: PRETA.
///  - take "Andar" (0–1 s): escala X do Quadril 2→3 (animada e não uniforme)
///    e rotação Z do Joelho 0→90°. Take "Pular" sem intervalo gravado
///    (LocalStart = LocalStop) e chaves de 0 a 0,5 s na translação Y.
std::string write_skinned_fbx(const std::string& folder) {
    aurea::test::Image8 red;
    red.width = red.height = 8;
    red.rgba.resize(8 * 8 * 4);
    for (usize i = 0; i < red.rgba.size(); i += 4) {
        red.rgba[i] = 255; red.rgba[i + 1] = 0; red.rgba[i + 2] = 0; red.rgba[i + 3] = 255;
    }
    const std::string tmp = folder + "tmp_embed.png";
    aurea::test::write_png(tmp, red);
    std::vector<u8> png;
    if (std::FILE* f = std::fopen(tmp.c_str(), "rb")) {
        u8 buf[4096];
        for (usize n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) png.insert(png.end(), buf, buf + n);
        std::fclose(f);
    }
    std::remove(tmp.c_str());   // só o conteúdo embutido existe

    const std::string ident = "1,0,0,0,0,1,0,0,0,0,1,0,";
    // "Transform" do cluster no arquivo = inversa(TransformLink) × malha na
    // pose de bind (malha em (0,0,1)).
    const char* oneSec = "46186158000";
    const char* halfSec = "23093079000";
    std::string fbx =
        "; FBX 7.4.0 project file\n"
        "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
        "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
        "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n"
        "\t}\n}\n"
        "Objects:  {\n"
        "\tGeometry: 1000, \"Geometry::Corpo\", \"Mesh\" {\n"
        "\t\tVertices: *15 {\n\t\t\ta: -0.5,0,0,0.5,0,0,0.5,2,0,-0.5,2,0,0,3,0\n\t\t}\n"
        "\t\tPolygonVertexIndex: *7 {\n\t\t\ta: 0,1,2,-4,3,2,-5\n\t\t}\n"
        "\t\tGeometryVersion: 124\n"
        "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n"
        "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
        "\t\t\tUV: *10 {\n\t\t\t\ta: 0,0,1,0,1,0.66,0,0.66,0.5,1\n\t\t\t}\n"
        "\t\t\tUVIndex: *7 {\n\t\t\t\ta: 0,1,2,3,3,2,4\n\t\t\t}\n\t\t}\n"
        "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
        "\t\t\tMappingInformationType: \"AllSame\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
        "\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n"
        "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
        "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
        "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
        "\t\t}\n"
        "\t}\n"
        "\tModel: 2000, \"Model::Corpo\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n"
        "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,1\n\t\t}\n\t}\n"
        "\tModel: 2100, \"Model::Quadril\", \"LimbNode\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n"
        "\t\t\tP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",2,2,2\n\t\t}\n\t}\n"
        "\tModel: 2200, \"Model::Joelho\", \"LimbNode\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n"
        "\t\t\tP: \"InheritType\", \"enum\", \"\", \"\",2\n"
        "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0.5,0\n\t\t}\n\t}\n"
        "\tDeformer: 5000, \"Deformer::Pele\", \"Skin\" {\n\t\tVersion: 101\n\t\tLink_DeformAcuracy: 50\n\t}\n"
        "\tDeformer: 5100, \"SubDeformer::Quadril\", \"Cluster\" {\n\t\tVersion: 100\n\t\tUserData: \"\", \"\"\n"
        "\t\tIndexes: *2 {\n\t\t\ta: 0,1\n\t\t}\n\t\tWeights: *2 {\n\t\t\ta: 1,1\n\t\t}\n"
        "\t\tTransform: *16 {\n\t\t\ta: 0.5,0,0,0,0,0.5,0,0,0,0,0.5,0,0,0,0.5,1\n\t\t}\n"
        "\t\tTransformLink: *16 {\n\t\t\ta: 2,0,0,0,0,2,0,0,0,0,2,0,0,0,0,1\n\t\t}\n\t}\n"
        "\tDeformer: 5200, \"SubDeformer::Joelho\", \"Cluster\" {\n\t\tVersion: 100\n\t\tUserData: \"\", \"\"\n"
        "\t\tIndexes: *2 {\n\t\t\ta: 2,3\n\t\t}\n\t\tWeights: *2 {\n\t\t\ta: 1,1\n\t\t}\n"
        "\t\tTransform: *16 {\n\t\t\ta: " + ident + "0,-1,1,1\n\t\t}\n"
        "\t\tTransformLink: *16 {\n\t\t\ta: " + ident + "0,1,0,1\n\t\t}\n\t}\n"
        "\tMaterial: 3000, \"Material::Pele\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n"
        "\t\tProperties70:  {\n\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0,0,0\n\t\t}\n\t}\n"
        "\tTexture: 4000, \"Texture::Pele\", \"\" {\n\t\tType: \"TextureVideoClip\"\n\t\tVersion: 202\n"
        "\t\tTextureName: \"Texture::Pele\"\n"
        "\t\tFileName: \"C:/Artista/nao_existe/Pele.png\"\n\t\tRelativeFilename: \"texturas/Pele.png\"\n\t}\n"
        "\tVideo: 4100, \"Video::Pele\", \"Clip\" {\n\t\tType: \"Clip\"\n"
        "\t\tFileName: \"C:/Artista/nao_existe/Pele.png\"\n\t\tRelativeFilename: \"texturas/Pele.png\"\n"
        "\t\tContent: , \"" + base64(png) + "\"\n\t}\n"
        // Take 1: "Andar".
        "\tAnimationStack: 6000, \"AnimStack::Andar\", \"\" {\n\t\tProperties70:  {\n"
        "\t\t\tP: \"LocalStart\", \"KTime\", \"Time\", \"\",0\n\t\t\tP: \"LocalStop\", \"KTime\", \"Time\", \"\"," + oneSec + "\n"
        "\t\t\tP: \"ReferenceStart\", \"KTime\", \"Time\", \"\",0\n\t\t\tP: \"ReferenceStop\", \"KTime\", \"Time\", \"\"," + oneSec + "\n"
        "\t\t}\n\t}\n"
        "\tAnimationLayer: 6010, \"AnimLayer::Base\", \"\" {\n\t}\n"
        + fbx_curve_node(6020, "S", 2) + fbx_curve(6030, "SX", oneSec, 2, 3)
        + fbx_curve_node(6040, "R", 0) + fbx_curve(6050, "RZ", oneSec, 0, 90) +
        // Take 2: "Pular" (sem intervalo gravado).
        "\tAnimationStack: 7000, \"AnimStack::Pular\", \"\" {\n\t\tProperties70:  {\n"
        "\t\t\tP: \"LocalStart\", \"KTime\", \"Time\", \"\",0\n\t\t\tP: \"LocalStop\", \"KTime\", \"Time\", \"\",0\n"
        "\t\t}\n\t}\n"
        "\tAnimationLayer: 7010, \"AnimLayer::Base\", \"\" {\n\t}\n"
        + fbx_curve_node(7020, "T", 0) + fbx_curve(7030, "TY", halfSec, 0, 10) +
        "}\n"
        "Connections:  {\n"
        "\tC: \"OO\",2000,0\n\tC: \"OO\",2100,0\n\tC: \"OO\",2200,2100\n"
        "\tC: \"OO\",1000,2000\n\tC: \"OO\",3000,2000\n"
        "\tC: \"OP\",4000,3000, \"DiffuseColor\"\n\tC: \"OO\",4100,4000\n"
        "\tC: \"OO\",5000,1000\n\tC: \"OO\",5100,5000\n\tC: \"OO\",5200,5000\n"
        "\tC: \"OO\",2100,5100\n\tC: \"OO\",2200,5200\n"
        "\tC: \"OO\",6010,6000\n\tC: \"OO\",6020,6010\n\tC: \"OO\",6040,6010\n"
        "\tC: \"OP\",6020,2100, \"Lcl Scaling\"\n\tC: \"OP\",6030,6020, \"d|X\"\n"
        "\tC: \"OP\",6040,2200, \"Lcl Rotation\"\n\tC: \"OP\",6050,6040, \"d|Z\"\n"
        "\tC: \"OO\",7010,7000\n\tC: \"OO\",7020,7010\n"
        "\tC: \"OP\",7020,2100, \"Lcl Translation\"\n\tC: \"OP\",7030,7020, \"d|Y\"\n"
        "}\n";
    const std::string path = folder + "personagem.fbx";
    aurea::test_fixtures::write_text(path, fbx.c_str());
    return path;
}

using SkinnedList = std::vector<std::pair<Vec3, Vec3>>;   // (posição na geometria, no mundo)

/// Posição com skin de cada vértice das primitivas (a conta do shader).
SkinnedList skinned_vertices(const SceneAsset& a, const Pose& pose) {
    SkinnedList out;
    for (usize n = 0; n < a.nodes.size(); ++n) {
        const i32 mi = a.nodes[n].mesh, si = a.nodes[n].skin;
        if (mi < 0 || si < 0) continue;
        const u32 off = pose.skinJointOffset[static_cast<usize>(si)];
        for (const Primitive& p : a.meshes[static_cast<usize>(mi)].primitives) {
            for (usize v = 0; v < p.positions.size(); ++v) {
                Vec3 w{0, 0, 0};
                for (usize k = 0; k < 4; ++k) {
                    const f32 wt = (&p.weights[v].x)[k];
                    if (wt == 0.0f) continue;
                    w = w + pose.jointMatrices[off + p.joints[v * 4 + k]].transform_point(p.positions[v]) * wt;
                }
                out.push_back({p.positions[v], w});
            }
        }
    }
    return out;
}

/// Referência: a própria ufbx avalia a cena no instante `t` (herança de
/// escala do jeito do FBX, direto nas matrizes, SEM compensação) e aplica o
/// skin com as matrizes dela. `stack` fora da lista = pose de repouso.
SkinnedList ufbx_reference(const std::string& path, usize stack, f64 t) {
    SkinnedList out;
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, nullptr);
    if (!scene) return out;
    ufbx_scene* ev = stack < scene->anim_stacks.count
                   ? ufbx_evaluate_scene(scene, scene->anim_stacks.data[stack]->anim, t, nullptr, nullptr) : nullptr;
    const ufbx_scene* s = ev ? ev : scene;
    for (usize mi = 0; mi < s->meshes.count; ++mi) {
        const ufbx_mesh* m = s->meshes.data[mi];
        const ufbx_skin_deformer* sk = m->skin_deformers.count ? m->skin_deformers.data[0] : nullptr;
        const ufbx_node* node = m->instances.count ? m->instances.data[0] : nullptr;
        for (usize v = 0; v < m->num_vertices; ++v) {
            const ufbx_vec3 p = m->vertices.data[v];
            ufbx_vec3 w{0, 0, 0};
            f64 total = 0.0;
            if (sk && v < sk->vertices.count) {
                const ufbx_skin_vertex sv = sk->vertices.data[v];
                for (u32 k = 0; k < sv.num_weights; ++k) {
                    const ufbx_skin_weight sw = sk->weights.data[sv.weight_begin + k];
                    const ufbx_vec3 q = ufbx_transform_position(&sk->clusters.data[sw.cluster_index]->geometry_to_world, p);
                    w.x += q.x * sw.weight; w.y += q.y * sw.weight; w.z += q.z * sw.weight;
                    total += sw.weight;
                }
            }
            if (total > 0.0) { w.x /= total; w.y /= total; w.z /= total; }
            else if (node) w = ufbx_transform_position(&node->geometry_to_world, p);
            out.push_back({Vec3{static_cast<f32>(p.x), static_cast<f32>(p.y), static_cast<f32>(p.z)},
                           Vec3{static_cast<f32>(w.x), static_cast<f32>(w.y), static_cast<f32>(w.z)}});
        }
    }
    if (ev) ufbx_free_scene(ev);
    ufbx_free_scene(scene);
    return out;
}

f32 dist3(Vec3 a, Vec3 b) {
    const Vec3 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

/// Cada vértice nosso bate com o da referência de mesma posição na geometria.
bool matches(const SkinnedList& ours, const SkinnedList& ref, f32 tol, f32* worst) {
    *worst = 0.0f;
    if (ours.empty() || ref.empty()) return false;
    for (const auto& [g, w] : ours) {
        const std::pair<Vec3, Vec3>* hit = nullptr;
        for (const auto& r : ref) if (dist3(r.first, g) < 1e-4f) hit = &r;
        if (!hit) return false;
        *worst = std::max(*worst, dist3(hit->second, w));
    }
    return *worst <= tol;
}

} // namespace

AUREA_TEST(Scene3D, FbxEmbeddedTextureDecodedFromMemory) {
    const std::string folder = aurea::test_fixtures::fresh_model_folder("aurea_teste_fbx_personagem");
    ImportResult r = import_scene_file(write_skinned_fbx(folder), ImportOptions{});
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK(a.missingTextures.empty());   // o caminho gravado não existe, mas o conteúdo veio junto
    AUREA_CHECK_EQ(a.images.size(), usize{1});
    if (!a.images.empty()) {
        AUREA_CHECK_EQ(a.images[0].width, 8u);
        AUREA_CHECK_EQ(a.images[0].rgba[0], u8{255});
        AUREA_CHECK_EQ(a.images[0].rgba[1], u8{0});
    }
    AUREA_CHECK(!a.materials.empty() && a.materials[0].baseColorTex.valid());
    // Cor difusa gravada preta + textura: a textura manda (antes: modelo preto).
    if (!a.materials.empty()) AUREA_CHECK_NEAR(a.materials[0].baseColor.x, 1.0f, 1e-6f);
}

AUREA_TEST(Scene3D, FbxAllTakesImportedAsClips) {
    const std::string folder = aurea::test_fixtures::fresh_model_folder("aurea_teste_fbx_personagem");
    ImportResult r = import_scene_file(write_skinned_fbx(folder), ImportOptions{});
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.animations.size(), usize{2});
    if (a.animations.size() != 2) return;
    AUREA_CHECK(a.animations[0].name == "Andar");
    AUREA_CHECK_NEAR(a.animations[0].duration, 1.0f, 1e-4f);
    // Take sem intervalo gravado: a duração vem das chaves (antes: descartado).
    AUREA_CHECK(a.animations[1].name == "Pular");
    AUREA_CHECK_NEAR(a.animations[1].duration, 0.5f, 1e-4f);
}

AUREA_TEST(Scene3D, FbxSkinBindPoseMatchesUnskinnedAndUfbx) {
    const std::string folder = aurea::test_fixtures::fresh_model_folder("aurea_teste_fbx_personagem");
    const std::string path = write_skinned_fbx(folder);
    ImportResult r = import_scene_file(path, ImportOptions{});
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.skins.size(), usize{1});
    // 2 ossos + a junta rígida do nó da malha (vértice 4 sem peso).
    if (!a.skins.empty()) AUREA_CHECK_EQ(a.skins[0].joints.size(), usize{3});
    Pose pose;
    evaluate_pose(a, -1, 0.0f, pose);
    const SkinnedList bind = skinned_vertices(a, pose);
    AUREA_CHECK_EQ(bind.size(), usize{5});
    // Pose de bind = pose de repouso: cada vértice com skin fica exatamente
    // onde a malha sem skin fica (nó da malha em (0,0,1) cm → metros).
    f32 worst = 0.0f;
    for (const auto& [g, w] : bind) worst = std::max(worst, dist3(w, (g + Vec3{0, 0, 1}) * 0.01f));
    std::printf("\n    bind: maior desvio %.3g m\n", static_cast<double>(worst));
    AUREA_CHECK(worst < 1e-6f);
    AUREA_CHECK_MSG(matches(bind, ufbx_reference(path, SIZE_MAX, 0.0), 1e-6f, &worst), "bind difere da ufbx");
    // Animado (escala do pai animada e não uniforme + rotação no osso que
    // ignora a escala do pai): o nosso T·R·S encadeado bate com a ufbx.
    for (f32 t : {0.25f, 0.5f, 1.0f}) {
        evaluate_pose(a, 0, t, pose);
        const bool ok = matches(skinned_vertices(a, pose), ufbx_reference(path, 0, t), 1e-5f, &worst);
        std::printf("    Andar t=%.2f: maior desvio %.3g m\n", static_cast<double>(t), static_cast<double>(worst));
        AUREA_CHECK_MSG(ok, "Andar difere da ufbx");
    }
    evaluate_pose(a, 1, 0.25f, pose);
    const SkinnedList jump = skinned_vertices(a, pose);
    AUREA_CHECK_MSG(matches(jump, ufbx_reference(path, 1, 0.25), 1e-5f, &worst), "Pular difere da ufbx");
    // O Pular sobe mesmo: 5 cm na metade (vértices do Quadril, y = 0).
    for (const auto& [g, w] : jump)
        if (g.y < 1e-4f) AUREA_CHECK_NEAR(w.y, 0.05f, 1e-5f);
}

// -----------------------------------------------------------------------------
// Modelos do Sketchfab: especular-brilho, WebP, textura ausente, extensões
// obrigatórias desconhecidas (o modelo entra; nunca "falhou" por um mapa).
// -----------------------------------------------------------------------------
namespace {

/// PNG sólido w×h em data URI (base64), para glTF sintético sem arquivos.
std::string solid_png_uri(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
    aurea::test::Image8 img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r; img.rgba[i + 1] = g; img.rgba[i + 2] = b; img.rgba[i + 3] = a;
    }
    const std::string tmp = "aurea_teste_png_uri.png";
    aurea::test::write_png(tmp, img);
    std::vector<u8> png;
    if (std::FILE* f = std::fopen(tmp.c_str(), "rb")) {
        u8 buf[4096];
        for (usize n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) png.insert(png.end(), buf, buf + n);
        std::fclose(f);
    }
    std::remove(tmp.c_str());
    return "data:image/png;base64," + base64(png);
}

const char* kTriangleGeometry =
    R"("buffers":[{"byteLength":60,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"}],
"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
             {"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],)";

ImportResult import_json(const std::string& json) {
    ImportOptions o;
    return import_gltf_memory(reinterpret_cast<const u8*>(json.data()), json.size(), "", o);
}

} // namespace

AUREA_TEST(Scene3D, SketchfabSpecGlossConvertsToMetalRough) {
    // Material 0: difusa quase preta + especular dourado com brilho total por
    // textura (metal polido). Material 1: só fatores, dielétrico vermelho fosco.
    const std::string json = std::string(R"({"asset":{"version":"2.0"},
"extensionsUsed":["KHR_materials_pbrSpecularGlossiness","EXT_extensao_inventada"],
"extensionsRequired":["KHR_materials_pbrSpecularGlossiness","EXT_extensao_inventada"],)") + kTriangleGeometry +
R"("images":[{"uri":")" + solid_png_uri(4, 4, 10, 10, 10, 255) + R"("},{"uri":")" + solid_png_uri(4, 4, 255, 200, 80, 255) + R"("}],
"textures":[{"source":0},{"source":1}],
"materials":[{"name":"ouro","extensions":{"KHR_materials_pbrSpecularGlossiness":{"diffuseTexture":{"index":0},"specularGlossinessTexture":{"index":1}}}},
             {"name":"plastico","extensions":{"KHR_materials_pbrSpecularGlossiness":{"diffuseFactor":[0.8,0.05,0.05,1],"specularFactor":[0.04,0.04,0.04],"glossinessFactor":0.3}}}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]},{"primitives":[{"attributes":{"POSITION":0},"material":1}]}],
"nodes":[{"mesh":0},{"mesh":1}],"scenes":[{"nodes":[0,1]}],"scene":0})";
    const ImportResult r = import_json(json);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    // Extensão obrigatória desconhecida: aviso, não recusa.
    bool warned = false;
    for (const std::string& w : a.warnings) warned = warned || w.find("EXT_extensao_inventada") != std::string::npos;
    AUREA_CHECK(warned);
    const Material* gold = material_named(a, "ouro");
    const Material* plastic = material_named(a, "plastico");
    AUREA_CHECK(gold && plastic);
    if (!gold || !plastic) return;
    // O mapa especular/brilho virou metal/rugosidade (antes: ignorado = sem reflexo).
    AUREA_CHECK(gold->metallicRoughnessTex.valid());
    AUREA_CHECK(gold->baseColorTex.valid());
    if (gold->metallicRoughnessTex.valid() && gold->baseColorTex.valid()) {
        const Image& mr = a.images[static_cast<usize>(gold->metallicRoughnessTex.image)];
        const Image& base = a.images[static_cast<usize>(gold->baseColorTex.image)];
        AUREA_CHECK(mr.width == 4 && mr.rgba.size() == 64);
        AUREA_CHECK(base.width == 4 && base.rgba.size() == 64);
        if (mr.rgba.size() == 64 && base.rgba.size() == 64) {
            std::printf("\n    ouro: rug %u metal %u base %u %u %u", mr.rgba[1], mr.rgba[2], base.rgba[0], base.rgba[1], base.rgba[2]);
            AUREA_CHECK(mr.rgba[1] < 5);                   // brilho total = rugosidade 0
            AUREA_CHECK(mr.rgba[2] > 200);                 // especular colorido forte = metal
            AUREA_CHECK(base.rgba[0] > 200 && base.rgba[0] > base.rgba[1] && base.rgba[1] > base.rgba[2]);   // dourado
        }
        AUREA_CHECK_NEAR(gold->metallic, 1.0f, 1e-6f);
        AUREA_CHECK_NEAR(gold->roughness, 1.0f, 1e-6f);
    }
    AUREA_CHECK(!plastic->metallicRoughnessTex.valid());
    AUREA_CHECK_NEAR(plastic->metallic, 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(plastic->roughness, 0.7f, 1e-4f);
    AUREA_CHECK(plastic->baseColor.x > 0.7f && plastic->baseColor.y < 0.1f);
}

AUREA_TEST(Scene3D, SketchfabTextureProblemsDoNotFailImport) {
    // 0: textura externa ausente; 1: WebP com PNG de reserva; 2: WebP sem reserva;
    // 3: imagem ilegível (bytes que não são imagem). Com KHR_texture_transform.
    const std::string json = std::string(R"({"asset":{"version":"2.0"},
"extensionsUsed":["EXT_texture_webp","KHR_texture_transform"],"extensionsRequired":["EXT_texture_webp"],)") + kTriangleGeometry +
R"("images":[{"uri":"sumiu.png"},{"uri":")" + solid_png_uri(2, 2, 0, 255, 0, 255) + R"("},{"uri":"cor.webp","mimeType":"image/webp"},
            {"uri":"data:image/png;base64,AAAAAAAAAAAAAAAA"}],
"textures":[{"source":0},{"source":1,"extensions":{"EXT_texture_webp":{"source":2}}},{"extensions":{"EXT_texture_webp":{"source":2}}},{"source":3}],
"materials":[{"name":"ausente","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},
             {"name":"reserva","pbrMetallicRoughness":{"baseColorTexture":{"index":1,"extensions":{"KHR_texture_transform":{"offset":[0.5,0],"scale":[2,2]}}}}},
             {"name":"webp","pbrMetallicRoughness":{"baseColorTexture":{"index":2}}},
             {"name":"ilegivel","pbrMetallicRoughness":{"baseColorTexture":{"index":3}}}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0},{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":1},
                         {"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":2},{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":3}]}],
"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    const ImportResult r = import_json(json);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.missingTextures.size(), 1u);
    if (!a.missingTextures.empty()) AUREA_CHECK(a.missingTextures[0] == "sumiu.png");
    const Material* fallback = material_named(a, "reserva");
    AUREA_CHECK(fallback && fallback->baseColorTex.valid());
    if (fallback && fallback->baseColorTex.valid()) {
        const Image& img = a.images[static_cast<usize>(fallback->baseColorTex.image)];
        AUREA_CHECK(img.width == 2 && !img.rgba.empty() && img.rgba[1] == 255);
        AUREA_CHECK_NEAR(fallback->baseColorTex.offset.x, 0.5f, 1e-6f);
        AUREA_CHECK_NEAR(fallback->baseColorTex.scale.x, 2.0f, 1e-6f);
    }
    const Material* webp = material_named(a, "webp");
    AUREA_CHECK(webp && !webp->baseColorTex.valid());
    // Ausente e ilegível: a referência fica, a imagem vazia (o render usa branco).
    const Material* broken = material_named(a, "ilegivel");
    AUREA_CHECK(broken != nullptr);
    if (broken && broken->baseColorTex.valid()) AUREA_CHECK(a.images[static_cast<usize>(broken->baseColorTex.image)].rgba.empty());
    usize warned = 0;
    for (const std::string& w : a.warnings) warned += w.find("textura ignorada") != std::string::npos || w.find("WebP") != std::string::npos
                                                      || w.find("webp") != std::string::npos;
    AUREA_CHECK(warned >= 3);
}

AUREA_TEST(Scene3D, ObjWithoutMtllibAcceptsMtlChosenLater) {
    namespace fx = aurea::test_fixtures;
    ImportOptions o;
    const std::string folder = fx::fresh_model_folder("aurea_teste_obj_sem_mtllib");
    const std::string obj = folder + "Cadeira.obj";
    fx::write_text(obj, "v 0 0 0\nv 1 0 0\nv 1 1 0\nvn 0 0 1\nusemtl Madeira\nf 1//1 2//1 3//1\n");
    // Sem `mtllib` e sem .mtl: a UI recebe o nome que falta (para escolher depois).
    ImportResult r = import_scene_file(obj, o);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (r.ok()) {
        AUREA_CHECK_EQ(r.asset->missingTextures.size(), 1u);
        if (!r.asset->missingTextures.empty()) AUREA_CHECK(r.asset->missingTextures[0] == "Cadeira.mtl");
    }
    // O .mtl escolhido depois, com outro nome: o único da pasta religa os materiais.
    fx::write_text(folder + "cadeira_materiais.mtl", "newmtl Madeira\nKd 0.0 0.0 1.0\n");
    r = import_scene_file(obj, o);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (r.ok()) {
        AUREA_CHECK(r.asset->missingTextures.empty());
        AUREA_CHECK(!r.asset->materials.empty());
        if (!r.asset->materials.empty()) AUREA_CHECK_NEAR(r.asset->materials[0].baseColor.z, 1.0f, 1e-3f);
    }
    // OBJ só de geometria: nada a pedir.
    const std::string plain = fx::fresh_model_folder("aurea_teste_obj_puro") + "Pedra.obj";
    fx::write_text(plain, "v 0 0 0\nv 1 0 0\nv 1 1 0\nf 1 2 3\n");
    r = import_scene_file(plain, o);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (r.ok()) AUREA_CHECK(r.asset->missingTextures.empty());
}
