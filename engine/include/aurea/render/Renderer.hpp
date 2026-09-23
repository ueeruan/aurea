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
#include "aurea/render/HeavyQuality.hpp"
#include "aurea/render/ParticleScene.hpp"
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
    /// Adjustment: camada de ajuste — sem fonte; os efeitos dela (plano no
    /// mesmo índice) leem a composição acumulada abaixo dela.
    enum class Kind : u8 { None = 0, Video, Image, Solid, Scene3D, Shape, Nested, Particles, Text, Adjustment, Vector };
    Kind kind = Kind::None;

    /// RGB NO TEMPO (Fase 7.3 §25): a MESMA camada, em até três instantes, um
    /// por canal de cor. Vazio (`channelCount == 0`) = o caminho normal, uma
    /// fonte só.
    ///
    /// Mora aqui, e não no acúmulo de amostras do desfoque de movimento,
    /// porque só o `prepare` fala com o decoder — e é o decoder que decide se
    /// o quadro daquele instante existe. O desfoque varia a MATRIZ, o RGB no
    /// tempo varia a FONTE; são coisas diferentes.
    struct ChannelFrame {
        FrameRef frame;
        Vec3     mask{0, 0, 0};
    };
    static constexpr u32 kMaxChannelFrames = 3;
    ChannelFrame channel[kMaxChannelFrames];
    u32 channelCount = 0;
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

    // Partículas (Aurea Particular): o bloco de parâmetros do shader, nº de
    // instâncias e blend. O bloco cresceu de 7 para 20 vec4 quando o sistema
    // deixou de ser três presets fixos e virou sistema parametrizável —
    // emissor, física, rastro, aux e colisão precisam caber.
    static constexpr u32 kParticleBlocks = 20;
    Vec4     particleBlock[kParticleBlocks]{};
    u32      particleSlots = 0;      ///< primárias (o aux multiplica por 1+n)
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
    /// Máscaras (render/MaskRaster.hpp): bloco em FrameSnapshot::maskData a
    /// partir de `maskFirst`, `maskCount` ativas, cobertura inicial e a chave
    /// do bloco (cache da cobertura).
    u32 maskFirst = 0;
    u32 maskCount = 0;
    f32 maskStart = 0.0f;
    u64 maskKey = 0;
    /// Track matte: esta camada aparece através da matte `matteIndex` (índice
    /// neste snapshot; −1 = matte ausente no instante).
    MatteMode matteMode = MatteMode::None;
    u64  matteId = 0;          ///< id (com sal da pré-composição) da matte
    i32  matteIndex = -1;
    /// Usada como matte por outra camada: não desenha por conta própria.
    bool matteOnly = false;
    /// Partículas (8.2): cena 3D, espaço mundo, histórico e desfoque por tempo.
    ParticleSpace particle;
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
    /// Camadas visíveis no instante que ficaram de fora por estarem inteiras
    /// fora da composição (sem decode, sem passe). Fase 8C §20.
    u32  culledLayers = 0;
    /// Pré-composições deste quadro (cada uma com o seu próprio snapshot,
    /// no tempo da fonte da camada). `target` = onde ela foi composta.
    std::vector<std::unique_ptr<FrameSnapshot>> nested;
    FGTexture target{};
    /// Glifos das camadas de texto deste quadro (a camada guarda o trecho).
    std::vector<GlyphInstance> glyphs;
    u32 glyphBase = 0;   ///< onde este snapshot começa no buffer do quadro (render)
    /// Blocos de máscara das camadas deste quadro (cabeçalhos + arestas).
    std::vector<Vec4> maskData;
    u32 maskBase = 0;    ///< onde este snapshot começa no buffer de máscaras (render)
    /// Malhas das camadas vetoriais (vec4) e onde começam no buffer do quadro.
    std::vector<Vec4> vec;
    u32 vecBase = 0;
    /// Histórico das partículas (8.2, render/ParticleScene.hpp) e onde começa.
    std::vector<Vec4> particleData;
    u32 particleBase = 0;
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
    /// Botões do preview AUTO 2.0 (Fase 8C): cada sistema lê o seu pelo
    /// `Renderer::preview_quality()`. O efetivo é o menor entre isto e
    /// `heavyScale`; com `finalQuality` tudo volta a 1 (o export não muda).
    PreviewQuality quality{};
    /// Sistemas pesados (8E) botão a botão. Com `heavyExplicit` falso sai de
    /// `HeavyQuality::from_scale(heavyScale, previewDenominator)`; o export
    /// (finalQuality) ignora os dois e usa `HeavyQuality::full()`.
    HeavyQuality heavy{};
    bool heavyExplicit = false;
};

