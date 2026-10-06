// Testes do orçamento de memória do import 3D (ModelBudget.hpp): a conta por
// aparelho, a redução de textura, a simplificação por meshoptimizer e o modelo
// pesado sintético que entra (otimizado) em vez de derrubar o app.
#include "TestFramework.hpp"
#include "ImageIO.hpp"
#include "MockBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/ModelBudget.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

using namespace aurea;
using namespace aurea::scene3d;

namespace aurea::scene3d {
struct SceneUploadPoolTestAccess {
    static void attach(SceneRenderer& renderer, GPUBackend& gpu) { renderer.gpu_ = &gpu; }
    static BufferHandle allocate(SceneRenderer& renderer, usize bytes) { return renderer.take_upload(renderer.morphPool_, bytes); }
};
}

AUREA_TEST(ModelBudget, SceneUploadPoolsTrimOnlyReturnedBuffersAndReleaseProjectStorage) {
    test::MockBackend gpu; gpu.mapBuffers = true;
    SceneRenderer renderer; SceneUploadPoolTestAccess::attach(renderer, gpu);
    FrameBegin frame; AUREA_CHECK(gpu.begin_offscreen_frame(frame).ok());
    const auto busy = SceneUploadPoolTestAccess::allocate(renderer, 1024 * 1024);
    AUREA_CHECK(busy.valid());
    AUREA_CHECK_EQ(renderer.resident_bytes(), 1024u * 1024u);
    AUREA_CHECK_EQ(renderer.trim_upload_pools(), 0u);
    AUREA_CHECK_EQ(gpu.mappedBuffers.size(), 1u);
    AUREA_CHECK(gpu.end_frame().ok());
    AUREA_CHECK_EQ(renderer.trim_upload_pools(), 1u);
    AUREA_CHECK_EQ(gpu.mappedBuffers.size(), 0u);
    AUREA_CHECK_EQ(renderer.resident_bytes(), 0u);
    AUREA_CHECK(gpu.begin_offscreen_frame(frame).ok());
    AUREA_CHECK(SceneUploadPoolTestAccess::allocate(renderer, 2048).valid());
    renderer.release_all();
    AUREA_CHECK(gpu.end_frame().ok()); // Old-generation ticket must not republish a destroyed buffer.
    AUREA_CHECK_EQ(renderer.trim_upload_pools(), 0u);
    AUREA_CHECK_EQ(renderer.resident_bytes(), 0u);
    AUREA_CHECK_EQ(gpu.mappedBuffers.size(), 0u);
    renderer.forget_device();
}

AUREA_TEST(ModelBudget, Base64TexturesUseTheParserAllocatorForEveryExit) {
    const std::string path = std::string(AUREA_TEST_DATA_DIR) + "/mirrored-normal.gltf";
    ImportOptions options; options.memoryBudget = 8ull << 20; options.maxTextureSize = 64;
    for (u32 pass = 0; pass < 3; ++pass) {
        const auto scene = import_gltf_file(path, options);
        AUREA_CHECK(scene.ok());
        if (scene.ok()) {
            AUREA_CHECK(!scene.asset->images.empty());
            AUREA_CHECK(scene.asset->images[0].width > 0);
        }
    }
    options.memoryBudget = 128;
    AUREA_CHECK(import_gltf_file(path, options).error == ImportError::TooHeavy);
}

AUREA_TEST(ModelBudget, ExternalTextureBudgetFailureIsReportedAsTooHeavy) {
    const std::string path = std::string(AUREA_TEST_DATA_DIR) + "/mirrored-normal.gltf";
    const auto size = std::filesystem::file_size(path);
    std::string json(static_cast<usize>(size), ' ');
    auto* file = std::fopen(path.c_str(), "rb"); AUREA_CHECK(file != nullptr); if (!file) return;
    const usize got = std::fread(json.data(), 1, json.size(), file); std::fclose(file);
    AUREA_CHECK_EQ(got, json.size());
    const auto start = json.find("data:image/png;base64,");
    AUREA_CHECK(start != std::string::npos); if (start == std::string::npos) return;
    json.replace(start, json.find('"', start) - start, "large.png");
    bool requested = false;
    ImportOptions options; options.memoryBudget = 128 * 1024; options.maxTextureSize = 1;
    options.readerUser = &requested;
    options.reader = [](const char*, std::vector<u8>& bytes, void* context) {
        *static_cast<bool*>(context) = true; bytes.resize(1024 * 1024); return true;
    };
    const auto imported = import_gltf_memory(reinterpret_cast<const u8*>(json.data()), json.size(), {}, options);
    AUREA_CHECK(requested);
    AUREA_CHECK(imported.error == ImportError::TooHeavy);
}

