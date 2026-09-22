// =============================================================================
//  Aurea / render / Renderer.hpp
//
//  Compositor + renderer: do modelo (composição num instante) aos pixels.
//
//  DUAS FASES, com o lock do modelo no meio:
//
//   prepare()   SOB o lock. Lê a composição NA ORDEM DO CORE (a ordem da
//               timeline É a ordem de composição — não existe outra lista),
//               avalia transform e parâmetros no instante, pega o frame de
//               vídeo de cada layer no cache da fonte, planeja os efeitos.
//               Tudo o que o frame precisa sai daqui COPIADO num snapshot.
//
//   render()    SEM o lock. Monta o FrameGraph a partir do snapshot, executa na
//               GPU e apresenta. A UI pode editar o projeto enquanto a GPU
//               trabalha, sem corrida.
//
//  O MESMO render serve ao preview (saída no swapchain, resolução adaptativa) e
//  ao export/testes (saída numa textura, resolução cheia). Não existe um
//  segundo renderer para o export.
// =============================================================================
#pragma once

#include "aurea/effects/EffectGraph.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/memory/Arena.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/RenderScheduler.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"
#include "aurea/timeline/Composition.hpp"

#include <unordered_map>
#include <vector>

namespace aurea {

class Project;

/// Pixels de uma imagem importada (RGBA8, sRGB, alfa reto).
struct ImagePixels {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
};

/// Origem da imagem de uma layer neste frame.
struct LayerSource {
    enum class Kind : u8 { None = 0, Video, Image, Solid, Scene3D };
    Kind kind = Kind::None;
    u32  width = 0;          ///< tamanho natural da layer (px)
    u32  height = 0;

    // Vídeo
    FrameRef frame;
    bool     frameExact = false;

    // Imagem
    AssetId  image{};
    const ImagePixels* pixels = nullptr;   ///< só usado no prepare

    // Cor sólida (shape retangular), linear
    Vec4     solid{};

    // Grupo 3D: índice em FrameSnapshot::scenes
    u32      sceneGroup = 0;
};

struct RenderLayer {
    LayerId     id{};
    LayerSource source;
    Mat4        compFromLayer = Mat4::identity();
    f32         opacity = 1.0f;
    BlendMode   blend = BlendMode::Normal;
    f32         texelScale = 1.0f;
};

struct FrameSnapshot {
    u32  compWidth = 0;
    u32  compHeight = 0;
    Vec4 background{0, 0, 0, 1};   ///< linear
    FrameIndex time{0};
    std::vector<RenderLayer> layers;   ///< do fundo para a frente
    std::vector<EffectPlan>  plans;    ///< um por layer (mesmo índice)
    /// Grupos 3D do frame (layers 3D consecutivas da pilha). O RenderLayer do
    /// grupo aponta para cá.
    std::vector<scene3d::SceneFrame> scenes;
    u32  videoLayers = 0;
    u32  staleVideoFrames = 0;         ///< mostrando frame aproximado (scrub)
    u32  missingVideoFrames = 0;       ///< nenhum frame ainda (primeiro decode)
};

struct RenderSettings {
    u32  previewNumerator = 1;
    u32  previewDenominator = 1;
    bool dither = true;
    bool gpuTimers = true;
    /// Cor atrás da composição na área de preview (linear). É o fundo da
    /// marca (#0F141A), para o preview não "piscar" contra a interface.
    Vec4 editorBackground{0.0048f, 0.0070f, 0.0103f, 1.0f};
    f32  viewportZoom = 1.0f;
    Vec2 viewportPan{0.0f, 0.0f};
};

/// Alvo fora da tela (export, testes visuais): a composição é escrita nesta
/// textura (RGBA16F, tamanho da composição × escala).
struct OffscreenTarget {
    TextureHandle texture{};
    u32 width = 0;
    u32 height = 0;

    /// Export: se válidos, a composição também sai em Y'CbCr 4:2:0 8 bits
    /// BT.709 de faixa limitada — Y em R8 (largura × altura) e CbCr em RG8
    /// (metade de cada lado). É o NV12 que o encoder recebe.
    TextureHandle yPlane{};
    TextureHandle uvPlane{};
    bool encodeDither = true;
};

/// Custo medido de um frame, por etapa. GPU vem das timestamp queries (de um
/// frame já concluído); 0 = não medido, nunca "instantâneo".
struct RenderTimings {
    f32 cpuPrepareMs = 0.0f;
    f32 cpuRecordMs = 0.0f;
    f32 acquireWaitMs = 0.0f;   ///< espera por imagem do swapchain / fence
    f32 presentMs = 0.0f;
    f32 gpuTotalMs = 0.0f;
    f32 gpuColorConvMs = 0.0f;
    f32 gpuEffectsMs = 0.0f;
    f32 gpuBlurMs = 0.0f;
    f32 gpuGlowMs = 0.0f;
    f32 gpuCompositeMs = 0.0f;
    f32 gpuOutputMs = 0.0f;
    bool gpuMeasured = false;
};

class Renderer final : public EffectResources {
public:
    Renderer();
    ~Renderer() override;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Cria shaders, samplers e PRÉ-AQUECE todos os pipelines embutidos.
    [[nodiscard]] Status initialize(GPUBackend& backend, const EffectRegistry& effects) noexcept;
    void shutdown() noexcept;
    /// O dispositivo morreu: esquece todos os objetos de GPU sem destruí-los.
    void forget_device() noexcept;
    [[nodiscard]] bool ready() const noexcept { return backend_ != nullptr; }