/// A qualidade dos sistemas pesados que vale para estes ajustes (export = cheia).
[[nodiscard]] HeavyQuality resolve_heavy(const RenderSettings& s) noexcept;

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
    /// Export sobreposto: se válidos, os planos Y e CbCr são copiados para
    /// estes buffers de leitura NO MESMO frame (sem submissão extra nem espera);
    /// quem chama espera o fence do frame e lê do buffer mapeado.
    BufferHandle yReadback{};
    BufferHandle uvReadback{};
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

    /// Pressão de memória do sistema (Fase 8 §13), com o lock de render do
    /// motor. `stage` segue aurea::TrimStage: ≥ 4 solta o cache de render que
    /// não entrou no último quadro (`frameNumber`) — planos de vídeo, flow,
    /// máscara, LUT, malha vetorial e o pool transitório; ≥ 6 solta os
    /// assets 3D sem uso. O quadro na tela não perde nada. Devolve quantas
    /// texturas saíram.
    u32 trim_memory(u8 stage, u64 frameNumber) noexcept;

    /// A PRÉVIA DE UM EFEITO (Fase 7.3 §13–§15): o efeito de verdade, com os
    /// valores PADRÃO da declaração, rodando sobre a cartela de demonstração
    /// (`effects/preview_plate.frag`). Um frame, fora da tela, sem projeto e
    /// sem composição — a prévia de um efeito não depende do que está aberto.
    ///
    /// Sai RGBA8 sRGB de alfa reto, do tamanho pedido. Devolve `NotImplemented`
    /// para efeito que não faz sentido num quadro solto (temporal, global) e
    /// `PipelineCompileFailed` quando o efeito não conseguiu montar os passes —
    /// nos dois casos a UI mostra a cartela genérica.
    /// Foto de base das prévias de efeito (RGBA8 sRGB, alfa reto). Vazia =
    /// a cartela de teste gerada no shader.
    void set_effect_preview_source(std::vector<u8> rgba, u32 width, u32 height) noexcept;
    [[nodiscard]] Status render_effect_preview(const EffectRegistry& effects, EffectTypeId type,
                                               u32 width, u32 height, std::vector<u8>& outRgba) noexcept;

    /// De onde vêm os modelos 3D (o motor guarda os SceneAsset).
    using ModelLookup = std::shared_ptr<const scene3d::SceneAsset> (*)(void* ctx, AssetId id);
    /// O último quadro deixou alguma camada de fora por recurso pendente? (lê e zera)
    [[nodiscard]] bool take_incomplete() noexcept { const bool b = incomplete_; incomplete_ = false; return b; }
    void set_model_lookup(ModelLookup fn, void* ctx) noexcept { modelLookup_ = fn; modelCtx_ = ctx; }
    using HdriLookup = std::shared_ptr<const scene3d::HdriPixels> (*)(void* ctx, AssetId id);
    void set_hdri_lookup(HdriLookup fn, void* ctx) noexcept { hdriLookup_ = fn; hdriCtx_ = ctx; }
    [[nodiscard]] const scene3d::SceneStats& scene_stats() const noexcept { return scene3d_.stats(); }
    [[nodiscard]] u64 scene_resident_bytes() const noexcept { return scene3d_.resident_bytes(); }
    /// Contadores dos sistemas pesados (texto, vetor, máscara, flow, partículas, 3D).
    [[nodiscard]] const HeavyStats& heavy_stats() const noexcept { return heavyStats_; }
    void reset_heavy_stats() noexcept { heavyStats_ = HeavyStats{}; }
    /// A qualidade dos sistemas pesados do último quadro renderizado.
    [[nodiscard]] const HeavyQuality& heavy_quality() const noexcept { return heavyQ_; }
    /// Benchmark A/B do instancing 3D (padrão: ligado).
    void set_scene_instancing(bool on) noexcept { scene3d_.set_instancing(on); }
    /// Orçamento do cache do optical flow (padrão 48 MB; o gerenciador de
    /// memória pode baixar sob pressão). Acima dele sai a camada mais antiga.
    void set_flow_cache_budget(u64 bytes) noexcept { flowCacheBudget_ = bytes; }
    [[nodiscard]] f32 effect_quality() const noexcept override { return heavyQ_.effects; }

    // --- EffectResources -----------------------------------------------------
    [[nodiscard]] TextureHandle curve_lut(const CurveData& curve) noexcept override;

    // --- Consultas -------------------------------------------------------------
    [[nodiscard]] const FrameGraph::Stats& graph_stats() const noexcept { return graph_.stats(); }
    [[nodiscard]] const TransientTexturePool::Stats& pool_stats() const noexcept { return pool_.stats(); }
    [[nodiscard]] ShaderLibrary& shaders() noexcept { return shaders_; }
    [[nodiscard]] u32 pipelines_prewarmed() const noexcept { return prewarmed_; }
    /// Pipelines de efeito/3D compilados pela varredura do projeto (fora do
    /// playback), depois da abertura. Teste de abertura e HUD.
    [[nodiscard]] u32 pipelines_warmed_for_project() const noexcept { return warmedForProject_; }
    [[nodiscard]] bool last_frame_zero_copy() const noexcept { return lastZeroCopy_; }
    [[nodiscard]] std::string graph_dump() const { return graph_.dump(); }
    [[nodiscard]] u32 frames_rendered() const noexcept { return framesRendered_; }
    /// Reduções do preview em vigor no quadro sendo preparado/renderizado
    /// (tudo 1 no export). Os sistemas pesados (3D, partículas, flow, blur)
    /// leem daqui o botão deles.
    [[nodiscard]] const PreviewQuality& preview_quality() const noexcept { return quality_; }
    /// O efetivo de um `RenderSettings` (a mesma regra do prepare/render).
    [[nodiscard]] static PreviewQuality effective_quality(const RenderSettings& s) noexcept;

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
        /// Camada de ajuste: índice do plano de efeitos em `FrameSnapshot::plans`
        /// (a textura só existe no passe, feita do fundo acumulado).
        u32       adjustPlan = kInvalidIndex;
    };
    /// Os desenhos da pilha → alvo: lotes de blend de hardware (Normal, Add)
    /// no mesmo alvo; modo que lê o fundo ou camada de ajuste = ping-pong.
    void composite_draws(FrameSnapshot& snap, FGTexture comp, const TextureDesc& compDesc,
                         const std::vector<CompositeDraw>& draws, EffectBuildContext& ctx) noexcept;

    /// Textura persistente de planos de vídeo (fallback sem zero-copy): uma
    /// por layer, reaproveitada frame a frame.
    struct PlanarTextures {
        TextureHandle plane[3]{};
        u32 width = 0, height = 0;
        PixelFormat format = PixelFormat::Unknown;
        u64 lastFrame = 0;
    };

    // Foto de base das prévias de efeito (sobe na primeira prévia depois de trocada).
    std::vector<u8> previewSrc_;
    u32 previewSrcW_ = 0, previewSrcH_ = 0;
    bool previewSrcDirty_ = false;
    TextureHandle previewSrcTex_{};

    struct ImageTexture {
        TextureHandle texture{};
        u32 width = 0, height = 0;
        u64 lastFrame = 0;
        /// A imagem já no espaço de trabalho (linear, pré-multiplicada) na
        /// densidade da camada: a conversão roda uma vez por tamanho, não a
        /// cada quadro (Fase 8C — eram N passes por quadro com N imagens).
        /// Dois tamanhos: a mesma imagem em duas camadas de escalas diferentes
        /// não se expulsa a cada quadro.
        struct Linear {
            TextureHandle tex{};
            u32 w = 0, h = 0;
            u64 builtFrame = ~0ull;   ///< quadro (do backend) em que o passe foi declarado
            u64 lastFrame = 0;
            FGTexture fg{};           ///< a importação no grafo do quadro `fgFrame`
            u64 fgFrame = ~0ull;
        };
        Linear linear[2];
    };
    void destroy_image_linear(ImageTexture& img) noexcept;
    u32 imageLinearHits_ = 0, imageLinearBuilds_ = 0;