AUREA_TEST(ModelBudget, InspectionBoundsJsonAndKeepsHeavyWarningWithoutRejectingUnknownGeometry) {
    const auto path = std::filesystem::temp_directory_path() / "aurea-estimate-budget.gltf";
    auto* file = std::fopen(path.string().c_str(), "wb");
    AUREA_CHECK(file != nullptr);
    if (!file) return;
    const char header[] = "{\"asset\":{\"version\":\"2.0\"}}";
    std::fwrite(header, 1, sizeof(header) - 1, file);
    std::fseek(file, 17 * 1024 * 1024, SEEK_SET); std::fputc(' ', file); std::fclose(file);
    const auto cost = estimate_model_cost(path.string());
    AUREA_CHECK(cost.valid && !cost.exact);
    AUREA_CHECK(cost.fileBytes >= 17ull * 1024 * 1024);
    const auto plan = plan_model_import(cost, {}, 2048);
    AUREA_CHECK(plan.heavy && !plan.tooHeavy);
    ImportOptions options; options.memoryBudget = 1024;
    AUREA_CHECK(import_gltf_file(path.string(), options).error == ImportError::TooHeavy);
    std::error_code error; std::filesystem::remove(path, error);
}

AUREA_TEST(ModelBudget, ResidentCostIncludesSpareMorphLodAndAnimationStorage) {
    SceneAsset scene;
    const u64 empty = scene_asset_memory_bytes(scene);
    scene.meshes.resize(1); scene.meshes[0].primitives.resize(1);
    auto& primitive = scene.meshes[0].primitives[0];
    primitive.positions.reserve(1000);
    primitive.morphTargets.resize(1); primitive.morphTargets[0].normals.reserve(2000);
    primitive.lods.resize(1); primitive.lods[0].reserve(3000);
    scene.animations.resize(1); scene.animations[0].samplers.resize(1);
    scene.animations[0].samplers[0].values.reserve(4000);
    AUREA_CHECK(scene_asset_memory_bytes(scene) >= empty + 3000 * sizeof(Vec3) + 3000 * sizeof(u32) + 4000 * sizeof(f32));
}

AUREA_TEST(ModelBudget, GltfBoundsInputAndParserBeforeLargeAllocations) {
    ImportOptions options; options.memoryBudget = 1024;
    std::vector<u8> oversized(2048, ' ');
    const auto memory = import_gltf_memory(oversized.data(), oversized.size(), {}, options);
    AUREA_CHECK(memory.error == ImportError::TooHeavy);
    const auto path = std::filesystem::temp_directory_path() / "aurea-gltf-bounded-input.gltf";
    if (auto* f = std::fopen(path.string().c_str(), "wb")) {
        std::fwrite(oversized.data(), 1, oversized.size(), f); std::fclose(f);
    }
    const auto disk = import_gltf_file(path.string(), options);
    AUREA_CHECK(disk.error == ImportError::TooHeavy);
    std::error_code ec; std::filesystem::remove(path, ec);

    const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"big.bin","byteLength":1000000000}]})";
    options.memoryBudget = 64 * 1024;
    bool read = false;
    options.readerUser = &read;
    options.reader = [](const char*, std::vector<u8>&, void* context) { *static_cast<bool*>(context) = true; return false; };
    const auto buffers = import_gltf_memory(reinterpret_cast<const u8*>(json.data()), json.size(), {}, options);
    AUREA_CHECK(buffers.error == ImportError::TooHeavy);
    AUREA_CHECK(!read);
    options.memoryBudget = json.size() + 16;
    const auto parser = import_gltf_memory(reinterpret_cast<const u8*>(json.data()), json.size(), {}, options);
    AUREA_CHECK(parser.error == ImportError::TooHeavy);
}

