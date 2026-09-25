// =============================================================================
//  Aurea / platform / ios / bridge / AureaEngine.mm
//
//  A implementação da superfície ObjC. É o espelho do `AureaEngine.kt` +
//  `aurea_jni.cpp` do Android: traduz tipos e chama o motor. NENHUMA regra de
//  edição mora aqui — se uma regra aparecesse neste arquivo, o iOS e o Android
//  divergiriam no mesmo projeto, que é exatamente o que a arquitetura evita.
//
//  DUAS ROTAS PARA O MOTOR, e quando usar cada uma:
//
//   1. BLOCO DE COMANDOS (`beginBatch`/`flush`): tudo que a UI mexe em alta
//      frequência durante um gesto — transform, propriedades de camada,
//      keyframes, parâmetros de efeito, texto, composição, playback. Uma
//      travessia de fronteira por frame, com desfazer correto e sem estado
//      meio-aplicado (é o desenho do command/Command.hpp).
//
//   2. CHAMADA DIRETA: importação e consultas (que são síncronas e caras por
//      natureza) e o que não tem comando — máscaras, rótulo/solo/guia/ajuste,
//      animadores de texto, presets, export. É a mesma divisão do JNI.
//
//  Threads: chamado só da MAIN. O motor tem as threads dele (render, decode,
//  áudio, export) e trata o próprio locking — a ponte não guarda estado que
//  precise de lock além do bloco de comandos, que é da UI.
// =============================================================================
#import "AureaEngine.h"

#import "AureaBridge.h"

#include "aurea/core/Log.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using aurea::Command;
using aurea::CommandType;
using aurea::LayerId;
// Os tipos curtos do núcleo (core/Types.hpp): usá-los aqui deixa claro que a
// ponte não inventa largura nenhuma — é a mesma régua do motor.
using aurea::f32;
using aurea::f64;
using aurea::i64;
using aurea::u8;
using aurea::u32;
using aurea::u64;
using aurea::usize;

// =============================================================================
// Chaves dos dicionários. Os nomes são os do BridgePods / do DeviceReport.kt:
// a mesma informação tem o mesmo nome nas duas plataformas.
// =============================================================================
NSString* const AureaPerfPreviewFps     = @"previewFps";
NSString* const AureaPerfCpuMs          = @"cpuMs";
NSString* const AureaPerfGpuMs          = @"gpuMs";
NSString* const AureaPerfDecodeMs       = @"decodeMs";
NSString* const AureaPerfDrawCalls      = @"drawCalls";
NSString* const AureaPerfTriangles      = @"triangles";
NSString* const AureaPerfParticles      = @"particles";
NSString* const AureaPerfPipelines      = @"pipelines";
NSString* const AureaPerfShaders        = @"shaders";
NSString* const AureaPerfPassesExecuted = @"passesExecuted";
NSString* const AureaPerfPassesCulled   = @"passesCulled";
NSString* const AureaPerfDroppedFrames  = @"droppedFrames";
NSString* const AureaPerfGpuMemoryBytes = @"gpuMemoryBytes";
NSString* const AureaPerfCpuMemoryBytes = @"cpuMemoryBytes";
NSString* const AureaPerfDecoder        = @"decoder";
NSString* const AureaPerfGpuName        = @"gpuName";

NSString* const AureaLayerId            = @"id";
NSString* const AureaLayerKind          = @"kind";
NSString* const AureaLayerName          = @"name";
NSString* const AureaLayerStartFrame    = @"startFrame";
NSString* const AureaLayerEndFrame      = @"endFrame";
NSString* const AureaLayerOffsetFrames  = @"offsetFrames";
NSString* const AureaLayerOpacity       = @"opacity";
NSString* const AureaLayerFlags         = @"flags";
NSString* const AureaLayerEffectCount   = @"effectCount";
NSString* const AureaLayerMaskCount     = @"maskCount";
NSString* const AureaLayerKeyframeCount = @"keyframeCount";
NSString* const AureaLayerBlendMode     = @"blendMode";
NSString* const AureaLayerSelected      = @"selected";
NSString* const AureaLayerVisible       = @"visible";
NSString* const AureaLayerLocked        = @"locked";
NSString* const AureaLayerSolo          = @"solo";
NSString* const AureaLayerAnimated      = @"animated";
NSString* const AureaLayerThreeD        = @"threeD";
NSString* const AureaLayerLabel         = @"label";

NSString* const AureaKeyframeProperty      = @"property";
NSString* const AureaKeyframeEffectIndex   = @"effectIndex";
NSString* const AureaKeyframeTime          = @"time";
NSString* const AureaKeyframeValue         = @"value";
NSString* const AureaKeyframeInterpolation = @"interpolation";

NSString* const AureaDetailKind             = @"kind";
NSString* const AureaDetailStartFrame       = @"startFrame";
NSString* const AureaDetailEndFrame         = @"endFrame";
NSString* const AureaDetailPosition         = @"position";
NSString* const AureaDetailScale            = @"scale";
NSString* const AureaDetailRotation         = @"rotation";
NSString* const AureaDetailAnchor           = @"anchor";
NSString* const AureaDetailOpacity          = @"opacity";
NSString* const AureaDetailSkew             = @"skew";
NSString* const AureaDetailAnimatedMask     = @"animatedMask";
NSString* const AureaDetailKeyAtPlayhead    = @"keyAtPlayhead";
NSString* const AureaDetailSourceSize       = @"sourceSize";
NSString* const AureaDetailSourceFps        = @"sourceFps";
NSString* const AureaDetailEffectCount      = @"effectCount";
NSString* const AureaDetailMaskCount        = @"maskCount";
NSString* const AureaDetailLocalPlayhead    = @"localPlayhead";
NSString* const AureaDetailParentId         = @"parentId";
NSString* const AureaDetailAudioGain        = @"audioGain";
NSString* const AureaDetailAudioVolume      = @"audioVolume";
NSString* const AureaDetailAudioPan         = @"audioPan";
NSString* const AureaDetailAudioFlags       = @"audioFlags";
NSString* const AureaDetailSpeed            = @"speed";
NSString* const AureaDetailTimeFlags        = @"timeFlags";
NSString* const AureaDetailShape            = @"shape";
NSString* const AureaDetailCorners          = @"corners";
NSString* const AureaDetailParentAffine     = @"parentAffine";
NSString* const AureaDetailGeomFlags        = @"geomFlags";

NSString* const AureaCompositionId         = @"id";
NSString* const AureaCompositionName       = @"name";
NSString* const AureaCompositionWidth      = @"width";
NSString* const AureaCompositionHeight     = @"height";
NSString* const AureaCompositionFps        = @"fps";
NSString* const AureaCompositionDuration   = @"duration";
NSString* const AureaCompositionBackground = @"background";
NSString* const AureaCompositionSizeCap    = @"sizeCap";
NSString* const AureaCompositionDepth      = @"depth";

NSString* const AureaEffectId         = @"effectId";
NSString* const AureaEffectTypeId     = @"typeId";
NSString* const AureaEffectName       = @"name";
NSString* const AureaEffectCategory   = @"category";
NSString* const AureaEffectEnabled    = @"enabled";
NSString* const AureaEffectParamCount = @"paramCount";
NSString* const AureaEffectKnown      = @"known";

NSString* const AureaParamIndex      = @"index";
NSString* const AureaParamType       = @"type";
NSString* const AureaParamLabel      = @"label";
NSString* const AureaParamUnit       = @"unit";
NSString* const AureaParamId         = @"id";
NSString* const AureaParamValue      = @"value";
NSString* const AureaParamDefault    = @"defaultValue";
NSString* const AureaParamMin        = @"min";
NSString* const AureaParamMax        = @"max";
NSString* const AureaParamAnimated   = @"animated";
NSString* const AureaParamEnumLabels = @"enumLabels";

NSString* const AureaExportRunning     = @"running";
NSString* const AureaExportFinished    = @"finished";
NSString* const AureaExportResult      = @"result";
NSString* const AureaExportFramesTotal = @"framesTotal";
NSString* const AureaExportFramesDone  = @"framesDone";
NSString* const AureaExportFps         = @"fps";
NSString* const AureaExportEtaSeconds  = @"etaSeconds";
NSString* const AureaExportFlags       = @"flags";
NSString* const AureaExportMessage     = @"message";

// =============================================================================
// Helpers
// =============================================================================
namespace {

std::string to_std(NSString* _Nullable s) {
    return s ? std::string(s.UTF8String ? s.UTF8String : "") : std::string{};
}

NSString* to_ns(const std::string& s) {
    // Uma string do motor pode ter bytes inválidos (nome de arquivo vindo de
    // fora); o NSString devolve nil e um "crash de UI" viraria um motivo bobo.
    NSString* out = [NSString stringWithUTF8String:s.c_str()];
    return out ? out : @"";
}

NSString* to_ns(const char* s) {
    if (!s) return @"";
    NSString* out = [NSString stringWithUTF8String:s];
    return out ? out : @"";
}

/// Handle empacotado da UI → LayerId do payload.
LayerId layer_of(long long packed) {
    return LayerId::unpack(static_cast<aurea::u64>(packed));
}

NSArray<NSNumber*>* floats_to_array(const float* v, NSUInteger n) {
    NSMutableArray<NSNumber*>* out = [NSMutableArray arrayWithCapacity:n];
    for (NSUInteger i = 0; i < n; ++i) [out addObject:@(v[i])];
    return out;
}

/// Recorte de `u32` e `f32` num NSDictionary de uma linha de camada.
NSDictionary<NSString*, id>* layer_row_dict(const aurea::bridge::LayerRow& r, NSString* name) {
    using namespace aurea::bridge;
    const u32 flags = r.flags;
    return @{
        AureaLayerId:            @(r.id),
        AureaLayerKind:          @(r.kind),
        AureaLayerName:          name,
        AureaLayerStartFrame:    @(r.startFrame),
        AureaLayerEndFrame:      @(r.endFrame),
        AureaLayerOffsetFrames:  @(r.offsetFrames),
        AureaLayerOpacity:       @(r.opacity),
        AureaLayerFlags:         @(flags),
        AureaLayerEffectCount:   @(r.effectCount),
        AureaLayerMaskCount:     @(r.maskCount),
        AureaLayerKeyframeCount: @(r.keyframeCount),
        AureaLayerBlendMode:     @(r.blendMode),
        AureaLayerSelected:      @((flags & kLayerRowFlagSelected) != 0),
        AureaLayerVisible:       @((flags & kLayerRowFlagVisible) != 0),
        AureaLayerLocked:        @((flags & kLayerRowFlagLocked) != 0),
        AureaLayerSolo:          @((flags & kLayerRowFlagSolo) != 0),
        AureaLayerAnimated:      @((flags & kLayerRowFlagAnimated) != 0),
        AureaLayerThreeD:        @((flags & kLayerRowFlagThreeD) != 0),
        AureaLayerLabel:         @((flags & kLayerRowLabelMask) >> kLayerRowLabelShift),
    };
}

NSDictionary<NSString*, id>* keyframe_row_dict(const aurea::bridge::KeyframeRow& k) {
    return @{
        AureaKeyframeProperty:      @(k.property),
        AureaKeyframeEffectIndex:   @(k.effectIndex),
        @"paramIndex":             @(k.paramIndex),
        AureaKeyframeTime:          @(k.time),
        AureaKeyframeValue:         @(k.value),
        AureaKeyframeInterpolation: @(k.interpolation),
    };
}

NSDictionary<NSString*, id>* effect_row_dict(const aurea::bridge::LayerEffectRow& e, NSString* name) {
    return @{
        AureaEffectId:         @(e.effectId),
        AureaEffectTypeId:     @(e.typeId),
        AureaEffectName:       name,
        AureaEffectEnabled:    @(e.enabled != 0),
        AureaEffectParamCount: @(e.paramCount),
        AureaEffectKnown:      @(e.known != 0),
    };
}

/// O blob de strings de uma consulta é uma concatenação; `offset`/`length`
/// recortam o pedaço de cada linha (o mesmo esquema do JNI).
NSString* slice(const std::vector<char>& blob, u32 offset, u32 length) {
    if (length == 0 || offset >= blob.size()) return @"";
    const usize len = std::min<usize>(length, blob.size() - offset);
    return [[NSString alloc] initWithBytes:blob.data() + offset length:len encoding:NSUTF8StringEncoding] ?: @"";
}

NSDictionary<NSString*, id>* param_row_dict(const aurea::bridge::EffectParamRow& p, const std::vector<char>& blob) {
    NSMutableArray<NSString*>* enums = [NSMutableArray array];
    if (p.enumLength > 0) {
        NSString* joined = slice(blob, p.enumOffset, p.enumLength);
        for (NSString* part in [joined componentsSeparatedByString:@"|"]) [enums addObject:part];
    }
    float value[4]{p.value[0], p.value[1], p.value[2], p.value[3]};
    float def[4]{p.defaultValue[0], p.defaultValue[1], p.defaultValue[2], p.defaultValue[3]};
    return @{
        @"flags":             @(p.flags),
        AureaParamIndex:      @(p.index),
        AureaParamType:       @(p.type),
        AureaParamLabel:      slice(blob, p.labelOffset, p.labelLength),
        AureaParamUnit:       slice(blob, p.unitOffset, p.unitLength),
        AureaParamId:         slice(blob, p.idOffset, p.idLength),
        AureaParamValue:      floats_to_array(value, 4),
        AureaParamDefault:    floats_to_array(def, 4),
        AureaParamMin:        @(p.minValue),
        AureaParamMax:        @(p.maxValue),
        AureaParamAnimated:   @(p.animated != 0),
        AureaParamEnumLabels: enums,
    };
}

} // namespace

// =============================================================================
@implementation AureaEngine {
    std::unique_ptr<aurea::ios::Host> _host;
    aurea::ios::Batch _batch;
    NSString* _cacheDirectory;
    NSString* _documentsDirectory;
    NSString* _lastImportError;
#if DEBUG
    NSDictionary<NSString*, id>* _lastCaptureDiagnostics;
#endif
}

- (instancetype)initWithCacheDirectory:(NSString*)cacheDirectory
                    documentsDirectory:(NSString*)documentsDirectory {
    self = [super init];
    if (self) {
        _cacheDirectory = [cacheDirectory copy] ?: @"";
        _documentsDirectory = [documentsDirectory copy] ?: @"";
        _lastImportError = @"";
    }
    return self;
}

- (void)dealloc {
    // Sem `stop` explícito o motor morre aqui — e morrer aqui é seguro: o Host
    // derruba o Engine antes do backend (a ordem que importa).
    if (_host) _host->shutdown();
}

- (aurea::Engine*)engine {
    return _host ? _host->engine() : nullptr;
}

// =============================================================================
// Ciclo de vida
// =============================================================================
- (BOOL)running {
    return _host && _host->initialized();
}

- (BOOL)startWithDevice:(id<MTLDevice>)device
            refreshRate:(double)refreshRate
                  debug:(BOOL)debug
                  error:(NSString* _Nullable* _Nullable)error {
    if (_host && _host->initialized()) return YES;

    _host = std::make_unique<aurea::ios::Host>();
    if (!_host) {
        if (error) *error = @"sem memoria para a ponte";
        return NO;
    }
    const aurea::Status s = _host->initialize(to_std(_cacheDirectory), to_std(_documentsDirectory),
                                              (__bridge void*)device,
                                              static_cast<float>(refreshRate), debug != NO);
    if (!s.ok()) {
        if (error) *error = to_ns(std::string(_host->last_error()));
        _host.reset();
        return NO;
    }
    return YES;
}

- (void)stop {
    if (!_host) return;
    _batch.clear();
    _host->shutdown();
    _host.reset();
}

- (void)suspend {
    if (auto* e = self.engine) (void)e->suspend();
}

- (void)resume {
    if (auto* e = self.engine) (void)e->resume();
}

- (void)invalidate {
    if (_host) _host->invalidate();
}

- (int64_t)trimMemory:(int32_t)level {
    auto* e = self.engine;
    return e ? static_cast<int64_t>(e->trim_memory(level).total) : 0;
}

- (void)setThermalLevel:(uint32_t)level throttling:(BOOL)throttling {
    if (auto* e = self.engine) e->set_thermal(level, throttling != NO);
}

- (NSString*)deviceSummary {
    auto* e = self.engine;
    if (!e) return @"";
    const aurea::DeviceCapabilities& caps = e->caps();
    std::string s = caps.gpu().deviceName;
    if (!caps.gpu().driverVersion.empty()) s += " · " + caps.gpu().driverVersion;
    if (s.empty()) s = "GPU nao identificada";
    return to_ns(s);
}

// =============================================================================
// Superfície
// =============================================================================
- (BOOL)attachMetalLayer:(CAMetalLayer*)layer width:(int)width height:(int)height {
    if (!_host) return NO;
    // `SurfaceDesc::nativeWindow` é o CAMetalLayer* no iOS. O layer é da VIEW:
    // o motor só o usa enquanto a view existir, e o `detachSurface` espera a
    // GPU largá-lo antes de a view morrer.
    return _host->attach_surface((__bridge void*)layer, static_cast<aurea::u32>(width),
                                 static_cast<aurea::u32>(height)) ? YES : NO;
}

