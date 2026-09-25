// =============================================================================
//  Aurea / platform / ios / bridge / AureaEngine.h
//
//  A superfície ObjC que o SwiftUI usa. ObjC PURO de propósito: este cabeçalho é
//  lido pelo Swift Bridging Header, que compila como ObjC — um `#include` de
//  C++ aqui dentro contaminaria a compilação do app inteiro.
//
//  REGRAS DESTA FRONTEIRA (as mesmas do JNI, e pelos mesmos motivos):
//
//   1. NENHUM BITMAP atravessa. O preview vai do renderer direto para o
//      CAMetalLayer da view; o Swift nunca monta um UIImage do preview.
//   2. A UI NÃO processa frame nem dita o ritmo. Ela manda comandos (em bloco,
//      uma travessia por frame) e lê estado.
//   3. Nenhum ponteiro do motor cruza. IDs de camada são `long long`.
//
//  A implementação (AureaEngine.mm) é o espelho do AureaEngine.kt + do
//  aurea_jni.cpp do Android: o C++ é a fonte de verdade, aqui só se traduz.
// =============================================================================
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import <simd/simd.h>

NS_ASSUME_NONNULL_BEGIN

#if DEBUG
/// Native media regression used only by the opt-in simulator export probe.
FOUNDATION_EXPORT NSDictionary<NSString*, id>* AureaVerifyVideoDecoder(NSString* path, NSUInteger expectedFrames);
#endif

/// O estado do motor que a UI lê a cada frame. Campos que a UI desenha, mais
/// nada: é o recorte de `bridge::EngineStatusPOD` (BridgePods.hpp) com os nomes
/// que o Swift entende.
typedef struct {
    int32_t  state;                 ///< EngineState (0 = nao inicializado, 1 = pronto, ...)
    int32_t  lastError;             ///< Errc
    uint32_t modelRevision;         ///< muda quando a UI precisa reler as listas
    uint32_t thumbnailGeneration;   ///< muda quando uma miniatura nova fica pronta
    float    compFps;
    uint32_t compWidth;
    uint32_t compHeight;
    float    currentFps;
    float    averageFrameMs;
    float    gpuMs;
    float    cpuMs;
    float    decodeMs;
    uint32_t previewWidth;
    uint32_t previewHeight;
    uint32_t previewNumerator;
    uint32_t previewDenominator;
    uint32_t previewAuto;
    int64_t  playhead;              ///< frame
    int64_t  duration;              ///< frames
    uint32_t playing;
    uint32_t layerCount;
    uint32_t selectedCount;
    uint32_t canUndo;
    uint32_t canRedo;
    uint32_t dirty;
    uint32_t recoveryAvailable;
    uint32_t droppedFrames;
    uint64_t gpuMemoryBytes;
    uint64_t cpuMemoryBytes;
} AureaStatus;

/// Chaves de `-perf` (painel DEV). Documentadas aqui em vez de espalhadas em
/// strings soltas pelo Swift.
extern NSString* const AureaPerfPreviewFps;
extern NSString* const AureaPerfCpuMs;
extern NSString* const AureaPerfGpuMs;
extern NSString* const AureaPerfDecodeMs;
extern NSString* const AureaPerfDrawCalls;
extern NSString* const AureaPerfTriangles;
extern NSString* const AureaPerfParticles;
extern NSString* const AureaPerfPipelines;
extern NSString* const AureaPerfShaders;
extern NSString* const AureaPerfPassesExecuted;
extern NSString* const AureaPerfPassesCulled;
extern NSString* const AureaPerfDroppedFrames;
extern NSString* const AureaPerfGpuMemoryBytes;
extern NSString* const AureaPerfCpuMemoryBytes;
extern NSString* const AureaPerfDecoder;
extern NSString* const AureaPerfGpuName;

/// Chaves de uma linha de camada (`-layers`). Espelham `bridge::LayerRow`.
extern NSString* const AureaLayerId;
extern NSString* const AureaLayerKind;
extern NSString* const AureaLayerName;
extern NSString* const AureaLayerStartFrame;
extern NSString* const AureaLayerEndFrame;
extern NSString* const AureaLayerOffsetFrames;
extern NSString* const AureaLayerOpacity;
extern NSString* const AureaLayerFlags;
extern NSString* const AureaLayerEffectCount;
extern NSString* const AureaLayerMaskCount;
extern NSString* const AureaLayerKeyframeCount;
extern NSString* const AureaLayerBlendMode;
extern NSString* const AureaLayerSelected;
extern NSString* const AureaLayerVisible;
extern NSString* const AureaLayerLocked;
extern NSString* const AureaLayerSolo;
extern NSString* const AureaLayerAnimated;
extern NSString* const AureaLayerThreeD;
extern NSString* const AureaLayerLabel;

/// Chaves de um keyframe (`-keyframesForLayer:` / `-allKeyframes`).
extern NSString* const AureaKeyframeProperty;
extern NSString* const AureaKeyframeEffectIndex;
extern NSString* const AureaKeyframeTime;
extern NSString* const AureaKeyframeValue;
extern NSString* const AureaKeyframeInterpolation;