namespace {

constexpr u64 kMB = 1024ull * 1024ull;
constexpr u64 kGB = 1024ull * kMB;

DeviceMemoryHint phone(u64 totalGb, u64 availMb, bool lowRam = false) {
    DeviceMemoryHint h;
    h.totalBytes = totalGb * kGB;
    h.availableBytes = availMb * kMB;
    h.lowRam = lowRam;
    return h;
}

/// Grade N × N de quads (2N² triângulos) no plano XY, com normal +Z e UV 0..1.
Primitive grid(u32 n) {
    Primitive p;
    for (u32 y = 0; y <= n; ++y) {
        for (u32 x = 0; x <= n; ++x) {
            const f32 u = static_cast<f32>(x) / n, v = static_cast<f32>(y) / n;
            // Um relevo suave: a simplificação tem o que decidir.
            p.positions.push_back(Vec3{u, v, 0.05f * std::sin(u * 6.0f) * std::cos(v * 5.0f)});
            p.normals.push_back(Vec3{0, 0, 1});
            p.uv0.push_back(Vec2{u, v});
        }
    }
    for (u32 y = 0; y < n; ++y) {
        for (u32 x = 0; x < n; ++x) {
            const u32 a = y * (n + 1) + x, b = a + 1, c = a + n + 1, d = c + 1;
            p.indices.insert(p.indices.end(), {a, b, d, a, d, c});
        }
    }
    for (const Vec3& v : p.positions) p.bounds.add(v);
    return p;
}

std::string tmp_dir() {
    return std::string("aurea_model_budget_tmp/");
}

void ensure_dir(const std::string& d) {
#if defined(_WIN32)
    std::string cmd = "mkdir \"" + d + "\" 2>nul";
    for (char& ch : cmd) if (ch == '/') ch = '\\';
#else
    std::string cmd = "mkdir -p \"" + d + "\" 2>/dev/null";
#endif
    (void)std::system(cmd.c_str());
}

/// glTF sintético pesado: grade N × N (posição, normal, UV, índices u32) num
/// .bin externo e uma textura PNG de `texW` × `texH` ao lado.
std::string write_heavy_gltf(const std::string& dir, u32 n, u32 texW, u32 texH) {
    ensure_dir(dir);
    const Primitive g = grid(n);
    std::vector<u8> bin;
    auto put = [&](const void* data, usize bytes) {
        const u8* p = static_cast<const u8*>(data);
        bin.insert(bin.end(), p, p + bytes);
    };
    const usize posOff = bin.size(); put(g.positions.data(), g.positions.size() * sizeof(Vec3));
    const usize nrmOff = bin.size(); put(g.normals.data(), g.normals.size() * sizeof(Vec3));
    const usize uvOff = bin.size(); put(g.uv0.data(), g.uv0.size() * sizeof(Vec2));
    const usize idxOff = bin.size(); put(g.indices.data(), g.indices.size() * sizeof(u32));
    {
        std::FILE* f = std::fopen((dir + "pesado.bin").c_str(), "wb");
        if (f) { std::fwrite(bin.data(), 1, bin.size(), f); std::fclose(f); }
    }
    test::Image8 tex;
    tex.width = texW;
    tex.height = texH;
    tex.rgba.resize(static_cast<usize>(texW) * texH * 4);
    for (u32 y = 0; y < texH; ++y)
        for (u32 x = 0; x < texW; ++x) {
            u8* px = &tex.rgba[(static_cast<usize>(y) * texW + x) * 4];
            px[0] = static_cast<u8>(x); px[1] = static_cast<u8>(y); px[2] = 128; px[3] = 255;
        }
    (void)test::write_png(dir + "pesado.png", tex);
    const usize vc = g.positions.size();
    char json[4096];
    std::snprintf(json, sizeof(json),
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],)"
        R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
        R"("textures":[{"source":0}],"images":[{"uri":"pesado.png"}],)"
        R"("buffers":[{"uri":"pesado.bin","byteLength":%zu}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":%zu,"byteLength":%zu},{"buffer":0,"byteOffset":%zu,"byteLength":%zu},)"
        R"({"buffer":0,"byteOffset":%zu,"byteLength":%zu},{"buffer":0,"byteOffset":%zu,"byteLength":%zu}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":%zu,"type":"VEC3","min":[0,0,-1],"max":[1,1,1]},)"
        R"({"bufferView":1,"componentType":5126,"count":%zu,"type":"VEC3"},)"
        R"({"bufferView":2,"componentType":5126,"count":%zu,"type":"VEC2"},)"
        R"({"bufferView":3,"componentType":5125,"count":%zu,"type":"SCALAR"}]})",
        bin.size(), posOff, g.positions.size() * sizeof(Vec3), nrmOff, g.normals.size() * sizeof(Vec3),
        uvOff, g.uv0.size() * sizeof(Vec2), idxOff, g.indices.size() * sizeof(u32), vc, vc, vc, g.indices.size());
    const std::string path = dir + "pesado.gltf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(json, 1, std::strlen(json), f); std::fclose(f); }
    return path;
}