- (void)detachSurface {
    if (_host) _host->detach_surface();
}

- (void)resizeSurfaceWidth:(int)width height:(int)height {
    if (_host) _host->resize_surface(static_cast<aurea::u32>(width), static_cast<aurea::u32>(height));
}

- (BOOL)hasSurface {
    return _host && _host->has_surface();
}

- (void)requestRender {
    if (_host) _host->request_render();
}

// =============================================================================
// Estado
// =============================================================================
- (BOOL)readStatus:(AureaStatus*)out {
    auto* e = self.engine;
    if (!e || !out) return NO;
    aurea::bridge::EngineStatusPOD pod;
    e->fill_status(pod);
    out->state               = pod.state;
    out->lastError           = pod.lastError;
    out->modelRevision       = pod.modelRevision;
    out->thumbnailGeneration = pod.thumbnailGeneration;
    out->compFps             = pod.compFps;
    out->compWidth           = pod.compWidth;
    out->compHeight          = pod.compHeight;
    out->currentFps          = pod.currentFps;
    out->averageFrameMs      = pod.averageFrameMs;
    out->gpuMs               = pod.gpuMs;
    out->cpuMs               = pod.cpuMs;
    out->decodeMs            = pod.decodeMs;
    out->previewWidth        = pod.previewWidth;
    out->previewHeight       = pod.previewHeight;
    out->previewNumerator    = pod.previewNumerator;
    out->previewDenominator  = pod.previewDenominator;
    out->previewAuto         = pod.previewAuto;
    out->playhead            = pod.playhead;
    out->duration            = pod.duration;
    out->playing             = pod.playing;
    out->layerCount          = pod.layerCount;
    out->selectedCount       = pod.selectedCount;
    out->canUndo             = pod.canUndo;
    out->canRedo             = pod.canRedo;
    out->dirty               = pod.dirty;
    out->recoveryAvailable   = pod.recoveryAvailable;
    out->droppedFrames       = pod.droppedFrames;
    out->gpuMemoryBytes      = pod.gpuMemoryBytes;
    out->cpuMemoryBytes      = pod.cpuMemoryBytes;
    return YES;
}

- (NSDictionary<NSString*, id>*)perf {
    auto* e = self.engine;
    if (!e) return @{};
    aurea::bridge::PerfPOD p;
    e->fill_perf(p);
    aurea::bridge::TelemetryPOD t;
    e->fill_telemetry(t);
    return @{
        @"decoder": to_ns(std::string(p.decoder)), @"gpuName": to_ns(std::string(p.gpuName)),
        @"gpuAllocations": @(p.gpuAllocations),
        @"gpuReservedBytes": @(p.gpuReservedBytes),
        @"scene3dBytes": @(p.scene3dBytes),
        @"memoryBudgetMB": @(p.memoryBudgetMB),
        @"heavyScale": @(p.heavyScale),
        @"audioOutputOpen": @(p.audioOutputOpen),
        @"audioMissingBlocks": @(p.audioMissingBlocks),
        @"audioUnderruns": @(p.audioUnderruns),
        @"audioOutputMs": @(p.audioOutputMs),
        @"audioQueuedMs": @(p.audioQueuedMs),
        @"maskCacheMisses": @(p.maskCacheMisses),
        @"maskCacheHits": @(p.maskCacheHits),
        @"flowCacheMisses": @(p.flowCacheMisses),
        @"flowCacheHits": @(p.flowCacheHits),
        @"activeEffects": @(p.activeEffects),
        @"particles": @(p.particles),
        @"culled3D": @(p.culled3D),
        @"triangles3D": @(p.triangles3D),
        @"draws3D": @(p.draws3D),
        @"drawCalls": @(p.drawCalls),
        @"cpuRecordMs": @(p.cpuRecordMs),
        @"cpuPrepareMs": @(p.cpuPrepareMs),
        @"pacingSamples": @(p.pacingSamples),
        @"pacingStdMs": @(p.pacingStdMs),
        @"pacingP99Ms": @(p.pacingP99Ms),
        @"pacingP95Ms": @(p.pacingP95Ms),
        @"pacingP50Ms": @(p.pacingP50Ms),
        @"thermal": @(p.thermal),
        @"layersRendered": @(p.layersRendered),
        @"staleFrames": @(p.staleFrames),
        @"coalesced": @(p.coalesced),
        @"seeks": @(p.seeks),
        @"gpuTimers": @(p.gpuTimers),
        @"hardwareDecoder": @(p.hardwareDecoder),
        @"zeroCopy": @(p.zeroCopy),
        @"pipelinesTotal": @(p.pipelinesTotal),
        @"pipelineCompilesLive": @(p.pipelineCompilesLive),
        @"aliasedTextures": @(p.aliasedTextures),
        @"physicalTextures": @(p.physicalTextures),
        @"transientTextures": @(p.transientTextures),
        @"texturesCreated": @(p.texturesCreated),
        @"passesCulled": @(p.passesCulled),
        @"passesExecuted": @(p.passesExecuted),
        @"transientBytes": @(p.transientBytes),
        @"gpuMemoryBytes": @(p.gpuMemoryBytes),
        @"ramBytes": @(p.ramBytes),
        @"decodedCacheBytes": @(p.decodedCacheBytes),
        @"decodedCacheFrames": @(p.decodedCacheFrames),
        @"previewHeight": @(p.previewHeight),
        @"previewWidth": @(p.previewWidth),
        @"renderAuto": @(p.renderAuto),
        @"renderScaleDen": @(p.renderScaleDen),
        @"renderScaleNum": @(p.renderScaleNum),
        @"droppedRecent": @(p.droppedRecent),
        @"droppedFrames": @(p.droppedFrames),
        @"frameBudgetMs": @(p.frameBudgetMs),
        @"lastSeekMs": @(p.lastSeekMs),
        @"acquireMs": @(p.acquireMs),
        @"presentMs": @(p.presentMs),
        @"outputMs": @(p.outputMs),
        @"compositeMs": @(p.compositeMs),
        @"glowMs": @(p.glowMs),
        @"blurMs": @(p.blurMs),
        @"effectsMs": @(p.effectsMs),
        @"colorConvMs": @(p.colorConvMs),
        @"decodeMs": @(p.decodeMs),
        @"gpuFrameMs": @(p.gpuFrameMs),
        @"cpuFrameMs": @(p.cpuFrameMs),
        @"previewFps": @(p.previewFps),
        AureaPerfPreviewFps:     @(p.previewFps),
        AureaPerfCpuMs:          @(p.cpuFrameMs),
        AureaPerfGpuMs:          @(p.gpuFrameMs),
        AureaPerfDecodeMs:       @(p.decodeMs),
        AureaPerfDrawCalls:      @(p.drawCalls),
        AureaPerfTriangles:      @(p.triangles3D),
        AureaPerfParticles:      @(p.particles),
        AureaPerfPipelines:      @(p.pipelinesTotal),
        AureaPerfShaders:        @(t.shaderCount),
        AureaPerfPassesExecuted: @(p.passesExecuted),
        AureaPerfPassesCulled:   @(p.passesCulled),
        AureaPerfDroppedFrames:  @(p.droppedFrames),
        AureaPerfGpuMemoryBytes: @(p.gpuMemoryBytes),
        AureaPerfCpuMemoryBytes: @(p.ramBytes),
        AureaPerfDecoder:        to_ns(p.decoder),
        AureaPerfGpuName:        to_ns(p.gpuName),
    };
}

#if DEBUG
- (NSDictionary<NSString*, id>*)renderDiagnostics {
    auto* e = self.engine;
    if (!e) return @{ @"engineRunning": @NO, @"capture": _lastCaptureDiagnostics ?: @{} };
    const aurea::EngineStatus status = e->read_status();
    const aurea::EngineTelemetry telemetry = e->read_telemetry();
    return @{
        @"engineRunning": @(self.running), @"hasSurface": @(self.hasSurface),
        @"state": @(static_cast<int32_t>(status.state)),
        @"lastError": @(static_cast<int32_t>(status.lastError)),
        @"lastErrorName": to_ns(std::string(aurea::to_string(status.lastError))),
        @"lastErrorDetail": to_ns(status.lastErrorDetail),
        @"frameIndex": @(telemetry.frame.frameIndex),
        @"shaderCount": @(telemetry.shaderCount),
        @"pipelineCount": @(telemetry.pipelineCount),
        @"shaderFailures": @(telemetry.shaderFailures),
        @"passesExecuted": @(telemetry.frame.passesExecuted),
        @"drawCalls": @(telemetry.frame.drawCalls),
        @"layersRendered": @(telemetry.frame.layersRendered),
        @"capture": _lastCaptureDiagnostics ?: @{}, @"perf": [self perf]
    };
}
#endif

- (NSDictionary<NSString*, NSNumber*>*)deviceReport {
    auto* e = self.engine;
    if (!e) return @{};
    i64 report[aurea::kDeviceReportSlots];
    aurea::write_device_report(e->caps(), report);
    // Os mesmos nomes do DeviceReport.kt: o mesmo número tem o mesmo nome nos
    // dois sistemas.
    return @{
        @"totalCores":         @(report[0]),
        @"performanceCores":   @(report[1]),
        @"efficiencyCores":    @(report[2]),
        @"totalMemoryMb":      @(report[3]),
        @"availableMemoryMb":  @(report[4]),
        @"budgetMb":           @(report[5]),
        @"maxTexture":         @(report[6]),
        @"maxPreviewWidth":    @(report[7]),
        @"maxPreviewHeight":   @(report[8]),
        @"maxExportWidth":     @(report[9]),
        @"maxExportHeight":    @(report[10]),
        @"decodeParallelism":  @(report[11]),
        @"workers":            @(report[12]),
        @"initialScale":       @(report[13]),
        @"tier":               @(report[14]),
        @"limit":              @(report[15]),
        @"bits":               @(report[20]),
        @"exportLimit":        @(report[21]),
        @"hevcEncodeMaxLong":  @(report[22]),
        @"hevcEncodeMaxShort": @(report[23]),
        @"heavyPercent":       @(report[24]),
        @"previewStartDenominator": @(report[25]),
        @"effectPreviewSide":  @(report[26]),
        @"decodeCachePercent": @(report[27]),
        @"thermalTier":        @(report[28]),
        @"shadowSize":         @(report[29]),
        @"proxyAboveShort":    @(report[30]),
        @"maxFrequencyMhz":    @(report[31]),
    };
}

- (NSDictionary<NSString*, id>*)exportProgress {
    auto* e = self.engine;
    if (!e) return @{};
    const aurea::Engine::ExportProgress p = e->export_progress();
    return @{
        AureaExportRunning:     @(p.running),
        AureaExportFinished:    @(p.finished),
        AureaExportResult:      @(static_cast<int32_t>(p.result)),
        AureaExportFramesTotal: @(p.framesTotal),
        AureaExportFramesDone:  @(p.framesDone),
        AureaExportFps:         @(p.fps),
        AureaExportEtaSeconds:  @(p.etaSeconds),
        AureaExportFlags:       @(p.flags),
        AureaExportMessage:     to_ns(std::string(p.message)),
    };
}

// =============================================================================
// Projeto
// =============================================================================
- (BOOL)newProjectWidth:(uint32_t)width height:(uint32_t)height fps:(double)fps title:(NSString*)title {
    auto* e = self.engine;
    if (!e) return NO;
    const std::string t = to_std(title);
    return e->new_project(width, height, fps, t.empty() ? nullptr : t.c_str()).ok() ? YES : NO;
}

- (BOOL)loadProject:(NSString*)path {
    auto* e = self.engine;
    return e && e->load_project(to_std(path).c_str()).ok() ? YES : NO;
}

- (BOOL)saveProject:(NSString*)path {
    [self flush];
    auto* e = self.engine;
    return e && e->save_project(to_std(path).c_str()).ok() ? YES : NO;
}

- (BOOL)autosaveProject {
    auto* e = self.engine;
    return e && e->autosave_project().ok() ? YES : NO;
}

- (BOOL)recoverSession {
    auto* e = self.engine;
    return e && e->recover_session().ok() ? YES : NO;
}

- (void)discardRecovery {
    if (auto* e = self.engine) e->discard_recovery();
}

- (NSString*)compositionName {
    auto* e = self.engine;
    return e ? to_ns(e->current_composition_name()) : @"";
}

- (uint32_t)precompDepth {
    auto* e = self.engine;
    return e ? e->precomp_depth() : 0;
}

- (BOOL)openPrecomp:(long long)layerId {
    auto* e = self.engine;
    return e && e->open_precomp(static_cast<aurea::u64>(layerId)) ? YES : NO;
}

- (BOOL)closePrecomp {
    auto* e = self.engine;
    return e && e->close_precomp() ? YES : NO;
}

- (uint32_t)lastLoadNotice {
    auto* e = self.engine;
    return e ? e->last_load_notice() : 0;
}

- (uint32_t)lastLoadMissingAssets {
    auto* e = self.engine;
    return e ? e->last_load_missing_assets() : 0;
}

// =============================================================================
// Comandos (bloco)
// =============================================================================
- (void)beginBatch {
    _batch.clear();
}

- (NSInteger)flush {
    auto* e = self.engine;
    if (!e || _batch.empty()) return 0;
    return static_cast<NSInteger>(_batch.submit(*e));
}

- (void)undo {
    if (auto* c = _batch.add(CommandType::Undo)) { (void)c; }
    [self flush];
}

- (void)redo {
    if (auto* c = _batch.add(CommandType::Redo)) { (void)c; }
    [self flush];
}

- (void)beginUndoGroup {
    if (auto* c = _batch.add(CommandType::UndoBeginGroup)) { (void)c; }
}

- (void)endUndoGroup {
    if (auto* c = _batch.add(CommandType::UndoEndGroup)) { (void)c; }
}

// =============================================================================
// Reprodução
// =============================================================================
- (void)play {
    if (auto* c = _batch.add(CommandType::PlaybackPlay)) { (void)c; }
    [self flush];
}

- (void)pause {
    if (auto* c = _batch.add(CommandType::PlaybackPause)) { (void)c; }
    [self flush];
}

- (void)togglePlayback {
    if (auto* c = _batch.add(CommandType::PlaybackToggle)) { (void)c; }
    [self flush];
}

- (void)seekToFrame:(int64_t)frame {
    auto* e = self.engine;
    u64 id = 0;
    u32 width = 0, height = 0;
    f64 fps = 0;
    i64 duration = 0;
    f32 background[4]{};
    if (!e || !e->query_composition(id, width, height, fps, duration, background)) return;
    if (auto* c = _batch.add(CommandType::PlaybackSeek)) {
        // The command contract uses nanoseconds; the native UI exposes frames.
        c->seek.time = aurea::tick_at(aurea::FrameIndex{frame}, fps);
    }
    [self flush];
}

- (void)stepFrames:(int32_t)frames {
    if (auto* c = _batch.add(CommandType::PlaybackStep)) {
        c->step.frames = frames;
    }
    [self flush];
}

- (void)scrubBegin {
    if (auto* c = _batch.add(CommandType::PlaybackScrubBegin)) { (void)c; }
    [self flush];
}

- (void)scrubToFrame:(int64_t)frame {
    auto* e = self.engine;
    u64 id = 0;
    u32 width = 0, height = 0;
    f64 fps = 0;
    i64 duration = 0;
    f32 background[4]{};
    if (!e || !e->query_composition(id, width, height, fps, duration, background)) return;
    if (auto* c = _batch.add(CommandType::PlaybackScrub)) {
        c->seek.time = aurea::tick_at(aurea::FrameIndex{frame}, fps);
    }
    [self flush];
}

- (void)scrubEnd {
    if (auto* c = _batch.add(CommandType::PlaybackScrubEnd)) { (void)c; }
    [self flush];
}

- (void)setLoop:(BOOL)loop {
    if (auto* c = _batch.add(CommandType::PlaybackSetLoop)) {
        c->loop.loop = loop != NO;
    }
    [self flush];
}

- (void)setPlaybackSpeed:(float)speed {
    if (auto* c = _batch.add(CommandType::PlaybackSetSpeed)) {
        c->speed.speed = speed;
    }
    [self flush];
}

- (void)setPreviewScaleNumerator:(uint32_t)numerator denominator:(uint32_t)denominator automatic:(BOOL)automatic {
    if (auto* c = _batch.add(CommandType::ViewportSetPreviewScale)) {
        c->preview_scale.scaleNumerator = numerator;
        c->preview_scale.scaleDenominator = denominator;
        c->preview_scale.automatic = automatic != NO;
    }
    [self flush];
}

// =============================================================================
// Camadas
// =============================================================================
- (void)setLayer:(long long)layerId name:(NSString*)name {
    if (auto* c = _batch.add(CommandType::LayerSetName)) {
        c->layer_ref.layer = layer_of(layerId);
        const aurea::ios::StringRef s = _batch.add_string(name.UTF8String);
        c->stringOffset = s.offset;
        c->stringLength = s.length;
    }
    [self flush];
}