/// Chaves do inspetor de camada (`-layerDetail:`).
extern NSString* const AureaDetailKind;
extern NSString* const AureaDetailStartFrame;
extern NSString* const AureaDetailEndFrame;
extern NSString* const AureaDetailPosition;       ///< NSArray<NSNumber*> de 3
extern NSString* const AureaDetailScale;
extern NSString* const AureaDetailRotation;       ///< graus
extern NSString* const AureaDetailAnchor;
extern NSString* const AureaDetailOpacity;
extern NSString* const AureaDetailSkew;
extern NSString* const AureaDetailAnimatedMask;   ///< bit n = propriedade n animada
extern NSString* const AureaDetailKeyAtPlayhead;  ///< bit n = keyframe no playhead
extern NSString* const AureaDetailSourceSize;
extern NSString* const AureaDetailSourceFps;
extern NSString* const AureaDetailEffectCount;
extern NSString* const AureaDetailMaskCount;
extern NSString* const AureaDetailLocalPlayhead;
extern NSString* const AureaDetailParentId;
extern NSString* const AureaDetailAudioGain;
extern NSString* const AureaDetailAudioVolume;
extern NSString* const AureaDetailAudioPan;
extern NSString* const AureaDetailAudioFlags;
extern NSString* const AureaDetailSpeed;
extern NSString* const AureaDetailTimeFlags;
extern NSString* const AureaDetailShape;
extern NSString* const AureaDetailCorners;        ///< 8 floats: caixa da camada na tela
extern NSString* const AureaDetailParentAffine;   ///< 6 floats (a b c d tx ty)
extern NSString* const AureaDetailGeomFlags;

/// Chaves da composição (`-composition`).
extern NSString* const AureaCompositionId;
extern NSString* const AureaCompositionName;
extern NSString* const AureaCompositionWidth;
extern NSString* const AureaCompositionHeight;
extern NSString* const AureaCompositionFps;
extern NSString* const AureaCompositionDuration;
extern NSString* const AureaCompositionBackground;   ///< NSArray de 4 (r g b a linear)
extern NSString* const AureaCompositionSizeCap;      ///< NSArray de 2 (lado maior, lado menor)
extern NSString* const AureaCompositionDepth;        ///< pré-composição aberta (0 = principal)

/// Chaves de efeito (`-effectCatalog`, `-effectsForLayer:`).
extern NSString* const AureaEffectId;
extern NSString* const AureaEffectTypeId;
extern NSString* const AureaEffectName;
extern NSString* const AureaEffectCategory;
extern NSString* const AureaEffectEnabled;
extern NSString* const AureaEffectParamCount;
extern NSString* const AureaEffectKnown;

/// Chaves de parâmetro de efeito (`-effectParamsForLayer:effectId:`).
extern NSString* const AureaParamIndex;
extern NSString* const AureaParamType;
extern NSString* const AureaParamLabel;
extern NSString* const AureaParamUnit;
extern NSString* const AureaParamId;
extern NSString* const AureaParamValue;        ///< NSArray de 4 (RGBA ou 1 valor)
extern NSString* const AureaParamDefault;
extern NSString* const AureaParamMin;
extern NSString* const AureaParamMax;
extern NSString* const AureaParamAnimated;
extern NSString* const AureaParamEnumLabels;   ///< NSArray<NSString*>

/// Progresso do export (`-exportProgress`).
extern NSString* const AureaExportRunning;
extern NSString* const AureaExportFinished;
extern NSString* const AureaExportResult;      ///< Errc
extern NSString* const AureaExportFramesTotal;
extern NSString* const AureaExportFramesDone;
extern NSString* const AureaExportFps;
extern NSString* const AureaExportEtaSeconds;
extern NSString* const AureaExportFlags;
extern NSString* const AureaExportMessage;

/// Códigos de export (Engine::ExportFlag).
typedef NS_OPTIONS(uint32_t, AureaExportFlag) {
    AureaExportFlagHardwareEncoder = 1u << 0,
    AureaExportFlagSoftwareEncoder = 1u << 1,
    AureaExportFlagThermalReduced  = 1u << 2,
};

/// Códigos de codec de saída (ExportCodec).
typedef NS_ENUM(uint16_t, AureaExportCodec) {
    AureaExportCodecH264 NS_SWIFT_NAME(h264) = 0,
    AureaExportCodecHEVC NS_SWIFT_NAME(hevc) = 1,
    AureaExportCodecAV1 NS_SWIFT_NAME(av1) = 2,
    AureaExportCodecProRes NS_SWIFT_NAME(proRes) = 3,
};

/// Propriedade animável (TrackProperty) — as que a bridge expõe.
typedef NS_ENUM(uint16_t, AureaTrackProperty) {
    AureaTrackPropertyPositionX = 0,
    AureaTrackPropertyPositionY = 1,
    AureaTrackPropertyPositionZ = 2,
    AureaTrackPropertyScaleX = 3,
    AureaTrackPropertyScaleY = 4,
    AureaTrackPropertyScaleZ = 5,
    AureaTrackPropertyRotationX = 6,
    AureaTrackPropertyRotationY = 7,
    AureaTrackPropertyRotationZ = 8,
    AureaTrackPropertyAnchorX = 9,
    AureaTrackPropertyAnchorY = 10,
    AureaTrackPropertyAnchorZ = 11,
    AureaTrackPropertyOpacity = 12,
    AureaTrackPropertySkewX = 13,
    AureaTrackPropertySkewY = 14,
    /// Os números batem com `TrackProperty` (core/Types.hpp), contados na
    /// ordem do enum: 15..29 são as trilhas 3D, de luz, de material e de
    /// texto; 30 é o remapeamento de tempo e 31 o parâmetro de efeito.
    AureaTrackPropertyTimeRemap = 30,
    AureaTrackPropertyEffectParam = 31,
};

/// As duas trilhas que não são transform, para quem monta um `TrackRef`
/// completo (o keyframe de efeito precisa do par effectIndex/paramIndex).
static const uint32_t AureaTrackPropertyInvalidEffectIndex = 0xFFFFFFFFu;

