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
    u32 reserved      = 0;    // +60  alinha em 64 e deixa espaço para o próximo campo
};

inline constexpr u32 kLayerRowFlagVisible  = 1u << 0;
inline constexpr u32 kLayerRowFlagLocked   = 1u << 1;
inline constexpr u32 kLayerRowFlagSolo     = 1u << 2;
inline constexpr u32 kLayerRowFlagAnimated = 1u << 3;
inline constexpr u32 kLayerRowFlagSelected = 1u << 4;
inline constexpr u32 kLayerRowFlagThreeD   = 1u << 5;

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
    u32 effectIndex   = 0;    // +4   kInvalidIndex quando não é de efeito
    i32 time          = 0;    // +8
    f32 value         = 0.0f; // +12
    u32 interpolation = 0;    // +16
    u32 reserved      = 0;    // +20
};

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
    char errorDetail[120]{};      // +8
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
// Trava final: tudo que atravessa a fronteira precisa ser copiável byte a byte.
// Um tipo com destrutor ou ponteiro aqui seria um desastre silencioso — o JNI
// copiaria o ponteiro e não o conteúdo.
// -----------------------------------------------------------------------------
static_assert(std::is_trivially_copyable_v<LayerRow>);
static_assert(std::is_trivially_copyable_v<KeyframeRow>);
static_assert(std::is_trivially_copyable_v<EngineStatusPOD>);
static_assert(std::is_trivially_copyable_v<ExportProgressPOD>);
static_assert(std::is_trivially_copyable_v<TelemetryPOD>);

// O motor guarda estes tamanhos para reservar os buffers diretos do lado Kotlin.
inline constexpr usize kCommandSizeBytes     = command_layout::kSize;
inline constexpr usize kLayerRowSizeBytes    = sizeof(LayerRow);
inline constexpr usize kKeyframeRowSizeBytes = sizeof(KeyframeRow);
inline constexpr usize kStatusSizeBytes      = sizeof(EngineStatusPOD);
inline constexpr usize kTelemetrySizeBytes   = sizeof(TelemetryPOD);
inline constexpr usize kExportProgressSizeBytes = sizeof(ExportProgressPOD);

} // namespace aurea::bridge
