// =============================================================================
//  Aurea / scene3d / ModelBudget.hpp
//
//  Orçamento de memória do import 3D. Um modelo de 2 milhões de triângulos com
//  texturas 8K não cabe num celular de 4 GB: sem orçamento, o import alocava
//  tudo em precisão cheia (cantos do FBX a 60 bytes cada, texturas decodificadas
//  em 8K = 256 MB por imagem, cópia de envio para a GPU) até o sistema matar o
//  app — e, de quebra, o launcher e o papel de parede.
//
//  O fluxo agora é:
//
//    estimate_model_cost (barato: só cabeçalhos e contagens)
//      → plan_model_import (o que cabe neste aparelho, por qualidade)
//      → a UI pergunta "Otimizar modelo" quando o Original não cabe
//      → o import aplica o orçamento (texturas reduzidas no decode, malhas
//        simplificadas por partes com meshoptimizer) e RECUSA com motivo
//        (ImportError::TooHeavy) em vez de ser morto.
//
//  A qualidade escolhida fica no asset do projeto: reabrir aplica a mesma
//  redução (e nunca menos do que o aparelho aguenta).
// =============================================================================
#pragma once

#include "aurea/scene3d/SceneAsset.hpp"

#include <string>

namespace aurea::scene3d {

/// Qualidade do import. Os números atravessam a bridge e ficam no projeto:
/// nada é reordenado.
enum class ModelQuality : u8 {
    Original = 0,   ///< sem simplificação; só o teto de textura do aparelho
    Balanced = 1,   ///< o padrão para modelo pesado
    Light    = 2,   ///< o mais leve: preview fluido em aparelho de entrada
};
inline constexpr u32 kModelQualityCount = 3;

/// O que a PLATAFORMA mede (Android: ActivityManager.MemoryInfo totalMem /
/// availMem / isLowRamDevice; iOS: ProcessInfo.physicalMemory e
/// os_proc_available_memory). Zero = desconhecido.
struct DeviceMemoryHint {
    u64  totalBytes = 0;
    u64  availableBytes = 0;
    bool lowRam = false;
    u32  gpuMaxTexture = 0;   ///< maior lado de textura aceito pela GPU
};

/// Limites de um import, já resolvidos para o aparelho.
struct ModelBudget {
    u64 memoryBytes = 0;      ///< teto do PICO do import (CPU + GPU: memória unificada no celular)
    u32 maxTriangles = 0;     ///< 0 = sem simplificação
    u32 maxTextureSize = 0;   ///< maior lado de textura guardado
    f32 simplifyError = 0.01f;///< erro relativo ao tamanho da malha (meshoptimizer)
};

/// Custo do arquivo, ANTES de importar. `exact = false` quando é estimativa
/// (FBX: as contagens só existem depois de ler a geometria inteira).
struct ModelCost {
    u64  fileBytes = 0;       ///< arquivo principal + buffers externos
    u64  parseBytes = 0;      ///< o que o leitor (cgltf/ufbx) segura durante o import
    /// Bytes por triângulo do pico transitório da maior parte, antes da
    /// simplificação (glTF desempacota; FBX/OBJ monta cantos de 60 bytes).
    u32  partBytesPerTriangle = 64;
    u64  triangles = 0;
    u64  vertices = 0;
    u64  largestPartTriangles = 0;
    u32  textures = 0;
    u64  texturePixels = 0;   ///< soma de largura × altura na resolução original
    u64  largestTexturePixels = 0;
    u32  largestTextureSide = 0;
    bool exact = true;
    bool valid = false;       ///< false = arquivo ilegível (o import dirá o motivo)
};

/// Plano por qualidade, para a UI decidir se pergunta.
struct ModelPlan {
    ModelCost cost;
    ModelBudget budget[kModelQualityCount]{};
    u64  peakBytes[kModelQualityCount]{};
    bool fits[kModelQualityCount]{};
    u64  keptTriangles[kModelQualityCount]{};
    ModelQuality recommended = ModelQuality::Original;
    /// Original não cabe (ou passaria do teto de triângulos do Equilibrado):
    /// a UI oferece "Otimizar modelo".
    bool heavy = false;
    /// Nem o Leve cabe: a UI recusa com o motivo, sem tentar.
    bool tooHeavy = false;
};

/// Teto de memória do import para o aparelho (pico, não residente).
[[nodiscard]] u64 model_memory_budget(const DeviceMemoryHint& hint) noexcept;

/// CPU allocations retained by an immutable scene, including animation, LODs,
/// morphs and spare vector capacity. Shared image storage is counted conservatively.
[[nodiscard]] u64 scene_asset_memory_bytes(const SceneAsset& scene) noexcept;

/// Limites de uma qualidade. `textureCap` é o teto do motor (2048 no celular).
[[nodiscard]] ModelBudget model_budget(const DeviceMemoryHint& hint, ModelQuality quality, u32 textureCap) noexcept;

/// Pico estimado do import (e do envio para a GPU) com esses limites.
[[nodiscard]] u64 estimate_import_peak(const ModelCost& cost, const ModelBudget& budget) noexcept;

/// Triângulos que sobram com esses limites.
[[nodiscard]] u64 kept_triangles(const ModelCost& cost, const ModelBudget& budget) noexcept;

[[nodiscard]] ModelPlan plan_model_import(const ModelCost& cost, const DeviceMemoryHint& hint, u32 textureCap) noexcept;

/// A qualidade que de fato vai ser usada: a pedida, ou a mais leve seguinte
/// quando a pedida não cabe neste aparelho (reabrir um projeto feito num
/// aparelho maior não pode derrubar o app).
[[nodiscard]] ModelQuality effective_quality(const ModelPlan& plan, ModelQuality wanted) noexcept;

/// Contagens do arquivo sem montar a cena: glTF/GLB pelo JSON e cabeçalhos das
/// imagens; OBJ contando linhas; FBX pelo tamanho (estimativa). Texturas soltas
/// na pasta do modelo entram pelo cabeçalho de cada imagem.
[[nodiscard]] ModelCost estimate_model_cost(const std::string& path) noexcept;

// --- Peças do orçamento, expostas para teste ------------------------------------

/// Reduz pela metade (caixa 2×2) até caber em `maxSide`.
void downscale_image(Image& img, u32 maxSide);

/// Simplifica a primitiva para ~`ratio` dos triângulos (meshoptimizer), com
/// normais e UV pesando no erro; `sloppy` permite o modo que ignora topologia
/// quando o normal não chega perto do alvo. Os vértices que sobraram são
/// compactados (todos os atributos, skin e morph juntos). Devolve os
/// triângulos finais.
u32 simplify_primitive(Primitive& p, f32 ratio, f32 error, bool sloppy, bool lockBorder = false);

} // namespace aurea::scene3d