- (void)setLayer:(long long)layerId visible:(BOOL)visible {
    if (auto* c = _batch.add(CommandType::LayerSetVisible)) {
        c->layer_visible.layer = layer_of(layerId);
        c->layer_visible.visible = visible != NO;
    }
    [self flush];
}

- (void)setLayer:(long long)layerId locked:(BOOL)locked {
    if (auto* c = _batch.add(CommandType::LayerSetLocked)) {
        c->layer_locked.layer = layer_of(layerId);
        c->layer_locked.locked = locked != NO;
    }
    [self flush];
}

- (void)setLayer:(long long)layerId solo:(BOOL)solo {
    // Sem comando para solo/rótulo/guia/ajuste: são chamadas diretas ao motor,
    // como no JNI do Android.
    if (auto* e = self.engine) (void)e->set_layer_solo(static_cast<aurea::u64>(layerId), solo != NO);
}

- (void)setLayer:(long long)layerId label:(uint32_t)label {
    if (auto* e = self.engine) (void)e->set_layer_label(static_cast<aurea::u64>(layerId), label);
}

- (void)setLayer:(long long)layerId adjustment:(BOOL)on {
    if (auto* e = self.engine) (void)e->set_layer_adjustment(static_cast<aurea::u64>(layerId), on != NO);
}

- (void)setLayer:(long long)layerId guide:(BOOL)on {
    if (auto* e = self.engine) (void)e->set_layer_guide(static_cast<aurea::u64>(layerId), on != NO);
}

- (void)setLayer:(long long)layerId blendMode:(uint32_t)blendMode {
    if (auto* c = _batch.add(CommandType::LayerSetBlendMode)) {
        c->layer_blend.layer = layer_of(layerId);
        c->layer_blend.mode = static_cast<aurea::BlendMode>(blendMode);
    }
    [self flush];
}

- (void)setLayer:(long long)layerId parent:(long long)parentId {
    if (auto* c = _batch.add(CommandType::LayerSetParent)) {
        c->layer_parent.layer = layer_of(layerId);
        c->layer_parent.parent = layer_of(parentId);
    }
    [self flush];
}

- (void)setLayer:(long long)layerId startFrame:(int32_t)start endFrame:(int32_t)end offsetFrames:(int32_t)offset setOffset:(BOOL)setOffset {
    if (auto* c = _batch.add(CommandType::LayerSetTimeRange)) {
        c->layer_range.layer = layer_of(layerId);
        c->layer_range.start = aurea::FrameIndex{start};
        c->layer_range.end = aurea::FrameIndex{end};
        c->layer_range.offset = aurea::FrameIndex{offset};
        c->layer_range.setOffset = setOffset != NO ? 1u : 0u;
    }
    [self flush];
}

- (void)setLayerOrder:(long long)layerId newIndex:(uint32_t)newIndex {
    if (auto* c = _batch.add(CommandType::LayerReorder)) {
        c->layer_reorder.layer = layer_of(layerId);
        c->layer_reorder.newIndex = newIndex;
    }
    [self flush];
}

- (void)splitLayer:(long long)layerId atFrame:(int32_t)frame {
    if (auto* c = _batch.add(CommandType::LayerSplit)) {
        c->layer_split.layer = layer_of(layerId);
        c->layer_split.at = aurea::FrameIndex{frame};
    }
    [self flush];
}

- (void)deleteLayers:(NSArray<NSNumber*>*)layerIds {
    for (NSNumber* n in layerIds) {
        if (auto* c = _batch.add(CommandType::LayerDelete)) {
            c->layer_ref.layer = layer_of(n.longLongValue);
        }
    }
    [self flush];
}

- (void)duplicateLayers:(NSArray<NSNumber*>*)layerIds {
    for (NSNumber* n in layerIds) {
        if (auto* c = _batch.add(CommandType::LayerDuplicate)) {
            c->layer_ref.layer = layer_of(n.longLongValue);
        }
    }
    [self flush];
}

- (void)rippleDeleteLayers:(NSArray<NSNumber*>*)layerIds {
    auto* e = self.engine;
    if (!e) return;
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    (void)e->ripple_delete(ids.data(), static_cast<aurea::u32>(ids.size()));
}

- (void)setEditMode:(BOOL)on {
    if (auto* e = self.engine) e->set_edit_mode(on != NO);
}

// =============================================================================
// Transform
// =============================================================================
- (void)setTransformForLayer:(long long)layerId
                  position:(simd_float3)position
                     scale:(simd_float3)scale
                  rotation:(simd_float3)rotation
                    anchor:(simd_float3)anchor
                   opacity:(float)opacity {
    if (auto* c = _batch.add(CommandType::LayerSetTransform)) {
        c->transform.layer = layer_of(layerId);
        c->transform.x = position.x; c->transform.y = position.y; c->transform.z = position.z;
        c->transform.sx = scale.x; c->transform.sy = scale.y; c->transform.sz = scale.z;
        c->transform.rx = rotation.x; c->transform.ry = rotation.y; c->transform.rz = rotation.z;
        c->transform.ax = anchor.x; c->transform.ay = anchor.y; c->transform.az = anchor.z;
        c->transform.opacity = opacity;
    }
    [self flush];
}

- (void)setPositionForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z {
    if (auto* c = _batch.add(CommandType::LayerSetPosition)) {
        c->position.layer = layer_of(layerId);
        c->position.x = x; c->position.y = y; c->position.z = z;
    }
}

- (void)setScaleForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z {
    if (auto* c = _batch.add(CommandType::LayerSetScale)) {
        c->scale.layer = layer_of(layerId);
        c->scale.sx = x; c->scale.sy = y; c->scale.sz = z;
    }
}

- (void)setRotationForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z {
    if (auto* c = _batch.add(CommandType::LayerSetRotation)) {
        c->rotation.layer = layer_of(layerId);
        c->rotation.rx = x; c->rotation.ry = y; c->rotation.rz = z;
    }
}

- (void)setAnchorForLayer:(long long)layerId x:(float)x y:(float)y z:(float)z {
    if (auto* c = _batch.add(CommandType::LayerSetAnchor)) {
        c->anchor.layer = layer_of(layerId);
        c->anchor.ax = x; c->anchor.ay = y; c->anchor.az = z;
    }
}

- (void)setOpacityForLayer:(long long)layerId value:(float)value {
    if (auto* c = _batch.add(CommandType::LayerSetOpacity)) {
        c->opacity.layer = layer_of(layerId);
        c->opacity.opacity = value;
    }
}

- (void)setSkewForLayer:(long long)layerId x:(float)x y:(float)y {
    if (auto* c = _batch.add(CommandType::LayerSetSkew)) {
        c->skew.layer = layer_of(layerId);
        c->skew.skewX = x; c->skew.skewY = y;
    }
}

// =============================================================================
// Keyframes
// =============================================================================
- (void)insertKeyframeForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time value:(float)value {
    if (auto* c = _batch.add(CommandType::KeyframeInsert)) {
        c->keyframe.track.layer = layer_of(layerId);
        c->keyframe.track.property = static_cast<aurea::TrackProperty>(property);
        c->keyframe.track.effectIndex = aurea::kInvalidIndex;
        c->keyframe.track.effectParamIndex = 0;
        c->keyframe.time = aurea::FrameIndex{time};
        c->keyframe.value = value;
    }
    [self flush];
}

- (void)insertKeyframeForLayer:(long long)layerId effectIndex:(uint32_t)effectIndex
                     paramIndex:(uint32_t)paramIndex time:(int32_t)time value:(float)value {
    if (auto* c = _batch.add(CommandType::KeyframeInsert)) {
        c->keyframe.track.layer = layer_of(layerId);
        c->keyframe.track.property = aurea::TrackProperty::EffectParam;
        c->keyframe.track.effectIndex = effectIndex;
        c->keyframe.track.effectParamIndex = paramIndex;
        c->keyframe.time = aurea::FrameIndex{time};
        c->keyframe.value = value;
    }
    [self flush];
}

- (void)deleteKeyframeForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time {
    if (auto* c = _batch.add(CommandType::KeyframeDelete)) {
        c->keyframe.track.layer = layer_of(layerId);
        c->keyframe.track.property = static_cast<aurea::TrackProperty>(property);
        c->keyframe.track.effectIndex = aurea::kInvalidIndex;
        c->keyframe.track.effectParamIndex = 0;
        c->keyframe.time = aurea::FrameIndex{time};
    }
    [self flush];
}

- (void)moveKeyframeForLayer:(long long)layerId property:(uint32_t)property from:(int32_t)from to:(int32_t)to {
    if (auto* c = _batch.add(CommandType::KeyframeMove)) {
        c->keyframe_move.track.layer = layer_of(layerId);
        c->keyframe_move.track.property = static_cast<aurea::TrackProperty>(property);
        c->keyframe_move.track.effectIndex = aurea::kInvalidIndex;
        c->keyframe_move.track.effectParamIndex = 0;
        c->keyframe_move.fromTime = aurea::FrameIndex{from};
        c->keyframe_move.toTime = aurea::FrameIndex{to};
    }
    [self flush];
}

- (void)setKeyframeValueForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time value:(float)value {
    if (auto* c = _batch.add(CommandType::KeyframeSetValue)) {
        c->keyframe.track.layer = layer_of(layerId);
        c->keyframe.track.property = static_cast<aurea::TrackProperty>(property);
        c->keyframe.track.effectIndex = aurea::kInvalidIndex;
        c->keyframe.track.effectParamIndex = 0;
        c->keyframe.time = aurea::FrameIndex{time};
        c->keyframe.value = value;
    }
    [self flush];
}

- (void)setKeyframeValueForLayer:(long long)layerId effectIndex:(uint32_t)effectIndex
                      paramIndex:(uint32_t)paramIndex time:(int32_t)time value:(float)value {
    if (auto* c = _batch.add(CommandType::KeyframeSetValue)) {
        c->keyframe.track.layer = layer_of(layerId);
        c->keyframe.track.property = aurea::TrackProperty::EffectParam;
        c->keyframe.track.effectIndex = effectIndex;
        c->keyframe.track.effectParamIndex = paramIndex;
        c->keyframe.time = aurea::FrameIndex{time};
        c->keyframe.value = value;
    }
    [self flush];
}

- (void)setKeyframeInterpolationForLayer:(long long)layerId property:(uint32_t)property time:(int32_t)time
                            interpolation:(uint32_t)interpolation
                                      bx1:(float)bx1 by1:(float)by1 bx2:(float)bx2 by2:(float)by2 {
    if (auto* c = _batch.add(CommandType::KeyframeSetInterpolation)) {
        c->keyframe_interp.track.layer = layer_of(layerId);
        c->keyframe_interp.track.property = static_cast<aurea::TrackProperty>(property);
        c->keyframe_interp.track.effectIndex = aurea::kInvalidIndex;
        c->keyframe_interp.track.effectParamIndex = 0;
        c->keyframe_interp.time = aurea::FrameIndex{time};
        c->keyframe_interp.interp = static_cast<aurea::Interpolation>(interpolation);
        c->keyframe_interp.bx1 = bx1; c->keyframe_interp.by1 = by1;
        c->keyframe_interp.bx2 = bx2; c->keyframe_interp.by2 = by2;
    }
    [self flush];
}

// =============================================================================
// Efeitos
// =============================================================================
- (void)addEffect:(uint32_t)effectTypeId toLayer:(long long)layerId atIndex:(uint32_t)index {
    if (auto* c = _batch.add(CommandType::EffectAdd)) {
        c->effect_add.layer = layer_of(layerId);
        c->effect_add.effectType = effectTypeId;
        c->effect_add.index = index;
    }
    [self flush];
}

- (void)removeEffect:(uint32_t)effectId fromLayer:(long long)layerId {
    if (auto* c = _batch.add(CommandType::EffectRemove)) {
        c->effect_ref.layer = layer_of(layerId);
        // O id que atravessa esta fronteira é o do payload: `LayerEffectRow::
        // effectId` é o identificador do efeito DENTRO da camada (é a chave dos
        // keyframes dele), e é esse que a UI devolve.
        c->effect_ref.effect = aurea::EffectId{effectId, 0};
    }
    [self flush];
}

- (void)setEffect:(uint32_t)effectId forLayer:(long long)layerId enabled:(BOOL)enabled {
    if (auto* c = _batch.add(CommandType::EffectSetEnabled)) {
        c->effect_enabled.layer = layer_of(layerId);
        c->effect_enabled.effect = aurea::EffectId{effectId, 0};
        c->effect_enabled.enabled = enabled != NO;
    }
    [self flush];
}

- (void)setEffect:(uint32_t)effectId forLayer:(long long)layerId paramIndex:(uint32_t)paramIndex value:(float)value {
    if (auto* c = _batch.add(CommandType::EffectSetParam)) {
        c->effect_param.layer = layer_of(layerId);
        c->effect_param.effect = aurea::EffectId{effectId, 0};
        c->effect_param.paramIndex = paramIndex;
        c->effect_param.value = value;
    }
}

- (void)setEffectColor:(uint32_t)effectId forLayer:(long long)layerId paramIndex:(uint32_t)paramIndex
                     r:(float)r g:(float)g b:(float)b a:(float)a {
    if (auto* c = _batch.add(CommandType::EffectSetColorParam)) {
        c->effect_color.layer = layer_of(layerId);
        c->effect_color.effect = aurea::EffectId{effectId, 0};
        c->effect_color.paramIndex = paramIndex;
        c->effect_color.r = r; c->effect_color.g = g; c->effect_color.b = b; c->effect_color.a = a;
    }
}

- (void)moveEffect:(uint32_t)effectId inLayer:(long long)layerId toIndex:(uint32_t)newIndex {
    if (auto* c = _batch.add(CommandType::EffectReorder)) {
        c->effect_reorder.layer = layer_of(layerId);
        c->effect_reorder.effect = aurea::EffectId{effectId, 0};
        c->effect_reorder.newIndex = newIndex;
    }
    [self flush];
}

- (void)copyEffects:(long long)layerId {
    if (auto* e = self.engine) (void)e->copy_effects(static_cast<aurea::u64>(layerId));
}

- (void)pasteEffects:(NSArray<NSNumber*>*)layerIds {
    auto* e = self.engine;
    if (!e) return;
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    (void)e->paste_effects(ids.data(), static_cast<aurea::u32>(ids.size()));
}

- (void)copyStyle:(long long)layerId {
    if (auto* e = self.engine) (void)e->copy_style(static_cast<aurea::u64>(layerId));
}

- (void)pasteStyle:(NSArray<NSNumber*>*)layerIds {
    auto* e = self.engine;
    if (!e) return;
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    (void)e->paste_style(ids.data(), static_cast<aurea::u32>(ids.size()));
}

- (void)copyKeyframes:(long long)layerId atFrame:(int32_t)frame {
    if (auto* e = self.engine) (void)e->copy_keyframes(static_cast<aurea::u64>(layerId), frame);
}

- (void)pasteKeyframes:(NSArray<NSNumber*>*)layerIds atFrame:(int32_t)frame {
    auto* e = self.engine;
    if (!e) return;
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    (void)e->paste_keyframes(ids.data(), static_cast<aurea::u32>(ids.size()), frame);
}

- (void)copyLayers:(NSArray<NSNumber*>*)layerIds {
    auto* e = self.engine;
    if (!e) return;
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    (void)e->copy_layers(ids.data(), static_cast<aurea::u32>(ids.size()));
}

- (void)pasteLayers:(int64_t)frame {
    if (auto* e = self.engine) (void)e->paste_layers(frame);
}

- (uint32_t)clipboardState {
    auto* e = self.engine;
    return e ? e->clipboard_state() : 0;
}

// =============================================================================
// Texto / forma / 3D
// =============================================================================
namespace {
NSDictionary<NSString*, id>* font_dictionary(const aurea::text::FontEntry& font) {
    return @{ @"family": [NSString stringWithUTF8String:font.family.c_str()] ?: @"",
              @"style": [NSString stringWithUTF8String:font.style.c_str()] ?: @"",
              @"weight": @(font.weight), @"italic": @(font.italic),
              @"path": [NSString stringWithUTF8String:font.path.c_str()] ?: @"" };
}
}

- (NSArray<NSDictionary<NSString*, id>*>*)availableFonts {
    NSMutableArray* result = [NSMutableArray array];
    if (auto* e = self.engine) for (const auto& font : e->list_fonts()) [result addObject:font_dictionary(font)];
    return result;
}

- (NSDictionary<NSString*, id>*)importFontAtPath:(NSString*)path {
    auto* e = self.engine;
    if (!e) return nil;
    auto result = e->import_font(path.UTF8String);
    return result.ok() ? font_dictionary(*result) : nil;
}