public:
    /// Imagens: conversões reaproveitadas / feitas (testes e HUD).
    void image_cache_stats(u32& hits, u32& builds) const noexcept { hits = imageLinearHits_; builds = imageLinearBuilds_; }
private:

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
    /// Imagem da camada × cobertura das máscaras (cobertura em cache por camada).
    void apply_masks(const RenderLayer& layer, LayerImage& img, u64 frameNumber) noexcept;
    /// O desenho da camada sozinho num alvo do tamanho da composição.
    [[nodiscard]] FGTexture draw_to_comp(const CompositeDraw& d, const TextureDesc& compDesc, f32 compW, f32 compH,
                                         const char* name) noexcept;
    void flush_uploads() noexcept;
    void collect_resources(u64 frameNumber) noexcept;
    void read_timings(RenderTimings& t) noexcept;

    GPUBackend* backend_ = nullptr;
    const EffectRegistry* effects_ = nullptr;
    EffectTypeId posterizeType_ = 0, echoType_ = 0, rgbTimeType_ = 0;
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
    /// Percurso dos snapshots (raiz + pré-composições) nos uploads do quadro:
    /// listas reaproveitadas — eram 6 alocações por quadro (Fase 8C).
    std::vector<FrameSnapshot*> snapAll_, snapStack_;
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
    // Máscaras: o buffer de arestas do quadro (anel) e a cobertura em cache por
    // camada — duas texturas alternadas, como o optical flow.
    BufferHandle maskBuf_[kGlyphRing]{};
    usize maskCap_[kGlyphRing]{};
    u32 maskSlot_ = 0;
    BufferHandle maskFrameBuf_{};
    void upload_masks(FrameSnapshot& snap) noexcept;
    struct MaskCache {
        TextureHandle tex[2]{};
        u64 key[2]{0, 0};
        u32 width = 0, height = 0;
        u32 next = 0;
        u64 lastFrame = 0;
    };
    std::unordered_map<u64, MaskCache> maskCache_;
    u32 maskHits_ = 0, maskMisses_ = 0;
    // Malhas vetoriais: o mesmo anel de buffers do quadro que os glifos.
    BufferHandle vecBuf_[kGlyphRing]{};
    usize vecCap_[kGlyphRing]{};
    u32 vecSlot_ = 0;
    BufferHandle vecFrameBuf_{};
    void upload_vectors(FrameSnapshot& snap) noexcept;
    // Partículas (8.2): o histórico do quadro (binding 16), no mesmo anel.
    BufferHandle particleBuf_[kGlyphRing]{};
    usize particleCap_[kGlyphRing]{};
    u32 particleSlot_ = 0;
    BufferHandle particleFrameBuf_{};
    void upload_particle_history(FrameSnapshot& snap) noexcept;
    /// Push do particles.vert para a subamostra `sub` da camada.
    [[nodiscard]] ParticlePush particle_push(const RenderLayer& layer, u32 sub, f32 weight) const noexcept;
    /// Desenhos das partículas 3D do grupo no passe da cena, com a câmera e o
    /// deslocamento de tempo do (sub)quadro `frame`. Memória da arena.
    [[nodiscard]] u32 scene_particle_draws(const scene3d::SceneFrame& group, const scene3d::SceneFrame& frame,
                                           scene3d::SceneParticleDraw*& out) noexcept;
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
    PreviewQuality quality_{};
    HeavyQuality heavyQ_{};
    HeavyStats heavyStats_{};
    u32 glyphRasterSeen_ = 0, glyphResetSeen_ = 0;   ///< leitura anterior de text::glyph_atlas_stats
    BufferHandle particleQuad_{};                      ///< 6 índices u16 do quad (partículas indexadas)
    /// Orçamento do cache do optical flow (texturas residentes, todas as camadas).
    u64 flowCacheBudget_ = 48ull << 20;
    void trim_flow_cache(u64 keepLayer, u64 incomingBytes) noexcept;
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
    /// Coberturas de máscara reaproveitadas / rasterizadas (testes e HUD).
    void mask_cache_stats(u32& hits, u32& misses) const noexcept { hits = maskHits_; misses = maskMisses_; }
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
    // --- Pré-aquecimento preguiçoso (Fase 8I, §60–62) ------------------------
    // A abertura só compila o que todo projeto usa. Fora do playback, cada
    // quadro preparado varre o projeto (efeitos e 3D de TODAS as camadas, não
    // só as do instante) e junta o que falta; o render compila antes do grafo.
    void scan_for_warmup(const Project& project) noexcept;
    void flush_warmup() noexcept;
    std::vector<PipelineKey> warmPending_;
    std::vector<EffectTypeId> warmedEffects_;   ///< ordenado; tipos já pedidos
    bool warmed3d_ = false;
    u32 warmedForProject_ = 0;
    u32 framesRendered_ = 0;
    u64 frameNumber_ = 0;
    bool lastZeroCopy_ = false;
};

} // namespace aurea