/// A ponte. Uma instância por app (o motor é um só, como no Android).
///
/// CICLO DE VIDA, na ordem que importa:
///   init → start → attachMetalLayer → (CADisplayLink chama requestRender)
///        → detachSurface → stop
/// `stop` destrói o Engine ANTES do backend Metal (a posse do backend é do
/// motor) e cancela o display link da view — a view chama `detachSurface`
/// antes de morrer.
NS_SWIFT_NAME(AureaEngine)
@interface AureaEngine : NSObject

/// `cache` e `documents` são as pastas do app. `documents` é onde o .aurea e a
/// mídia importada moram — o MESMO formato de arquivo do Android.
- (instancetype)initWithCacheDirectory:(NSString*)cacheDirectory
                    documentsDirectory:(NSString*)documentsDirectory NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

/// Sobe o motor: dispositivo Metal, backend, renderer, thread de render.
/// `device` nulo = o dispositivo padrão do sistema.
- (BOOL)startWithDevice:(nullable id<MTLDevice>)device
          refreshRate:(double)refreshRate
                debug:(BOOL)debug
                error:(NSString* _Nullable* _Nullable)error;

- (void)stop;
/// App em segundo plano: pausa, devolve os decoders, grava o cache de pipeline.
- (void)suspend;
- (void)resume;
/// A janela voltou a aparecer: reapresenta mesmo sem mudança no modelo.
- (void)invalidate;
/// Pressão de memória: `level` no mesmo espírito do TRIM_MEMORY_* do Android.
- (int64_t)trimMemory:(int32_t)level;
/// Estado térmico (0 nominal … 4 crítico) vindo de NSProcessInfo.
- (void)setThermalLevel:(uint32_t)level throttling:(BOOL)throttling;

@property (nonatomic, readonly) BOOL running;
/// Nome da GPU + versão do driver, para os Ajustes.
@property (nonatomic, readonly, copy) NSString* deviceSummary;

// --- Superfície -------------------------------------------------------------
/// A `CAMetalLayer*` da view entra AQUI: `SurfaceDesc::nativeWindow` é o layer
/// no iOS (ver render/GPUBackend.hpp). O layer é EMPRESTADO — a posse é da view.
- (BOOL)attachMetalLayer:(CAMetalLayer*)layer width:(int)width height:(int)height;
- (void)detachSurface;
- (void)resizeSurfaceWidth:(int)width height:(int)height;
@property (nonatomic, readonly) BOOL hasSurface;
/// Acorda a thread de render. É o que o CADisplayLink chama uma vez por vsync.
- (void)requestRender;

// --- Estado -----------------------------------------------------------------
/// `YES` se leu. Sem alocação: a UI chama isto uma vez por frame.
- (BOOL)readStatus:(AureaStatus*)out NS_SWIFT_NAME(readStatus(_:));
/// Painel DEV: medido, nunca estimado (chaves AureaPerf*).
- (NSDictionary<NSString*, id>*)perf;
#if DEBUG
/// Read-only native render/capture diagnostics for parity CI, absent in Release.
- (NSDictionary<NSString*, id>*)renderDiagnostics;
#endif
/// O que o motor decidiu para ESTE aparelho (chaves do DeviceReport do Android).
- (NSDictionary<NSString*, NSNumber*>*)deviceReport;
/// Export em andamento (chaves AureaExport*).
- (NSDictionary<NSString*, id>*)exportProgress;

// --- Projeto ----------------------------------------------------------------
- (BOOL)newProjectWidth:(uint32_t)width height:(uint32_t)height fps:(double)fps title:(nullable NSString*)title;
- (BOOL)loadProject:(NSString*)path;
- (BOOL)saveProject:(NSString*)path;
- (BOOL)recoverSession;
- (void)discardRecovery;
/// Nome da composição aberta ("" quando não há projeto).
@property (nonatomic, readonly, copy) NSString* compositionName;
/// Pré-composição aberta (0 = principal).
@property (nonatomic, readonly) uint32_t precompDepth;
- (BOOL)openPrecomp:(long long)layerId;
- (BOOL)closePrecomp;
/// Bits do que a última abertura precisou fazer (Engine::LoadNotice).
@property (nonatomic, readonly) uint32_t lastLoadNotice;
@property (nonatomic, readonly) uint32_t lastLoadMissingAssets;

// --- Comandos (bloco) --------------------------------------------------------
// A UI começa um bloco, emite os comandos do gesto e fecha. O `flush` atravessa
// a fronteira UMA vez — é o mesmo desenho do CommandBatch.kt do Android.
- (void)beginBatch;
/// Manda o bloco pendente e o esvazia. Devolve quantos comandos entraram.
- (NSInteger)flush;

- (void)undo;
- (void)redo;
- (void)beginUndoGroup;
- (void)endUndoGroup;

// --- Reprodução -------------------------------------------------------------
- (void)play;
- (void)pause;
- (void)togglePlayback;
- (void)seekToFrame:(int64_t)frame;
- (void)stepFrames:(int32_t)frames;
- (void)scrubToFrame:(int64_t)frame;
- (void)scrubBegin;
- (void)scrubEnd;
- (void)setLoop:(BOOL)loop;
- (void)setPlaybackSpeed:(float)speed;
- (void)setPreviewScaleNumerator:(uint32_t)numerator denominator:(uint32_t)denominator automatic:(BOOL)automatic;