- (NSDictionary<NSString*, id>*)text3DForLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return nil;
    aurea::scene3d::Text3DSpec spec;
    if (!e->query_text3d(static_cast<aurea::u64>(layerId), spec)) return nil;
    return @{ @"content": [NSString stringWithUTF8String:spec.content.c_str()] ?: @"",
              @"fontPath": [NSString stringWithUTF8String:spec.fontPath.c_str()] ?: @"",
              @"depth": @(spec.depth), @"animation": @(spec.animation),
              @"animationDuration": @(spec.animationDuration), @"animationStagger": @(spec.animationStagger),
              @"animationAmount": @(spec.animationAmount), @"metallic": @(spec.metallic),
              @"roughness": @(spec.roughness), @"alignment": @(spec.alignment), @"bevel": @(spec.bevel), @"bevelWidth": @(spec.bevelWidth), @"bevelDepth": @(spec.bevelDepth), @"bevelSegments": @(spec.bevelSegments), @"bevelRoundness": @(spec.bevelRoundness), @"regionMaterials": @(spec.regionMaterials), @"specular": @(spec.specular), @"emissiveStrength": @(spec.emissiveStrength), @"sideMetallic": @(spec.side.metallic), @"sideRoughness": @(spec.side.roughness), @"bevelMetallic": @(spec.bevelMat.metallic), @"bevelRoughness": @(spec.bevelMat.roughness), @"color": @[@(spec.color.x), @(spec.color.y), @(spec.color.z), @(spec.color.w)], @"sideColor": @[@(spec.side.color.x), @(spec.side.color.y), @(spec.side.color.z), @(spec.side.color.w)], @"bevelColor": @[@(spec.bevelMat.color.x), @(spec.bevelMat.color.y), @(spec.bevelMat.color.z), @(spec.bevelMat.color.w)], @"emissive": @[@(spec.emissive.x), @(spec.emissive.y), @(spec.emissive.z), @1] };
}

- (BOOL)setText3DForLayer:(long long)layerId property:(NSString*)property stringValue:(NSString*)stringValue numberValue:(float)numberValue {
    auto* e = self.engine;
    if (!e) return NO;
    aurea::scene3d::Text3DSpec spec;
    if (!e->query_text3d(static_cast<aurea::u64>(layerId), spec)) return NO;
    if ([property isEqualToString:@"content"]) spec.content = stringValue.UTF8String ?: "";
    else if ([property isEqualToString:@"fontPath"]) spec.fontPath = stringValue.UTF8String ?: "";
    else if ([property isEqualToString:@"depth"]) spec.depth = numberValue;
    else if ([property isEqualToString:@"animation"]) spec.animation = static_cast<aurea::u32>(numberValue);
    else if ([property isEqualToString:@"animationDuration"]) spec.animationDuration = numberValue;
    else if ([property isEqualToString:@"animationStagger"]) spec.animationStagger = numberValue;
    else if ([property isEqualToString:@"animationAmount"]) spec.animationAmount = numberValue;
    else if ([property isEqualToString:@"metallic"]) spec.metallic = numberValue;
    else if ([property isEqualToString:@"roughness"]) spec.roughness = numberValue;
    else if ([property isEqualToString:@"alignment"]) spec.alignment = static_cast<aurea::u32>(std::max(0.f, numberValue));
    else if ([property isEqualToString:@"bevel"]) spec.bevel = numberValue > 0.5f;
    else if ([property isEqualToString:@"bevelWidth"]) spec.bevelWidth = numberValue;
    else if ([property isEqualToString:@"bevelDepth"]) spec.bevelDepth = numberValue;
    else if ([property isEqualToString:@"bevelSegments"]) spec.bevelSegments = static_cast<aurea::u32>(std::max(0.f, numberValue));
    else if ([property isEqualToString:@"bevelRoundness"]) spec.bevelRoundness = numberValue;
    else if ([property isEqualToString:@"regionMaterials"]) spec.regionMaterials = numberValue > 0.5f;
    else if ([property isEqualToString:@"specular"]) spec.specular = numberValue;
    else if ([property isEqualToString:@"emissiveStrength"]) spec.emissiveStrength = numberValue;
    else if ([property isEqualToString:@"sideMetallic"]) spec.side.metallic = numberValue;
    else if ([property isEqualToString:@"sideRoughness"]) spec.side.roughness = numberValue;
    else if ([property isEqualToString:@"bevelMetallic"]) spec.bevelMat.metallic = numberValue;
    else if ([property isEqualToString:@"bevelRoughness"]) spec.bevelMat.roughness = numberValue;
    else return NO;
    return e->set_text3d(static_cast<aurea::u64>(layerId), spec).ok();
}

- (BOOL)setText3DColor:(long long)layerId region:(uint32_t)region values:(NSArray<NSNumber*>*)values {
    auto* e = self.engine; aurea::scene3d::Text3DSpec spec;
    if (!e || values.count != 4 || !e->query_text3d(layerId, spec)) return NO;
    aurea::Vec4 color{values[0].floatValue, values[1].floatValue, values[2].floatValue, values[3].floatValue};
    if (region == 0) spec.color = color;
    else if (region == 1) spec.side.color = color;
    else if (region == 2) spec.bevelMat.color = color;
    else if (region == 3) spec.emissive = {color.x, color.y, color.z};
    else return NO;
    return e->set_text3d(layerId, spec).ok();
}
- (BOOL)applyText3DPreset:(long long)layerId preset:(uint32_t)preset {
    auto* e = self.engine; aurea::scene3d::Text3DSpec spec;
    if (!e || preset > 5 || !e->query_text3d(layerId, spec)) return NO;
    // Mesmas seis receitas de Text3DPreset em EditorStore.kt.
    spec.specular = 1; spec.emissive = {0,0,0}; spec.emissiveStrength = 1;
    switch (preset) {
        case 0: spec.bevel = true; spec.regionMaterials = false; spec.color = {.95f,.96f,.98f,1}; spec.metallic = 1; spec.roughness = .05f; break;
        case 1: spec.bevel = true; spec.regionMaterials = false; spec.color = {1,.77f,.34f,1}; spec.metallic = 1; spec.roughness = .18f; break;
        case 2: spec.color = {.78f,.79f,.8f,1}; spec.metallic = 1; spec.roughness = .45f; break;
        case 3: spec.color = {.9f,.1f,.12f,1}; spec.metallic = 0; spec.roughness = .08f; break;
        case 4: spec.color = {.85f,.85f,.86f,1}; spec.metallic = 0; spec.roughness = .92f; spec.specular = .15f; break;
        case 5: spec.color = {.1f,1,.85f,1}; spec.metallic = 0; spec.roughness = .35f; spec.emissive = {.1f,1,.85f}; spec.emissiveStrength = 3.5f; break;
    }
    return e->set_text3d(layerId, spec).ok();
}
- (NSArray<NSNumber*>*)modelShadows:(long long)layerId {
    auto* e = self.engine; float v[2]{}; if (!e || !e->query_model_shadows(layerId, v)) return @[]; return @[@(v[0]), @(v[1])];
}
- (BOOL)setModelShadows:(long long)layerId cast:(BOOL)cast receive:(BOOL)receive {
    auto* e = self.engine; return e && e->set_model_shadows(layerId, cast, receive);
}
- (int64_t)removeGaps { auto* e = self.engine; return e ? e->remove_gaps() : 0; }
- (BOOL)trimComposition:(int64_t)frame { auto* e = self.engine; return e && e->trim_composition(frame); }

- (long long)detectBeatsForLayer:(long long)layerId bpm:(double*)bpm {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const auto result = e->detect_beats(static_cast<aurea::u64>(layerId), bpm);
    return result.ok() ? static_cast<long long>(*result) : -static_cast<long long>(result.status().code());
}

- (NSArray<NSNumber*>*)motionBlurSettings {
    auto* e = self.engine; bool on = false; float shutter = 180; if (!e || !e->query_motion_blur(on, shutter)) return @[]; return @[@(on), @(shutter)];
}
- (void)setMotionBlurSettings:(BOOL)enabled shutter:(float)shutter {
    if (auto* e = self.engine) { (void)e->set_composition_motion_blur(enabled); (void)e->set_shutter_angle(shutter); }
}

- (NSDictionary<NSString*, id>*)textForLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return nil;
    aurea::TextData t;
    if (!e->query_text(static_cast<aurea::u64>(layerId), t)) return nil;
    return @{
        @"content": [NSString stringWithUTF8String:t.content.c_str()] ?: @"",
        @"size": @(t.size),
        @"fontFamily": [NSString stringWithUTF8String:t.fontFamily.c_str()] ?: @"",
        @"fontWeight": @(t.fontWeight),
        @"fontItalic": @(t.fontItalic),
        @"alignment": @(t.alignment),
        @"strokeWidth": @(t.strokeWidth),
        @"color": @[@(t.color.x), @(t.color.y), @(t.color.z), @(t.color.w)],
        @"strokeColor": @[@(t.strokeColor.x), @(t.strokeColor.y), @(t.strokeColor.z), @(t.strokeColor.w)],
    };
}

- (BOOL)setTextFontForLayer:(long long)layerId family:(NSString*)family weight:(uint32_t)weight italic:(BOOL)italic path:(NSString*)path {
    auto* e = self.engine;
    return e && e->set_text_font(static_cast<aurea::u64>(layerId),
                                 family.UTF8String ?: "", weight, italic != NO, path.UTF8String ?: "");
}

- (void)setText:(long long)layerId content:(NSString*)content {
    if (auto* c = _batch.add(CommandType::TextSetContent)) {
        c->layer_ref.layer = layer_of(layerId);
        const aurea::ios::StringRef s = _batch.add_string(content.UTF8String);
        c->stringOffset = s.offset;
        c->stringLength = s.length;
    }
    [self flush];
}

- (void)setText:(long long)layerId size:(float)size {
    if (auto* c = _batch.add(CommandType::TextSetSize)) {
        c->text_size.layer = layer_of(layerId);
        c->text_size.size = size;
    }
}

- (void)setText:(long long)layerId colorR:(float)r g:(float)g b:(float)b a:(float)a {
    if (auto* c = _batch.add(CommandType::TextSetColor)) {
        c->text_color.layer = layer_of(layerId);
        c->text_color.r = r; c->text_color.g = g; c->text_color.b = b; c->text_color.a = a;
    }
}

- (void)setText:(long long)layerId alignment:(uint32_t)alignment {
    if (auto* c = _batch.add(CommandType::TextSetAlignment)) {
        c->text_align.layer = layer_of(layerId);
        c->text_align.alignment = alignment;
    }
}

- (void)setText:(long long)layerId strokeWidth:(float)width {
    if (auto* c = _batch.add(CommandType::TextSetStrokeWidth)) {
        c->text_stroke_width.layer = layer_of(layerId);
        c->text_stroke_width.width = width;
    }
}

- (NSInteger)addTextAnimator:(long long)layerId props:(uint32_t)props {
    if (auto* e = self.engine) return e->add_text_animator(static_cast<aurea::u64>(layerId), props);
    return -1;
}

- (void)removeTextAnimator:(long long)layerId index:(uint32_t)index {
    if (auto* e = self.engine) (void)e->remove_text_animator(static_cast<aurea::u64>(layerId), index);
}

- (void)setTextAnimParam:(long long)layerId index:(uint32_t)index param:(uint32_t)param value:(float)value {
    if (auto* e = self.engine) {
        (void)e->set_text_anim_param(static_cast<aurea::u64>(layerId), index, param, value);
    }
}

- (void)toggleTextAnimKey:(long long)layerId index:(uint32_t)index param:(uint32_t)param {
    if (auto* e = self.engine) {
        (void)e->toggle_text_anim_key(static_cast<aurea::u64>(layerId), index, param);
    }
}

- (BOOL)applyTextPreset:(long long)layerId preset:(uint32_t)preset {
    if (auto* e = self.engine) return e->apply_text_preset(static_cast<aurea::u64>(layerId), preset);
    return NO;
}

- (void)setShape:(long long)layerId param:(uint32_t)param value:(float)value {
    if (auto* c = _batch.add(CommandType::ShapeSetParam)) {
        c->shape_param.layer = layer_of(layerId);
        c->shape_param.param = param;
        c->shape_param.value = value;
    }
}

- (void)setShape:(long long)layerId fillR:(float)r g:(float)g b:(float)b a:(float)a {
    if (auto* c = _batch.add(CommandType::ShapeSetFill)) {
        c->text_color.layer = layer_of(layerId);
        c->text_color.r = r; c->text_color.g = g; c->text_color.b = b; c->text_color.a = a;
    }
}

- (void)setShape:(long long)layerId strokeR:(float)r g:(float)g b:(float)b a:(float)a {
    if (auto* c = _batch.add(CommandType::ShapeSetStroke)) {
        c->text_color.layer = layer_of(layerId);
        c->text_color.r = r; c->text_color.g = g; c->text_color.b = b; c->text_color.a = a;
    }
}

- (void)setCameraTrack:(uint32_t)mode forLayer:(long long)layerId {
    if (auto* e = self.engine) (void)e->start_camera_track(static_cast<aurea::u64>(layerId), mode);
}

- (void)setModelTransformInScene:(uint64_t)scene modelIndex:(uint32_t)modelIndex
                              x:(float)x y:(float)y z:(float)z
                             sx:(float)sx sy:(float)sy sz:(float)sz
                             rx:(float)rx ry:(float)ry rz:(float)rz {
    if (auto* c = _batch.add(CommandType::SceneSetModelTransform)) {
        c->scene_model_transform.scene = aurea::Scene3DId::unpack(scene);
        c->scene_model_transform.modelIndex = modelIndex;
        c->scene_model_transform.x = x; c->scene_model_transform.y = y; c->scene_model_transform.z = z;
        c->scene_model_transform.sx = sx; c->scene_model_transform.sy = sy; c->scene_model_transform.sz = sz;
        c->scene_model_transform.rx = rx; c->scene_model_transform.ry = ry; c->scene_model_transform.rz = rz;
    }
    [self flush];
}

- (void)setMaterialInScene:(uint64_t)scene modelIndex:(uint32_t)modelIndex
             materialIndex:(uint32_t)materialIndex param:(uint32_t)param value:(float)value {
    if (auto* c = _batch.add(CommandType::SceneSetMaterialParam)) {
        c->scene_material.scene = aurea::Scene3DId::unpack(scene);
        c->scene_material.modelIndex = modelIndex;
        c->scene_material.materialIndex = materialIndex;
        c->scene_material.param = param;
        c->scene_material.value = value;
    }
    [self flush];
}

- (BOOL)setEnvironmentIntensity:(float)intensity rotation:(float)rotation {
    auto* e = self.engine;
    return e && e->set_environment_params(intensity, rotation) ? YES : NO;
}

- (NSArray<NSNumber*>*)environment {
    auto* e = self.engine;
    if (!e) return @[];
    f32 v[3]{};
    if (!e->query_environment(v)) return @[];
    return @[@(v[0]), @(v[1]), @(v[2])];
}

- (BOOL)setObjectEnvironmentForLayer:(long long)layerId source:(uint32_t)source hdri:(long long)hdri
                           intensity:(float)intensity rotation:(float)rotation exposure:(float)exposure {
    auto* e = self.engine;
    if (!e) return NO;
    return e->set_object_environment(static_cast<aurea::u64>(layerId), source,
                                     static_cast<aurea::u64>(hdri), intensity, rotation, exposure)
               ? YES : NO;
}

- (NSArray<NSNumber*>*)objectEnvironmentForLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return @[];
    f32 v[5]{};
    u64 asset = 0;
    if (!e->query_object_environment(static_cast<aurea::u64>(layerId), v, &asset)) return @[];
    return @[@(v[0]), @(asset), @(v[2]), @(v[3]), @(v[4])];
}

- (NSArray<NSNumber*>*)materialsForLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return @[];
    const u32 count = std::min<u32>(4096, e->query_materials(static_cast<u64>(layerId), nullptr, 0));
    std::vector<f32> values(static_cast<usize>(count) * 8);
    const u32 written = count ? std::min(count, e->query_materials(static_cast<u64>(layerId), values.data(), count)) : 0;
    NSMutableArray<NSNumber*>* out = [NSMutableArray arrayWithCapacity:written * 8];
    for (u32 i = 0; i < written * 8; ++i) [out addObject:@(values[i])];
    return out;
}

- (BOOL)setMaterialForLayer:(long long)layerId index:(uint32_t)index param:(uint32_t)param value:(float)value {
    auto* e = self.engine;
    return e && e->set_material_param(static_cast<u64>(layerId), index, param, value).ok() ? YES : NO;
}

// =============================================================================
// Composição
// =============================================================================
- (void)setComposition:(uint64_t)composition width:(uint32_t)width height:(uint32_t)height {
    if (auto* c = _batch.add(CommandType::CompositionSetSize)) {
        c->comp_size.comp = aurea::CompositionId::unpack(composition);
        c->comp_size.width = width;
        c->comp_size.height = height;
    }
    [self flush];
}

- (void)setComposition:(uint64_t)composition fps:(double)fps {
    if (auto* c = _batch.add(CommandType::CompositionSetFps)) {
        c->comp_fps.comp = aurea::CompositionId::unpack(composition);
        c->comp_fps.fps = fps;
    }
    [self flush];
}

