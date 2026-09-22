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

#include <memory>
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
class Composition;
struct Layer;
/// Matriz composição ← camada no instante, com a cadeia de pais (3D quando a
/// camada ou um pai vive no espaço 3D). O MESMO cálculo do renderer.
[[nodiscard]] Mat4 layer_world_matrix(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
/// Composição ← camada como o renderer desenha: igual a `layer_world_matrix`
/// no 2D; no espaço 3D, já com a câmera e a perspectiva (dividir por w).
[[nodiscard]] Mat4 layer_comp_matrix(const Composition& comp, const Layer& l, FrameIndex time, bool* perspective = nullptr) noexcept;
/// Composição (px) ← mundo 3D: a câmera ativa da composição no instante.
[[nodiscard]] Mat4 comp_view_projection(const Composition& comp, FrameIndex time) noexcept;
/// A camada (ou um pai) vive no espaço 3D no instante? (a mesma regra do render)
[[nodiscard]] bool wants_layer_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
/// Mundo 3D da camada com a cadeia de pais (o mesmo dos modelos/luzes).
[[nodiscard]] Mat4 layer_world_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept;

/// Um glifo na GPU (std430, espelho de shaders/text/glyph.vert).
struct GlyphInstance {
    Vec4 rect;     ///< x0 y0 x1 y1, px da layer
    Vec4 uv;
    Vec4 fill;     ///< linear, alfa
    Vec4 stroke;   ///< linear, alfa
    Mat4 xform = Mat4::identity();   ///< px da layer (Z = profundidade por caractere)
    Vec4 misc;     ///< _, _, k, largura do contorno (px da layer)
    Vec4 extra;    ///< desfoque (px), _, _, _
};
static_assert(sizeof(GlyphInstance) == 160, "layout std430 do glifo");

struct LayerSource {
    enum class Kind : u8 { None = 0, Video, Image, Solid, Scene3D, Shape, Nested, Particles, Text, Vector };
    Kind kind = Kind::None;
    u32  width = 0;          ///< tamanho natural da layer (px)
    u32  height = 0;

    // Vídeo
    FrameRef frame;
    bool     frameExact = false;
    /// Mistura de quadros: o quadro seguinte da fonte e o peso dele (0..1).
    FrameRef frameB;
    f32      blendT = 0.0f;
    u8       blendMode = 0;   ///< 1 mistura, 2 optical flow
    /// Desfoque vetorial: fração do quadro que o obturador cobre (0 = não).
    f32      vectorBlur = 0.0f;

    // Imagem
    AssetId  image{};
    const ImagePixels* pixels = nullptr;   ///< só usado no prepare

    // Cor sólida (shape retangular), linear
    Vec4     solid{};

    // Grupo 3D: índice em FrameSnapshot::scenes
    u32      sceneGroup = 0;

    // Pré-composição: índice em FrameSnapshot::nested
    u32      nestedIndex = 0;

    // Texto (GPU): glifos em FrameSnapshot::glyphs. Com desfoque de movimento
    // por letra, `glyphSets` conjuntos seguidos (um por instante do obturador).
    u32      glyphFirst = 0;
    u32      glyphCount = 0;
    u32      glyphSets = 1;
    Vec4     textPersp{0, 0, 0, 0};   ///< cx, cy da layer, distância focal (px) para o 3D por caractere

    // Partículas: o bloco de parâmetros do shader (7 vec4), nº de slots, blend.
    Vec4     particleBlock[7]{};
    u32      particleSlots = 0;
    bool     particleAdditive = true;

    // Forma vetorial (SDF): tipo, canto, pontas, raio interno, preenchida,
    // cores lineares pré-multiplicadas, largura do contorno (px).
    u32      shapeType = 0;
    Vec4     shapeParams{};      ///< canto, pontas, raio interno, preenchida
    Vec4     shapeFill{};
    Vec4     shapeStroke{};
    f32      shapeStrokeWidth = 0.0f;

    // Camada vetorial: triângulos em FrameSnapshot::vec (2 vec4 por vértice,
    // a partir de `vecFirst`) e as tintas logo depois (`paintFirst`, em vec4).
    u32      vecFirst = 0;
    u32      vecCount = 0;       ///< vértices
    u32      paintFirst = 0;
};

struct RenderLayer {
    LayerId     id{};
    LayerSource source;
    Mat4        compFromLayer = Mat4::identity();
    f32         opacity = 1.0f;
    BlendMode   blend = BlendMode::Normal;
    f32         texelScale = 1.0f;
    /// Desfoque de movimento: composição ← camada em cada amostra do
    /// obturador. Vazio = sem desfoque (ou camada parada no intervalo).
    std::vector<Mat4> blurMatrices;
    /// Amostras temporais genéricas (eco, RGB no tempo): matriz, peso e
    /// máscara de canal (0 = todos). Com elas, o desfoque fica de fora.
    struct TemporalSample { Mat4 m; f32 weight = 1.0f; Vec3 mask{0, 0, 0}; };
    std::vector<TemporalSample> temporal;
    /// Camada 2D no espaço 3D que vive dentro de um grupo de cena (desenhada
    /// com profundidade pelo grupo, não na composição): índice do grupo, ou −1.
    i32 planeGroup = -1;
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
    /// Pré-composições deste quadro (cada uma com o seu próprio snapshot,
    /// no tempo da fonte da camada). `target` = onde ela foi composta.
    std::vector<std::unique_ptr<FrameSnapshot>> nested;
    FGTexture target{};
    /// Glifos das camadas de texto deste quadro (a camada guarda o trecho).
    std::vector<GlyphInstance> glyphs;
    u32 glyphBase = 0;   ///< onde este snapshot começa no buffer do quadro (render)
    /// Malhas das camadas vetoriais (vec4) e onde começam no buffer do quadro.
    std::vector<Vec4> vec;
    u32 vecBase = 0;
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
    /// Export: amostras de desfoque de movimento da qualidade final
    /// (`MotionBlurSettings::samples`); prévia usa `previewSamples`.
    bool finalQuality = false;
    /// Preview sob calor (ThermalManager): fração do custo das operações
    /// caras — resolução do optical flow, partículas, amostras de desfoque.
    /// 1 = completo; ≤ 0,25 também troca o movimento de pixels pela mistura.
    /// O export sempre usa 1.
    f32  heavyScale = 1.0f;
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
    /// O último quadro deixou alguma camada de fora por recurso pendente? (lê e zera)
    [[nodiscard]] bool take_incomplete() noexcept { const bool b = incomplete_; incomplete_ = false; return b; }
    void set_model_lookup(ModelLookup fn, void* ctx) noexcept { modelLookup_ = fn; modelCtx_ = ctx; }
    using HdriLookup = std::shared_ptr<const scene3d::HdriPixels> (*)(void* ctx, AssetId id);
    void set_hdri_lookup(HdriLookup fn, void* ctx) noexcept { hdriLookup_ = fn; hdriCtx_ = ctx; }
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
    /// Camadas do snapshot → alvo (fonte, efeitos, desfoque, composição). As
    /// pré-composições entram antes, cada uma no seu alvo (recursivo).
    void compose_layers(FrameSnapshot& snap, FGTexture comp, const TextureDesc& compDesc, u64 frameNumber,
                        std::vector<CompositeDraw>& draws, u32 depth) noexcept;
    [[nodiscard]] bool build_source(const RenderLayer& layer, u32 layerIndex, bool hasEffects,
                                    LayerImage& out, std::vector<FrameRef>& framesUsed,
                                    u64 frameNumber) noexcept;
    [[nodiscard]] bool build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                          FGTexture target, u64 frameNumber, DecodedFrame* frame) noexcept;
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
    HdriLookup hdriLookup_ = nullptr;
    void* hdriCtx_ = nullptr;
    void* modelCtx_ = nullptr;
    FrameGraph graph_;
    TransientTexturePool pool_;
    Arena arena_{64 * 1024};

    std::vector<CompositeDraw> draws_;
    std::vector<FrameRef> framesInFlight_;
    std::unordered_map<u64, PlanarTextures> planar_;   ///< por LayerId empacotado
    // Texto na GPU: atlas de glifos (R8) e o buffer de glifos do quadro (anel).
    TextureHandle glyphAtlas_{};
    u64 glyphAtlasGen_ = 0;
    static constexpr u32 kGlyphRing = 4;
    BufferHandle glyphBuf_[kGlyphRing]{};
    usize glyphCap_[kGlyphRing]{};
    u32 glyphSlot_ = 0;
    BufferHandle glyphFrameBuf_{};
    void upload_glyphs(FrameSnapshot& snap) noexcept;
    // Malhas vetoriais: o mesmo anel de buffers do quadro que os glifos.
    BufferHandle vecBuf_[kGlyphRing]{};
    usize vecCap_[kGlyphRing]{};
    u32 vecSlot_ = 0;
    BufferHandle vecFrameBuf_{};
    void upload_vectors(FrameSnapshot& snap) noexcept;
    /// Malha vetorial por camada: refeita só quando a chave (grupos avaliados +
    /// densidade) muda — camada parada não retriangula a cada quadro.
    struct VectorCacheEntry { u64 key = 0; u64 lastFrame = 0; std::vector<Vec4> verts, paints; Vec2 min{}, max{}; };
    std::unordered_map<u64, VectorCacheEntry> vectorCache_;
    /// Cache do optical flow por camada: duas texturas alternadas (a que um
    /// quadro em voo lê nunca é a que o próximo escreve), cada uma com o par
    /// de quadros da fonte que a gerou.
    struct FlowCache {
        TextureHandle tex[2]{};
        u64 key[2]{0, 0};
        u32 width = 0, height = 0;
        u32 next = 0;
        u64 lastFrame = 0;
    };
    std::unordered_map<u64, FlowCache> flowCache_;
    u32 flowHits_ = 0, flowMisses_ = 0;
    f32 heavyScale_ = 1.0f;
    /// Planos de cada grupo 3D do snapshot sendo composto (camadas 2D na cena).
    std::vector<std::vector<scene3d::ScenePlane>> groupPlanes_;
    bool flowCacheEnabled_ = true;   ///< do quadro sendo renderizado (RenderSettings::heavyScale)
    [[nodiscard]] FGTexture video_flow(u64 layerKey, u64 pairKey, FGTexture a, FGTexture b, u32 w, u32 h, u32& baseW, u32& baseH,
                                       u64 frameNumber) noexcept;
public:
    /// Acertos/erros do cache do optical flow (testes e HUD).
    void flow_cache_stats(u32& hits, u32& misses) const noexcept { hits = flowHits_; misses = flowMisses_; }
    /// Benchmark: sem cache, todo quadro calcula o fluxo.
    void set_flow_cache_enabled(bool on) noexcept { flowCacheEnabled_ = on; }
private:
    std::unordered_map<u64, ImageTexture> images_;     ///< por AssetId empacotado
    std::unordered_map<u64, LutTexture> luts_;         ///< por hash da curva
    std::vector<PendingUpload> uploads_;
    std::unordered_map<u64, u64> textKeys_;   ///< chave sintética da camada de texto → chave dos pixels
    bool incomplete_ = false;   ///< o último quadro deixou camada de fora (recurso pendente)
    std::vector<GpuTiming> timingScratch_;

    const std::vector<scene3d::SceneFrame>* currentScenes_ = nullptr;
    const FrameSnapshot* currentSnap_ = nullptr;   ///< dono das pré-composições da composição em curso
    u32 prepareDepth_ = 0;                          ///< aninhamento do prepare (guarda de recursão)
    u32 nestSalt_ = 0;                              ///< ≠ 0 dentro de uma pré-composição (ids únicos)
    u32 compTargetW_ = 0, compTargetH_ = 0;
    u32 prewarmed_ = 0;
    u32 framesRendered_ = 0;
    u64 frameNumber_ = 0;
    bool lastZeroCopy_ = false;
};

} // namespace aurea