// --- Camadas ----------------------------------------------------------------
- (void)setLayer:(long long)layerId name:(NSString*)name;
- (void)setLayer:(long long)layerId visible:(BOOL)visible;
- (void)setLayer:(long long)layerId locked:(BOOL)locked;
- (void)setLayer:(long long)layerId solo:(BOOL)solo;
- (void)setLayer:(long long)layerId label:(uint32_t)label;
- (void)setLayer:(long long)layerId blendMode:(uint32_t)blendMode;
- (void)setLayer:(long long)layerId parent:(long long)parentId;
- (void)setLayer:(long long)layerId startFrame:(int32_t)start endFrame:(int32_t)end offsetFrames:(int32_t)offset setOffset:(BOOL)setOffset;
- (void)setLayer:(long long)layerId adjustment:(BOOL)on;
- (void)setLayer:(long long)layerId guide:(BOOL)on;
- (void)setLayerOrder:(long long)layerId newIndex:(uint32_t)newIndex;
- (void)splitLayer:(long long)layerId atFrame:(int32_t)frame;
- (void)deleteLayers:(NSArray<NSNumber*>*)layerIds;
- (void)duplicateLayers:(NSArray<NSNumber*>*)layerIds;
- (void)rippleDeleteLayers:(NSArray<NSNumber*>*)layerIds;
- (void)setEditMode:(BOOL)on;

// --- Transform --------------------------------------------------------------
- (void)setTransformForLayer:(long long)layerId
                  position:(simd_float3)position
                     scale:(simd_float3)scale
                  rotation:(simd_float3)rotation
                    anchor:(simd_float3)anchor
                   opacity:(float)opacity;
- (void)setPositionForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setPosition(forLayer:x:y:z:));
- (void)setScaleForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setScale(forLayer:x:y:z:));
- (void)setRotationForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setRotation(forLayer:x:y:z:));
- (void)setAnchorForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z NS_SWIFT_NAME(setAnchor(forLayer:x:y:z:));
- (void)setOpacityForLayer:(long long)layerId value:(float)value NS_SWIFT_NAME(setOpacity(forLayer:value:));
- (void)setSkewForLayer:(long long)layerId x:(float)x y:(float)y NS_SWIFT_NAME(setSkew(forLayer:x:y:));

// --- Keyframes --------------------------------------------------------------
- (void)insertKeyframeForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time value:(float)value;
- (void)deleteKeyframeForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time NS_SWIFT_NAME(deleteKeyframe(forLayer:property:time:));
- (void)moveKeyframeForLayer:(long long)layerId property:(uint32_t)property from:(int32_t)from to:(int32_t)to;
- (void)setKeyframeValueForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time value:(float)value NS_SWIFT_NAME(setKeyframeValue(forLayer:property:time:value:));
- (void)setKeyframeInterpolationForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time
                            interpolation:(uint32_t)interpolation
                                      bx1:(float)bx1 by1:(float)by1 bx2:(float)bx2 by2:(float)by2 NS_SWIFT_NAME(setKeyframeInterpolation(forLayer:property:time:interpolation:bx1:by1:bx2:by2:));
/// Parâmetro de efeito animável (o TrackRef completo).
- (void)insertKeyframeForLayer:(long long)layerId effectIndex:(uint32_t)effectIndex
                     paramIndex:(uint32_t)paramIndex time:(int32_t)time value:(float)value;
- (void)setKeyframeValueForLayer:(long long)layerId effectIndex:(uint32_t)effectIndex
                      paramIndex:(uint32_t)paramIndex time:(int32_t)time value:(float)value;

// --- Efeitos ----------------------------------------------------------------
- (void)addEffect:(uint32_t)effectTypeId toLayer:(long long)layerId atIndex:(uint32_t)index;
- (void)removeEffect:(uint32_t)effectId fromLayer:(long long)layerId;
- (void)setEffect:(uint32_t)effectId forLayer:(long long)layerId enabled:(BOOL)enabled;
- (void)setEffect:(uint32_t)effectId forLayer:(long long)layerId paramIndex:(uint32_t)paramIndex value:(float)value;
- (void)setEffectColor:(uint32_t)effectId forLayer:(long long)layerId paramIndex:(uint32_t)paramIndex
                     r:(float)r g:(float)g b:(float)b a:(float)a;
- (void)moveEffect:(uint32_t)effectId inLayer:(long long)layerId toIndex:(uint32_t)newIndex;
- (void)copyEffects:(long long)layerId;
- (void)pasteEffects:(NSArray<NSNumber*>*)layerIds;
- (void)copyStyle:(long long)layerId;
- (void)pasteStyle:(NSArray<NSNumber*>*)layerIds;
- (void)copyKeyframes:(long long)layerId atFrame:(int32_t)frame;
- (void)pasteKeyframes:(NSArray<NSNumber*>*)layerIds atFrame:(int32_t)frame;
- (void)copyLayers:(NSArray<NSNumber*>*)layerIds;
- (void)pasteLayers:(int64_t)frame;
/// Bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes (Engine::clipboard_state).
@property (nonatomic, readonly) uint32_t clipboardState;