- (void)setComposition:(uint64_t)composition duration:(int64_t)frames {
    if (auto* c = _batch.add(CommandType::CompositionSetDuration)) {
        c->comp_duration.comp = aurea::CompositionId::unpack(composition);
        c->comp_duration.duration = aurea::FrameIndex{frames};
    }
    [self flush];
}

- (void)setComposition:(uint64_t)composition backgroundR:(float)r g:(float)g b:(float)b a:(float)a {
    if (auto* c = _batch.add(CommandType::CompositionSetBackground)) {
        c->comp_background.comp = aurea::CompositionId::unpack(composition);
        c->comp_background.r = r; c->comp_background.g = g; c->comp_background.b = b; c->comp_background.a = a;
    }
    [self flush];
}

// =============================================================================
// Importação
// =============================================================================
- (long long)importVideo:(NSString*)path name:(NSString*)name {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    aurea::VideoImport request;
    request.sourcePath = to_std(path);
    request.displayName = to_std(name);
    const aurea::Result<aurea::u64> r = e->import_video(request);
    if (!r.ok()) {
        _lastImportError = to_ns(std::string(r.status().message()));
        return -static_cast<long long>(r.status().code());
    }
    _lastImportError = @"";
    return static_cast<long long>(*r);
}

- (long long)importAudio:(NSString*)path name:(NSString*)name {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    aurea::VideoImport request;
    request.sourcePath = to_std(path);
    request.displayName = to_std(name);
    const aurea::Result<aurea::u64> r = e->import_audio(request);
    if (!r.ok()) {
        _lastImportError = to_ns(std::string(r.status().message()));
        return -static_cast<long long>(r.status().code());
    }
    _lastImportError = @"";
    return static_cast<long long>(*r);
}

- (long long)extractAudioFromLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const aurea::Result<aurea::u64> r = e->extract_audio(static_cast<aurea::u64>(layerId));
    if (!r.ok()) {
        _lastImportError = to_ns(std::string(r.status().message()));
        return -static_cast<long long>(r.status().code());
    }
    return static_cast<long long>(*r);
}

- (long long)importImageFile:(NSString*)path name:(NSString*)name {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    // A decodificação usa o MESMO carregador que o motor usa ao reabrir o
    // projeto (`ios_load_image`, ImageIO) — um caminho só, sem uma segunda
    // decodificação que pudesse dar resultado diferente.
    aurea::ImagePixels pixels;
    const std::string source = to_std(path);
    if (!aurea::ios::ios_load_image(source.c_str(), pixels, nullptr)) {
        _lastImportError = @"imagem ilegivel";
        return -static_cast<long long>(aurea::Errc::UnsupportedFormat);
    }
    const std::string display = to_std(name);
    const aurea::Result<aurea::u64> r = e->import_image(pixels.rgba.data(), pixels.width, pixels.height,
                                                        display.empty() ? source.c_str() : display.c_str(),
                                                        source.c_str());
    if (!r.ok()) {
        _lastImportError = to_ns(std::string(r.status().message()));
        return -static_cast<long long>(r.status().code());
    }
    _lastImportError = @"";
    return static_cast<long long>(*r);
}

- (long long)importModel:(NSString*)path name:(NSString*)name {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    aurea::ModelImport request;
    request.path = to_std(path);
    request.displayName = to_std(name);
    std::string detail;
    const aurea::Result<aurea::u64> r = e->import_model(request, nullptr, &detail);
    if (!r.ok()) {
        _lastImportError = to_ns(detail.empty() ? std::string(r.status().message()) : detail);
        return -static_cast<long long>(r.status().code());
    }
    _lastImportError = @"";
    return static_cast<long long>(*r);
}

- (long long)importObjectHDRI:(NSString*)path layer:(long long)layer {
    auto* e = self.engine; if (!e) return -1;
    const auto result = e->import_hdri(path.UTF8String, layer);
    if (!result.ok()) { _lastImportError = to_ns(std::string(result.status().message())); return -1; }
    _lastImportError = @""; return static_cast<long long>(*result);
}
- (long long)importHdri:(NSString*)path {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const std::string p = to_std(path);
    const aurea::Result<aurea::u64> r = e->import_hdri(p.c_str());
    if (!r.ok()) {
        _lastImportError = to_ns(std::string(r.status().message()));
        return -static_cast<long long>(r.status().code());
    }
    return static_cast<long long>(*r);
}

- (void)clearHdri {
    if (auto* e = self.engine) (void)e->clear_hdri();
}

- (long long)addShape:(uint32_t)preset {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const aurea::Result<aurea::u64> r = e->add_shape(preset);
    return r.ok() ? static_cast<long long>(*r) : -static_cast<long long>(r.status().code());
}

- (long long)addText:(NSString*)content {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const std::string s = to_std(content);
    const aurea::Result<aurea::u64> r = e->add_text(s.empty() ? nullptr : s.c_str());
    return r.ok() ? static_cast<long long>(*r) : -static_cast<long long>(r.status().code());
}

- (void)setSceneEditor:(BOOL)enabled yaw:(float)yaw pitch:(float)pitch distance:(float)distance {
    if (auto* e = self.engine) e->set_scene_editor(enabled != NO, yaw, pitch, distance);
}
- (NSArray<NSNumber*>*)sceneGuides {
    float lines[256 * 5]{};
    auto* e = self.engine;
    const auto count = e ? e->query_scene_guides(lines, 256) : 0;
    return floats_to_array(lines, count * 5);
}
- (void)layoutTransform:(long long)layer property:(uint32_t)property value:(float)value {
    if (auto* c = _batch.add(CommandType::LayerLayoutTransform))
        c->shape_param = aurea::ShapeParamPayload{LayerId::unpack(static_cast<aurea::u64>(layer)), property, value};
}
- (long long)addLight:(uint32_t)kind {
    if (!self.engine) return -1;
    const auto id = self.engine->add_light(kind); return id.ok() ? static_cast<long long>(*id) : -1;
}
- (NSArray<NSNumber*>*)lightInfo:(long long)layer {
    float values[10]{};
    if (!self.engine || !self.engine->query_light(static_cast<aurea::u64>(layer), values)) return @[];
    return floats_to_array(values, 10);
}
- (void)setLightParam:(long long)layer param:(uint32_t)param value:(float)value {
    if (auto* c = _batch.add(CommandType::LayerSetLightParam))
        c->shape_param = aurea::ShapeParamPayload{LayerId::unpack(static_cast<aurea::u64>(layer)), param, value};
}
- (long long)addCamera {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const auto result = e->add_camera();
    return result.ok() ? static_cast<long long>(*result) : -static_cast<long long>(result.status().code());
}

- (long long)addNull:(BOOL)threeD {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const aurea::Result<aurea::u64> r = e->add_null(threeD != NO);
    return r.ok() ? static_cast<long long>(*r) : -static_cast<long long>(r.status().code());
}

- (long long)addText3D:(NSString*)content depth:(float)depth alignment:(uint32_t)alignment
                     r:(float)r g:(float)g b:(float)b {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    aurea::scene3d::Text3DSpec spec;
    spec.content = to_std(content);
    spec.depth = depth;
    spec.alignment = alignment;
    spec.color = aurea::Vec4{r, g, b, 1.0f};
    const aurea::Result<aurea::u64> res = e->add_text3d(spec);
    return res.ok() ? static_cast<long long>(*res) : -static_cast<long long>(res.status().code());
}

- (long long)addParticles:(uint32_t)preset {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const aurea::Result<aurea::u64> r = e->add_particles(preset);
    return r.ok() ? static_cast<long long>(*r) : -static_cast<long long>(r.status().code());
}

- (long long)freezeFrameForLayer:(long long)layerId frame:(int32_t)frame hold:(int32_t)holdFrames {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    const aurea::Result<aurea::u64> r = e->freeze_frame(static_cast<aurea::u64>(layerId), frame, holdFrames);
    return r.ok() ? static_cast<long long>(*r) : -static_cast<long long>(r.status().code());
}

- (NSString*)lastImportError {
    return _lastImportError ?: @"";
}

// =============================================================================
// Consultas
// =============================================================================
- (NSArray<NSDictionary<NSString*, id>*>*)layers {
    auto* e = self.engine;
    if (!e) return @[];
    // O status já diz quantas camadas há: dimensiona os buffers de uma vez, sem
    // laço de crescimento (é o motivo de `EngineStatus::layerCount` existir).
    aurea::bridge::EngineStatusPOD status;
    e->fill_status(status);
    u32 capacity = status.layerCount + 4;
    if (capacity < 16) capacity = 16;
    const u32 blobCapacity = 64 * 1024;

    std::vector<aurea::bridge::LayerRow> rows(capacity);
    std::vector<char> blob(blobCapacity);
    u32 written = e->query_layers(rows.data(), capacity, blob.data(), blobCapacity);
    if (written == capacity) {
        // Encheu: o projeto cresceu entre o status e a consulta. Cresce e relê.
        capacity *= 2;
        rows.assign(capacity, {});
        written = e->query_layers(rows.data(), capacity, blob.data(), blobCapacity);
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) {
        [out addObject:layer_row_dict(rows[i], slice(blob, rows[i].nameOffset, rows[i].nameLength))];
    }
    return out;
}

- (NSArray<NSDictionary<NSString*, id>*>*)keyframesForLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<aurea::bridge::KeyframeRow> rows(256);
    u32 written = e->query_keyframes(static_cast<aurea::u64>(layerId), rows.data(), 256);
    if (written == 256) {
        rows.assign(written * 4, {});
        written = e->query_keyframes(static_cast<aurea::u64>(layerId), rows.data(), static_cast<u32>(rows.size()));
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) [out addObject:keyframe_row_dict(rows[i])];
    return out;
}

- (NSArray<NSDictionary<NSString*, id>*>*)allKeyframes {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<aurea::bridge::KeyframeIndexRow> index(16);
    std::vector<aurea::bridge::KeyframeRow> rows(256);
    u32 layers = 0;
    // A consulta não escreve NADA quando não cabe (é o contrato dela): o
    // primeiro passo só descobre o tamanho.
    u32 total = e->query_all_keyframes(index.data(), static_cast<u32>(index.size()),
                                       rows.data(), static_cast<u32>(rows.size()), &layers);
    if (total > static_cast<u32>(rows.size()) || layers > static_cast<u32>(index.size())) {
        index.assign(layers + 4, {});
        rows.assign(total + 4, {});
        total = e->query_all_keyframes(index.data(), static_cast<u32>(index.size()),
                                       rows.data(), static_cast<u32>(rows.size()), &layers);
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:layers];
    u32 cursor = 0;
    for (u32 i = 0; i < layers; ++i) {
        NSMutableArray<NSDictionary<NSString*, id>*>* keys =
            [NSMutableArray arrayWithCapacity:index[i].count];
        for (u32 k = 0; k < index[i].count && cursor < total; ++k, ++cursor) {
            [keys addObject:keyframe_row_dict(rows[cursor])];
        }
        [out addObject:@{ @"layerId": @(index[i].layerId), @"keys": keys }];
    }
    return out;
}

- (NSDictionary<NSString*, id>*)layerDetail:(long long)layerId {
    auto* e = self.engine;
    if (!e) return nil;
    aurea::bridge::LayerDetailPOD d;
    if (!e->query_layer_detail(static_cast<aurea::u64>(layerId), d)) return nil;
    return @{
        AureaDetailKind:           @(d.kind),
        AureaDetailStartFrame:     @(d.startFrame),
        AureaDetailEndFrame:       @(d.endFrame),
        @"offsetFrames":          @(d.offsetFrames),
        @"sourceFrames":          @(d.sourceFrames),
        @"audioFadeIn":           @(d.audioFadeIn),
        @"audioFadeOut":          @(d.audioFadeOut),
        AureaDetailPosition:       floats_to_array(d.position, 3),
        AureaDetailScale:          floats_to_array(d.scale, 3),
        AureaDetailRotation:       floats_to_array(d.rotation, 3),
        AureaDetailAnchor:         floats_to_array(d.anchor, 3),
        AureaDetailOpacity:        @(d.opacity),
        AureaDetailSkew:           floats_to_array(d.skew, 2),
        AureaDetailAnimatedMask:   @(d.animatedMask),
        AureaDetailKeyAtPlayhead:  @(d.keyAtPlayheadMask),
        AureaDetailSourceSize:     @[@(d.sourceWidth), @(d.sourceHeight)],
        AureaDetailSourceFps:      @(d.sourceFps),
        AureaDetailEffectCount:    @(d.effectCount),
        AureaDetailMaskCount:      @(d.maskCount),
        AureaDetailLocalPlayhead:  @(d.localPlayhead),
        AureaDetailParentId:       @(d.parentId),
        AureaDetailAudioGain:      @(d.audioGain),
        AureaDetailAudioVolume:    @(d.audioVolume),
        AureaDetailAudioPan:       @(d.audioPan),
        AureaDetailAudioFlags:     @(d.audioFlags),
        AureaDetailSpeed:          @(d.speed),
        AureaDetailTimeFlags:      @(d.timeFlags),
        @"shapeTypePoints":        @(d.shapeTypePoints),
        AureaDetailShape:          @[@(d.shapeTypePoints), @(d.shapeFill), @(d.shapeStroke),
                                     @(d.shapeStrokeWidth), @(d.shapeCorner), @(d.shapeInner)],
        AureaDetailCorners:        floats_to_array(d.corners, 8),
        AureaDetailParentAffine:   floats_to_array(d.parentAffine, 6),
        AureaDetailGeomFlags:      @(d.geomFlags),
    };
}

- (NSDictionary<NSString*, id>*)composition {
    auto* e = self.engine;
    if (!e) return nil;
    u64 id = 0;
    u32 w = 0, h = 0;
    f64 fps = 0.0;
    i64 duration = 0;
    f32 bg[4]{};
    if (!e->query_composition(id, w, h, fps, duration, bg)) return nil;
    u32 capLong = 0, capShort = 0;
    e->composition_size_cap(capLong, capShort);
    return @{
        AureaCompositionId:         @(id),
        AureaCompositionName:       [self compositionName],
        AureaCompositionWidth:      @(w),
        AureaCompositionHeight:     @(h),
        AureaCompositionFps:        @(fps),
        AureaCompositionDuration:   @(duration),
        AureaCompositionBackground: floats_to_array(bg, 4),
        AureaCompositionSizeCap:    @[@(capLong), @(capShort)],
        AureaCompositionDepth:      @(self.precompDepth),
    };
}

- (NSArray<NSDictionary<NSString*, id>*>*)effectCatalog {
    auto* e = self.engine;
    if (!e) return @[];
    u32 capacity = 64;
    std::vector<aurea::bridge::EffectCatalogRow> rows;
    std::vector<char> blob(64 * 1024);
    u32 written = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        rows.assign(capacity, {});
        written = e->query_effect_catalog(rows.data(), capacity, blob.data(), static_cast<u32>(blob.size()));
        if (written < capacity) break;
        capacity *= 2;
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) {
        [out addObject:@{
            @"effectClass":        @(rows[i].effectClass),
            AureaEffectTypeId:     @(rows[i].typeId),
            AureaEffectName:       slice(blob, rows[i].nameOffset, rows[i].nameLength),
            AureaEffectCategory:   slice(blob, rows[i].categoryOffset, rows[i].categoryLength),
            AureaEffectParamCount: @(rows[i].paramCount),
        }];
    }
    return out;
}

- (NSArray<NSDictionary<NSString*, id>*>*)effectsForLayer:(long long)layerId {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<aurea::bridge::LayerEffectRow> rows(64);
    std::vector<char> blob(32 * 1024);
    u32 written = e->query_layer_effects(static_cast<aurea::u64>(layerId), rows.data(),
                                         static_cast<u32>(rows.size()), blob.data(),
                                         static_cast<u32>(blob.size()));
    if (written == static_cast<u32>(rows.size())) {
        rows.assign(written * 2, {});
        written = e->query_layer_effects(static_cast<aurea::u64>(layerId), rows.data(),
                                         static_cast<u32>(rows.size()), blob.data(),
                                         static_cast<u32>(blob.size()));
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) {
        [out addObject:effect_row_dict(rows[i], slice(blob, rows[i].nameOffset, rows[i].nameLength))];
    }
    return out;
}

- (NSArray<NSDictionary<NSString*, id>*>*)effectParamsForLayer:(long long)layerId effectId:(uint32_t)effectId {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<aurea::bridge::EffectParamRow> rows(64);
    std::vector<char> blob(32 * 1024);
    u32 written = e->query_effect_params(static_cast<aurea::u64>(layerId), effectId, rows.data(),
                                         static_cast<u32>(rows.size()), blob.data(),
                                         static_cast<u32>(blob.size()));
    if (written == static_cast<u32>(rows.size())) {
        rows.assign(written * 2, {});
        written = e->query_effect_params(static_cast<aurea::u64>(layerId), effectId, rows.data(),
                                         static_cast<u32>(rows.size()), blob.data(),
                                         static_cast<u32>(blob.size()));
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) [out addObject:param_row_dict(rows[i], blob)];
    return out;
}

