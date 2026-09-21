// =============================================================================
//  Aurea / render / ShaderLibrary.hpp
//
//  Fonte única de shader, cache de pipeline.
//
//  O problema real que isto resolve: compilar um pipeline Vulkan/Metal custa
//  de 5 a 200 ms. Se isso acontece na primeira vez que um efeito aparece
//  durante o playback, o usuário vê um congelamento de um terço de segundo no
//  meio da reprodução — e pior, ele acontece várias vezes, uma por combinação
//  de estado de blend/format.
//
//  Três camadas de defesa:
//
//   1. FONTE ÚNICA. O shader é escrito uma vez. Android recebe SPIR-V, iOS
//      recebe MSL gerado de SPIR-V no build (SPIRV-Cross), não escrito à mão.
//      Preview e export usam o mesmo shader — impossível divergirem.
//
//   2. CACHE POR CHAVE ESTRUTURAL. A chave do pipeline é (shader, blend,
//      formats, samples), não um ponteiro. Duas layers com o mesmo blend mode
//      compartilham o pipeline.
//
//   3. PRÉ-AQUECIMENTO. Ao abrir um projeto, o motor varre os efeitos usados e
//      compila os pipelines de que vai precisar ANTES do primeiro frame —
//      enquanto a UI ainda mostra o carregamento. Também é o que o export faz
//      antes de começar: nenhum pipeline novo durante a exportação.
//
//  Se um pipeline não está no cache e não pode ser compilado, o passe é
//  PULADO e o frame sai sem ele — com um aviso na telemetria. Nunca se desenha
//  com um pipeline inválido, e nunca se finge que o efeito rodou.
// =============================================================================
#pragma once

#include "aurea/render/GPUBackend.hpp"
#include "aurea/core/Result.hpp"

#include <vector>
#include <string>
#include <unordered_map>

namespace aurea {

/// Identidade estrutural de um shader. Duas instâncias com a mesma descrição
/// são o mesmo shader.
struct ShaderKey {
    const char* source = nullptr;
    ShaderStage stage = ShaderStage::Fragment;
    /// Variantes por #define. Ex.: ENABLE_MASK, BLEND_ADD, HAS_LUT.
    u32 variantFlags = 0;

    friend bool operator==(const ShaderKey& a, const ShaderKey& b) noexcept {
        return a.source == b.source && a.stage == b.stage
            && a.variantFlags == b.variantFlags;
    }
};

struct ShaderKeyHash {
    [[nodiscard]] usize operator()(const ShaderKey& k) const noexcept {
        // Mistura simples: ponteiro + estágio + flags. Sem colisão prática
        // porque o número de shaders é pequeno (centenas).
        usize h = reinterpret_cast<usize>(k.source);
        h ^= static_cast<usize>(k.stage) * 0x9E3779B97F4A7C15ull;
        h ^= static_cast<usize>(k.variantFlags) * 0xC2B2AE3D27D4EB4Full;
        return h;
    }
};

/// Identidade estrutural de um pipeline. Todos os campos que mudam o estado
/// compilado entram aqui — e SÓ eles. Um campo que não muda o pipeline na
/// chave produz recompilação inútil; um que muda e não está na chave produz
/// artefato silencioso.
struct PipelineKey {
    /// Distingue compute de gráfico. Sem este campo, um pipeline de compute com
    /// vertex/fragment nulos colidiria com um gráfico de shaders nulos — e os
    /// dois se confundiriam no cache.
    bool isCompute = false;

    ShaderKey vertex{};
    ShaderKey fragment{};
    ShaderKey compute{};
    BlendMode blend = BlendMode::Normal;
    bool depthTest = false;
    bool depthWrite = false;
    bool cullBackFace = false;
    u32  colorAttachmentCount = 1;
    SurfaceFormat colorFormats[4] = {};
    SurfaceFormat depthFormat = SurfaceFormat::Depth24;
    u32  sampleCount = 1;

    friend bool operator==(const PipelineKey& a, const PipelineKey& b) noexcept;
};

struct PipelineKeyHash {
    [[nodiscard]] usize operator()(const PipelineKey& k) const noexcept;
};

/// Descreve um parâmetro que a UI pode mexer sem recompilar o shader.
/// Só entra aqui o que vira uniform buffer. O que vira #define precisa de uma
/// variante nova — e é por isso que a lista de variantes é pequena e fixa.
struct ShaderUniform {
    const char* name = "";
    u32 offset = 0;
    u32 size = 0;
    u32 arrayCount = 1;
};

class ShaderLibrary {
public:
    static constexpr u32 kMaxShaders = 512;
    static constexpr u32 kMaxPipelines = 1024;