// --- Texto / forma / 3D -----------------------------------------------------
- (nullable NSDictionary<NSString*, id>*)textForLayer:(long long)layerId NS_SWIFT_NAME(text(forLayer:));
- (BOOL)setTextFontForLayer:(long long)layerId family:(NSString*)family weight:(uint32_t)weight italic:(BOOL)italic path:(NSString*)path NS_SWIFT_NAME(setTextFont(forLayer:family:weight:italic:path:));
- (NSArray<NSDictionary<NSString*, id>*>*)availableFonts;
- (nullable NSDictionary<NSString*, id>*)importFontAtPath:(NSString*)path;
- (nullable NSDictionary<NSString*, id>*)text3DForLayer:(long long)layerId NS_SWIFT_NAME(text3D(forLayer:));
- (BOOL)setText3DForLayer:(long long)layerId property:(NSString*)property stringValue:(nullable NSString*)stringValue numberValue:(float)numberValue NS_SWIFT_NAME(setText3D(forLayer:property:stringValue:numberValue:));
- (void)setText:(long long)layerId content:(NSString*)content;
- (void)setText:(long long)layerId size:(float)size;
- (void)setText:(long long)layerId colorR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setText:(long long)layerId strokeR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setText:(long long)layerId alignment:(uint32_t)alignment;
- (void)setText:(long long)layerId strokeWidth:(float)width;
- (void)addTextAnimator:(long long)layerId props:(uint32_t)props;
- (void)removeTextAnimator:(long long)layerId index:(uint32_t)index;
- (void)setTextAnimParam:(long long)layerId index:(uint32_t)index param:(uint32_t)param value:(float)value;
- (void)toggleTextAnimKey:(long long)layerId index:(uint32_t)index param:(uint32_t)param;
- (void)applyTextPreset:(long long)layerId preset:(uint32_t)preset;
- (void)setShape:(long long)layerId param:(uint32_t)param value:(float)value;
- (void)setShape:(long long)layerId fillR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setShape:(long long)layerId strokeR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setCameraTrack:(uint32_t)mode forLayer:(long long)layerId;
- (void)setModelTransformInScene:(uint64_t)scene modelIndex:(uint32_t)modelIndex
                          x:(float)x y:(float)y z:(float)z
                         sx:(float)sx sy:(float)sy sz:(float)sz
                         rx:(float)rx ry:(float)ry rz:(float)rz;
- (void)setMaterialInScene:(uint64_t)scene modelIndex:(uint32_t)modelIndex
             materialIndex:(uint32_t)materialIndex param:(uint32_t)param value:(float)value;
/// Ambiente do PROJETO (HDRI + intensidade + giro, em graus).
- (BOOL)setEnvironmentIntensity:(float)intensity rotation:(float)rotation;
/// {tem HDRI, intensidade, giro}.
- (NSArray<NSNumber*>*)environment;
/// Ambiente POR OBJETO (v22): `source` 0 = o do projeto, 1 = o dele;
/// `hdri` 0 volta ao estudio neutro daquele objeto.
- (BOOL)setObjectEnvironmentForLayer:(long long)layerId source:(uint32_t)source hdri:(long long)hdri
                           intensity:(float)intensity rotation:(float)rotation exposure:(float)exposure NS_SWIFT_NAME(setObjectEnvironment(forLayer:source:hdri:intensity:rotation:exposure:));
/// {fonte, asset, intensidade, giro, exposicao}.
- (NSArray<NSNumber*>*)objectEnvironmentForLayer:(long long)layerId;

// --- Composição -------------------------------------------------------------
- (void)setComposition:(uint64_t)composition width:(uint32_t)width height:(uint32_t)height;
- (void)setComposition:(uint64_t)composition fps:(double)fps;
- (void)setComposition:(uint64_t)composition duration:(int64_t)frames;
- (void)setComposition:(uint64_t)composition backgroundR:(float)r g:(float)g b:(float)b a:(float)a;

// --- Áudio da camada --------------------------------------------------------
- (void)setLayer:(long long)layerId audioGain:(float)gain;
- (void)setLayer:(long long)layerId audioVolume:(float)volume;
- (void)setLayer:(long long)layerId audioPan:(float)pan;
- (void)setLayer:(long long)layerId audioMuted:(BOOL)muted;
- (void)setLayer:(long long)layerId audioSolo:(BOOL)solo;
- (void)setLayer:(long long)layerId fadeIn:(int32_t)frames;
- (void)setLayer:(long long)layerId fadeOut:(int32_t)frames;
- (void)setLayer:(long long)layerId speed:(float)speed;
- (void)setLayer:(long long)layerId reversed:(BOOL)reversed;

// --- Pré-composição ---------------------------------------------------------
/// Agrupar: as camadas viram uma composição nova e no lugar delas fica UMA
/// camada que a mostra. Devolve a camada nova, ou −Errc.
- (long long)precomposeLayers:(NSArray<NSNumber*>*)layerIds name:(nullable NSString*)name;
/// Desagrupar. Devolve "" quando deu certo, ou o motivo da recusa (a frase que
/// a UI mostra — o motor recusa o que MUDARIA o resultado, nunca o que é
/// trabalhoso).
- (NSString*)ungroupPrecomp:(long long)layerId;

// --- Importação -------------------------------------------------------------
/// Caminho de arquivo no sandbox (o Swift copia o que vem do seletor para lá).
/// Devolve o id da camada, ou −Errc (ver `-lastImportError`).
- (long long)importVideo:(NSString*)path name:(NSString*)name;
- (long long)importAudio:(NSString*)path name:(NSString*)name;
- (long long)extractAudioFromLayer:(long long)layerId;
/// RGBA8 sRGB de alfa reto, já decodificado pelo Swift (ImageIO num helper não
/// é necessário: use `-importImageFile:` para o caminho sem cópia de ida).
- (long long)importImageFile:(NSString*)path name:(NSString*)name;
- (long long)importModel:(NSString*)path name:(NSString*)name;
- (long long)importHdri:(NSString*)path;
- (long long)importObjectHDRI:(NSString*)path layer:(long long)layer;
- (void)clearHdri;
- (long long)addShape:(uint32_t)preset;
- (long long)addText:(nullable NSString*)content;
- (long long)addNull:(BOOL)threeD;
- (long long)addText3D:(NSString*)content depth:(float)depth alignment:(uint32_t)alignment
                     r:(float)r g:(float)g b:(float)b;
