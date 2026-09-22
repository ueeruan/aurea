// =============================================================================
//  Aurea / bridge / BridgePods.hpp
//
//  O CONTRATO DE MEMÓRIA entre o motor e a UI nativa.
//
//  Por que este arquivo existe, em vez de a UI ler os structs internos:
//
//  `Layer`, `EngineStatus` e companhia são estruturas do MOTOR. Elas mudam
//  quando o motor precisa — um campo novo em `EngineStatus` para a telemetria,
//  um alinhamento diferente num compilador. Se a UI lesse essas structs por
//  offset, cada mudança interna quebraria a UI sem aviso: os campos sairiam
//  deslocados e a tela mostraria números errados, sem crash e sem erro de
//  compilação. É a pior categoria de bug possível.
//
//  Aqui os tipos da fronteira são SEPARADOS dos internos e congelados. O bridge
//  copia de um para o outro campo a campo. Isso custa algumas dezenas de
//  instruções por frame — irrelevante — e em troca:
//
//    - mudar um struct interno NÃO afeta a UI;
//    - cada offset tem um `static_assert`, então um campo novo fora de lugar
//      quebra a COMPILAÇÃO, no motor, e não o app em produção;
//    - o arquivo é a documentação: quem lê sabe exatamente o que atravessa.
//
//  Todos os tipos são `trivially_copyable` e têm tamanho explícito. A ordem dos
//  campos é a ordem na memória, sem reordenação do compilador.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cstddef>
#include <type_traits>