/// OBJ sintético com uma parte só maior que o pedaço da leitura em partes.
std::string write_heavy_obj(const std::string& dir, u32 n) {
    ensure_dir(dir);
    const std::string path = dir + "pesado.obj";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return path;
    for (u32 y = 0; y <= n; ++y)
        for (u32 x = 0; x <= n; ++x)
            std::fprintf(f, "v %.5f %.5f %.5f\n", static_cast<f64>(x) / n, static_cast<f64>(y) / n,
                         0.05 * std::sin(6.0 * x / n) * std::cos(5.0 * y / n));
    for (u32 y = 0; y <= n; ++y)
        for (u32 x = 0; x <= n; ++x) std::fprintf(f, "vt %.5f %.5f\n", static_cast<f64>(x) / n, static_cast<f64>(y) / n);
    std::fprintf(f, "vn 0 0 1\n");
    for (u32 y = 0; y < n; ++y)
        for (u32 x = 0; x < n; ++x) {
            const u32 a = y * (n + 1) + x + 1, b = a + 1, c = a + n + 1, d = c + 1;
            std::fprintf(f, "f %u/%u/1 %u/%u/1 %u/%u/1 %u/%u/1\n", a, a, b, b, d, d, c, c);
        }
    std::fclose(f);
    return path;
}

bool primitive_valid(const Primitive& p) {
    const usize vc = p.positions.size();
    if (vc == 0 || p.indices.empty() || p.indices.size() % 3 != 0) return false;
    if (!p.normals.empty() && p.normals.size() != vc) return false;
    if (!p.uv0.empty() && p.uv0.size() != vc) return false;
    if (!p.tangents.empty() && p.tangents.size() != vc) return false;
    for (u32 i : p.indices) if (i >= vc) return false;
    for (const Vec3& n : p.normals) if (std::fabs(n.length() - 1.0f) > 1e-3f) return false;
    for (const Vec2& t : p.uv0) if (!(t.x >= -1e-4f && t.x <= 1.0001f && t.y >= -1e-4f && t.y <= 1.0001f)) return false;
    return true;
}

} // namespace

