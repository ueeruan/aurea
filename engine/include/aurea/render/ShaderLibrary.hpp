// =============================================================================
//  Aurea / render / ShaderLibrary.hpp
//
//  ShaderCache + PipelineCache + SamplerCache do motor.
//
//  O problema que isto resolve: criar um pipeline custa de 5 a 200 ms. Se isso
//  acontece na primeira vez que um efeito aparece DURANTE o playback, o vídeo
//  congela por um terço de segundo. Três defesas:
//
//   1. SPIR-V PRONTO. Os shaders chegam compilados do build (ShaderIds.hpp) e
//      os módulos são criados uma vez, na inicialização.
//
//   2. CHAVE ESTRUTURAL. O pipeline é identificado por (shaders, blend, formato,
//      sampler imutável) — duas layers com o mesmo efeito dividem o pipeline.
//
//   3. PRÉ-AQUECIMENTO EM DUAS ETAPAS (Fase 8I, §60–62). Na abertura, só os
//      pipelines que TODO projeto usa (composição, vídeo, forma, vetor, texto,
//      máscara, pilha de cor, saída). Os de efeito e de 3D ficam para quando
//      o projeto aberto os tem: o renderer varre a composição quando ela muda
//      e compila o que falta antes do quadro seguinte — fora do playback
//      contínuo, que é onde um pipeline novo congela a imagem. Todo efeito
//      declara os pipelines que usa (Effect::pipelines). O cache de pipeline
//      do driver é persistido em disco pelo backend (com versão, conferência
//      e descarte do arquivo corrompido), então da segunda execução em diante
//      cada compilação é quase só uma consulta.
//
//  `compiles_since_mark()` conta pipelines criados depois que o playback
//  começou. O número certo é ZERO, e há teste e telemetria para isso.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/render/GPUBackend.hpp"
#include "aurea/shaders/ShaderIds.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace aurea {

/// Layouts de vértice dos pipelines 3D. Poucos e fixos: cada malha cabe num
/// deles, e o número de variantes de pipeline fica previsível.
///
///   fluxo 0  posição   float3                         (12 B)
///   fluxo 1  shading   normal f3, tangente f4, uv0 f2, uv1 f2, cor u8×4 (48 B)
///   fluxo 2  skin      juntas u16×4, pesos unorm16×4  (16 B)
enum class MeshLayout : u8 {
    None = 0,        ///< o shader gera os vértices (quads 2D)
    PositionOnly,    ///< profundidade/sombra: só o fluxo 0
    Static,          ///< fluxos 0 + 1
    Skinned,         ///< fluxos 0 + 1 + 2
    SkinnedPosition, ///< sombra de malha com skin: fluxos 0 + 2
};

[[nodiscard]] VertexLayout vertex_layout_for(MeshLayout layout) noexcept;

inline constexpr u32 kMeshPositionStride = 12;
inline constexpr u32 kMeshShadingStride = 48;
inline constexpr u32 kMeshSkinStride = 16;

struct PipelineKey {
    ShaderId vertex   = ShaderId::Count;
    ShaderId fragment = ShaderId::Count;
    ShaderId compute  = ShaderId::Count;
    bool          blendEnabled = false;
    BlendMode     blend = BlendMode::Normal;
    SurfaceFormat format = SurfaceFormat::RGBA16F;
    Topology      topology = Topology::TriangleList;
    /// Sampler imutável (conversão YCbCr de um formato externo). 0 = nenhum.
    u64           immutableSampler = 0;

    // --- 3D ------------------------------------------------------------------
    MeshLayout    mesh = MeshLayout::None;
    bool          hasDepth = false;
    bool          depthOnly = false;
    bool          depthTest = false;
    bool          depthWrite = false;
    CompareOp     depthCompare = CompareOp::GreaterOrEqual;   ///< Z reverso
    SurfaceFormat depthFormat = SurfaceFormat::Depth32F;
    CullMode      cull = CullMode::None;
    bool          frontFaceCCW = true;
    f32           depthBiasConstant = 0.0f;
    f32           depthBiasSlope = 0.0f;

    [[nodiscard]] bool is_compute() const noexcept { return compute != ShaderId::Count; }

