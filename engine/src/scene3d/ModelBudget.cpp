// =============================================================================
//  Aurea / scene3d / ModelBudget.cpp — a conta do orçamento do import 3D.
//
//  Só aritmética (testada no host). A leitura dos cabeçalhos do arquivo
//  (estimate_model_cost) mora em GltfImporter.cpp, junto do cgltf e do stb.
// =============================================================================
#include "aurea/scene3d/ModelBudget.hpp"

#include <algorithm>
#include <type_traits>

namespace aurea::scene3d {
namespace {

constexpr u64 kMB = 1024ull * 1024ull;

/// Bytes por vértice guardado no SceneAsset (posição, normal, tangente, dois
/// UV, cor, skin): o pior caso comum de um personagem.
constexpr u64 kVertexBytes = 84;
/// Bytes por vértice no envio para a GPU (posição + ShadingVertex + skin).
constexpr u64 kGpuVertexBytes = 72;
/// Índices: 3 × u32 por triângulo, mais os níveis de detalhe (50 % + 25 %).
constexpr u64 kIndexBytesPerTri = 12 * 7 / 4;

u64 kept_texture_pixels(const ModelCost& c, u32 cap) noexcept {
    if (c.texturePixels == 0) return 0;
    const u64 capPx = static_cast<u64>(cap) * cap;
    if (cap == 0) return c.texturePixels;
    // Cada textura fica com no máximo cap × cap; a soma nunca cresce.
    return std::min<u64>(c.texturePixels, std::max<u64>(1, c.textures) * capPx);
}

f64 keep_ratio(const ModelCost& c, const ModelBudget& b) noexcept {
    if (b.maxTriangles == 0 || c.triangles <= b.maxTriangles) return 1.0;
    return static_cast<f64>(b.maxTriangles) / static_cast<f64>(c.triangles);
}

} // namespace

u64 scene_asset_memory_bytes(const SceneAsset& s) noexcept {
    u64 bytes = sizeof(s);
    auto array = [&bytes](const auto& v) { bytes += static_cast<u64>(v.capacity()) * sizeof(typename std::decay_t<decltype(v)>::value_type); };
    auto name = [&bytes](const std::string& v) { bytes += v.capacity() + 1; };
    name(s.sourceName);
    array(s.nodes); array(s.roots); array(s.meshes); array(s.materials); array(s.images);
    array(s.samplers); array(s.cameras); array(s.lights); array(s.skins); array(s.animations);
    array(s.textUnits); array(s.textLogicalUnits); array(s.warnings); array(s.missingTextures);
    for (const auto& n : s.nodes) { name(n.name); array(n.children); array(n.materials); array(n.morphWeights); }
    for (const auto& m : s.meshes) {
        name(m.name); array(m.primitives); array(m.morphWeights);
        for (const auto& p : m.primitives) {
            array(p.positions); array(p.normals); array(p.tangents); array(p.uv0); array(p.uv1);
            array(p.colors); array(p.joints); array(p.weights); array(p.indices); array(p.lods); array(p.morphTargets);
            for (const auto& lod : p.lods) array(lod);
            for (const auto& morph : p.morphTargets) { array(morph.positions); array(morph.normals); array(morph.tangents); }
        }
    }
    for (const auto& m : s.materials) { name(m.name); array(m.ignoredExtensions); for (const auto& x : m.ignoredExtensions) name(x); }
    for (usize n = 0; n < s.images.size(); ++n) {
        const auto& i = s.images[n]; name(i.name); name(i.uri); array(i.rgba);
        if (i.sharedRgba && std::none_of(s.images.begin(), s.images.begin() + n,
            [&i](const Image& earlier) { return earlier.sharedRgba == i.sharedRgba; })) array(*i.sharedRgba);
    }
    for (const auto& c : s.cameras) name(c.name);
    for (const auto& l : s.lights) name(l.name);
    for (const auto& k : s.skins) { name(k.name); array(k.joints); array(k.inverseBind); }
    for (const auto& a : s.animations) {
        name(a.name); array(a.samplers); array(a.channels);
        for (const auto& sampler : a.samplers) { array(sampler.times); array(sampler.values); }
    }
    for (const auto& w : s.warnings) name(w);
    for (const auto& w : s.missingTextures) name(w);
    return bytes;
}

u64 model_memory_budget(const DeviceMemoryHint& hint) noexcept {
    // Pico, não residente: o import segura o arquivo lido, a parte em
    // precisão cheia e a cópia de envio para a GPU ao mesmo tempo. Uma fração
    // do que o sistema diz estar livre AGORA (o resto é da UI, do decode de
    // vídeo e do próprio sistema) e da RAM total (aparelho que "tem" memória
    // livre só porque matou o launcher não ganha teto maior por isso).
    u64 budget = 0;
    if (hint.availableBytes) budget = hint.availableBytes * 45 / 100;
    if (hint.totalBytes) {
        const u64 fromTotal = hint.totalBytes * (hint.lowRam ? 10 : 18) / 100;
        budget = budget ? std::min(budget, fromTotal) : fromTotal;
    }
    if (budget == 0) budget = 384 * kMB;   // sem medição: o piso conservador do motor
    if (hint.lowRam) budget = std::min<u64>(budget, 256 * kMB);
    if constexpr (sizeof(void*) <= 4) budget = std::min<u64>(budget, 320 * kMB);   // espaço de endereço de 32 bits
    return std::clamp<u64>(budget, 96 * kMB, 2048 * kMB);
}

ModelBudget model_budget(const DeviceMemoryHint& hint, ModelQuality quality, u32 textureCap) noexcept {
    ModelBudget b;
    b.memoryBytes = model_memory_budget(hint);
    u32 cap = textureCap ? textureCap : 2048;
    if (hint.gpuMaxTexture) cap = std::min(cap, hint.gpuMaxTexture);
    // Triângulos por qualidade, proporcionais ao orçamento: um aparelho com
    // 1 GB livre segura bem mais que um de entrada — mas o preview em tempo
    // real tem teto próprio (setecentos e cinquenta mil já é um personagem
    // de cinema no celular).
    const u64 balanced = std::clamp<u64>(b.memoryBytes / 1200, 100000, 750000);
    const u64 light = std::clamp<u64>(b.memoryBytes / 6000, 40000, 150000);
    switch (quality) {
        case ModelQuality::Original:
            b.maxTriangles = 0;
            b.maxTextureSize = hint.lowRam ? std::min(cap, 1024u) : cap;
            b.simplifyError = 0.0f;
            break;
        case ModelQuality::Balanced:
            b.maxTriangles = static_cast<u32>(hint.lowRam ? std::min<u64>(balanced, 150000) : balanced);
            b.maxTextureSize = std::min(cap, hint.lowRam ? 1024u : 2048u);
            b.simplifyError = 0.01f;
            break;
        case ModelQuality::Light:
            b.maxTriangles = static_cast<u32>(hint.lowRam ? std::min<u64>(light, 50000) : light);
            b.maxTextureSize = std::min(cap, hint.lowRam ? 512u : 1024u);
            b.simplifyError = 0.03f;
            break;
    }
    return b;
}

u64 kept_triangles(const ModelCost& cost, const ModelBudget& budget) noexcept {
    return static_cast<u64>(static_cast<f64>(cost.triangles) * keep_ratio(cost, budget));
}

u64 estimate_import_peak(const ModelCost& c, const ModelBudget& b) noexcept {
    const f64 r = keep_ratio(c, b);
    const u64 keptTris = static_cast<u64>(static_cast<f64>(c.triangles) * r);
    const u64 keptVerts = static_cast<u64>(static_cast<f64>(c.vertices) * r);
    const u64 keptGeometry = keptVerts * kVertexBytes + keptTris * kIndexBytesPerTri;
    const u64 keptPx = kept_texture_pixels(c, b.maxTextureSize);
    const u64 texCpu = keptPx * 4;
    const u64 texGpu = keptPx * 4 * 4 / 3;   // com mips
    // A maior parte em precisão cheia, antes de simplificar (o FBX é lido em
    // pedaços: a parte já vem limitada pelo leitor).
    const u64 part = c.largestPartTriangles * c.partBytesPerTriangle;
    // Decode de UMA textura por vez (saída + planos internos do decodificador,
    // ~8 bytes por pixel na resolução original), nunca as N juntas. O import
    // pula (com aviso) a imagem que não cabe no que SOBRA do teto naquela hora
    // — então a textura nunca é o que faz o modelo "não caber": no pior caso
    // ele entra sem ela.
    const u64 held = c.parseBytes + keptGeometry + texCpu;
    const u64 room = b.memoryBytes > held ? b.memoryBytes - held : 0;
    const u64 decode = b.memoryBytes ? std::min<u64>(c.largestTexturePixels * 8, room) : c.largestTexturePixels * 8;
    // A parte cheia (fase da geometria) e o decode (fase das texturas) não
    // acontecem juntos.
    const u64 importPeak = held + std::max(part, decode);
    // Depois do import o leitor sai; entram a cópia de envio e a da GPU.
    const u64 gpuGeometry = keptVerts * kGpuVertexBytes + keptTris * kIndexBytesPerTri;
    const u64 residentPeak = keptGeometry + texCpu + gpuGeometry * 2 + texGpu;
    return std::max(importPeak, residentPeak);
}

ModelPlan plan_model_import(const ModelCost& cost, const DeviceMemoryHint& hint, u32 textureCap) noexcept {
    ModelPlan plan;
    plan.cost = cost;
    for (u32 q = 0; q < kModelQualityCount; ++q) {
        plan.budget[q] = model_budget(hint, static_cast<ModelQuality>(q), textureCap);
        plan.peakBytes[q] = estimate_import_peak(cost, plan.budget[q]);
        plan.keptTriangles[q] = kept_triangles(cost, plan.budget[q]);
        plan.fits[q] = plan.peakBytes[q] <= plan.budget[q].memoryBytes;
    }
    if (!cost.valid) {
        // Ilegível: o import dirá o motivo certo; nada de oferecer otimização.
        plan.fits[0] = true;
        return plan;
    }
    const u32 orig = static_cast<u32>(ModelQuality::Original);
    const u32 bal = static_cast<u32>(ModelQuality::Balanced);
    const u32 light = static_cast<u32>(ModelQuality::Light);
    // Pesado = o Original não cabe, ou tem o dobro dos triângulos que o
    // Equilibrado manteria (cabe na memória, mas o preview engasga).
    plan.heavy = !plan.fits[orig] || cost.triangles > 2ull * plan.budget[bal].maxTriangles
        || (!cost.exact && cost.triangles == 0 && cost.fileBytes > 0);
    plan.recommended = !plan.heavy ? ModelQuality::Original
                     : plan.fits[bal] ? ModelQuality::Balanced : ModelQuality::Light;
    // Estimativa (FBX pelo tamanho) não recusa sozinha: o import tenta com o
    // teto de memória do leitor e recusa com motivo se passar.
    plan.tooHeavy = cost.exact && !plan.fits[light];
    return plan;
}

ModelQuality effective_quality(const ModelPlan& plan, ModelQuality wanted) noexcept {
    for (u32 q = static_cast<u32>(wanted); q < kModelQualityCount; ++q) {
        if (plan.fits[q]) return static_cast<ModelQuality>(q);
    }
    return ModelQuality::Light;
}

} // namespace aurea::scene3d