AUREA_TEST(ModelBudget, BudgetFollowsTheDeviceMemory) {
    // Celular de 4 GB com 1,5 GB livre: 45 % do livre (675 MB) < 18 % do total (737 MB).
    AUREA_CHECK_EQ(model_memory_budget(phone(4, 1536)), 1536ull * kMB * 45 / 100);
    // Pouca RAM (isLowRamDevice): no máximo 256 MB, nunca abaixo do piso.
    AUREA_CHECK(model_memory_budget(phone(2, 900, true)) <= 256 * kMB);
    AUREA_CHECK(model_memory_budget(phone(1, 50, true)) >= 96 * kMB);
    // Sem medição: o piso conservador do motor.
    AUREA_CHECK_EQ(model_memory_budget(DeviceMemoryHint{}), 384 * kMB);
    // Aparelho maior → teto maior.
    AUREA_CHECK(model_memory_budget(phone(12, 6000)) > model_memory_budget(phone(4, 1536)));

    const DeviceMemoryHint mid = phone(4, 1536);
    const ModelBudget o = model_budget(mid, ModelQuality::Original, 2048);
    const ModelBudget b = model_budget(mid, ModelQuality::Balanced, 2048);
    const ModelBudget l = model_budget(mid, ModelQuality::Light, 2048);
    AUREA_CHECK_EQ(o.maxTriangles, 0u);                 // Original não simplifica
    AUREA_CHECK(b.maxTriangles > l.maxTriangles && l.maxTriangles > 0);
    AUREA_CHECK_EQ(b.maxTextureSize, 2048u);
    AUREA_CHECK_EQ(l.maxTextureSize, 1024u);
    // Pouca RAM: texturas menores em toda qualidade; GPU limitada manda.
    AUREA_CHECK_EQ(model_budget(phone(2, 900, true), ModelQuality::Original, 2048).maxTextureSize, 1024u);
    AUREA_CHECK_EQ(model_budget(phone(2, 900, true), ModelQuality::Light, 2048).maxTextureSize, 512u);
    DeviceMemoryHint weakGpu = mid;
    weakGpu.gpuMaxTexture = 1024;
    AUREA_CHECK_EQ(model_budget(weakGpu, ModelQuality::Original, 2048).maxTextureSize, 1024u);
}

AUREA_TEST(ModelBudget, PlanAsksToOptimizeHeavyModelsAndRefusesImpossibleOnes) {
    const DeviceMemoryHint mid = phone(4, 1536);
    // O relato do beta: milhões de triângulos e texturas 8K num celular médio.
    ModelCost heavy;
    heavy.valid = true;
    heavy.triangles = 6'000'000;
    heavy.vertices = 3'500'000;
    heavy.largestPartTriangles = 262'144;
    heavy.partBytesPerTriangle = 312;
    heavy.parseBytes = 200 * kMB;   // GLB: posições, normais, UV e índices do arquivo
    heavy.textures = 6;
    heavy.texturePixels = 6ull * 8192 * 8192;
    heavy.largestTexturePixels = 8192ull * 8192;
    heavy.largestTextureSide = 8192;
    const ModelPlan plan = plan_model_import(heavy, mid, 2048);
    AUREA_CHECK(!plan.fits[0]);                          // como está, não cabe
    AUREA_CHECK(plan.heavy);
    AUREA_CHECK(!plan.tooHeavy);
    AUREA_CHECK(plan.fits[2]);                           // o Leve cabe
    AUREA_CHECK(plan.recommended != ModelQuality::Original);
    AUREA_CHECK(plan.keptTriangles[2] < plan.keptTriangles[1]);
    AUREA_CHECK(plan.peakBytes[2] < plan.peakBytes[0]);

    // Um modelo leve entra direto, sem diálogo.
    ModelCost light;
    light.valid = true;
    light.triangles = 20'000;
    light.vertices = 12'000;
    light.largestPartTriangles = 20'000;
    light.parseBytes = 2 * kMB;
    light.textures = 1;
    light.texturePixels = 1024ull * 1024;
    light.largestTexturePixels = 1024ull * 1024;
    const ModelPlan lp = plan_model_import(light, mid, 2048);
    AUREA_CHECK(lp.fits[0]);
    AUREA_CHECK(!lp.heavy);
    AUREA_CHECK(lp.recommended == ModelQuality::Original);

    // Impossível até no Leve (o arquivo sozinho passa do teto): recusa sem tentar.
    ModelCost huge = heavy;
    huge.parseBytes = 8 * kGB;
    const ModelPlan hp = plan_model_import(huge, mid, 2048);
    AUREA_CHECK(hp.tooHeavy);
    // Estimativa (FBX pelo tamanho) nunca recusa sozinha: o import com teto decide.
    huge.exact = false;
    AUREA_CHECK(!plan_model_import(huge, mid, 2048).tooHeavy);

    // Reabrir num aparelho menor sobe para a qualidade que cabe.
    AUREA_CHECK(effective_quality(plan, ModelQuality::Original) != ModelQuality::Original);
    AUREA_CHECK(effective_quality(lp, ModelQuality::Original) == ModelQuality::Original);
    AUREA_CHECK(effective_quality(hp, ModelQuality::Balanced) == ModelQuality::Light);
}