    [[nodiscard]] static PipelineKey graphics(ShaderId vs, ShaderId fs, SurfaceFormat fmt,
                                              bool blendEnabled = false,
                                              BlendMode mode = BlendMode::Normal) noexcept {
        PipelineKey k;
        k.vertex = vs;
        k.fragment = fs;
        k.format = fmt;
        k.blendEnabled = blendEnabled;
        k.blend = mode;
        return k;
    }
    /// Passe de tela cheia (fullscreen.vert + fragment) — o caso de todo efeito.
    [[nodiscard]] static PipelineKey fullscreen(ShaderId fs, SurfaceFormat fmt) noexcept {
        return graphics(ShaderId::common_fullscreen_vert, fs, fmt);
    }
    [[nodiscard]] static PipelineKey compute_shader(ShaderId cs) noexcept {
        PipelineKey k;
        k.compute = cs;
        return k;
    }

    friend bool operator==(const PipelineKey&, const PipelineKey&) noexcept = default;
};

struct PipelineKeyHash {
    [[nodiscard]] usize operator()(const PipelineKey& k) const noexcept;
};

/// Samplers que todo passe usa. Criados uma vez.
enum class CommonSampler : u8 {
    LinearClamp = 0,   ///< o padrão: bilinear, borda repetida
    LinearBorder,      ///< bilinear, fora da imagem = transparente
    NearestClamp,
    LinearRepeat,
    LinearMirror,
    Count,
};

class ShaderLibrary {
public:
    ShaderLibrary() = default;
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    /// Cria os módulos de shader e os samplers comuns.
    [[nodiscard]] Status initialize(GPUBackend& backend) noexcept;
    /// Destrói tudo (encerramento normal).
    void shutdown() noexcept;
    /// O dispositivo morreu e levou os objetos: esquece os handles sem
    /// destruí-los. `initialize` recria.
    void forget_device() noexcept;

    [[nodiscard]] bool ready() const noexcept { return backend_ != nullptr; }

    [[nodiscard]] ShaderHandle shader(ShaderId id) const noexcept;
    [[nodiscard]] SamplerHandle sampler(CommonSampler s) const noexcept;

    /// Devolve (ou cria) o pipeline. Falha é registrada e o chamador pula o
    /// passe — nunca se desenha com pipeline inválido.
    [[nodiscard]] Result<PipelineHandle> pipeline(const PipelineKey& key) noexcept;

    /// Compila antecipadamente. Devolve quantos foram criados agora.
    u32 prewarm(const PipelineKey* keys, u32 count) noexcept;

    /// A partir daqui, toda criação de pipeline conta como "durante o
    /// playback" — o painel DEV mostra, e o número certo é zero.
    void mark_steady_state() noexcept { compilesSinceMark_ = 0; steady_ = true; }
    [[nodiscard]] u32 compiles_since_mark() const noexcept { return compilesSinceMark_; }

    [[nodiscard]] u32 pipeline_count() const noexcept { return static_cast<u32>(pipelines_.size()); }
    [[nodiscard]] bool has_pipeline(const PipelineKey& key) const noexcept { return pipelines_.contains(key); }

    /// Impressão digital (FNV-1a 64) de todo o SPIR-V embutido. É a versão do
    /// cache de pipeline em disco: shader mudou (app atualizado) → o arquivo
    /// antigo é descartado em vez de acumular entradas mortas. Calculada uma
    /// vez por processo (~330 KB de SPIR-V, fração de milissegundo).
    [[nodiscard]] static u64 spirv_fingerprint() noexcept;
    [[nodiscard]] u32 compile_failures() const noexcept { return failures_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return lastError_; }

    // --- Recarga de shader (desenvolvimento, desktop) -------------------------
    //
    // Com `AUREA_SHADER_DIR` apontando para a pasta de .spv do build, o motor
    // recarrega o shader alterado e descarta os pipelines que o usavam. Não é
    // usado no Android de produção: lá os shaders são os embutidos.
    u32 reload_changed(const char* spvDirectory) noexcept;

private:
    GPUBackend* backend_ = nullptr;
    ShaderHandle shaders_[kShaderCount]{};
    SamplerHandle samplers_[static_cast<u32>(CommonSampler::Count)]{};
    std::unordered_map<PipelineKey, PipelineHandle, PipelineKeyHash> pipelines_;

    /// SPIR-V recarregado do disco (substitui o embutido enquanto viver).
    std::vector<std::vector<u32>> overrides_;
    std::vector<i64> overrideStamp_;

    u32 failures_ = 0;
    u32 compilesSinceMark_ = 0;
    bool steady_ = false;
    std::string lastError_;
};

} // namespace aurea
