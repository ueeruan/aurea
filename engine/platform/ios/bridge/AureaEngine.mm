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

#include <algorithm>
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
    auto* e = self.engine;
    return e && e->save_project(to_std(path).c_str()).ok() ? YES : NO;
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
    if (auto* c = _batch.add(CommandType::PlaybackSeek)) {
        c->seek.time = aurea::TickNs{frame};
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
    if (auto* c = _batch.add(CommandType::PlaybackScrub)) {
        c->seek.time = aurea::TickNs{frame};
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

- (void)addTextAnimator:(long long)layerId props:(uint32_t)props {
    if (auto* e = self.engine) (void)e->add_text_animator(static_cast<aurea::u64>(layerId), props);
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

- (void)applyTextPreset:(long long)layerId preset:(uint32_t)preset {
    if (auto* e = self.engine) (void)e->apply_text_preset(static_cast<aurea::u64>(layerId), preset);
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
    if (!e->query_object_environment(static_cast<aurea::u64>(layerId), v)) return @[];
    return @[@(v[0]), @(v[1]), @(v[2]), @(v[3]), @(v[4])];
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

// =============================================================================
// Imagens (miniatura / captura / prévia de efeito)
// =============================================================================
- (NSData*)thumbnailForLayer:(long long)layerId frame:(int32_t)frame
                      height:(uint32_t)height outWidth:(uint32_t*)outWidth {
    auto* e = self.engine;
    if (!e || height == 0) return nil;
    // O pior caso é 16:9 na altura pedida; a consulta devolve 0 enquanto a
    // miniatura não está pronta (o status avisa quando chega).
    const usize capacity = static_cast<usize>(height) * height * 4;
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
    if (!e || maxDim == 0) return nil;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    if (!e->capture_frame_rgba(maxDim, rgba, w, h).ok() || rgba.empty()) return nil;
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
    settings.container = 0;   // MP4, o mesmo do Android
    const std::string out = to_std(path);
    return e->start_export(settings, out.c_str()).ok() ? YES : NO;
}

- (void)cancelExport {
    if (auto* e = self.engine) (void)e->cancel_export();
}

@end