- (NSArray<NSDictionary<NSString*, id>*>*)effectSpecs:(uint32_t)typeId {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<aurea::bridge::EffectParamRow> rows(64);
    std::vector<char> blob(32 * 1024);
    u32 written = e->query_effect_specs(typeId, rows.data(), static_cast<u32>(rows.size()),
                                        blob.data(), static_cast<u32>(blob.size()));
    if (written == static_cast<u32>(rows.size())) {
        rows.assign(written * 2, {});
        written = e->query_effect_specs(typeId, rows.data(), static_cast<u32>(rows.size()),
                                        blob.data(), static_cast<u32>(blob.size()));
    }
    NSMutableArray<NSDictionary<NSString*, id>*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) [out addObject:param_row_dict(rows[i], blob)];
    return out;
}

- (NSArray<NSNumber*>*)curveForLayer:(long long)layerId property:(uint32_t)property
                                from:(int32_t)from to:(int32_t)to count:(uint32_t)count {
    auto* e = self.engine;
    if (!e || count == 0) return @[];
    std::vector<f32> values(count);
    const u32 written = e->query_curve(static_cast<aurea::u64>(layerId), property, from, to,
                                       values.data(), count);
    return floats_to_array(values.data(), written);
}

- (NSData*)waveformForLayer:(long long)layerId startFrame:(double)startFrame
            framesPerBucket:(double)framesPerBucket count:(uint32_t)count {
    auto* e = self.engine;
    if (!e || count == 0) return nil;
    std::vector<u8> buckets(count);
    const u32 written = e->query_waveform(static_cast<aurea::u64>(layerId), startFrame, framesPerBucket,
                                          count, buckets.data());
    if (written == 0) return nil;
    return [NSData dataWithBytes:buckets.data() length:written];
}

- (void)selectLayers:(NSArray<NSNumber*>*)layerIds {
    auto* e = self.engine;
    if (!e) return;
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    if (ids.empty()) {
        e->clear_selection();
        return;
    }
    e->set_selection(ids.data(), static_cast<aurea::u32>(ids.size()));
    e->request_render();
}

- (void)clearSelection {
    if (auto* e = self.engine) e->clear_selection();
}

- (NSArray<NSNumber*>*)selection {
    auto* e = self.engine;
    if (!e) return @[];
    const u32 n = e->selection_count();
    std::vector<aurea::u64> ids(n > 0 ? n : 1);
    const u32 written = e->get_selection(ids.data(), n > 0 ? n : 1);
    NSMutableArray<NSNumber*>* out = [NSMutableArray arrayWithCapacity:written];
    for (u32 i = 0; i < written; ++i) [out addObject:@(ids[i])];
    return out;
}

- (NSArray<NSNumber*>*)searchLayers:(NSString*)query {
    auto* e = self.engine;
    if (!e) return @[];
    const std::vector<aurea::u64> ids = e->search_layers(to_std(query));
    NSMutableArray<NSNumber*>* out = [NSMutableArray arrayWithCapacity:ids.size()];
    for (aurea::u64 id : ids) [out addObject:@(id)];
    return out;
}

// =============================================================================
// Áudio da camada
// =============================================================================
- (void)setLayer:(long long)layerId audioGain:(float)gain {
    if (auto* c = _batch.add(CommandType::AudioSetGain)) {
        c->audio_gain.layer = layer_of(layerId);
        c->audio_gain.gain = gain;
    }
    [self flush];
}

- (void)setLayer:(long long)layerId audioVolume:(float)volume {
    // Volume PARADO (o animado são keyframes de AudioVolume).
    if (auto* c = _batch.add(CommandType::AudioSetVolume)) {
        c->audio_gain.layer = layer_of(layerId);
        c->audio_gain.gain = volume;
    }
}

- (void)setLayer:(long long)layerId audioPan:(float)pan {
    if (auto* c = _batch.add(CommandType::AudioSetPan)) {
        c->audio_gain.layer = layer_of(layerId);
        c->audio_gain.gain = pan;
    }
}

- (void)setLayer:(long long)layerId audioMuted:(BOOL)muted {
    if (auto* c = _batch.add(CommandType::AudioSetMuted)) {
        c->audio_flag.layer = layer_of(layerId);
        c->audio_flag.flag = muted != NO;
    }
    [self flush];
}
- (void)setText:(long long)layerId strokeR:(float)r g:(float)g b:(float)b a:(float)a {
    if (auto* c = _batch.add(CommandType::TextSetStrokeColor)) {
        c->text_color.layer = layer_of(layerId);
        c->text_color.r = r; c->text_color.g = g; c->text_color.b = b; c->text_color.a = a;
    }
}

- (void)setLayer:(long long)layerId audioSolo:(BOOL)solo {
    if (auto* c = _batch.add(CommandType::AudioSetSolo)) {
        c->audio_flag.layer = layer_of(layerId);
        c->audio_flag.flag = solo != NO;
    }
    [self flush];
}