    ShaderLibrary() = default;

    [[nodiscard]] Status initialize(GPUBackend& backend) noexcept;
    void shutdown() noexcept;

    // --- Shaders --------------------------------------------------------------

    /// Compila (ou devolve do cache) um shader. `variantFlags` seleciona a
    /// variante; a fonte pode usar `#if AUREA_VARIANT_*` para compilá-la.
    [[nodiscard]] Result<ShaderHandle> get_shader(const ShaderKey& key) noexcept;

    /// Devia o cache de shaders do backend. O handle fica estável — é por isso
    /// que o cache é indexado por chave e não por handle do driver: perder o
    /// dispositivo não invalida o que o motor guardou.
    void invalidate_device_shaders() noexcept;

    // --- Pipelines ------------------------------------------------------------

    /// Compila (ou devolve do cache) um pipeline.
    [[nodiscard]] Result<PipelineHandle> get_pipeline(const PipelineKey& key) noexcept;

    /// Pipeline de composição pronto para o caso mais comum: quad, blend do
    /// modo pedido, alvo RGBA16F. A maioria do frame passa por aqui.
    [[nodiscard]] Result<PipelineHandle> composite_pipeline(BlendMode blend,
                                                            SurfaceFormat targetFormat,
                                                            u32 sampleCount) noexcept;

    /// Pipeline de compute para uma shader de partícula, culling ou redução.
    [[nodiscard]] Result<PipelineHandle> compute_pipeline(const ShaderKey& shader) noexcept;

    // --- Pré-aquecimento ------------------------------------------------------

    /// Compila antecipadamente os pipelines que este conjunto de blends e
    /// formatos vai precisar. Chamado ao abrir o projeto e antes do export.
    /// Devolve quantos foram compilados (o resto já estava em cache).
    u32 prewarm_blend_modes(const BlendMode* modes, u32 count,
                            SurfaceFormat targetFormat, u32 sampleCount) noexcept;

    /// Compila os pipelines dos efeitos de um projeto inteiro. É o que garante
    /// que o export nunca compile nada no meio.
    u32 prewarm_effects(const u32* effectTypeIds, u32 count,
                        SurfaceFormat targetFormat) noexcept;

    // --- Estatísticas ---------------------------------------------------------

    [[nodiscard]] u32 shader_count() const noexcept { return static_cast<u32>(shaders_.size()); }
    [[nodiscard]] u32 pipeline_count() const noexcept { return static_cast<u32>(pipelines_.size()); }
    [[nodiscard]] u32 compile_failures() const noexcept { return compileFailures_; }

    /// Cache hit rate do frame atual, para a telemetria. Uma taxa baixa em
    /// regime estacionário significa que a chave tem campo demais.
    [[nodiscard]] f32 hit_rate() const noexcept {
        const u64 total = hits_ + misses_;
        return total ? static_cast<f32>(static_cast<f64>(hits_) / static_cast<f64>(total)) : 0.0f;
    }
    void reset_frame_stats() noexcept { hits_ = 0; misses_ = 0; }

    /// Erro da última compilação que falhou. A UI mostra isto — um efeito que
    /// não compila precisa aparecer como "não disponível", não como silêncio.
    [[nodiscard]] const std::string& last_error() const noexcept { return lastError_; }

private:
    struct ShaderEntry {
        ShaderKey    key{};
        ShaderHandle handle{};
        bool         valid = false;
    };
    struct PipelineEntry {
        PipelineKey    key{};
        PipelineHandle handle{};
        bool           valid = false;
    };

    GPUBackend* backend_ = nullptr;

    std::vector<ShaderEntry>   shaders_;
    std::vector<PipelineEntry> pipelines_;
    std::unordered_map<ShaderKey, u32, ShaderKeyHash>   shaderIndex_;
    std::unordered_map<PipelineKey, u32, PipelineKeyHash> pipelineIndex_;

    u32 compileFailures_ = 0;
    u64 hits_ = 0;
    u64 misses_ = 0;
    std::string lastError_;
};

} // namespace aurea