- (long long)addParticles:(uint32_t)preset;
- (long long)freezeFrameForLayer:(long long)layerId frame:(int32_t)frame hold:(int32_t)holdFrames;
/// Motivo legível da última importação que falhou ("" quando deu certo).
@property (nonatomic, readonly, copy) NSString* lastImportError;

// --- Consultas --------------------------------------------------------------
/// Uma linha por camada, da frente para o fundo (chaves AureaLayer*).
- (NSArray<NSDictionary<NSString*, id>*>*)layers;
- (NSArray<NSDictionary<NSString*, id>*>*)keyframesForLayer:(long long)layerId;
/// Todos os keyframes da composição numa travessia: NSArray de NSDictionary com
/// `layerId` + `keys`.
- (NSArray<NSDictionary<NSString*, id>*>*)allKeyframes;
- (nullable NSDictionary<NSString*, id>*)layerDetail:(long long)layerId;
- (NSArray<NSNumber*>*)shapeParams:(long long)layerId;
- (NSArray<NSNumber*>*)keyframeEasing:(long long)layerId property:(uint32_t)property frame:(int32_t)frame;
- (BOOL)editShape:(long long)layerId param:(uint32_t)param value:(float)value continuing:(BOOL)continuing;
- (BOOL)keyShape:(long long)layerId param:(uint32_t)param;
- (NSArray<NSNumber*>*)trackMatte:(long long)layerId;
- (NSArray<NSNumber*>*)maskData:(long long)layerId;
- (int32_t)addMask:(long long)layerId points:(NSArray<NSNumber*>*)points closed:(BOOL)closed;
- (BOOL)removeMask:(long long)layerId mask:(uint32_t)mask;
- (BOOL)setMaskPath:(long long)layerId mask:(uint32_t)mask points:(NSArray<NSNumber*>*)points closed:(BOOL)closed undo:(BOOL)undo;
- (BOOL)setMaskProps:(long long)layerId mask:(uint32_t)mask operation:(uint32_t)operation inverted:(BOOL)inverted feather:(float)feather expansion:(float)expansion opacity:(float)opacity;
- (BOOL)keyMask:(long long)layerId mask:(uint32_t)mask;
- (BOOL)toggleMaskKey:(long long)layerId mask:(uint32_t)mask NS_SWIFT_NAME(toggleMaskKey(_:mask:));
- (NSArray<NSNumber*>*)textAnimators:(long long)layerId;
- (BOOL)setTextAnimator:(long long)layerId index:(uint32_t)index values:(NSArray<NSNumber*>*)values;
- (NSArray<NSNumber*>*)textStyle:(long long)layerId;
- (BOOL)setTextStyle:(long long)layerId values:(NSArray<NSNumber*>*)values;
- (NSArray<NSNumber*>*)particleParams:(long long)layerId;
- (BOOL)setParticle:(long long)layerId param:(uint32_t)param value:(float)value;
- (BOOL)applyParticlePreset:(long long)layerId preset:(uint32_t)preset;
- (NSArray<NSNumber*>*)particleLinks:(long long)layerId;
- (BOOL)setParticleLink:(long long)layerId kind:(uint32_t)kind target:(long long)target;
- (NSArray<NSNumber*>*)particleCurve:(long long)layerId kind:(uint32_t)kind;
- (BOOL)setParticleCurve:(long long)layerId kind:(uint32_t)kind values:(NSArray<NSNumber*>*)values;
- (void)keyParameter:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time value:(float)value;
- (NSString*)savePreset:(long long)layerId kind:(uint32_t)kind name:(NSString*)name;
- (NSString*)savePreset:(long long)layerId kind:(uint32_t)kind name:(NSString*)name parts:(uint32_t)parts NS_SWIFT_NAME(savePreset(_:kind:name:parts:));
- (NSArray<NSNumber*>*)parseCaptionPreset:(NSString*)json NS_SWIFT_NAME(parseCaptionPreset(_:));
- (NSString*)makeCaptionPreset:(NSString*)name options:(NSDictionary<NSString*, NSNumber*>*)options NS_SWIFT_NAME(makeCaptionPreset(_:options:));
- (NSArray<NSNumber*>*)parseCurvePreset:(NSString*)json NS_SWIFT_NAME(parseCurvePreset(_:));
- (NSString*)makeCurvePreset:(NSString*)name interpolation:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles NS_SWIFT_NAME(makeCurvePreset(_:interpolation:handles:));
- (NSString*)applyPreset:(long long)layerId json:(NSString*)json duration:(int64_t)duration;
- (NSString*)trackPoint:(long long)layerId x:(float)x y:(float)y stabilize:(BOOL)stabilize;
- (NSDictionary<NSString*, id>*)cameraTrackingStatus;
- (NSArray<NSNumber*>*)gizmo:(long long)layerId length:(float)length NS_SWIFT_NAME(gizmo(_:length:));
- (NSArray<NSNumber*>*)gizmoMoveLocal:(long long)layerId axis:(uint32_t)axis amount:(float)amount NS_SWIFT_NAME(gizmoMoveLocal(_:axis:amount:));
- (void)cancelCameraTracking;
- (NSString*)applyCameraTracking;
- (NSString*)trackMask:(long long)layerId mask:(uint32_t)mask mode:(uint32_t)mode;
- (NSString*)layerMediaPath:(long long)layerId;
- (NSArray<NSDictionary<NSString*, id>*>*)parseSRT:(NSString*)srt;
- (BOOL)isFillerWord:(NSString*)word NS_SWIFT_NAME(isFillerWord(_:));
- (NSString*)createCaptions:(long long)layerId words:(NSArray<NSDictionary<NSString*, id>*>*)words options:(NSDictionary<NSString*, NSNumber*>*)options;
- (uint32_t)captionCount:(long long)layerId;
- (void)removeCaptions:(long long)layerId;
- (NSArray<NSNumber*>*)trackCurve:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param from:(int32_t)from to:(int32_t)to;
- (NSArray<NSNumber*>*)trackEasing:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time;
- (void)editTrackKey:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time action:(uint32_t)action value:(float)value targetTime:(int32_t)targetTime interpolation:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles;
- (NSDictionary<NSString*, id>*)expression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param;
- (NSString*)setExpression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param source:(NSString*)source;
- (void)enableExpression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param enabled:(BOOL)enabled;
- (NSDictionary<NSString*, id>*)setExpressions:(long long)layerId tracks:(NSArray<NSNumber*>*)tracks source:(NSString*)source NS_SWIFT_NAME(setExpressions(_:tracks:source:));
- (BOOL)enableExpressions:(long long)layerId tracks:(NSArray<NSNumber*>*)tracks enabled:(BOOL)enabled NS_SWIFT_NAME(enableExpressions(_:tracks:enabled:));
- (NSDictionary<NSString*, id>*)checkExpressionSyntax:(NSString*)source NS_SWIFT_NAME(checkExpressionSyntax(_:));
- (NSArray<NSNumber*>*)timeRemap:(long long)layerId;
- (int32_t)editTimeRemap:(long long)layerId index:(int32_t)index time:(int64_t)time value:(float)value interpolation:(int32_t)interpolation;
- (void)removeTimeRemap:(long long)layerId index:(uint32_t)index;
- (long long)addVector:(uint32_t)preset;
- (NSArray<NSDictionary<NSString*, id>*>*)vectorGroups:(long long)layerId;
- (BOOL)editVectorGroup:(long long)layerId group:(uint32_t)group field:(uint32_t)field values:(NSArray<NSNumber*>*)values;
- (BOOL)renameVectorGroup:(long long)layerId group:(uint32_t)group name:(NSString*)name;
- (int32_t)addVectorGroup:(long long)layerId kind:(uint32_t)kind;
- (BOOL)removeVectorGroup:(long long)layerId group:(uint32_t)group;
- (int32_t)addVectorPath:(long long)layerId group:(uint32_t)group kind:(uint32_t)kind;
- (BOOL)removeVectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path;
- (NSArray<NSNumber*>*)vectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path;
- (BOOL)setVectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path values:(NSArray<NSNumber*>*)values continuing:(BOOL)continuing;
- (BOOL)keyVectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path;
- (BOOL)makeVectorPathEditable:(long long)layerId group:(uint32_t)group path:(uint32_t)path NS_SWIFT_NAME(makeVectorPathEditable(_:group:path:));
- (BOOL)toggleVectorPathKey:(long long)layerId group:(uint32_t)group path:(uint32_t)path NS_SWIFT_NAME(toggleVectorPathKey(_:group:path:));
- (BOOL)setVectorParam:(long long)layerId group:(uint32_t)group param:(uint32_t)param value:(float)value;
- (BOOL)keyVectorParam:(long long)layerId group:(uint32_t)group param:(uint32_t)param;
- (BOOL)toggleVectorParamKey:(long long)layerId group:(uint32_t)group param:(uint32_t)param NS_SWIFT_NAME(toggleVectorParamKey(_:group:param:));
- (long long)addFreehand:(long long)layerId points:(NSArray<NSNumber*>*)points error:(float)error;
- (long long)importSVG:(NSString*)text name:(NSString*)name;
- (NSArray<NSNumber*>*)textPath:(long long)layerId;
- (BOOL)setTextPath:(long long)layerId target:(long long)target offset:(float)offset perpendicular:(BOOL)perpendicular reversed:(BOOL)reversed;
- (BOOL)setTextSpan:(long long)layerId start:(uint32_t)start end:(uint32_t)end color:(NSArray<NSNumber*>*)color weight:(uint32_t)weight scale:(float)scale;
- (BOOL)clearTextSpans:(long long)layerId start:(uint32_t)start end:(uint32_t)end;
- (BOOL)setText3DColor:(long long)layerId region:(uint32_t)region values:(NSArray<NSNumber*>*)values;
- (BOOL)applyText3DPreset:(long long)layerId preset:(uint32_t)preset;
- (NSArray<NSNumber*>*)modelShadows:(long long)layerId;
- (BOOL)setModelShadows:(long long)layerId cast:(BOOL)cast receive:(BOOL)receive;
- (int64_t)removeGaps;
- (BOOL)trimComposition:(int64_t)frame;
- (long long)detectBeatsForLayer:(long long)layerId bpm:(double*)bpm NS_SWIFT_NAME(detectBeats(forLayer:bpm:));
- (NSArray<NSNumber*>*)motionBlurSettings;
- (void)setMotionBlurSettings:(BOOL)enabled shutter:(float)shutter;
- (nullable NSDictionary<NSString*, id>*)composition;
- (NSArray<NSDictionary<NSString*, id>*>*)effectCatalog;
- (NSArray<NSDictionary<NSString*, id>*>*)effectsForLayer:(long long)layerId;
- (NSArray<NSDictionary<NSString*, id>*>*)effectParamsForLayer:(long long)layerId effectId:(uint32_t)effectId;
/// Declaração dos parâmetros de um TIPO (o catálogo, sem camada).
- (NSArray<NSDictionary<NSString*, id>*>*)effectSpecs:(uint32_t)typeId;
/// Curva de uma propriedade: `count` amostras entre `from` e `to` (frames).
- (NSArray<NSNumber*>*)curveForLayer:(long long)layerId property:(uint32_t)property
                                from:(int32_t)from to:(int32_t)to count:(uint32_t)count;