- (NSArray<NSNumber*>*)shapeParams:(long long)layerId {
    auto* e = self.engine;
    float values[aurea::Engine::kShapeParamFloats]{};
    if (!e || !e->query_shape_params(layerId, values, aurea::Engine::kShapeParamFloats)) return @[];
    return floats_to_array(values, aurea::Engine::kShapeParamFloats);
}
- (NSArray<NSNumber*>*)particleParams:(long long)layerId {
    auto* e = self.engine;
    constexpr auto count = static_cast<aurea::u32>(aurea::ParticleParam::Count);
    float v[count]{};
    if (!e || !e->query_particles(layerId, v)) return @[];
    return floats_to_array(v, count);
}
- (BOOL)setParticle:(long long)layerId param:(uint32_t)param value:(float)value {
    auto* e = self.engine; return e && e->set_particle_param(layerId, param, value);
}
- (BOOL)applyParticlePreset:(long long)layerId preset:(uint32_t)preset {
    auto* e = self.engine; return e && e->apply_particle_preset(layerId, preset);
}
- (NSArray<NSNumber*>*)particleLinks:(long long)layerId {
    auto* e = self.engine; aurea::u64 v[4]{};
    if (!e || !e->query_particle_links(layerId, v)) return @[];
    return @[@(v[0]), @(v[1]), @(v[2]), @(v[3])];
}
- (BOOL)setParticleLink:(long long)layerId kind:(uint32_t)kind target:(long long)target {
    auto* e = self.engine;
    if (!e) return NO;
    if (kind == 0) return e->set_particle_source(layerId, target);
    if (kind == 1) return e->set_particle_texture(layerId, target);
    if (kind == 2) return e->set_particle_mesh(layerId, target);
    return NO;
}
- (NSArray<NSNumber*>*)particleCurve:(long long)layerId kind:(uint32_t)kind {
    auto* e = self.engine; float v[32]{};
    if (!e || kind > 2) return @[];
    const auto count = e->query_particle_curve(layerId, kind, v, 32);
    return floats_to_array(v, std::min<aurea::u32>(count, 8) * (kind == 0 ? 4 : 2));
}
- (BOOL)setParticleCurve:(long long)layerId kind:(uint32_t)kind values:(NSArray<NSNumber*>*)values {
    auto* e = self.engine; float v[32]{};
    const auto stride = kind == 0 ? 4 : 2;
    if (!e || kind > 2 || values.count > 32 || values.count % stride != 0) return NO;
    for (NSUInteger n = 0; n < values.count; ++n) v[n] = values[n].floatValue;
    return e->set_particle_life_curves(layerId, kind, v, static_cast<aurea::u32>(values.count / stride));
}
- (void)keyParameter:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time value:(float)value {
    if (auto* c = _batch.add(CommandType::KeyframeInsert)) {
        c->keyframe.track.layer = layer_of(layerId);
        c->keyframe.track.property = static_cast<aurea::TrackProperty>(property);
        c->keyframe.track.effectIndex = effect; c->keyframe.track.effectParamIndex = param;
        c->keyframe.time = aurea::FrameIndex{time}; c->keyframe.value = value;
    }
    [self flush];
}
- (NSString*)savePreset:(long long)layerId kind:(uint32_t)kind name:(NSString*)name {
    auto* e = self.engine;
    if (!e || kind > 2) return @"";
    const auto json = e->save_preset(layerId, static_cast<aurea::presets::PresetKind>(kind), to_std(name));
    return [NSString stringWithUTF8String:json.c_str()] ?: @"";
}
- (NSString*)savePreset:(long long)layerId kind:(uint32_t)kind name:(NSString*)name parts:(uint32_t)parts {
    auto* e = self.engine; if (!e || kind > 2) return @"";
    const auto json = e->save_preset(layerId, static_cast<aurea::presets::PresetKind>(kind), to_std(name), parts);
    return to_ns(json);
}
- (NSString*)applyPreset:(long long)layerId json:(NSString*)json duration:(int64_t)duration {
    auto* e = self.engine;
    if (!e) return @"Motor indisponível";
    std::string error;
    if (e->apply_preset(layerId, to_std(json), duration, &error)) return @"";
    return [NSString stringWithUTF8String:error.c_str()] ?: @"Preset inválido";
}
- (NSArray<NSNumber*>*)parseCaptionPreset:(NSString*)json {
    aurea::presets::Preset p;
    if (!aurea::presets::parse(to_std(json), p) || p.kind != aurea::presets::PresetKind::Caption) return @[];
    const auto& o = p.caption;
    return @[@(o.mode), @(o.maxWords), @(o.maxChars), @(o.maxLines), @(o.style), @(o.highlight), @(o.uppercase), @(o.breakOnPause), @(p.removeFillers), @(o.pauseSec), @(o.posY), @(o.sizeFrac), @(o.highlightColor.x), @(o.highlightColor.y), @(o.highlightColor.z)];
}
- (NSString*)makeCaptionPreset:(NSString*)name options:(NSDictionary<NSString*, NSNumber*>*)options {
    aurea::text::CaptionOptions o;
    auto number = [&](NSString* key, float fallback) { NSNumber* n = options[key]; return n && std::isfinite(n.floatValue) ? n.floatValue : fallback; };
    o.mode = static_cast<uint32_t>(std::clamp(number(@"mode", 0), 0.f, 1.f));
    o.maxWords = static_cast<uint32_t>(std::clamp(number(@"maxWords", 4), 1.f, 20.f));
    o.maxChars = static_cast<uint32_t>(std::clamp(number(@"maxChars", 18), 4.f, 80.f));
    o.maxLines = static_cast<uint32_t>(std::clamp(number(@"maxLines", 2), 1.f, 5.f));
    o.style = static_cast<uint32_t>(std::clamp(number(@"style", 2), 0.f, static_cast<float>(aurea::text::kCaptionStyleCount - 1)));
    o.highlight = number(@"highlight", 1) != 0; o.uppercase = number(@"uppercase", 0) != 0;
    o.breakOnPause = number(@"breakOnPause", 1) != 0;
    o.pauseSec = std::clamp(number(@"pauseSec", 0.6f), 0.05f, 5.f);
    o.posY = std::clamp(number(@"posY", 0.78f), 0.1f, 0.9f);
    o.sizeFrac = std::clamp(number(@"sizeFrac", 0.065f), 0.01f, 0.3f);
    o.highlightColor = {std::clamp(number(@"highlightR", 1), 0.f, 1.f), std::clamp(number(@"highlightG", 0.83f), 0.f, 1.f), std::clamp(number(@"highlightB", 0), 0.f, 1.f), 1.f};
    return to_ns(aurea::presets::make_caption_preset(to_std(name), o, number(@"removeFillers", 1) != 0));
}
- (NSArray<NSNumber*>*)parseCurvePreset:(NSString*)json {
    aurea::presets::Preset p;
    if (!aurea::presets::parse(to_std(json), p) || p.kind != aurea::presets::PresetKind::Curve) return @[];
    return @[@(static_cast<uint32_t>(p.curveInterp)), @(p.x1), @(p.y1), @(p.x2), @(p.y2)];
}
- (NSString*)makeCurvePreset:(NSString*)name interpolation:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles {
    if (handles.count != 4) return @"";
    for (NSNumber* n in handles) if (!std::isfinite(n.floatValue)) return @"";
    const auto interp = static_cast<aurea::Interpolation>(std::min<uint32_t>(interpolation, static_cast<uint32_t>(aurea::Interpolation::Steps)));
    return to_ns(aurea::presets::make_curve_preset(to_std(name), interp, std::clamp(handles[0].floatValue, 0.f, 1.f), std::clamp(handles[1].floatValue, -2.f, 3.f), std::clamp(handles[2].floatValue, 0.f, 1.f), std::clamp(handles[3].floatValue, -2.f, 3.f)));
}
- (NSString*)trackPoint:(long long)layerId x:(float)x y:(float)y stabilize:(BOOL)stabilize {
    auto* e = self.engine; if (!e) return @"Motor indisponível";
    auto result = e->track_point(layerId, x, y, stabilize);
    return result.ok() ? @"" : @"Não foi possível rastrear esse ponto. Escolha um detalhe visível no vídeo.";
}
- (NSDictionary<NSString*, id>*)cameraTrackingStatus {
    auto* e = self.engine; if (!e) return @{};
    const auto s = e->camera_track_status();
    return @{@"state": @(s.state), @"progress": @(s.progress), @"frames": @(s.frames), @"solved": @(s.framesSolved),
             @"confidence": @(s.confidence), @"error": @(s.rmsError), @"rotationOnly": @(s.rotationOnly),
             @"cached": @(s.cached), @"fovDeg": @(s.fovDeg),
             @"message": [NSString stringWithUTF8String:s.message.c_str()] ?: @""};
}
- (NSArray<NSNumber*>*)gizmo:(long long)layerId length:(float)length {
    float points[8]{}; auto* e = self.engine;
    return e && e->query_gizmo(layerId, length, points) ? floats_to_array(points, 8) : @[];
}
- (NSArray<NSNumber*>*)gizmoMoveLocal:(long long)layerId axis:(uint32_t)axis amount:(float)amount {
    float xyz[3]{}; auto* e = self.engine;
    return e && e->gizmo_move_local(layerId, axis, amount, xyz) ? floats_to_array(xyz, 3) : @[];
}
- (void)cancelCameraTracking { if (auto* e = self.engine) e->cancel_camera_track(); }
- (NSString*)applyCameraTracking {
    auto* e = self.engine; if (!e) return @"Motor indisponível";
    return e->apply_camera_track().ok() ? @"" : @"A análise ainda não produziu uma câmera válida.";
}
- (NSString*)trackMask:(long long)layerId mask:(uint32_t)mask mode:(uint32_t)mode {
    auto* e = self.engine; if (!e) return @"Motor indisponível";
    return e->track_mask(layerId, mask, mode).ok() ? @"" : @"A máscara não encontrou detalhes suficientes para seguir.";
}
- (NSString*)layerMediaPath:(long long)layerId {
    auto* e = self.engine; if (!e) return @"";
    const auto p = e->layer_media_path(layerId); return [NSString stringWithUTF8String:p.c_str()] ?: @"";
}
- (NSArray<NSDictionary<NSString*, id>*>*)parseSRT:(NSString*)srt {
    NSMutableArray* out = [NSMutableArray array];
    for (const auto& word : aurea::text::parse_srt(to_std(srt))) {
        [out addObject:@{@"word": [NSString stringWithUTF8String:word.text.c_str()] ?: @"", @"start": @(word.start), @"end": @(word.end)}];
    }
    return out;
}
- (BOOL)isFillerWord:(NSString*)word { return aurea::text::is_filler_word(to_std(word)); }
- (NSString*)createCaptions:(long long)layerId words:(NSArray<NSDictionary<NSString*, id>*>*)words options:(NSDictionary<NSString*, NSNumber*>*)options {
    auto* e = self.engine; if (!e) return @"Motor indisponível";
    std::vector<aurea::text::CaptionWord> parsed;
    for (NSDictionary* item in words) {
        NSString* word = item[@"word"];
        const auto start = [item[@"start"] doubleValue], end = [item[@"end"] doubleValue];
        if (![word isKindOfClass:[NSString class]] || !std::isfinite(start) || !std::isfinite(end) || end <= start) continue;
        parsed.push_back({to_std(word), start, end});
    }
    aurea::text::CaptionOptions o;
    o.mode = [options[@"mode"] unsignedIntValue]; o.style = [options[@"style"] unsignedIntValue];
    o.maxWords = options[@"maxWords"] ? [options[@"maxWords"] unsignedIntValue] : 4;
    o.maxChars = options[@"maxChars"] ? [options[@"maxChars"] unsignedIntValue] : 18;
    o.maxLines = options[@"maxLines"] ? [options[@"maxLines"] unsignedIntValue] : 2;
    o.highlight = [options[@"highlight"] boolValue]; o.uppercase = [options[@"uppercase"] boolValue];
    o.breakOnPause = options[@"breakOnPause"] ? [options[@"breakOnPause"] boolValue] : true;
    o.pauseSec = options[@"pauseSec"] ? std::clamp([options[@"pauseSec"] floatValue], 0.05f, 5.f) : 0.6f;
    o.highlightColor = {options[@"highlightR"] ? [options[@"highlightR"] floatValue] : 1.f,
                        options[@"highlightG"] ? [options[@"highlightG"] floatValue] : 0.83f,
                        options[@"highlightB"] ? [options[@"highlightB"] floatValue] : 0.f, 1.f};
    o.posY = options[@"posY"] ? [options[@"posY"] floatValue] : 0.78f;
    o.sizeFrac = options[@"sizeFrac"] ? [options[@"sizeFrac"] floatValue] : 0.065f;
    if ([options[@"removeFillers"] boolValue]) parsed = aurea::text::remove_filler_words(parsed);
    return e->create_captions(layerId, parsed, o).ok() ? @"" : @"Não foi possível gerar legendas. Confira os tempos e o áudio da camada.";
}
- (uint32_t)captionCount:(long long)layerId { auto* e = self.engine; return e ? e->caption_count(layerId) : 0; }
- (void)removeCaptions:(long long)layerId { if (auto* e = self.engine) (void)e->remove_captions(layerId); }
- (NSArray<NSNumber*>*)trackCurve:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param from:(int32_t)from to:(int32_t)to {
    auto* e = self.engine; float v[160]{};
    const auto n = e ? e->query_track_curve(layerId, property, effect, param, from, to, v, 160) : 0;
    return floats_to_array(v, n);
}
- (NSArray<NSNumber*>*)trackEasing:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time {
    auto* e = self.engine; float v[4]{};
    if (!e || !e->query_keyframe_easing(layerId, property, effect, param, time, v)) return @[];
    return floats_to_array(v, 4);
}
- (void)editTrackKey:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param time:(int32_t)time action:(uint32_t)action value:(float)value targetTime:(int32_t)targetTime interpolation:(uint32_t)interpolation handles:(NSArray<NSNumber*>*)handles {
    if (property >= static_cast<u32>(aurea::TrackProperty::_Count) || action > 3) return;
    aurea::TrackRef track{}; track.layer = layer_of(layerId); track.property = static_cast<aurea::TrackProperty>(property);
    track.effectIndex = effect; track.effectParamIndex = param;
    const auto type = action == 0 ? CommandType::KeyframeSetValue : action == 1 ? CommandType::KeyframeDelete : action == 2 ? CommandType::KeyframeMove : CommandType::KeyframeSetInterpolation;
    if (auto* c = _batch.add(type)) {
        if (action < 2) { c->keyframe.track = track; c->keyframe.time = aurea::FrameIndex{time}; c->keyframe.value = value; }
        else if (action == 2) { c->keyframe_move.track = track; c->keyframe_move.fromTime = aurea::FrameIndex{time}; c->keyframe_move.toTime = aurea::FrameIndex{targetTime}; }
        else {
            c->keyframe_interp.track = track; c->keyframe_interp.time = aurea::FrameIndex{time};
            c->keyframe_interp.interp = static_cast<aurea::Interpolation>(interpolation);
            if (handles.count == 4) { c->keyframe_interp.bx1 = handles[0].floatValue; c->keyframe_interp.by1 = handles[1].floatValue; c->keyframe_interp.bx2 = handles[2].floatValue; c->keyframe_interp.by2 = handles[3].floatValue; }
        }
    }
    [self flush];
}
- (NSDictionary<NSString*, id>*)expression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param {
    auto* e = self.engine; aurea::Engine::ExpressionInfo info;
    if (!e || !e->query_expression(layerId, property, effect, param, info)) return @{};
    return @{@"exists": @(info.exists), @"enabled": @(info.enabled), @"source": to_ns(info.source), @"error": to_ns(info.error.message), @"line": @(info.error.line), @"column": @(info.error.column), @"value": @(info.value)};
}
- (NSString*)setExpression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param source:(NSString*)source {
    auto* e = self.engine; if (!e) return @"Motor indisponível";
    aurea::expr::Diagnostic d; const auto result = e->set_expression(layerId, property, effect, param, source.UTF8String, &d);
    return d.ok ? (result.ok() ? @"" : to_ns(std::string(result.message()))) : [NSString stringWithFormat:@"%u:%u — %@", d.line, d.column, to_ns(d.message)];
}
- (void)enableExpression:(long long)layerId property:(uint32_t)property effect:(uint32_t)effect param:(uint32_t)param enabled:(BOOL)enabled {
    if (auto* e = self.engine) (void)e->set_expression_enabled(layerId, property, effect, param, enabled);
}
- (NSDictionary<NSString*, id>*)setExpressions:(long long)layerId tracks:(NSArray<NSNumber*>*)tracks source:(NSString*)source {
    auto* e = self.engine;
    if (!e || tracks.count == 0 || tracks.count > 48 || tracks.count % 3) return @{@"accepted": @NO, @"ok": @NO, @"message": @"Trilha inválida", @"line": @0, @"column": @0};
    std::vector<aurea::u32> keys; keys.reserve(tracks.count);
    for (NSNumber* n in tracks) keys.push_back(n.unsignedIntValue);
    aurea::expr::Diagnostic diagnostic;
    const auto result = e->set_expressions(layerId, keys.data(), static_cast<aurea::u32>(keys.size() / 3), source.UTF8String, &diagnostic);
    return @{@"accepted": @(result.ok()), @"ok": @(result.ok() && diagnostic.ok), @"message": result.ok() ? to_ns(diagnostic.message) : to_ns(std::string(result.message())), @"line": @(diagnostic.line), @"column": @(diagnostic.column)};
}
- (BOOL)enableExpressions:(long long)layerId tracks:(NSArray<NSNumber*>*)tracks enabled:(BOOL)enabled {
    auto* e = self.engine;
    if (!e || tracks.count == 0 || tracks.count > 48 || tracks.count % 3) return NO;
    std::vector<aurea::u32> keys; keys.reserve(tracks.count);
    for (NSNumber* n in tracks) keys.push_back(n.unsignedIntValue);
    return e->set_expressions_enabled(layerId, keys.data(), static_cast<aurea::u32>(keys.size() / 3), enabled);
}
- (NSDictionary<NSString*, id>*)checkExpressionSyntax:(NSString*)source {
    const auto diagnostic = aurea::expr::check_syntax(to_std(source));
    return @{@"accepted": @YES, @"ok": @(diagnostic.ok), @"message": to_ns(diagnostic.message), @"line": @(diagnostic.line), @"column": @(diagnostic.column)};
}
- (NSArray<NSNumber*>*)timeRemap:(long long)layerId {
    auto* e = self.engine; if (!e) return @[];
    float header[5]{}; if (e->query_time_remap(layerId, header, 5) < 5) return @[];
    const auto capacity = 5 + std::min<u32>(static_cast<u32>(header[0]), 100000) * 7;
    std::vector<float> v(capacity); const auto n = e->query_time_remap(layerId, v.data(), capacity);
    return floats_to_array(v.data(), n);
}
- (int32_t)editTimeRemap:(long long)layerId index:(int32_t)index time:(int64_t)time value:(float)value interpolation:(int32_t)interpolation {
    auto* e = self.engine; return e ? e->edit_time_remap_key(layerId, index, time, value, interpolation) : -1;
}
- (void)removeTimeRemap:(long long)layerId index:(uint32_t)index { if (auto* e = self.engine) (void)e->remove_time_remap_key(layerId, index); }
- (long long)addVector:(uint32_t)preset {
    auto* e = self.engine; if (!e) return -1; const auto r = e->add_vector_layer(preset); return r.ok() ? static_cast<long long>(*r) : -1;
}
- (NSArray<NSDictionary<NSString*, id>*>*)vectorGroups:(long long)layerId {
    auto* e = self.engine; std::vector<float> data; std::string names; aurea::VectorData doc;
    if (!e || !e->vector_document(layerId, data, names) || !aurea::vector::decode_document(data.data(), data.size(), names, doc)) return @[];
    NSMutableArray* groups = [NSMutableArray array];
    auto paint = [](const aurea::VectorPaint& p) -> NSDictionary* {
        NSMutableArray* stops = [NSMutableArray array];
        for (const auto& s : p.stops) [stops addObject:@[@(s.pos), @(s.color.x), @(s.color.y), @(s.color.z), @(s.color.w)]];
        return @{@"type": @(p.type), @"color": @[@(p.color.x), @(p.color.y), @(p.color.z), @(p.color.w)], @"points": @[@(p.start.x), @(p.start.y), @(p.end.x), @(p.end.y)], @"stops": stops};
    };
    for (u32 index = 0; index < doc.groups.size(); ++index) {
        const auto& g = doc.groups[index];
        float params[aurea::Engine::kVectorParamFloats]{};
        (void)e->query_vector_params(layerId, index, params, aurea::Engine::kVectorParamFloats);
        NSMutableArray* paths = [NSMutableArray array];
        NSMutableArray* pathKeyCounts = [NSMutableArray array];
        for (const auto& p : g.paths) [pathKeyCounts addObject:@(p.keys.size())];
        for (const auto& p : g.paths) [paths addObject:@[@(static_cast<u32>(p.kind)), @(p.reversed), @(p.center.x), @(p.center.y), @(p.size.x), @(p.size.y), @(p.roundness), @(p.points), @(p.outerRadius), @(p.innerRadius), @(p.outerRoundness), @(p.innerRoundness), @(p.rotation)]];
        [groups addObject:@{@"id": @(index), @"name": to_ns(g.name), @"visible": @(g.visible), @"merge": @(g.merge), @"params": floats_to_array(params, aurea::Engine::kVectorParamFloats), @"pathKeyCounts": pathKeyCounts,
            @"fillEnabled": @(g.fill.enabled), @"fillRule": @(g.fill.rule), @"fill": paint(g.fill.paint),
            @"strokeEnabled": @(g.stroke.enabled), @"stroke": paint(g.stroke.paint), @"cap": @(g.stroke.cap), @"join": @(g.stroke.join), @"miter": @(g.stroke.miterLimit), @"dashes": floats_to_array(g.stroke.dashes.data(), static_cast<u32>(g.stroke.dashes.size())),
            @"trimEnabled": @(g.trim.enabled), @"trimMode": @(g.trim.mode), @"repeatEnabled": @(g.repeater.enabled), @"repeatAbove": @(g.repeater.above), @"repeatAnchor": @[@(g.repeater.anchor.x), @(g.repeater.anchor.y)], @"anchor": @[@(g.anchor.x), @(g.anchor.y)], @"paths": paths}];
    }
    return groups;
}
- (BOOL)editVectorGroup:(long long)layerId group:(uint32_t)group field:(uint32_t)field values:(NSArray<NSNumber*>*)values {
    auto* e = self.engine; std::vector<float> data; std::string names; aurea::VectorData doc;
    if (!e || !e->vector_document(layerId, data, names) || !aurea::vector::decode_document(data.data(), data.size(), names, doc) || group >= doc.groups.size()) return NO;
    std::vector<float> v; for (NSNumber* n in values) { if (!std::isfinite(n.floatValue)) return NO; v.push_back(n.floatValue); }
    if (v.empty() && field != 16) return NO;
    auto& g = doc.groups[group];
    auto color = [&]() { return aurea::Vec4{v[0], v[1], v[2], v[3]}; };
    auto stops = [&](aurea::VectorPaint& p) { p.stops.clear(); for (usize n = 0; n + 4 < v.size(); n += 5) p.stops.push_back({v[n], {v[n+1], v[n+2], v[n+3], v[n+4]}}); };
    switch (field) {
        case 0: g.visible = v[0] > .5f; break;
        case 1: g.merge = static_cast<u8>(std::clamp(v[0], 0.f, 4.f)); break;
        case 2: g.fill.enabled = v[0] > .5f; break;
        case 3: g.fill.rule = v[0] > .5f; break;
        case 4: g.fill.paint.type = static_cast<u8>(std::clamp(v[0], 0.f, 2.f)); break;
        case 5: if (v.size() != 4) return NO; g.fill.paint.color = color(); break;
        case 6: if (v.size() != 4) return NO; g.fill.paint.start = {v[0],v[1]}; g.fill.paint.end = {v[2],v[3]}; break;
        case 7: if (v.size() % 5 || v.size() > 40) return NO; stops(g.fill.paint); break;
        case 8: g.stroke.enabled = v[0] > .5f; break;
        case 9: g.stroke.paint.type = static_cast<u8>(std::clamp(v[0], 0.f, 2.f)); break;
        case 10: if (v.size() != 4) return NO; g.stroke.paint.color = color(); break;
        case 11: if (v.size() != 4) return NO; g.stroke.paint.start = {v[0],v[1]}; g.stroke.paint.end = {v[2],v[3]}; break;
        case 12: if (v.size() % 5 || v.size() > 40) return NO; stops(g.stroke.paint); break;
        case 13: g.stroke.cap = static_cast<u8>(std::clamp(v[0], 0.f, 2.f)); break;
        case 14: g.stroke.join = static_cast<u8>(std::clamp(v[0], 0.f, 2.f)); break;
        case 15: g.stroke.miterLimit = std::max(1.f, v[0]); break;
        case 16: g.stroke.dashes = v; break;
        case 17: g.trim.enabled = v[0] > .5f; break;
        case 18: g.trim.mode = v[0] > .5f; break;
        case 19: g.repeater.enabled = v[0] > .5f; break;
        case 20: g.repeater.above = v[0] > .5f; break;
        case 21: if (v.size() != 2) return NO; g.repeater.anchor = {v[0],v[1]}; break;
        case 22: if (v.size() != 2) return NO; g.anchor = {v[0],v[1]}; break;
        case 23: {
            if (v.size() != 14 || v[0] < 0 || v[0] >= g.paths.size()) return NO;
            auto& p = g.paths[static_cast<u32>(v[0])]; p.reversed = v[2] > .5f;
            p.center = {v[3],v[4]}; p.size = {v[5],v[6]}; p.roundness = v[7]; p.points = v[8];
            p.outerRadius = v[9]; p.innerRadius = v[10]; p.outerRoundness = v[11]; p.innerRoundness = v[12]; p.rotation = v[13]; break;
        }
        default: return NO;
    }
    aurea::vector::encode_document(doc, data, names);
    return e->set_vector_document(layerId, data.data(), data.size(), names, false);
}
- (BOOL)renameVectorGroup:(long long)layerId group:(uint32_t)group name:(NSString*)name {
    auto* e = self.engine; std::vector<float> data; std::string names; aurea::VectorData doc;
    if (!e || !e->vector_document(layerId, data, names) || !aurea::vector::decode_document(data.data(), data.size(), names, doc) || group >= doc.groups.size()) return NO;
    doc.groups[group].name = to_std(name); aurea::vector::encode_document(doc, data, names);
    return e->set_vector_document(layerId, data.data(), data.size(), names, false);
}
- (int32_t)addVectorGroup:(long long)layerId kind:(uint32_t)kind { auto* e = self.engine; return e ? e->add_vector_group(layerId, kind) : -1; }
- (BOOL)removeVectorGroup:(long long)layerId group:(uint32_t)group { auto* e = self.engine; return e && e->remove_vector_group(layerId, group); }
- (int32_t)addVectorPath:(long long)layerId group:(uint32_t)group kind:(uint32_t)kind { auto* e = self.engine; return e ? e->add_vector_path(layerId, group, kind, nullptr, 0) : -1; }
- (BOOL)removeVectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path { auto* e = self.engine; return e && e->remove_vector_path(layerId, group, path); }
- (NSArray<NSNumber*>*)vectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path {
    auto* e = self.engine; std::vector<float> v; if (!e || !e->vector_path_at(layerId, group, path, v)) return @[]; return floats_to_array(v.data(), static_cast<u32>(v.size()));
}
- (BOOL)setVectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path values:(NSArray<NSNumber*>*)values continuing:(BOOL)continuing {
    auto* e = self.engine; std::vector<float> v; for (NSNumber* n in values) v.push_back(n.floatValue);
    return e && e->set_vector_path(layerId, group, path, v.data(), v.size(), continuing);
}
- (BOOL)keyVectorPath:(long long)layerId group:(uint32_t)group path:(uint32_t)path { auto* e = self.engine; return e && e->ensure_vector_path_key(layerId, group, path); }
- (BOOL)makeVectorPathEditable:(long long)layerId group:(uint32_t)group path:(uint32_t)path { auto* e = self.engine; return e && e->make_vector_path_editable(layerId, group, path); }
- (BOOL)toggleVectorPathKey:(long long)layerId group:(uint32_t)group path:(uint32_t)path { auto* e = self.engine; return e && e->toggle_vector_path_key(layerId, group, path); }
- (BOOL)setVectorParam:(long long)layerId group:(uint32_t)group param:(uint32_t)param value:(float)value { auto* e = self.engine; return e && e->set_vector_param(layerId, group, param, value, false); }
- (BOOL)keyVectorParam:(long long)layerId group:(uint32_t)group param:(uint32_t)param { auto* e = self.engine; return e && e->ensure_vector_param_key(layerId, group, param); }
- (BOOL)toggleVectorParamKey:(long long)layerId group:(uint32_t)group param:(uint32_t)param { auto* e = self.engine; return e && e->toggle_vector_param_key(layerId, group, param); }
- (long long)addFreehand:(long long)layerId points:(NSArray<NSNumber*>*)points error:(float)error {
    auto* e = self.engine; if (!e || points.count % 2) return -1; std::vector<float> v; for (NSNumber* n in points) v.push_back(n.floatValue);
    const auto result = e->add_freehand_path(layerId, v.data(), v.size(), error); return result.ok() ? static_cast<long long>(*result) : -1;
}
- (long long)importSVG:(NSString*)text name:(NSString*)name { auto* e = self.engine; if (!e) return -1; const auto r = e->import_svg(to_std(text), name.UTF8String); return r.ok() ? static_cast<long long>(*r) : -1; }
- (NSArray<NSNumber*>*)textPath:(long long)layerId {
    auto* e = self.engine; u64 target = 0; float offset = 0; bool perpendicular = false, reverse = false;
    if (!e || !e->query_text_path(layerId, target, offset, perpendicular, reverse)) return @[];
    return @[@(target), @(offset), @(perpendicular), @(reverse)];
}
- (BOOL)setTextPath:(long long)layerId target:(long long)target offset:(float)offset perpendicular:(BOOL)perpendicular reversed:(BOOL)reversed { auto* e = self.engine; return e && e->set_text_path(layerId, target, offset, perpendicular, reversed); }
- (BOOL)setTextSpan:(long long)layerId start:(uint32_t)start end:(uint32_t)end color:(NSArray<NSNumber*>*)color weight:(uint32_t)weight scale:(float)scale {
    auto* e = self.engine; aurea::Vec4 c{1,1,1,1}; if (color.count == 4) c = {color[0].floatValue, color[1].floatValue, color[2].floatValue, color[3].floatValue};
    return e && e->set_text_span(layerId, start, end, color.count == 4, c, weight, scale);
}
- (BOOL)clearTextSpans:(long long)layerId start:(uint32_t)start end:(uint32_t)end { auto* e = self.engine; return e && e->clear_text_spans(layerId, start, end); }
- (NSArray<NSNumber*>*)keyframeEasing:(long long)layerId property:(uint32_t)property frame:(int32_t)frame {
    auto* e = self.engine; float v[4]{};
    if (!e || !e->query_keyframe_easing(layerId, property, aurea::kInvalidIndex, 0, frame, v)) return @[];
    return floats_to_array(v, 4);
}
- (BOOL)editShape:(long long)layerId param:(uint32_t)param value:(float)value continuing:(BOOL)continuing {
    auto* e = self.engine;
    return e && e->set_shape_param(layerId, param, value, continuing);
}
- (BOOL)keyShape:(long long)layerId param:(uint32_t)param {
    auto* e = self.engine;
    return e && e->ensure_shape_param_key(layerId, param);
}
- (NSArray<NSNumber*>*)trackMatte:(long long)layerId {
    auto* e = self.engine;
    aurea::u64 matte = 0; aurea::u32 mode = 0;
    if (!e || !e->query_track_matte(layerId, matte, mode)) return @[];
    return @[@(matte), @(mode)];
}
- (NSArray<NSNumber*>*)maskData:(long long)layerId {
    auto* e = self.engine; if (!e) return @[];
    std::vector<float> values(4096);
    auto size = e->query_masks(layerId, values.data(), static_cast<aurea::u32>(values.size()));
    if (size > values.size()) { values.resize(size); size = e->query_masks(layerId, values.data(), static_cast<aurea::u32>(values.size())); }
    return floats_to_array(values.data(), std::min<size_t>(size, values.size()));
}
- (int32_t)addMask:(long long)layerId points:(NSArray<NSNumber*>*)points closed:(BOOL)closed {
    auto* e = self.engine;
    if (!e || points.count % 6 || points.count > 24576) return -1;
    std::vector<float> v; v.reserve(points.count);
    for (NSNumber* n in points) v.push_back(n.floatValue);
    return e->add_mask(layerId, v.data(), static_cast<aurea::u32>(v.size() / 6), closed);
}
- (BOOL)removeMask:(long long)layerId mask:(uint32_t)mask {
    auto* e = self.engine; return e && e->remove_mask(layerId, mask);
}
- (BOOL)setMaskPath:(long long)layerId mask:(uint32_t)mask points:(NSArray<NSNumber*>*)points closed:(BOOL)closed undo:(BOOL)undo {
    auto* e = self.engine;
    if (!e || points.count % 6 || points.count > 24576) return NO;
    std::vector<float> v; v.reserve(points.count);
    for (NSNumber* n in points) v.push_back(n.floatValue);
    return e->set_mask_path(layerId, mask, v.data(), static_cast<aurea::u32>(v.size() / 6), closed, undo);
}
- (BOOL)setMaskProps:(long long)layerId mask:(uint32_t)mask operation:(uint32_t)operation inverted:(BOOL)inverted feather:(float)feather expansion:(float)expansion opacity:(float)opacity {
    auto* e = self.engine; return e && e->set_mask_props(layerId, mask, operation, inverted, feather, expansion, opacity);
}
- (BOOL)keyMask:(long long)layerId mask:(uint32_t)mask {
    auto* e = self.engine; return e && e->ensure_mask_path_key(layerId, mask);
}
- (BOOL)toggleMaskKey:(long long)layerId mask:(uint32_t)mask {
    auto* e = self.engine; return e && e->toggle_mask_path_key(layerId, mask);
}
- (NSArray<NSNumber*>*)textAnimators:(long long)layerId {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<float> values(40 * 64);
    // The core returns the animator COUNT, not the number of floats.
    const auto count = e->query_text_animators(layerId, values.data(), static_cast<aurea::u32>(values.size()));
    return floats_to_array(values.data(), std::min<size_t>(count, 64) * 40);
}
- (BOOL)setTextAnimator:(long long)layerId index:(uint32_t)index values:(NSArray<NSNumber*>*)values {
    auto* e = self.engine;
    if (!e || values.count != 40) return NO;
    float raw[40];
    for (NSUInteger i = 0; i < 40; ++i) raw[i] = values[i].floatValue;
    return e->set_text_animator(layerId, index, raw);
}
- (NSArray<NSNumber*>*)textStyle:(long long)layerId {
    auto* e = self.engine;
    float values[18]{};
    if (!e || !e->query_text_style(layerId, values)) return @[];
    return floats_to_array(values, 18);
}
- (BOOL)setTextStyle:(long long)layerId values:(NSArray<NSNumber*>*)values {
    auto* e = self.engine;
    if (!e || values.count != 18) return NO;
    float raw[18];
    for (NSUInteger i = 0; i < 18; ++i) raw[i] = values[i].floatValue;
    return e->set_text_style(layerId, raw);
}