namespace aurea::bridge {

// -----------------------------------------------------------------------------
// Command — o mesmo layout que Command.hpp usa, congelado aqui.
//
// `Command` já é POD de propósito (ver command/Command.hpp), então o bridge o
// repassa direto. Este bloco só fixa os offsets que a UI usa para ESCREVER.
// -----------------------------------------------------------------------------
namespace command_layout {
    inline constexpr usize kSize          = 128;
    inline constexpr usize kOffsetType    = 0;    // u16
    inline constexpr usize kOffsetStrOff  = 4;    // u32
    inline constexpr usize kOffsetStrLen  = 8;    // u32
    inline constexpr usize kOffsetPayload = 16;   // union, 64 bytes
    inline constexpr usize kOffsetCorrId  = 80;   // u64
    inline constexpr usize kOffsetReserved = 88;  // u64[5]
    inline constexpr usize kPayloadSize   = 64;
}

// -----------------------------------------------------------------------------
// LayerRow — uma linha da lista de camadas.
//
// A UI desenha a timeline a partir de N destes numa única travessia. Enviar um
// objeto por camada seria N alocações por frame no heap gerenciado, e o coletor
// cobraria a conta durante o playback.
// -----------------------------------------------------------------------------
struct LayerRow {
    u64 id            = 0;    // +0   handle empacotado da camada
    u32 kind          = 0;    // +8
    u32 zIndex        = 0;    // +12  posição vertical (0 = frente)
    i32 startFrame    = 0;    // +16
    i32 endFrame      = 0;    // +20
    f32 opacity       = 1.0f; // +24
    u32 flags         = 0;    // +28
    u32 effectCount   = 0;    // +32
    u32 maskCount     = 0;    // +36
    u32 keyframeCount = 0;    // +40
    u32 nameOffset    = 0;    // +44  offset no blob de nomes
    u32 nameLength    = 0;    // +48
    u32 blendMode     = 0;    // +52
    u32 parentIndex   = 0;    // +56  kInvalidIndex = sem pai
    i32 offsetFrames  = 0;    // +60  deslocamento do conteúdo (in-point); keyframe na timeline = t + start - offset
};

inline constexpr u32 kLayerRowFlagVisible  = 1u << 0;
inline constexpr u32 kLayerRowFlagLocked   = 1u << 1;
inline constexpr u32 kLayerRowFlagSolo     = 1u << 2;
inline constexpr u32 kLayerRowFlagAnimated = 1u << 3;
inline constexpr u32 kLayerRowFlagSelected = 1u << 4;
inline constexpr u32 kLayerRowFlagThreeD   = 1u << 5;
inline constexpr u32 kLayerRowFlagAdjustment = 1u << 6;   ///< camada de ajuste
inline constexpr u32 kLayerRowFlagGuide    = 1u << 7;     ///< guia (não exporta)
/// Etiqueta de cor (0 = nenhuma) nos bits 8..11.
inline constexpr u32 kLayerRowLabelShift   = 8;
inline constexpr u32 kLayerRowLabelMask    = 0xFu << kLayerRowLabelShift;

static_assert(sizeof(LayerRow) == 64, "LayerRow e contrato de ABI com a UI");
static_assert(offsetof(LayerRow, id) == 0);
static_assert(offsetof(LayerRow, kind) == 8);
static_assert(offsetof(LayerRow, zIndex) == 12);
static_assert(offsetof(LayerRow, startFrame) == 16);
static_assert(offsetof(LayerRow, endFrame) == 20);
static_assert(offsetof(LayerRow, opacity) == 24);
static_assert(offsetof(LayerRow, flags) == 28);
static_assert(offsetof(LayerRow, effectCount) == 32);
static_assert(offsetof(LayerRow, maskCount) == 36);
static_assert(offsetof(LayerRow, keyframeCount) == 40);
static_assert(offsetof(LayerRow, nameOffset) == 44);
static_assert(offsetof(LayerRow, nameLength) == 48);
static_assert(offsetof(LayerRow, blendMode) == 52);
static_assert(offsetof(LayerRow, parentIndex) == 56);

// -----------------------------------------------------------------------------
// KeyframeRow — um keyframe na barra da timeline.
// -----------------------------------------------------------------------------
struct KeyframeRow {
    u32 property      = 0;    // +0
    u32 effectIndex   = 0;    // +4   id do efeito; kInvalidIndex quando não é de efeito
    i32 time          = 0;    // +8   tempo LOCAL da layer (timeline = time + start - offset)
    f32 value         = 0.0f; // +12
    u32 interpolation = 0;    // +16
    u32 paramIndex    = 0;    // +20  parâmetro de efeito: param*4 + componente
};

/// Detalhe de UMA camada para o inspetor: transform avaliado no playhead (a
/// animação já aplicada), o que está animado e o que tem keyframe exatamente
/// no playhead (estado do botão de losango). A UI não guarda cópia disso:
/// relê a cada mudança.
struct LayerDetailPOD {
    u64 id                = 0;    // +0
    u32 kind              = 0;    // +8
    u32 flags             = 0;    // +12  kLayerRowFlag*
    i32 startFrame        = 0;    // +16
    i32 endFrame          = 0;    // +20
    i32 offsetFrames      = 0;    // +24  deslocamento do conteúdo (in-point)
    u32 blendMode         = 0;    // +28
    f32 position[3]{};            // +32
    f32 scale[3]{};               // +44
    f32 rotation[3]{};            // +56  graus
    f32 anchor[3]{};              // +68
    f32 opacity           = 1.0f; // +80
    f32 skew[2]{};                // +84
    u32 animatedMask      = 0;    // +92  bit n = TrackProperty n (0..14) tem keyframes
    u32 keyAtPlayheadMask = 0;    // +96  bit n = keyframe exatamente no playhead
    u32 sourceWidth       = 0;    // +100
    u32 sourceHeight      = 0;    // +104
    f32 sourceFps         = 0.0f; // +108
    i32 sourceFrames      = 0;    // +112 duração da mídia em frames da composição (0 = sem limite)
    u32 effectCount       = 0;    // +116
    u32 maskCount         = 0;    // +120
    i32 localPlayhead     = 0;    // +124 playhead no tempo local da layer
    u64 parentId          = 0;    // +128 0 = sem pai
    // Áudio (vídeo com trilha e camada de áudio)
    f32 audioGain         = 1.0f; // +136 ganho do clipe, linear
    f32 audioVolume       = 1.0f; // +140 volume no playhead, linear (1 = 100%)
    f32 audioPan          = 0.0f; // +144 balanço −1..1
    i32 audioFadeIn       = 0;    // +148 frames
    i32 audioFadeOut      = 0;    // +152 frames
    u32 audioFlags        = 0;    // +156 kAudioFlag*
    f32 speed             = 1.0f; // +160 velocidade do conteúdo (0 = congelado)
    u32 timeFlags         = 0;    // +164 bit0 = reverso
    u32 shapeTypePoints   = 0;    // +168 tipo | pontas << 16
    u32 shapeFill         = 0;    // +172 RGBA8 sRGB
    u32 shapeStroke       = 0;    // +176 RGBA8 sRGB
    f32 shapeStrokeWidth  = 0.0f; // +180
    f32 shapeCorner       = 0.0f; // +184
    f32 shapeInner        = 0.0f; // +188
    /// Cantos TL, TR, BR, BL da caixa da camada em px da composição, com a
    /// cadeia de pais e a perspectiva da câmera — o MESMO cálculo do renderer.
    f32 corners[8]        = {};   // +192
    /// Pai → composição, afim 2D (a b c d tx ty: x' = a·x + c·y + tx,
    /// y' = b·x + d·y + ty). Identidade sem pai. Arrastar no palco converte o
    /// delta da tela pelo inverso disto.
    f32 parentAffine[6]   = {1, 0, 0, 1, 0, 0}; // +224
    u32 geomFlags         = 0;    // +248 bit0 cantos válidos, bit1 perspectiva
    u32 reserved0         = 0;    // +252
};
inline constexpr u32 kGeomCornersValid = 1u << 0;
inline constexpr u32 kGeomPerspective = 1u << 1;
inline constexpr u32 kAudioFlagMuted = 1u << 0;
inline constexpr u32 kAudioFlagSolo = 1u << 1;
inline constexpr u32 kAudioFlagHasAudio = 1u << 2;
inline constexpr u32 kAudioFlagVolumeAnimated = 1u << 3;
static_assert(sizeof(LayerDetailPOD) == 256, "LayerDetailPOD e contrato de ABI");
static_assert(offsetof(LayerDetailPOD, corners) == 192);
static_assert(offsetof(LayerDetailPOD, geomFlags) == 248);
static_assert(offsetof(LayerDetailPOD, position) == 32);
static_assert(offsetof(LayerDetailPOD, animatedMask) == 92);
static_assert(offsetof(LayerDetailPOD, parentId) == 128);
static_assert(offsetof(LayerDetailPOD, audioGain) == 136);
static_assert(offsetof(LayerDetailPOD, audioFlags) == 156);

static_assert(sizeof(KeyframeRow) == 24, "KeyframeRow e contrato de ABI com a UI");
static_assert(offsetof(KeyframeRow, property) == 0);
static_assert(offsetof(KeyframeRow, effectIndex) == 4);
static_assert(offsetof(KeyframeRow, time) == 8);
static_assert(offsetof(KeyframeRow, value) == 12);
static_assert(offsetof(KeyframeRow, interpolation) == 16);

// -----------------------------------------------------------------------------
// EngineStatus — o que a UI lê a cada frame.
//
// Ordenado por frequência de uso: os campos que a UI lê para desenhar
// (playhead, preview, contadores) ficam no começo, na primeira linha de cache.
// -----------------------------------------------------------------------------
struct EngineStatusPOD {
    i32 state            = 0;     // +0
    i32 lastError        = 0;     // +4
    char errorDetail[96]{};       // +8
    u32 modelRevision    = 0;     // +104 muda a cada alteração do modelo (a UI relê listas)
    u32 reservedRev      = 0;     // +108
    f32 compFps          = 0.0f;  // +112 composição atual (a UI converte frame ↔ tempo)
    u32 compWidth        = 0;     // +116
    u32 compHeight       = 0;     // +120
    u32 thumbnailGeneration = 0;  // +124 muda quando uma miniatura nova fica pronta
    f32 currentFps       = 0.0f;  // +128
    f32 averageFrameMs   = 0.0f;  // +132
    f32 gpuMs            = 0.0f;  // +136
    f32 cpuMs            = 0.0f;  // +140
    f32 decodeMs         = 0.0f;  // +144
    f32 cacheHitRate     = 0.0f;  // +148
    f32 memoryPressure   = 0.0f;  // +152
    u32 previewWidth     = 0;     // +156
    u32 previewHeight    = 0;     // +160
    u32 previewNumerator = 1;     // +164
    u32 previewDenominator = 1;   // +168
    u32 previewAuto      = 1;     // +172  (u32 e não bool: bool tem tamanho
                                  //         dependente de ABI em algumas
                                  //         plataformas, e aqui o tamanho é
                                  //         contrato)
    i64 playhead         = 0;     // +176
    i64 duration         = 0;     // +184
    u32 playing          = 0;     // +192
    u32 layerCount       = 0;     // +196
    u32 selectedCount    = 0;     // +200
    u32 canUndo          = 0;     // +204
    u32 canRedo          = 0;     // +208
    u32 undoDepth        = 0;     // +212
    u32 assetCount       = 0;     // +216
    u32 dirty            = 0;     // +220
    u32 recoveryAvailable = 0;    // +224
    u32 droppedFrames    = 0;     // +228
    u32 passesExecuted   = 0;     // +232
    u32 passesCulled     = 0;     // +236
    u64 gpuMemoryBytes   = 0;     // +240
    u64 cpuMemoryBytes   = 0;     // +248
};
// 256 bytes: quatro linhas de cache. Redondo de propósito — a struct é copiada
// inteira a cada frame e um tamanho não múltiplo do cache desperdiça banda.

static_assert(sizeof(EngineStatusPOD) == 256, "EngineStatusPOD e contrato de ABI");
static_assert(offsetof(EngineStatusPOD, state) == 0);
static_assert(offsetof(EngineStatusPOD, compFps) == 112);
static_assert(offsetof(EngineStatusPOD, currentFps) == 128);
static_assert(offsetof(EngineStatusPOD, previewWidth) == 156);
static_assert(offsetof(EngineStatusPOD, playing) == 192);
static_assert(offsetof(EngineStatusPOD, playhead) == 176);
static_assert(offsetof(EngineStatusPOD, gpuMemoryBytes) == 240);

// -----------------------------------------------------------------------------
// ExportProgress
// -----------------------------------------------------------------------------
struct ExportProgressPOD {
    u32 running     = 0;      // +0
    u32 finished    = 0;      // +4
    i32 result      = 0;      // +8
    u32 framesTotal = 0;      // +12
    u32 framesDone  = 0;      // +16
    f32 fps         = 0.0f;   // +20
    u32 etaSeconds  = 0;      // +24
    u32 reserved    = 0;      // +28
    char message[96]{};       // +32
};

static_assert(sizeof(ExportProgressPOD) == 128, "ExportProgressPOD e contrato de ABI");
static_assert(offsetof(ExportProgressPOD, running) == 0);
static_assert(offsetof(ExportProgressPOD, result) == 8);
static_assert(offsetof(ExportProgressPOD, message) == 32);

// -----------------------------------------------------------------------------
// EngineTelemetry — painel de debug.
// -----------------------------------------------------------------------------
struct TelemetryPOD {
    f32 frameMs        = 0.0f;   // +0
    f32 gpuMs          = 0.0f;   // +4
    f32 cpuMs          = 0.0f;   // +8
    f32 decodeMs       = 0.0f;   // +12
    f32 frameCacheHit  = 0.0f;   // +16
    f32 pipelineHit    = 0.0f;   // +20
    u32 workerCount    = 0;      // +24
    u32 passesExecuted = 0;      // +28
    u32 passesCulled   = 0;      // +32
    u32 drawCalls      = 0;      // +36
    u32 triangles      = 0;      // +40
    u32 particles      = 0;      // +44
    u32 shaderCount    = 0;      // +48
    u32 pipelineCount  = 0;      // +52
    u32 shaderFailures = 0;      // +56
    u32 activeEffects  = 0;      // +60
    u32 activeLayers   = 0;      // +64
    u32 adaptiveChanges = 0;     // +68
    u32 physicalResources = 0;   // +72
    u32 logicalResources  = 0;   // +76
    f32 thermal        = 0.0f;   // +80
    u32 throttling     = 0;      // +84
    u64 gpuMemoryBytes = 0;      // +88
    u64 cpuMemoryBytes = 0;      // +96
    u64 undoBlobBytes  = 0;      // +104
    u64 commandsDropped = 0;     // +112
    u32 framesInFlight = 0;      // +120
    u32 reserved       = 0;      // +124
};

static_assert(sizeof(TelemetryPOD) == 128, "TelemetryPOD e contrato de ABI");
static_assert(offsetof(TelemetryPOD, frameMs) == 0);
static_assert(offsetof(TelemetryPOD, workerCount) == 24);
static_assert(offsetof(TelemetryPOD, gpuMemoryBytes) == 88);

// -----------------------------------------------------------------------------
// PerfPOD — o painel DEV de performance.
//
// Tudo que o painel mostra sai daqui, medido: GPU por timestamp query (0 =
// "não medido", nunca "instantâneo"), decode pela thread de decode, frames
// perdidos pelo FrameScheduler. Nada é estimado para parecer bonito.
// -----------------------------------------------------------------------------
struct PerfPOD {
    f32 previewFps          = 0.0f;  // +0   frames apresentados por segundo
    f32 cpuFrameMs          = 0.0f;  // +4   preparo + gravação
    f32 gpuFrameMs          = 0.0f;  // +8
    f32 decodeMs            = 0.0f;  // +12  por frame, thread de decode
    f32 colorConvMs         = 0.0f;  // +16  GPU: YUV → linear
    f32 effectsMs           = 0.0f;  // +20  GPU: todos os efeitos
    f32 blurMs              = 0.0f;  // +24
    f32 glowMs              = 0.0f;  // +28
    f32 compositeMs         = 0.0f;  // +32
    f32 outputMs            = 0.0f;  // +36  GPU: passe de saída
    f32 presentMs           = 0.0f;  // +40  CPU: submissão + apresentação
    f32 acquireMs           = 0.0f;  // +44  espera por imagem/fence
    f32 lastSeekMs          = 0.0f;  // +48  do pedido ao frame pronto
    f32 frameBudgetMs       = 0.0f;  // +52
    u32 droppedFrames       = 0;     // +56  total da sessão
    u32 droppedRecent       = 0;     // +60  último segundo
    u32 renderScaleNum      = 1;     // +64
    u32 renderScaleDen      = 1;     // +68
    u32 renderAuto          = 1;     // +72
    u32 previewWidth        = 0;     // +76
    u32 previewHeight       = 0;     // +80
    u32 decodedCacheFrames  = 0;     // +84
    u64 decodedCacheBytes   = 0;     // +88
    u64 ramBytes            = 0;     // +96
    u64 gpuMemoryBytes      = 0;     // +104
    u64 transientBytes      = 0;     // +112
    u32 passesExecuted      = 0;     // +120
    u32 passesCulled        = 0;     // +124
    u32 texturesCreated     = 0;     // +128  neste frame (o certo em regime é 0)
    u32 transientTextures   = 0;     // +132
    u32 physicalTextures    = 0;     // +136
    u32 aliasedTextures     = 0;     // +140
    u32 pipelineCompilesLive = 0;    // +144  criados durante o playback (o certo é 0)
    u32 pipelinesTotal      = 0;     // +148
    u32 zeroCopy            = 0;     // +152
    u32 hardwareDecoder     = 0;     // +156
    u32 gpuTimers           = 0;     // +160
    u32 seeks               = 0;     // +164
    u32 coalesced           = 0;     // +168
    u32 staleFrames         = 0;     // +172
    u32 layersRendered      = 0;     // +176
    u32 thermal             = 0;     // +180
    char decoder[48]{};              // +184
    char gpuName[24]{};              // +232
    // --- Fase 8A (HUD): só contadores que o motor já tem na mão -------------
    // Ritmo (§145): intervalo entre quadros APRESENTADOS tocando, percentis
    // da última janela de 1 s. 0 amostras = não tocou (os campos não valem).
    f32 pacingP50Ms         = 0.0f;  // +256
    f32 pacingP95Ms         = 0.0f;  // +260
    f32 pacingP99Ms         = 0.0f;  // +264
    f32 pacingStdMs         = 0.0f;  // +268 desvio padrão (variância = ²)
    u32 pacingSamples       = 0;     // +272
    f32 cpuPrepareMs        = 0.0f;  // +276 parte do cpuFrameMs sob o lock
    f32 cpuRecordMs         = 0.0f;  // +280 gravação do FrameGraph
    u32 drawCalls           = 0;     // +284 renderer 2D (camadas + passes)
    u32 draws3D             = 0;     // +288 SceneStats (0 sem cena 3D)
    u32 triangles3D         = 0;     // +292
    u32 culled3D            = 0;     // +296 primitivas fora do frustum
    u32 particles           = 0;     // +300 slots de partícula no quadro
    u32 activeEffects       = 0;     // +304 efeitos vivos (neutros saem do plano)
    u32 flowCacheHits       = 0;     // +308 optical flow (acumulado)
    u32 flowCacheMisses     = 0;     // +312
    u32 maskCacheHits       = 0;     // +316 máscaras rasterizadas (acumulado)
    u32 maskCacheMisses     = 0;     // +320
    u32 audioQueuedMs       = 0;     // +324 anel do mixer (à frente da saída)
    u32 audioOutputMs       = 0;     // +328 buffer da saída da plataforma
    u32 audioUnderruns      = 0;     // +332 total da sessão
    u32 audioMissingBlocks  = 0;     // +336 trechos mixados sem o bloco decodificado
    u32 audioOutputOpen     = 0;     // +340 0 = sem saída (os campos de áudio não valem)
    f32 heavyScale          = 1.0f;  // +344 fração das operações caras (calor)
    u32 memoryBudgetMB      = 0;     // +348 orçamento do MemoryManager
    u64 scene3dBytes        = 0;     // +352 geometria + texturas 3D residentes
    u64 gpuReservedBytes    = 0;     // +360 blocos pedidos ao driver
    u32 gpuAllocations      = 0;     // +368
    u32 reserved8a[3]{};             // +372
};
static_assert(sizeof(PerfPOD) == 384, "PerfPOD e contrato de ABI");
static_assert(offsetof(PerfPOD, droppedFrames) == 56);
static_assert(offsetof(PerfPOD, decodedCacheBytes) == 88);
static_assert(offsetof(PerfPOD, passesExecuted) == 120);
static_assert(offsetof(PerfPOD, decoder) == 184);
static_assert(offsetof(PerfPOD, gpuName) == 232);
static_assert(offsetof(PerfPOD, pacingP50Ms) == 256);
static_assert(offsetof(PerfPOD, drawCalls) == 284);
static_assert(offsetof(PerfPOD, audioQueuedMs) == 324);
static_assert(offsetof(PerfPOD, scene3dBytes) == 352);
static_assert(offsetof(PerfPOD, gpuAllocations) == 368);

// -----------------------------------------------------------------------------
// Catálogo de efeitos (menu "adicionar efeito"). Nomes no blob de strings.
// -----------------------------------------------------------------------------
struct EffectCatalogRow {
    u32 typeId          = 0;   // +0   id estável (hash da chave)
    u32 effectClass     = 0;   // +4
    u32 paramCount      = 0;   // +8
    u32 nameOffset      = 0;   // +12
    u32 nameLength      = 0;   // +16
    u32 categoryOffset  = 0;   // +20
    u32 categoryLength  = 0;   // +24
    u32 reserved        = 0;   // +28
};
static_assert(sizeof(EffectCatalogRow) == 32, "EffectCatalogRow e contrato de ABI");

/// Um efeito aplicado numa layer, na ordem de aplicação.
struct LayerEffectRow {
    u32 effectId        = 0;   // +0   id local à layer (chave dos keyframes)
    u32 typeId          = 0;   // +4
    u32 enabled         = 0;   // +8
    u32 paramCount      = 0;   // +12
    u32 nameOffset      = 0;   // +16
    u32 nameLength      = 0;   // +20
    u32 known           = 0;   // +24  0 = tipo que esta versão não conhece
    u32 reserved        = 0;   // +28
};
static_assert(sizeof(LayerEffectRow) == 32, "LayerEffectRow e contrato de ABI");

/// Um parâmetro de um efeito aplicado: declaração + valor atual.
struct EffectParamRow {
    u32 index           = 0;    // +0
    u32 type            = 0;    // +4   ParamType
    u32 flags           = 0;    // +8
    u32 enumCount       = 0;    // +12
    f32 minValue        = 0.0f; // +16
    f32 maxValue        = 0.0f; // +20
    f32 value[4]{};             // +24  valor no instante do playhead
    f32 defaultValue[4]{};      // +40
    u32 labelOffset     = 0;    // +56
    u32 labelLength     = 0;    // +60
    u32 unitOffset      = 0;    // +64
    u32 unitLength      = 0;    // +68
    u32 enumOffset      = 0;    // +72  rótulos separados por '|'
    u32 enumLength      = 0;    // +76
    u32 animated        = 0;    // +80  tem keyframe
    u32 reserved[3]{};          // +84
};
static_assert(sizeof(EffectParamRow) == 96, "EffectParamRow e contrato de ABI");
static_assert(offsetof(EffectParamRow, value) == 24);
static_assert(offsetof(EffectParamRow, labelOffset) == 56);
static_assert(offsetof(EffectParamRow, animated) == 80);

// -----------------------------------------------------------------------------
// Trava final: tudo que atravessa a fronteira precisa ser copiável byte a byte.
// Um tipo com destrutor ou ponteiro aqui seria um desastre silencioso — o JNI
// copiaria o ponteiro e não o conteúdo.
// -----------------------------------------------------------------------------
static_assert(std::is_trivially_copyable_v<LayerRow>);
static_assert(std::is_trivially_copyable_v<KeyframeRow>);
static_assert(std::is_trivially_copyable_v<EngineStatusPOD>);
static_assert(std::is_trivially_copyable_v<ExportProgressPOD>);
static_assert(std::is_trivially_copyable_v<TelemetryPOD>);
static_assert(std::is_trivially_copyable_v<PerfPOD>);
static_assert(std::is_trivially_copyable_v<EffectCatalogRow>);
static_assert(std::is_trivially_copyable_v<LayerEffectRow>);
static_assert(std::is_trivially_copyable_v<EffectParamRow>);

// O motor guarda estes tamanhos para reservar os buffers diretos do lado Kotlin.
inline constexpr usize kCommandSizeBytes     = command_layout::kSize;
inline constexpr usize kLayerRowSizeBytes    = sizeof(LayerRow);
inline constexpr usize kKeyframeRowSizeBytes = sizeof(KeyframeRow);
inline constexpr usize kStatusSizeBytes      = sizeof(EngineStatusPOD);
inline constexpr usize kTelemetrySizeBytes   = sizeof(TelemetryPOD);
inline constexpr usize kExportProgressSizeBytes = sizeof(ExportProgressPOD);

} // namespace aurea::bridge