/// Waveform: `count` baldes u8 (compansão raiz) a partir de `startFrame`.
- (nullable NSData*)waveformForLayer:(long long)layerId startFrame:(double)startFrame
                    framesPerBucket:(double)framesPerBucket count:(uint32_t)count;
- (void)selectLayers:(NSArray<NSNumber*>*)layerIds;
- (void)clearSelection;
- (NSArray<NSNumber*>*)selection;
- (NSArray<NSNumber*>*)searchLayers:(NSString*)query;
- (void)setTimeRemap:(BOOL)on forLayer:(long long)layerId;
- (void)applySpeedRamp:(uint32_t)preset forLayer:(long long)layerId;
- (void)setEchoForLayer:(long long)layerId count:(uint32_t)count delay:(float)delay decay:(float)decay;
- (void)setRgbTimeForLayer:(long long)layerId delay:(float)delay;
- (void)setMotionBlur:(BOOL)on forLayer:(long long)layerId;
- (void)setTransitionForLayer:(long long)layerId out:(BOOL)out type:(uint32_t)type frames:(uint32_t)frames;
- (void)setTrackMatteForLayer:(long long)layerId matte:(long long)matteLayerId mode:(uint32_t)mode;
- (void)setFrameBlendForLayer:(long long)layerId mode:(uint32_t)mode;
- (void)setVectorBlurForLayer:(long long)layerId amount:(float)amount NS_SWIFT_NAME(setVectorBlur(forLayer:amount:));
- (void)toggleMarker:(int64_t)frame;
- (NSArray<NSNumber*>*)markers;   ///< tripletas (frame, cor, tipo)