- (void)setLayer:(long long)layerId fadeIn:(int32_t)frames {
    if (auto* c = _batch.add(CommandType::AudioSetFadeIn)) {
        c->audio_fade.layer = layer_of(layerId);
        c->audio_fade.duration = aurea::FrameIndex{frames};
    }
    [self flush];
}

- (void)setLayer:(long long)layerId fadeOut:(int32_t)frames {
    if (auto* c = _batch.add(CommandType::AudioSetFadeOut)) {
        c->audio_fade.layer = layer_of(layerId);
        c->audio_fade.duration = aurea::FrameIndex{frames};
    }
    [self flush];
}

- (void)setLayer:(long long)layerId speed:(float)speed {
    if (auto* c = _batch.add(CommandType::LayerSetSpeed)) {
        c->audio_gain.layer = layer_of(layerId);
        c->audio_gain.gain = speed;
    }
    [self flush];
}

- (void)setLayer:(long long)layerId reversed:(BOOL)reversed {
    if (auto* c = _batch.add(CommandType::LayerSetReversed)) {
        c->audio_flag.layer = layer_of(layerId);
        c->audio_flag.flag = reversed != NO;
    }
    [self flush];
}

// =============================================================================
// Pré-composição (agrupar / desagrupar)
// =============================================================================
- (long long)precomposeLayers:(NSArray<NSNumber*>*)layerIds name:(NSString*)name {
    auto* e = self.engine;
    if (!e) return -static_cast<long long>(aurea::Errc::InvalidState);
    std::vector<aurea::u64> ids;
    ids.reserve(layerIds.count);
    for (NSNumber* n in layerIds) ids.push_back(static_cast<aurea::u64>(n.longLongValue));
    if (ids.empty()) return -static_cast<long long>(aurea::Errc::InvalidArgument);
    const std::string label = to_std(name);
    const aurea::Result<aurea::u64> r = e->precompose(ids.data(), static_cast<aurea::u32>(ids.size()),
                                                     label.empty() ? nullptr : label.c_str());
    if (!r.ok()) return -static_cast<long long>(r.status().code());
    return static_cast<long long>(*r);
}

- (NSString*)ungroupPrecomp:(long long)layerId {
    auto* e = self.engine;
    if (!e) return @"motor indisponivel";
    std::string why;
    const aurea::Result<aurea::u32> r = e->ungroup_precomp(static_cast<aurea::u64>(layerId), &why);
    if (r.ok()) return @"";
    return to_ns(why.empty() ? std::string("nao deu para desagrupar") : why);
}

- (void)setTimeRemap:(BOOL)on forLayer:(long long)layerId {
    if (auto* e = self.engine) (void)e->set_time_remap(static_cast<aurea::u64>(layerId), on != NO);
}

- (void)applySpeedRamp:(uint32_t)preset forLayer:(long long)layerId {
    if (auto* e = self.engine) (void)e->apply_speed_ramp(static_cast<aurea::u64>(layerId), preset);
}

- (void)setEchoForLayer:(long long)layerId count:(uint32_t)count delay:(float)delay decay:(float)decay {
    if (auto* e = self.engine) (void)e->set_echo(static_cast<aurea::u64>(layerId), count, delay, decay);
}

- (void)setRgbTimeForLayer:(long long)layerId delay:(float)delay {
    if (auto* e = self.engine) (void)e->set_rgb_time(static_cast<aurea::u64>(layerId), delay);
}

- (void)setMotionBlur:(BOOL)on forLayer:(long long)layerId {
    if (auto* e = self.engine) (void)e->set_motion_blur(static_cast<aurea::u64>(layerId), on != NO);
}

- (void)setTransitionForLayer:(long long)layerId out:(BOOL)out type:(uint32_t)type frames:(uint32_t)frames {
    if (auto* e = self.engine) {
        (void)e->set_transition(static_cast<aurea::u64>(layerId), out != NO, type, frames);
    }
}

- (void)setTrackMatteForLayer:(long long)layerId matte:(long long)matteLayerId mode:(uint32_t)mode {
    if (auto* e = self.engine) {
        (void)e->set_track_matte(static_cast<aurea::u64>(layerId), static_cast<aurea::u64>(matteLayerId), mode);
    }
}

- (void)setFrameBlendForLayer:(long long)layerId mode:(uint32_t)mode {
    if (auto* e = self.engine) (void)e->set_frame_blend(static_cast<aurea::u64>(layerId), mode);
}

- (void)setVectorBlurForLayer:(long long)layerId amount:(float)amount {
    if (auto* e = self.engine) (void)e->set_vector_blur(static_cast<aurea::u64>(layerId), amount);
}

- (void)toggleMarker:(int64_t)frame {
    if (auto* e = self.engine) (void)e->toggle_marker(frame);
}

- (NSArray<NSNumber*>*)markers {
    auto* e = self.engine;
    if (!e) return @[];
    std::vector<i64> raw(3 * 32);
    u32 total = e->query_markers(raw.data(), 32);
    if (total > 32) {
        raw.assign(static_cast<usize>(total) * 3, 0);
        total = e->query_markers(raw.data(), total);
    }
    NSMutableArray<NSNumber*>* out = [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(total) * 3];
    for (u32 i = 0; i < total * 3; ++i) [out addObject:@(raw[i])];
    return out;
}

- (BOOL)editMarker:(int64_t)from to:(int64_t)to color:(uint32_t)color label:(NSString*)label {
    if (auto* e = self.engine) return e->edit_marker(from, to, color, to_std(label));
    return NO;
}

- (BOOL)deleteMarker:(int64_t)frame {
    if (auto* e = self.engine) return e->delete_marker(frame);
    return NO;
}

- (NSString*)markerLabel:(int64_t)frame {
    if (auto* e = self.engine) return to_ns(e->marker_label(frame));
    return @"";
}

// =============================================================================
// Imagens (miniatura / captura / prévia de efeito)
// =============================================================================
- (NSData*)thumbnailForLayer:(long long)layerId frame:(int32_t)frame
                      height:(uint32_t)height outWidth:(uint32_t*)outWidth {
    auto* e = self.engine;
    if (!e || height == 0) return nil;
    if (height > 512) return nil;
    aurea::bridge::LayerDetailPOD detail{};
    if (!e->query_layer_detail(layerId, detail) || detail.sourceHeight == 0) return nil;
    // A largura segue a mídia, inclusive vídeos horizontais e panoramas.
    const auto widthHint = std::max<usize>(1, static_cast<usize>(std::ceil(static_cast<double>(height) * detail.sourceWidth / detail.sourceHeight)));
    const usize capacity = widthHint * height * 4;
    if (capacity > 64 * 1024 * 1024) return nil;
    std::vector<u8> pixels(capacity);
    u32 width = 0;
    const u32 bytes = e->query_thumbnail(static_cast<aurea::u64>(layerId), frame, height,
                                         pixels.data(), static_cast<u32>(capacity), &width);
    if (bytes == 0) return nil;
    if (outWidth) *outWidth = width;
    return [NSData dataWithBytes:pixels.data() length:bytes];
}

- (NSData*)captureFrame:(uint32_t)maxDim outWidth:(uint32_t*)outWidth outHeight:(uint32_t*)outHeight {
    auto* e = self.engine;
#if DEBUG
    _lastCaptureDiagnostics = @{ @"attempted": @YES, @"ok": @NO,
        @"detail": e ? @"dimensao de captura invalida" : @"motor indisponivel" };
#endif
    if (!e || maxDim == 0) return nil;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    const aurea::Status result = e->capture_frame_rgba(maxDim, rgba, w, h);
#if DEBUG
    _lastCaptureDiagnostics = @{ @"attempted": @YES, @"ok": @(result.ok() && !rgba.empty()),
        @"status": @(static_cast<int32_t>(result.code())),
        @"message": to_ns(std::string(result.message())), @"detail": to_ns(std::string(result.detail())),
        @"width": @(w), @"height": @(h), @"bytes": @(rgba.size()) };
#endif
    if (!result.ok() || rgba.empty()) return nil;
    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = h;
    return [NSData dataWithBytes:rgba.data() length:rgba.size()];
}

- (NSData*)effectPreview:(uint32_t)typeId width:(uint32_t)width height:(uint32_t)height outSize:(CGSize*)outSize {
    auto* e = self.engine;
    if (!e || width == 0 || height == 0) return nil;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    if (!e->render_effect_preview(typeId, width, height, rgba, w, h).ok() || rgba.empty()) return nil;
    if (outSize) *outSize = CGSizeMake(w, h);
    return [NSData dataWithBytes:rgba.data() length:rgba.size()];
}

- (BOOL)setEffectPreviewSource:(NSData*)rgba width:(uint32_t)width height:(uint32_t)height {
    auto* e = self.engine;
    if (!e || rgba.length < static_cast<NSUInteger>(width) * height * 4) return NO;
    return e->set_effect_preview_source(static_cast<const u8*>(rgba.bytes), width, height) ? YES : NO;
}

// =============================================================================
// Export
// =============================================================================
- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps {
    return [self startExportTo:path codec:codec height:height fps:fps bitrateMbps:bitrateMbps
             audioBitrateKbps:audioBitrateKbps aiUpscale:0];
}

- (BOOL)startExportTo:(NSString*)path codec:(AureaExportCodec)codec
               height:(uint32_t)height fps:(double)fps
          bitrateMbps:(uint32_t)bitrateMbps audioBitrateKbps:(uint32_t)audioBitrateKbps
            aiUpscale:(uint32_t)aiUpscale {
    auto* e = self.engine;
    if (!e) return NO;
    aurea::ExportSettings settings;
    // `height` é o LADO MENOR pedido; a largura vem da proporção (o motor
    // calcula, e nunca estica).
    settings.height = height;
    settings.fps = fps;
    settings.videoCodec = static_cast<aurea::ExportCodec>(codec);
    settings.videoBitrateMbps = bitrateMbps;
    settings.audioBitrateKbps = audioBitrateKbps;
    settings.aiUpscale = aiUpscale;
    settings.container = 0;   // MP4, o mesmo do Android
    const std::string out = to_std(path);
    return e->start_export(settings, out.c_str()).ok() ? YES : NO;
}

- (void)cancelExport {
    if (auto* e = self.engine) (void)e->cancel_export();
}

@end