AUREA_TEST(ModelBudget, TextureDownscaleHalvesToTheCapAveragingPixels) {
    Image img;
    img.width = 8;
    img.height = 4;
    img.rgba.resize(8 * 4 * 4);
    for (u32 y = 0; y < 4; ++y)
        for (u32 x = 0; x < 8; ++x) {
            u8* p = &img.rgba[(y * 8 + x) * 4];
            p[0] = (x % 2) ? 200 : 100;   // média 150
            p[1] = 40; p[2] = 0; p[3] = 255;
        }
    downscale_image(img, 2);
    AUREA_CHECK_EQ(img.width, 2u);
    AUREA_CHECK_EQ(img.height, 1u);
    AUREA_CHECK_EQ(img.rgba.size(), usize{2 * 1 * 4});
    AUREA_CHECK_EQ(static_cast<u32>(img.rgba[0]), 150u);
    AUREA_CHECK_EQ(static_cast<u32>(img.rgba[1]), 40u);
    AUREA_CHECK_EQ(static_cast<u32>(img.rgba[3]), 255u);
    // Já dentro do teto: nada muda.
    downscale_image(img, 4);
    AUREA_CHECK_EQ(img.width, 2u);
}

AUREA_TEST(ModelBudget, SimplificationReducesTrianglesKeepingUvAndNormalsValid) {
    Primitive p = grid(200);   // 80 000 triângulos
    const u32 before = p.triangle_count();
    const Aabb box = p.bounds;
    const u32 after = simplify_primitive(p, 0.1f, 0.01f, true);
    AUREA_CHECK_EQ(after, p.triangle_count());
    AUREA_CHECK(after < before / 5);           // reduziu de verdade
    AUREA_CHECK(after > 0);
    AUREA_CHECK(primitive_valid(p));           // índices, UV e normais coerentes
    AUREA_CHECK(p.positions.size() < 201u * 201u);   // vértices não usados saíram
    AUREA_CHECK_NEAR(p.bounds.extent().x, box.extent().x, 0.02f);   // a silhueta fica
    AUREA_CHECK_NEAR(p.bounds.extent().y, box.extent().y, 0.02f);
    // Pedido sem redução: nada muda.
    Primitive q = grid(10);
    AUREA_CHECK_EQ(simplify_primitive(q, 1.0f, 0.01f, true), 200u);
}