// --- Imagens (miniatura / captura / prévia de efeito) -----------------------
/// Miniatura da camada em RGBA8 sRGB (nil = ainda na fila; `AureaStatus.
/// thumbnailGeneration` avisa quando chega). `outWidth` recebe a largura.
- (nullable NSData*)thumbnailForLayer:(long long)layerId frame:(int32_t)frame
                               height:(uint32_t)height outWidth:(nullable uint32_t*)outWidth;
/// Frame do playhead em RGBA8 sRGB com o lado maior em `maxDim` (capa do
/// projeto na Home). NUNCA o preview: o preview é o CAMetalLayer.
- (nullable NSData*)captureFrame:(uint32_t)maxDim outWidth:(nullable uint32_t*)outWidth
                        outHeight:(nullable uint32_t*)outHeight;
/// Prévia de um efeito (ficha do catálogo) em RGBA8 sRGB.
- (nullable NSData*)effectPreview:(uint32_t)typeId width:(uint32_t)width height:(uint32_t)height
                          outSize:(nullable CGSize*)outSize;
/// Foto de base das prévias de efeito (RGBA8). O app manda uma vez.
- (BOOL)setEffectPreviewSource:(NSData*)rgba width:(uint32_t)width height:(uint32_t)height;

// --- Export -----------------------------------------------------------------
/// `path` é o arquivo de saída no sandbox. `height` é o LADO MENOR pedido; a
/// largura sai da proporção da composição (nunca estica).
- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps;
- (void)cancelExport;
/// Thumbnail quadrado do frame do playhead não é export: use `captureFrame`.
@end

// =============================================================================
// A view do preview.
//
// UIView cujo LAYER é um CAMetalLayer (`+layerClass`): o preview do motor vai
// direto para esse layer, sem bitmap no caminho — nada de UIImage, nada de
// `drawRect:`. O SwiftUI a envolve num UIViewRepresentable.
//
// O CADisplayLink vive aqui: um tique por vsync chama `-requestRender` no
// motor. Ele NÃO desenha nada — o desenho é da thread de render do motor, que
// dorme quando não há o que mostrar (`render_frame(onlyIfChanged)`). O tique é
// o pulso que mantém o preview andando no ritmo da tela.
// =============================================================================
NS_SWIFT_NAME(MetalPreviewView)
@interface AureaMetalView : UIView

/// O motor. Fraco: a view não é dona do motor (quem é, é a sessão do app).
@property (nonatomic, weak, nullable) AureaEngine* engine;

/// O dispositivo Metal. Precisa ser O MESMO com que o backend do motor foi
/// criado (`-startWithDevice:`), senão o layer recusa os drawables.
@property (nonatomic, strong, nullable) id<MTLDevice> device;

/// O CAMetalLayer desta view, pronto para o `attachMetalLayer:`.
@property (nonatomic, readonly) CAMetalLayer* metalLayer;

/// Pausa o display link (a view saiu da tela, o app foi para segundo plano).
@property (nonatomic, getter=isPaused) BOOL paused;

/// A view de fato anexou/desanexou a superfície (a UI usa para saber se o
/// preview está de pé). KVO-observável.
@property (nonatomic, readonly) BOOL surfaceAttached;

@end

NS_ASSUME_NONNULL_END