    /// Fase 1 (sob o lock do modelo).
    void prepare(const Composition& comp, const Project& project, FrameIndex time,
                 MediaManager* media, const ImagePixels* (*imageLookup)(void*, AssetId), void* imageCtx,
                 const RenderSettings& settings, u64 frameNumber, i32 playDirection,
                 DecodeMode decodeMode, f32 speed, FrameSnapshot& out);

    /// Fase 2 (sem lock). `offscreen` nulo = apresenta no swapchain.
    [[nodiscard]] Status render(FrameSnapshot& snapshot, const RenderSettings& settings,
                                const OffscreenTarget* offscreen, FrameStats& stats,
                                RenderTimings& timings) noexcept;

    /// Descarta texturas de imagem e LUTs (projeto fechado).
    void release_project_resources() noexcept;

    /// De onde vêm os modelos 3D (o motor guarda os SceneAsset).
    using ModelLookup = std::shared_ptr<const scene3d::SceneAsset> (*)(void* ctx, AssetId id);
    void set_model_lookup(ModelLookup fn, void* ctx) noexcept { modelLookup_ = fn; modelCtx_ = ctx; }
    [[nodiscard]] const scene3d::SceneStats& scene_stats() const noexcept { return scene3d_.stats(); }
    [[nodiscard]] u64 scene_resident_bytes() const noexcept { return scene3d_.resident_bytes(); }

    // --- EffectResources -----------------------------------------------------
    [[nodiscard]] TextureHandle curve_lut(const CurveData& curve) noexcept override;

    // --- Consultas -------------------------------------------------------------
    [[nodiscard]] const FrameGraph::Stats& graph_stats() const noexcept { return graph_.stats(); }
    [[nodiscard]] const TransientTexturePool::Stats& pool_stats() const noexcept { return pool_.stats(); }
    [[nodiscard]] ShaderLibrary& shaders() noexcept { return shaders_; }
    [[nodiscard]] u32 pipelines_prewarmed() const noexcept { return prewarmed_; }
    [[nodiscard]] bool last_frame_zero_copy() const noexcept { return lastZeroCopy_; }
    [[nodiscard]] std::string graph_dump() const { return graph_.dump(); }
    [[nodiscard]] u32 frames_rendered() const noexcept { return framesRendered_; }

private:
    struct CompositeDraw {
        FGTexture texture{};
        Rect      region{};
        Mat4      compFromLayer = Mat4::identity();
        f32       opacity = 1.0f;
        BlendMode blend = BlendMode::Normal;
        /// Borda transparente (antisserrilhado de layer girada) para texturas
        /// de imagem; repetição para o sólido de 1x1, que a borda apagaria.
        u64       sampler = 0;
    };

    /// Textura persistente de planos de vídeo (fallback sem zero-copy): uma
    /// por layer, reaproveitada frame a frame.
    struct PlanarTextures {
        TextureHandle plane[3]{};
        u32 width = 0, height = 0;
        PixelFormat format = PixelFormat::Unknown;
        u64 lastFrame = 0;
    };

    struct ImageTexture {
        TextureHandle texture{};
        u32 width = 0, height = 0;
        u64 lastFrame = 0;
    };

    struct LutTexture {
        TextureHandle texture{};
        u64 lastFrame = 0;
    };

    struct PendingUpload {
        TextureHandle texture{};
        std::vector<u8> data;
        u32 bytesPerRow = 0;
    };

    void fill_scene_context(const Composition& comp, FrameIndex time, FrameSnapshot& out) const noexcept;
    [[nodiscard]] bool build_source(const RenderLayer& layer, u32 layerIndex, bool hasEffects,
                                    LayerImage& out, std::vector<FrameRef>& framesUsed,
                                    u64 frameNumber) noexcept;
    [[nodiscard]] bool build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                          FGTexture target, u64 frameNumber) noexcept;
    void flush_uploads() noexcept;
    void collect_resources(u64 frameNumber) noexcept;
    void read_timings(RenderTimings& t) noexcept;

    GPUBackend* backend_ = nullptr;
    const EffectRegistry* effects_ = nullptr;
    ShaderLibrary shaders_;
    scene3d::SceneRenderer scene3d_;
    ModelLookup modelLookup_ = nullptr;
    void* modelCtx_ = nullptr;
    FrameGraph graph_;
    TransientTexturePool pool_;
    Arena arena_{64 * 1024};

    std::vector<CompositeDraw> draws_;
    std::vector<FrameRef> framesInFlight_;
    std::unordered_map<u64, PlanarTextures> planar_;   ///< por LayerId empacotado
    std::unordered_map<u64, ImageTexture> images_;     ///< por AssetId empacotado
    std::unordered_map<u64, LutTexture> luts_;         ///< por hash da curva
    std::vector<PendingUpload> uploads_;
    std::vector<GpuTiming> timingScratch_;

    const std::vector<scene3d::SceneFrame>* currentScenes_ = nullptr;
    u32 compTargetW_ = 0, compTargetH_ = 0;
    u32 prewarmed_ = 0;
    u32 framesRendered_ = 0;
    u64 frameNumber_ = 0;
    bool lastZeroCopy_ = false;
};

} // namespace aurea