AUREA_TEST(ModelBudget, HeavySyntheticGltfLoadsOptimizedUnderBudget) {
    const std::string dir = tmp_dir() + "gltf/";
    const std::string path = write_heavy_gltf(dir, 400, 2048, 1024);   // 320 000 triângulos
    const ModelCost cost = estimate_model_cost(path);
    AUREA_CHECK(cost.valid);
    AUREA_CHECK(cost.exact);
    AUREA_CHECK_EQ(cost.triangles, 320'000ull);
    AUREA_CHECK_EQ(cost.textures, 1u);
    AUREA_CHECK_EQ(cost.largestTextureSide, 2048u);

    // Aparelho de pouca memória: o plano pede otimização.
    const DeviceMemoryHint small = phone(2, 700, true);
    const ModelPlan plan = plan_model_import(cost, small, 2048);
    AUREA_CHECK(plan.heavy);
    AUREA_CHECK(!plan.tooHeavy);

    const ModelBudget b = model_budget(small, ModelQuality::Light, 2048);
    ImportOptions o;
    o.maxTextureSize = b.maxTextureSize;
    o.memoryBudget = b.memoryBytes;
    o.maxTriangles = b.maxTriangles;
    o.simplifyError = b.simplifyError;
    ImportProgress progress;
    ImportResult r = import_scene_file(path, o, &progress);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.stats.sourceTriangles, 320'000u);
    AUREA_CHECK(a.stats.triangles <= b.maxTriangles + b.maxTriangles / 4);
    AUREA_CHECK(a.stats.triangles > 1000u);
    for (const Mesh& m : a.meshes) for (const Primitive& p : m.primitives) AUREA_CHECK(primitive_valid(p));
    AUREA_CHECK(!a.images.empty());
    for (const Image& img : a.images) {
        if (img.rgba.empty()) continue;
        AUREA_CHECK(img.width <= b.maxTextureSize && img.height <= b.maxTextureSize);
    }
    AUREA_CHECK_EQ(a.stats.texturesReduced, 1u);
    // A conta final do que ficou cabe no teto.
    ModelCost kept;
    kept.valid = true;
    kept.triangles = a.stats.triangles;
    kept.vertices = a.stats.vertices;
    kept.texturePixels = a.stats.imageBytes / 4;
    AUREA_CHECK(estimate_import_peak(kept, ModelBudget{}) <= b.memoryBytes);

    // Original com teto minúsculo: recusado com motivo — nunca morto.
    ImportOptions tight;
    tight.memoryBudget = 8 * kMB;
    ImportResult refused = import_scene_file(path, tight);
    AUREA_CHECK(refused.error == ImportError::TooHeavy);
    AUREA_CHECK(refused.asset == nullptr);
    AUREA_CHECK(!refused.detail.empty());
}

AUREA_TEST(ModelBudget, HeavyObjIsReadInPartsAndSimplified) {
    const std::string dir = tmp_dir() + "obj/";
    const std::string path = write_heavy_obj(dir, 380);   // 288 800 triângulos numa parte só
    const ModelCost cost = estimate_model_cost(path);
    AUREA_CHECK(cost.valid);
    AUREA_CHECK_EQ(cost.triangles, 288'800ull);
    ImportOptions o;
    o.memoryBudget = 512 * kMB;
    o.maxTriangles = 40'000;
    o.simplifyError = 0.03f;
    ImportResult r = import_scene_file(path, o);
    AUREA_CHECK_MSG(r.ok(), r.detail.c_str());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    AUREA_CHECK_EQ(a.stats.sourceTriangles, 288'800u);
    // Lido em pedaços: mais de uma primitiva para a mesma parte de material.
    AUREA_CHECK(a.stats.primitives >= 2u);
    // Borda travada entre pedaços segura um pouco mais que o alvo, mas reduz muito.
    AUREA_CHECK(a.stats.triangles < 288'800u / 3);
    for (const Mesh& m : a.meshes) for (const Primitive& p : m.primitives) AUREA_CHECK(primitive_valid(p));
    // A caixa do modelo inteiro continua a mesma (nenhum pedaço sumiu).
    AUREA_CHECK_NEAR(a.bounds.extent().x, 1.0f, 0.02f);
    AUREA_CHECK_NEAR(a.bounds.extent().y, 1.0f, 0.02f);
}

AUREA_TEST(ModelBudget, EngineStoresTheQualityAndReopensOptimized) {
    const std::string dir = tmp_dir() + "engine/";
    const std::string path = write_heavy_gltf(dir, 300, 256, 256);   // 180 000 triângulos
    const std::string project = tmp_dir() + "otimizado.aurea";
    std::remove(project.c_str());
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 256ull * 1024 * 1024;
    cfg.disableAutosave = true;
    u32 keptTriangles = 0;
    {
        Engine e;
        AUREA_CHECK(e.initialize(cfg).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, "modelo").ok());
        const ModelPlan plan = e.inspect_model(path, phone(4, 1536));
        AUREA_CHECK(plan.cost.valid);
        AUREA_CHECK_EQ(plan.cost.triangles, 180'000ull);
        ModelImport req;
        req.path = path;
        req.displayName = "pesado";
        req.quality = ModelQuality::Light;
        req.memory = phone(4, 1536);
        std::string detail;
        const Result<u64> layer = e.import_model(req, nullptr, &detail);
        AUREA_CHECK_MSG(layer.ok(), detail.c_str());
        if (!layer.ok()) { e.shutdown(); return; }
        const ModelImportReport rep = e.last_model_import();
        AUREA_CHECK_EQ(rep.sourceTriangles, 180'000u);
        AUREA_CHECK(rep.triangles < rep.sourceTriangles);
        AUREA_CHECK(rep.quality == ModelQuality::Light);
        keptTriangles = rep.triangles;
        bool found = false;
        e.project()->for_each_asset([&](AssetId, const Asset& a) {
            if (a.kind != AssetKind::Model3D) return;
            found = true;
            AUREA_CHECK_EQ(static_cast<u32>(a.model.importQuality), 2u);
            AUREA_CHECK_EQ(a.model.sourceTriangles, 180'000u);
        });
        AUREA_CHECK(found);
        AUREA_CHECK(e.save_project(project.c_str()).ok());
        e.shutdown();
    }
    {
        Engine f;
        AUREA_CHECK(f.initialize(cfg).ok());
        AUREA_CHECK(f.load_project(project.c_str()).ok());
        u32 models = 0;
        f.project()->for_each_asset([&](AssetId id, const Asset& a) {
            if (a.kind != AssetKind::Model3D) return;
            ++models;
            // A qualidade voltou do arquivo, e o modelo reaberto é o otimizado.
            AUREA_CHECK_EQ(static_cast<u32>(a.model.importQuality), 2u);
            const auto scene = f.model_asset(id.pack());
            AUREA_CHECK(scene != nullptr);
            if (scene) AUREA_CHECK(scene->stats.triangles <= keptTriangles + keptTriangles / 10);
        });
        AUREA_CHECK_EQ(models, 1u);
        f.shutdown();
    }
    std::remove(project.c_str());
}

AUREA_TEST(ModelBudget, Scene3DSectionRoundTripsAndOldProjectsStayOriginal) {
    auto created = Project::create_new(320, 180, 30.0, "q");
    AUREA_CHECK(created.ok());
    Project p = std::move(*created);
    Asset img;
    img.kind = AssetKind::Image;
    img.name = "foto";
    (void)p.add_asset(std::move(img));
    Asset model;
    model.kind = AssetKind::Model3D;
    model.name = "modelo";
    model.sourcePath = "modelo.glb";
    model.model.importQuality = 1;
    model.model.sourceTriangles = 1'234'567;
    (void)p.add_asset(std::move(model));
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(p, SaveOptions{}, bytes).ok());
    Project back;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(back, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    AUREA_CHECK(!report.partial);
    u32 seen = 0;
    back.for_each_asset([&](AssetId, const Asset& a) {
        if (a.kind != AssetKind::Model3D) return;
        ++seen;
        AUREA_CHECK_EQ(static_cast<u32>(a.model.importQuality), 1u);
        AUREA_CHECK_EQ(a.model.sourceTriangles, 1'234'567u);
    });
    AUREA_CHECK_EQ(seen, 1u);

    // Projeto sem modelo otimizado: a seção nem é gravada (arquivo igual ao de antes).
    auto plainCreated = Project::create_new(320, 180, 30.0, "q");
    AUREA_CHECK(plainCreated.ok());
    Project plain = std::move(*plainCreated);
    Asset m2;
    m2.kind = AssetKind::Model3D;
    m2.sourcePath = "outro.glb";
    (void)plain.add_asset(std::move(m2));
    std::vector<u8> plainBytes;
    AUREA_CHECK(ProjectSerializer::encode(plain, SaveOptions{}, plainBytes).ok());
    Project plainBack;
    AUREA_CHECK(ProjectSerializer::load_bytes(plainBack, plainBytes.data(), plainBytes.size(), LoadOptions{}).ok());
    plainBack.for_each_asset([&](AssetId, const Asset& a) {
        if (a.kind == AssetKind::Model3D) AUREA_CHECK_EQ(static_cast<u32>(a.model.importQuality), 0u);
    });
}
