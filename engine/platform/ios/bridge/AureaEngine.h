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
    uint32_t previewBufferStatus;  ///< shared packed ready/target/active/limited flags
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
/// A LINHA da timeline (`LayerRow::trackId`): mesma linha = mesma fileira.
/// 0 = projeto antigo sem linha (a camada é a linha dela).
extern NSString* const AureaLayerTrackId;

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
extern NSString* const AureaParamMin;         ///< faixa do SLIDER
extern NSString* const AureaParamMax;
extern NSString* const AureaParamHardMin;     ///< faixa DIGITADA (teclado); contém a do slider
extern NSString* const AureaParamHardMax;
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
/// Motivo da falha (aurea::ExportFailure, export/ExportRules.hpp): 0 = nenhum.
extern NSString* const AureaExportFailure;
extern NSString* const AureaExportMessage;

/// Códigos de export (Engine::ExportFlag).
typedef NS_OPTIONS(uint32_t, AureaExportFlag) {
    AureaExportFlagHardwareEncoder = 1u << 0,
    AureaExportFlagSoftwareEncoder = 1u << 1,
    AureaExportFlagThermalReduced  = 1u << 2,
    AureaExportFlagFrameFallback   = 1u << 3,
    /// Export refeito no modo de segurança depois de o encoder travar.
    AureaExportFlagSafeMode        = 1u << 4,
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
- (float)clampPinchFactor:(float)factor scaleX:(float)x scaleY:(float)y scaleZ:(float)z threeD:(BOOL)threeD;
/// Pinça 3D com a regra de profundidade do motor (Z de conteúdo acompanha X).
- (float)clampPinchFactor3D:(float)factor kind:(int)kind scaleX:(float)x scaleY:(float)y scaleZ:(float)z NS_SWIFT_NAME(clampPinchFactor3D(_:kind:scaleX:scaleY:scaleZ:));
/// Escala 3D de gesto já no formato gravado: axis 0..2 eixo, 3 uniforme, 4 ajustar (GestureMath.hpp).
- (NSArray<NSNumber*>*)gestureScale3D:(int)kind scaleX:(float)x scaleY:(float)y scaleZ:(float)z axis:(int)axis factor:(float)factor NS_SWIFT_NAME(gestureScale3D(_:scaleX:scaleY:scaleZ:axis:factor:));
- (NSArray<NSNumber*>*)fitCanvas:(NSArray<NSNumber*>*)values fill:(BOOL)fill NS_SWIFT_NAME(fitCanvas(_:fill:));
- (NSArray<NSNumber*>*)previewGestureBasis:(long long)layer;
- (NSArray<NSNumber*>*)previewGestureValue:(NSArray<NSNumber*>*)basis dx:(float)dx dy:(float)dy rotate:(BOOL)rotate;

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
/// Process allocation headroom reported by iOS; zero can mean the app exceeded its limit.
/// Simulator without an app limit uses a measured host-RAM/residency budget.
- (uint64_t)availableMemoryBytes;
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
- (void)detachMetalLayer:(CAMetalLayer*)layer;
- (void)resizeMetalLayer:(CAMetalLayer*)layer width:(int)width height:(int)height;
- (void)resizeSurfaceWidth:(int)width height:(int)height;
@property (nonatomic, readonly) BOOL hasSurface;
/// Acorda a thread de render e força um quadro.
- (void)requestRender;
/// Pulso do vsync: acorda a thread, que só redesenha se algo mudou.
- (void)wakeRender;

// --- Estado -----------------------------------------------------------------
/// `YES` se leu. Sem alocação: a UI chama isto uma vez por frame.
- (BOOL)readStatus:(AureaStatus*)out NS_SWIFT_NAME(readStatus(_:));
/// Pares de quadros [inicio, fim exclusivo] já renderizados na composição atual.
/// Snapshot curto: não aguarda o renderer nem a GPU.
- (NSArray<NSNumber*>*)previewBufferRanges NS_SWIFT_NAME(previewBufferRanges());
- (uint32_t)localAiStatus NS_SWIFT_NAME(localAiStatus());
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
/// fps livre (1–240; o motor encaixa a razão NTSC) e o fundo inicial da
/// composição em RGBA sRGB, fora do histórico.
- (BOOL)newProjectWidth:(uint32_t)width height:(uint32_t)height fps:(double)fps title:(nullable NSString*)title
            backgroundR:(float)r g:(float)g b:(float)b a:(float)a;
- (BOOL)loadProject:(NSString*)path;
- (BOOL)saveProject:(NSString*)path;
/// Flush UI commands before dispatching. Safe to execute on an IO queue.
- (BOOL)autosaveProject;
/// Sair do app / segundo plano: grava QUALQUER mudança (drena os comandos),
/// sem as regras do autosave. -1 = já estava gravado (nada escrito);
/// 0 = gravou; outro = código Errc da falha (o arquivo anterior segue intacto).
- (NSInteger)saveProjectIfDirty;
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
/// Zoom/pan da VISTA da prévia (pan em px do drawable). Só o passe de saída:
/// sem desfazer, nunca no render/export.
- (void)setViewportZoom:(float)zoom panX:(float)panX panY:(float)panY;

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
@property(nonatomic, readonly) BOOL timelineEditMode;
/// LINHA MAGNÉTICA da camada: cortes dela andam como faixa de montagem.
- (BOOL)setLayer:(long long)layerId magneticTrack:(BOOL)on;
- (BOOL)layerMagneticTrack:(long long)layerId;
/// Arrasta o trecho para outro ponto da mesma linha, reordenando a fita.
- (BOOL)reorderClip:(long long)layerId toFrame:(int64_t)targetFrame;
/// Arrasto vertical de UM trecho (só ele anda). mode 0 = fileira própria logo
/// acima da fileira de anchorId (0 = no fundo); 1 = entrar na linha de anchorId
/// se couber no tempo (senão fileira própria ali). Um passo de desfazer.
- (BOOL)moveLayer:(long long)layerId toRowOf:(long long)anchorId mode:(int)mode NS_SWIFT_NAME(moveLayer(_:toRowOf:mode:));

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
- (void)autoKeyframeForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time value:(float)value;
/// Gesto de transform (0..14) com Auto-Key: o MOTOR decide com as trilhas vivas
/// — animada ganha chave no quadro que a prévia mostra, parada muda o valor
/// (Command.hpp kAutoKey*). `wholeGroup`: grupo XYZ do 3D animado, todo eixo
/// ganha chave. Par do `gestureKeyframe` do CommandBatch.kt.
- (void)gestureKeyframeForLayer:(long long)layerId property:(uint32_t)property value:(float)value wholeGroup:(BOOL)wholeGroup NS_SWIFT_NAME(gestureKeyframe(forLayer:property:value:wholeGroup:));
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
- (void)copyEffect:(unsigned int)effectId fromLayer:(long long)layerId;
- (void)pasteEffects:(NSArray<NSNumber*>*)layerIds;
- (void)copyStyle:(long long)layerId;
- (void)pasteStyle:(NSArray<NSNumber*>*)layerIds;
- (BOOL)copyTransform:(long long)layerId;
- (uint32_t)pasteTransform:(NSArray<NSNumber*>*)layerIds;
- (void)copyKeyframes:(long long)layerId atFrame:(int32_t)frame;
- (uint32_t)keyframeSelection:(long long)layerId references:(NSArray<NSNumber*>*)references action:(uint32_t)action delta:(int32_t)delta;
- (void)pasteKeyframes:(NSArray<NSNumber*>*)layerIds atFrame:(int32_t)frame;
/// Copiar animação: todos os keyframes da camada (Engine::copy_animation).
- (uint32_t)copyAnimation:(long long)layerId;
/// Otimizar keyframes (Engine::optimize_keyframes): property < 0 = todas.
- (uint32_t)optimizeKeyframes:(long long)layerId property:(int32_t)property tolerance:(float)tolerance;
- (void)copyLayers:(NSArray<NSNumber*>*)layerIds;
- (void)pasteLayers:(int64_t)frame;
/// Bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes (Engine::clipboard_state).
@property (nonatomic, readonly) uint32_t clipboardState;

// --- Texto / forma / 3D -----------------------------------------------------
- (nullable NSDictionary<NSString*, id>*)textForLayer:(long long)layerId NS_SWIFT_NAME(text(forLayer:));
- (BOOL)setTextFontForLayer:(long long)layerId family:(NSString*)family weight:(uint32_t)weight italic:(BOOL)italic path:(NSString*)path NS_SWIFT_NAME(setTextFont(forLayer:family:weight:italic:path:));
- (NSArray<NSDictionary<NSString*, id>*>*)availableFonts;
- (nullable NSDictionary<NSString*, id>*)importFontAtPath:(NSString*)path;
- (int)importColorLut:(long long)layer effect:(uint32_t)effect path:(NSString*)path;
- (NSString*)colorLutName:(long long)layer effect:(uint32_t)effect;
- (nullable NSDictionary<NSString*, id>*)text3DForLayer:(long long)layerId NS_SWIFT_NAME(text3D(forLayer:));
- (BOOL)setText3DForLayer:(long long)layerId property:(NSString*)property stringValue:(nullable NSString*)stringValue numberValue:(float)numberValue NS_SWIFT_NAME(setText3D(forLayer:property:stringValue:numberValue:));
- (void)setText:(long long)layerId content:(NSString*)content;
- (void)setText:(long long)layerId size:(float)size;
- (void)setText:(long long)layerId colorR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setText:(long long)layerId strokeR:(float)r g:(float)g b:(float)b a:(float)a;
- (void)setText:(long long)layerId alignment:(uint32_t)alignment;
- (void)setText:(long long)layerId strokeWidth:(float)width;
- (NSInteger)addTextAnimator:(long long)layerId props:(uint32_t)props;
- (void)removeTextAnimator:(long long)layerId index:(uint32_t)index;
- (NSInteger)duplicateTextAnimator:(long long)layerId index:(uint32_t)index;
- (BOOL)moveTextAnimator:(long long)layerId from:(uint32_t)from to:(uint32_t)to;
- (void)setTextAnimParam:(long long)layerId index:(uint32_t)index param:(uint32_t)param value:(float)value;
- (void)toggleTextAnimKey:(long long)layerId index:(uint32_t)index param:(uint32_t)param;
- (BOOL)applyTextPreset:(long long)layerId preset:(uint32_t)preset;
// --- Animadores de camada (32 floats cada — Engine::kLayerAnimFloats) --------
- (NSArray<NSNumber*>*)layerAnimators:(long long)layerId;
- (NSInteger)addLayerAnimator:(long long)layerId;
- (void)removeLayerAnimator:(long long)layerId index:(uint32_t)index;
- (BOOL)setLayerAnimator:(long long)layerId index:(uint32_t)index values:(NSArray<NSNumber*>*)values;
- (void)setLayerAnimParam:(long long)layerId index:(uint32_t)index param:(uint32_t)param value:(float)value;
- (void)toggleLayerAnimKey:(long long)layerId index:(uint32_t)index param:(uint32_t)param;
- (uint32_t)copyLayerAnimators:(long long)layerId;
- (uint32_t)pasteLayerAnimators:(NSArray<NSNumber*>*)layerIds;
- (uint32_t)layerAnimatorClipboard;
/// Comprimento do rastro do desfoque desta camada (× o obturador do projeto).
- (void)setLayerMotionBlurLength:(float)factor forLayer:(long long)layerId;
- (float)layerMotionBlurLength:(long long)layerId;
/// Ajuste: 0 = todas abaixo, 1 = só a logo abaixo, 2 = só as escolhidas.
- (void)setAdjustmentScope:(uint32_t)scope forLayer:(long long)layerId;
- (uint32_t)adjustmentScope:(long long)layerId;
/// Escopo 2: põe/tira a camada `targetId` da lista do ajuste (a lista marcada do app antigo).
- (void)setAdjustmentTarget:(long long)targetId on:(BOOL)on forLayer:(long long)layerId NS_SWIFT_NAME(setAdjustmentTarget(_:on:forLayer:));
- (NSArray<NSNumber*>*)adjustmentTargets:(long long)layerId NS_SWIFT_NAME(adjustmentTargets(_:));
/// Presets do tipo de efeito, achatados: [id estável, nome do motor, id, nome, …].
- (NSArray<NSString*>*)effectPresets:(uint32_t)typeId NS_SWIFT_NAME(effectPresets(_:));
/// Aplica o preset (todos os valores, um passo de desfazer).
- (BOOL)applyEffectPreset:(uint32_t)preset effect:(uint32_t)effectId forLayer:(long long)layerId NS_SWIFT_NAME(applyEffectPreset(_:effect:forLayer:));
/// Roto Brush do Rotobrush IA: pontos em px da composição (x,y intercalados).
- (BOOL)rotoAddStroke:(NSArray<NSNumber *> *)xy background:(BOOL)background radius:(float)radius effect:(uint32_t)effectId forLayer:(long long)layerId NS_SWIFT_NAME(rotoAddStroke(_:background:radius:effect:forLayer:));
- (BOOL)rotoUndoStrokeForEffect:(uint32_t)effectId layer:(long long)layerId NS_SWIFT_NAME(rotoUndoStroke(effect:layer:));
- (BOOL)rotoPropagateEffect:(uint32_t)effectId layer:(long long)layerId NS_SWIFT_NAME(rotoPropagate(effect:layer:));
- (void)rotoCancel NS_SWIFT_NAME(rotoCancel());
- (BOOL)rotoSetView:(uint32_t)mode effect:(uint32_t)effectId layer:(long long)layerId NS_SWIFT_NAME(rotoSetView(_:effect:layer:));
/// [feitos, total, rodando, falhou, traços no quadro, traços no total]
- (NSArray<NSNumber *> *)rotoStatusForEffect:(uint32_t)effectId layer:(long long)layerId NS_SWIFT_NAME(rotoStatus(effect:layer:));
/// Grupo: câmera de fora alcança as camadas de dentro (−1 = não é grupo).
- (BOOL)setGroupCameraPassThrough:(BOOL)on forLayer:(long long)layerId;
- (int32_t)groupCameraPassThrough:(long long)layerId;
/// "Aceita luzes": a camada 2D no espaço 3D recebe as luzes da composição.
- (BOOL)setLayerAcceptsLights:(BOOL)on forLayer:(long long)layerId;
/// −1 = camada sem a opção (câmera, luz, modelo 3D, áudio, nulo); 0/1.
- (int32_t)layerAcceptsLights:(long long)layerId;
/// "" = deu certo; senão o motivo da recusa.
- (NSString*)addLayers:(NSArray<NSNumber*>*)layerIds toGroup:(long long)groupId;
- (NSString*)removeLayerFromGroup:(long long)layerId NS_SWIFT_NAME(removeLayerFromGroup(_:));
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
- (NSArray<NSNumber*>*)sceneSettings;
- (BOOL)setSceneSetting:(uint32_t)parameter value:(float)value;
- (BOOL)setEnvironmentBackground:(BOOL)visible;
- (BOOL)setEnvironmentBackgroundRangeStart:(long long)start end:(long long)end;
- (BOOL)setEnvironmentIntensity:(float)intensity rotation:(float)rotation;
/// {tem HDRI, intensidade, giro}.
- (NSArray<NSNumber*>*)environment;
/// Ambiente POR OBJETO (v22): `source` 0 = o do projeto, 1 = o dele;
/// `hdri` 0 volta ao estudio neutro daquele objeto.
- (BOOL)setObjectEnvironmentForLayer:(long long)layerId source:(uint32_t)source hdri:(long long)hdri
                           intensity:(float)intensity rotation:(float)rotation exposure:(float)exposure NS_SWIFT_NAME(setObjectEnvironment(forLayer:source:hdri:intensity:rotation:exposure:));
/// {fonte, asset, intensidade, giro, exposicao}.
- (NSArray<NSNumber*>*)objectEnvironmentForLayer:(long long)layerId;
- (NSArray<NSNumber*>*)materialsForLayer:(long long)layerId NS_SWIFT_NAME(materials(forLayer:));
- (BOOL)setMaterialForLayer:(long long)layerId index:(uint32_t)index param:(uint32_t)param value:(float)value NS_SWIFT_NAME(setMaterial(forLayer:index:param:value:));

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
/// "Otimizar modelo": `quality` = 0 Original, 1 Equilibrado, 2 Leve (scene3d::ModelQuality).
/// A memória do aparelho (physicalMemory, os_proc_available_memory) é medida
/// aqui na hora. Erro −9 (BudgetExceeded) = pesado demais: recusado, nunca morto.
- (long long)importModel:(NSString*)path name:(NSString*)name quality:(int)quality;
/// Antes do import: custo do arquivo e o que cabe neste aparelho. Mesmo layout
/// do Android (aurea_jni.cpp, nativeInspectModel): 0 válido · 1 exato ·
/// 2 pesado · 3 pesado demais · 4 recomendada · 5 triângulos · 6 vértices ·
/// 7 texturas · 8 maior lado · 9 orçamento · 10..12 cabe · 13..15 pico ·
/// 16..18 triângulos que ficam · 19..21 teto de textura (por qualidade).
- (NSArray<NSNumber*>*)inspectModel:(NSString*)path;
/// Etapa (ImportPhase) × 1000 + fração × 1000 do import em curso.
- (int)importModelProgress;
- (void)cancelModelImport;
/// O último import: [triângulos do arquivo, que ficaram, texturas reduzidas, puladas, qualidade].
- (NSArray<NSNumber*>*)lastModelImport;
/// Texturas (e o .mtl do OBJ) que o modelo 3D da layer referencia e não achou: só o nome do arquivo.
- (NSArray<NSString*>*)modelMissingTextures:(long long)layerId;
/// Pasta absoluta do arquivo do modelo (com a barra no fim); vazio = não é modelo importado.
- (NSString*)modelFolder:(long long)layerId;
/// Relê o modelo com as texturas copiadas para a pasta dele. ≥ 0 = quantas ainda faltam; < 0 = −código
/// (motivo em lastImportError). Bloqueia: chamar fora da thread principal.
- (int)reloadModelTextures:(long long)layerId;
- (long long)importHdri:(NSString*)path;
- (long long)importObjectHDRI:(NSString*)path layer:(long long)layer;
- (void)clearHdri;
- (long long)addShape:(uint32_t)preset;
- (long long)addText:(nullable NSString*)content;
- (NSString*)playbackReport;
- (BOOL)setRawPlayback:(BOOL)enabled;
- (void)setSceneEditor:(BOOL)enabled yaw:(float)yaw pitch:(float)pitch distance:(float)distance;
- (NSArray<NSNumber*>*)sceneGuides;
/// Cena 3D: camada 3D sob o ponto (px da composição) pelo corpo real; 0 = nada.
- (long long)scenePickX:(float)x y:(float)y radius:(float)radius NS_SWIFT_NAME(scenePick(x:y:radius:));
- (void)layoutTransform:(long long)layer property:(uint32_t)property value:(float)value;
- (long long)addLight:(uint32_t)kind;
- (NSArray<NSNumber*>*)lightInfo:(long long)layer;
- (void)setLightParam:(long long)layer param:(uint32_t)param value:(float)value;
/// Lente da câmera 3D (9 valores, ver Engine::query_camera_lens): mm, FOV°,
/// DOF, foco (px), f/, desfoque ×, px/m, máscara de trilhas com keyframe,
/// ativa. Vazio se a camada não é câmera.
- (NSArray<NSNumber*>*)cameraLens:(long long)layer NS_SWIFT_NAME(cameraLens(_:));
/// Pick Focus: distância no eixo ótico até o 3D sob o ponto (px da
/// composição); < 0 = nada 3D ali. Só mede; grave com setCameraParam.
- (float)pickFocusDistance:(long long)layer x:(float)x y:(float)y NS_SWIFT_NAME(pickFocusDistance(_:x:y:));
/// LayerSetCameraParam: 0 mm, 1 DOF (0/1), 2 distância de foco, 3 f/, 4 desfoque ×.
- (void)setCameraParam:(long long)layer param:(uint32_t)param value:(float)value NS_SWIFT_NAME(setCameraParam(_:param:value:));
- (long long)addCamera;
- (long long)addNull:(BOOL)threeD;
/// "Vincular a novo nulo": cria um nulo no centro (mundo) das camadas e faz
/// dele o pai de todas — nada sai do lugar na tela; nulo 3D se alguma camada
/// é 3D; um passo de desfazer. Devolve o id do nulo, ou −Errc.
- (long long)parentToNewNull:(NSArray<NSNumber*>*)layerIds;
/// "Escalonar": cascata de `stepFrames` na ordem de `layerIds` (a primeira
/// fica); `keysOnly` = só a animação anda. Um passo de desfazer. Devolve
/// quantas camadas andaram, ou −Errc.
- (int)staggerLayers:(NSArray<NSNumber*>*)layerIds stepFrames:(int)stepFrames keysOnly:(BOOL)keysOnly;
/// Shared timing arrangement (0 starts, 1 sequence, 2 ends, 3/4 distribution, 5/6 playhead).
- (int)arrangeLayerTimes:(NSArray<NSNumber*>*)layerIds mode:(uint32_t)mode playhead:(int64_t)playhead;
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
- (BOOL)setMaskParam:(long long)layerId mask:(uint32_t)mask param:(uint32_t)param value:(float)value;
- (BOOL)toggleMaskParamKey:(long long)layerId mask:(uint32_t)mask param:(uint32_t)param;
// Rig 2D (camada de imagem; Engine::query_rig e família). Juntas: 5 floats
// cada (id, pai ou −1, x, y na composição, key no cabeçote).
- (NSArray<NSNumber*>*)rigJoints:(long long)layerId bind:(BOOL)bind;
- (int32_t)rigAddJoint:(long long)layerId parent:(int32_t)parent x:(float)x y:(float)y;
- (BOOL)rigMoveJoint:(long long)layerId joint:(int32_t)joint x:(float)x y:(float)y continuing:(BOOL)continuing;
- (BOOL)rigRemoveJoint:(long long)layerId joint:(int32_t)joint;
- (BOOL)rigClear:(long long)layerId;
- (int32_t)rigAutoHumanoid:(long long)layerId NS_SWIFT_NAME(rigAutoHumanoid(_:));
- (BOOL)rigPoseJoint:(long long)layerId joint:(int32_t)joint x:(float)x y:(float)y continuing:(BOOL)continuing;
- (void)setRigSetupLayer:(long long)layerId;
// Malha de deformação (Engine::query_mesh_warp e família): 4 floats de cabeçalho
// {linhas, colunas, key no cabeçote, animada} + 10 por vértice, normalizados à
// caixa da camada. `grip` 0 = vértice, 1..4 = alça esquerda, direita, cima, baixo.
- (NSArray<NSNumber*>*)meshWarp:(long long)layerId effect:(int32_t)effectId NS_SWIFT_NAME(meshWarp(_:effect:));
- (BOOL)meshWarpDrag:(long long)layerId effect:(int32_t)effectId vertex:(int32_t)vertex grip:(int32_t)grip u:(float)u v:(float)v autoKey:(BOOL)autoKey continuing:(BOOL)continuing NS_SWIFT_NAME(meshWarpDrag(_:effect:vertex:grip:u:v:autoKey:continuing:));
- (BOOL)meshWarpReset:(long long)layerId effect:(int32_t)effectId NS_SWIFT_NAME(meshWarpReset(_:effect:));
// Fantoche (Engine::query_puppet e família; u, v = fração da caixa da camada):
// pinos = 4 floats cada {índice, u, v, key no cabeçote}; malha = 6 por triângulo.
- (NSArray<NSNumber*>*)puppetPins:(long long)layerId effect:(int32_t)effectId NS_SWIFT_NAME(puppetPins(_:effect:));
- (NSArray<NSNumber*>*)puppetMesh:(long long)layerId effect:(int32_t)effectId NS_SWIFT_NAME(puppetMesh(_:effect:));
- (int32_t)puppetAddPin:(long long)layerId effect:(int32_t)effectId u:(float)u v:(float)v NS_SWIFT_NAME(puppetAddPin(_:effect:u:v:));
- (BOOL)puppetMovePin:(long long)layerId effect:(int32_t)effectId pin:(int32_t)pin u:(float)u v:(float)v autoKey:(BOOL)autoKey continuing:(BOOL)continuing NS_SWIFT_NAME(puppetMovePin(_:effect:pin:u:v:autoKey:continuing:));
- (BOOL)puppetRemovePin:(long long)layerId effect:(int32_t)effectId pin:(int32_t)pin NS_SWIFT_NAME(puppetRemovePin(_:effect:pin:));
// Formas 3D (Engine::add_shape3d e família; scene3d/Shape3D.hpp). Receita:
// [forma, nº de partes, 5 por parte (RGBA sRGB, 1 = tem imagem)]. Partes no
// cabeçote: 14 floats cada (Engine::kShapePartFloats).
- (long long)addShape3D:(uint32_t)kind name:(NSString*)name NS_SWIFT_NAME(addShape3D(kind:name:));
- (NSArray<NSNumber*>*)shape3D:(long long)layerId NS_SWIFT_NAME(shape3D(_:));
- (BOOL)setShape3DPartStyle:(long long)layerId part:(int32_t)part color:(nullable NSArray<NSNumber*>*)color image:(nullable NSString*)image NS_SWIFT_NAME(setShape3DPartStyle(_:part:color:image:));
- (NSArray<NSNumber*>*)shape3DParts:(long long)layerId NS_SWIFT_NAME(shape3DParts(_:));
- (BOOL)setShape3DPart:(long long)layerId part:(int32_t)part values:(NSArray<NSNumber*>*)values mask:(uint32_t)mask continuing:(BOOL)continuing NS_SWIFT_NAME(setShape3DPart(_:part:values:mask:continuing:));
- (int32_t)toggleShape3DPartKey:(long long)layerId part:(int32_t)part NS_SWIFT_NAME(toggleShape3DPartKey(_:part:));
- (BOOL)resetShape3DPart:(long long)layerId part:(int32_t)part NS_SWIFT_NAME(resetShape3DPart(_:part:));
- (NSArray<NSNumber*>*)shape3DPartGizmo:(long long)layerId part:(int32_t)part length:(float)length localSpace:(BOOL)localSpace NS_SWIFT_NAME(shape3DPartGizmo(_:part:length:localSpace:));
- (NSArray<NSNumber*>*)shape3DPartMove:(long long)layerId part:(int32_t)part axis:(uint32_t)axis amount:(float)amount NS_SWIFT_NAME(shape3DPartMove(_:part:axis:amount:));
// Divide o cubo em `count` fatias (2..16) no eixo `axis` (0 X, 1 Y, 2 Z): a camada vira um
// nulo 3D com as fatias filhas (Engine::split_shape3d). Id do nulo, ou -1.
- (long long)splitShape3D:(long long)layerId axis:(uint32_t)axis count:(uint32_t)count NS_SWIFT_NAME(splitShape3D(_:axis:count:));
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
/// Preset de efeitos com SÓ o efeito `effectId` (id da instância) e os keyframes dele. "" = não existe.
- (NSString*)saveEffectPreset:(long long)layerId effect:(uint32_t)effectId name:(NSString*)name NS_SWIFT_NAME(saveEffectPreset(_:effect:name:));
/// XML do Alight Motion (ou os bytes do pacote .zip/.amproj) → envelope JSON
/// {"preset", "name", "layer", "mapped", "skipped", "warnings", "error"}; "preset" vai para applyPreset.
- (NSString*)importAlightMotion:(NSData*)data NS_SWIFT_NAME(importAlightMotion(_:));
- (NSArray<NSNumber*>*)parseCaptionPreset:(NSString*)json NS_SWIFT_NAME(parseCaptionPreset(_:));
- (NSString*)makeCaptionPreset:(NSString*)name options:(NSDictionary<NSString*, NSNumber*>*)options NS_SWIFT_NAME(makeCaptionPreset(_:options:));
- (NSArray<NSNumber*>*)parseCurvePreset:(NSString*)json NS_SWIFT_NAME(parseCurvePreset(_:));
- (NSString*)makeCurvePreset:(NSString*)name interpolation:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles NS_SWIFT_NAME(makeCurvePreset(_:interpolation:handles:));
/// O trecho de curva amostrado pelo motor (`sample_keyframe_ease`): `count` valores em t = i/(count−1).
/// handles = [x1, y1, x2, y2, força]; [] = pedido inválido.
- (NSArray<NSNumber*>*)sampleEase:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles count:(uint32_t)count NS_SWIFT_NAME(sampleEase(_:handles:count:));
- (NSString*)applyPreset:(long long)layerId json:(NSString*)json duration:(int64_t)duration;
- (NSString*)trackPoint:(long long)layerId x:(float)x y:(float)y stabilize:(BOOL)stabilize;
- (NSDictionary<NSString*, id>*)cameraTrackingStatus;
- (BOOL)startMotionTrack:(long long)layer tool:(uint32_t)tool model:(uint32_t)model backward:(BOOL)backward points:(NSArray<NSNumber*>*)points feature:(float)feature search:(float)search;
- (void)cancelMotionTrack;
- (BOOL)restoreMotionTrack:(long long)layer;
- (NSDictionary<NSString*, id>*)motionTrackStatus;
- (NSString*)applyMotionTrack:(long long)target apply:(uint32_t)apply lock:(BOOL)lock smooth:(float)smooth maxScale:(float)maxScale crop:(uint32_t)crop;
- (NSArray<NSNumber*>*)gizmo:(long long)layerId length:(float)length NS_SWIFT_NAME(gizmo(_:length:));
- (NSArray<NSNumber*>*)gizmo:(long long)layerId length:(float)length localSpace:(BOOL)localSpace NS_SWIFT_NAME(gizmo(_:length:localSpace:));
- (NSArray<NSNumber*>*)gizmoMoveLocal:(long long)layerId axis:(uint32_t)axis amount:(float)amount NS_SWIFT_NAME(gizmoMoveLocal(_:axis:amount:));
/// Trackball do gizmo de girar (core/Trackball.hpp): origem (2), eixos na vista A (9),
/// frame F (9) e Rotação XYZ (3) — 23 números, ou vazio sem gizmo 3D.
- (NSArray<NSNumber*>*)trackball:(long long)layerId NS_SWIFT_NAME(trackball(_:));
/// Um passo do arrasto: 27 argumentos → Rotação XYZ + Q acumulado (7), ou vazio.
- (NSArray<NSNumber*>*)trackballDrag:(NSArray<NSNumber*>*)args NS_SWIFT_NAME(trackballDrag(_:));
/// Parte sob o dedo: 0..2 anel X/Y/Z, 3 anel da vista, 4 esfera, −1 nada.
- (int)trackballHit:(NSArray<NSNumber*>*)axes x:(float)x y:(float)y radius:(float)radius tolerance:(float)tolerance NS_SWIFT_NAME(trackballHit(_:x:y:radius:tolerance:));
- (void)cancelCameraTracking;
- (BOOL)refineCameraTrack:(BOOL)remove motion:(uint32_t)motion fov:(float)fov;
- (NSArray<NSNumber*>*)cameraTrackTarget:(long long)frame;
- (BOOL)calibrateCameraScene:(uint32_t)operation distance:(float)distance;
- (BOOL)placeModelOnTrack:(long long)layer NS_SWIFT_NAME(placeModelOnTrack(_:));
- (NSString*)applyCameraTracking;
- (BOOL)restoreCameraTrackForLayer:(long long)layerId NS_SWIFT_NAME(restoreCameraTrack(forLayer:));
- (NSArray<NSNumber*>*)cameraTrackDetailsAtFrame:(long long)frame NS_SWIFT_NAME(cameraTrackDetails(atFrame:));
- (uint32_t)selectCameraTrackPoints:(NSArray<NSNumber*>*)ids operation:(uint32_t)operation NS_SWIFT_NAME(selectCameraTrackPoints(_:operation:));
- (NSString*)createCameraTrackObject:(uint32_t)kind NS_SWIFT_NAME(createCameraTrackObject(_:));
- (NSArray<NSNumber*>*)cameraFeaturesAtFrame:(long long)frame NS_SWIFT_NAME(cameraFeatures(atFrame:));
- (NSString*)applyCameraSelectionAtFrame:(long long)frame x0:(float)x0 y0:(float)y0 x1:(float)x1 y1:(float)y1 NS_SWIFT_NAME(applyCameraSelection(atFrame:x0:y0:x1:y1:));
- (NSString*)trackMask:(long long)layerId mask:(uint32_t)mask mode:(uint32_t)mode;
- (NSString*)layerMediaPath:(long long)layerId;
/// "Substituir mídia" (EngineMedia.cpp): vídeo ou foto novo na MESMA camada
/// (transform, keyframes, efeitos, máscaras e tempo ficam). Id ou −Errc.
- (long long)replaceLayer:(long long)layerId withVideo:(NSString*)path name:(NSString*)name;
- (long long)replaceLayer:(long long)layerId withImageFile:(NSString*)path name:(NSString*)name;
/// Arquivo de origem da camada de vídeo/áudio/imagem ("" = nenhum).
- (NSString*)layerSourcePath:(long long)layerId;
/// Mídias de um `.aurea` fechado: 4 strings por mídia (gravado, legível, nome, tipo). nil = não abre.
- (nullable NSArray<NSString*>*)projectFileMedia:(NSString*)path;
/// Arquivo do projeto (ProjectPackage.cpp): [erro (0 = ok), incluídas, puladas].
/// `media` = 3 strings por mídia (gravado, legível agora, nome).
+ (NSArray<NSNumber*>*)exportProjectPackage:(NSString*)projectPath to:(NSString*)outPath title:(NSString*)title
                                  appVersion:(NSString*)appVersion media:(NSArray<NSString*>*)media;
/// [erro (0 = ok), título, versão do app, religadas, ausentes]. Nunca sobrescreve `projectOut`.
+ (NSArray<NSString*>*)importProjectPackage:(NSString*)packagePath project:(NSString*)projectOut mediaDir:(NSString*)mediaDir;
- (NSArray<NSDictionary<NSString*, id>*>*)parseSRT:(NSString*)srt;
- (BOOL)isFillerWord:(NSString*)word NS_SWIFT_NAME(isFillerWord(_:));
- (NSString*)createCaptions:(long long)layerId words:(NSArray<NSDictionary<NSString*, id>*>*)words options:(NSDictionary<NSString*, NSNumber*>*)options;
- (uint32_t)captionCount:(long long)layerId;
- (NSArray<NSDictionary<NSString*, id>*>* _Nullable)transcribeLocal:(long long)layerId model:(NSString*)model language:(NSString*)language error:(NSError* _Nullable * _Nullable)error;
- (int)captionProgress:(BOOL)cancel;
- (NSString*)captionTracks;
- (NSString*)saveCaptionBundle:(long long)layer name:(NSString*)name;
- (BOOL)applyCaptionBundle:(long long)layer data:(NSString*)data;
- (BOOL)editCaptionTrack:(long long)layer command:(NSString*)command;
- (void)removeCaptions:(long long)layerId;
- (NSArray<NSNumber*>*)trackCurve:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param from:(int32_t)from to:(int32_t)to;
- (NSArray<NSNumber*>*)trackEasing:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time;
/// action: 0 valor, 1 apagar, 2 mover, 3 interpolação, 4 inserir (cria a track se faltar).
- (void)editTrackKey:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time action:(uint32_t)action value:(float)value targetTime:(int32_t)targetTime interpolation:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles;
- (NSDictionary<NSString*, id>*)expression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param;
- (NSString*)setExpression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param source:(NSString*)source;
- (void)enableExpression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param enabled:(BOOL)enabled;
- (NSDictionary<NSString*, id>*)setExpressions:(long long)layerId tracks:(NSArray<NSNumber*>*)tracks source:(NSString*)source NS_SWIFT_NAME(setExpressions(_:tracks:source:));
- (BOOL)enableExpressions:(long long)layerId tracks:(NSArray<NSNumber*>*)tracks enabled:(BOOL)enabled NS_SWIFT_NAME(enableExpressions(_:tracks:enabled:));
- (NSDictionary<NSString*, id>*)checkExpressionSyntax:(NSString*)source NS_SWIFT_NAME(checkExpressionSyntax(_:));
- (NSArray<NSNumber*>*)timeRemap:(long long)layerId;
- (int32_t)editTimeRemap:(long long)layerId index:(int32_t)index time:(int64_t)time value:(float)value interpolation:(int32_t)interpolation;
- (BOOL)setTimeRemapValue:(long long)layerId time:(int64_t)time value:(float)value;
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
- (long long)importPSD:(NSString*)path name:(NSString*)name;
- (NSString*)foregroundModelDirectory;
- (long long)createGrid:(NSArray<NSNumber*>*)layers;
- (NSArray<NSNumber*>*)textPath:(long long)layerId;
- (BOOL)setTextPath:(long long)layerId target:(long long)target offset:(float)offset perpendicular:(BOOL)perpendicular reversed:(BOOL)reversed;
- (BOOL)setTextSpan:(long long)layerId start:(uint32_t)start end:(uint32_t)end color:(NSArray<NSNumber*>*)color weight:(uint32_t)weight scale:(float)scale;
- (BOOL)clearTextSpans:(long long)layerId start:(uint32_t)start end:(uint32_t)end;
- (BOOL)setText3DColor:(long long)layerId region:(uint32_t)region values:(NSArray<NSNumber*>*)values;
- (BOOL)applyText3DPreset:(long long)layerId preset:(uint32_t)preset;
/// Animação de texto 3D: 3 modos (entrada, saída, loop) × 5 floats — preset (−1
/// nenhum), unidade (1 letra, 2 palavra, 3 linha), duração (s), atraso (ms),
/// unidades do texto. Vazio = a camada não é texto 3D.
- (NSArray<NSNumber*>*)text3DAnim:(long long)layerId;
/// Um toque aplica o preset no modo (preset < 0 remove); as letras viram nós próprios.
- (BOOL)applyText3DAnim:(long long)layerId preset:(int32_t)preset mode:(uint32_t)mode unit:(uint32_t)unit
               duration:(float)duration stagger:(float)stagger;
- (NSArray<NSNumber*>*)modelShadows:(long long)layerId;
- (BOOL)setModelShadows:(long long)layerId cast:(BOOL)cast receive:(BOOL)receive;
/// Mostrar interior (dupla face) do objeto 3D: 1 mostra, 0 não, −1 não é objeto 3D.
- (int32_t)modelInterior:(long long)layerId;
/// Liga/desliga o interior (um passo de desfazer).
- (BOOL)setModelInterior:(long long)layerId on:(BOOL)on;
/// Miniatura (bola de estúdio) do material `material` do objeto 3D, `size` ×
/// `size` em RGBA8 sRGB com alfa reto. Pode ser chamada fora da main thread.
- (nullable NSData*)materialPreview:(long long)layerId material:(uint32_t)material size:(uint32_t)size;
/// A bola de um material pronto do texto 3D (0..6), no mesmo formato.
- (nullable NSData*)text3DPresetPreview:(uint32_t)preset size:(uint32_t)size;
- (int64_t)removeGaps;
- (BOOL)editClipTime:(long long)layerId operation:(uint32_t)operation amount:(int64_t)amount previous:(long long)previous next:(long long)next;
- (BOOL)trimComposition:(int64_t)frame;
- (long long)detectBeatsForLayer:(long long)layerId bpm:(double*)bpm NS_SWIFT_NAME(detectBeats(forLayer:bpm:));
/// [enabled, shutterAngle, shutterPhase, samples, adaptiveLimit, previewSamples].
- (NSArray<NSNumber*>*)motionBlurSettings;
- (void)setMotionBlurSettings:(BOOL)enabled shutter:(float)shutter;
- (BOOL)setMotionBlurSettings:(BOOL)enabled shutter:(float)shutter phase:(float)phase
                      samples:(uint32_t)samples adaptiveLimit:(uint32_t)adaptiveLimit;
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
/// Ao contrário do Remapear tempo: espelha a curva no clipe (liga se preciso).
- (BOOL)reverseTimeRemapForLayer:(long long)layerId NS_SWIFT_NAME(reverseTimeRemap(forLayer:));
/// Manter o tom do áudio fora de 1× (velocidade/remapeamento).
- (void)setKeepPitchForLayer:(long long)layerId on:(BOOL)on NS_SWIFT_NAME(setKeepPitch(forLayer:on:));
- (void)setVectorBlurForLayer:(long long)layerId amount:(float)amount NS_SWIFT_NAME(setVectorBlur(forLayer:amount:));
- (void)toggleMarker:(int64_t)frame;
/// Tap de batida tocando: marca o instante que soa, sem pausar nem alternar. Quadro ou -1.
- (int64_t)markBeatLive;
- (BOOL)editMarker:(int64_t)from to:(int64_t)to color:(uint32_t)color label:(NSString*)label NS_SWIFT_NAME(editMarker(from:to:color:label:));
- (BOOL)deleteMarker:(int64_t)frame;
- (NSString*)markerLabel:(int64_t)frame;
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
- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps
            aiUpscale:(uint32_t)aiUpscale;
- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps
            aiUpscale:(uint32_t)aiUpscale trimToContent:(BOOL)trimToContent;
/// `quality` 0 Baixa / 1 Normal / 2 Alta: a taxa automática do motor
/// (BitratePolicy.hpp) quando `bitrateMbps` é 0.
- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps
            aiUpscale:(uint32_t)aiUpscale trimToContent:(BOOL)trimToContent
              quality:(uint32_t)quality;
/// `safeMode` 0..2: o modo de segurança que o motor sugeriu depois de o
/// encoder travar (bits 16..17 de AureaExportFlags; export/ExportWatchdog.hpp).
- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps
            aiUpscale:(uint32_t)aiUpscale trimToContent:(BOOL)trimToContent
              quality:(uint32_t)quality safeMode:(uint32_t)safeMode;
/// A taxa de vídeo (bps) que o export usaria — a mesma regra do encoder, para a
/// tela mostrar o tamanho estimado sem conta própria.
- (uint32_t)exportBitrateBps:(uint32_t)width height:(uint32_t)height fps:(double)fps
                       codec:(AureaExportCodec)codec quality:(uint32_t)quality
                  customMbps:(uint32_t)customMbps;
- (long long)exportDuration:(BOOL)trimToContent;
- (void)cancelExport;
/// Export como imagem (motor: export/ImageEncode.hpp). `format` 0 = quadro do
/// playhead em PNG, 1 = sequência PNG num .zip, 2 = GIF. `shortSide` 0 = a
/// resolução da composição; `maxWidth` = largura máxima do GIF; `fps` 0 =
/// padrão do formato. Progresso e cancelamento são os do vídeo
/// (`-exportProgress`, `-cancelExport`). Devolve o código de erro (0 = começou).
- (int32_t)startImageExportTo:(NSString*)path format:(uint32_t)format shortSide:(uint32_t)shortSide
                     maxWidth:(uint32_t)maxWidth fps:(double)fps trimToContent:(BOOL)trimToContent;
/// O plano pela regra do motor: "width", "height", "frames", "alpha", "bytes", "fps".
/// Nulo sem composição.
- (nullable NSDictionary<NSString*, NSNumber*>*)imageExportPlan:(uint32_t)format shortSide:(uint32_t)shortSide
                                                        maxWidth:(uint32_t)maxWidth fps:(double)fps
                                                   trimToContent:(BOOL)trimToContent;
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
