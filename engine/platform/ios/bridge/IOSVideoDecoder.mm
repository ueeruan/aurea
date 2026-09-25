// =============================================================================
//  Aurea / platform / ios / bridge / IOSVideoDecoder.mm
//
//  A mídia do iOS: VideoToolbox. Um arquivo, cinco responsabilidades, todas do
//  mesmo assunto (o que SÓ a plataforma sabe sobre mídia):
//
//   1. VideoToolboxFactory   → `aurea::VideoSourceFactory` (a interface que o
//                              núcleo já tem — ver media/MediaManager.hpp):
//                              `probe`, `open_video`, `open_audio`.
//   2. AVFoundationVideoDecoder → `aurea::VideoDecoderBackend`: AVAssetReader
//                              decodifica em ordem de apresentacao, para CVPixelBuffer com
//                              MetalCompatibility + IOSurface vazio, que é o que
//                              faz o buffer virar textura Metal SEM cópia.
//   3. AudioToolboxDecoder   → `aurea::audio::AudioDecoderBackend` (audio/Audio.
//                              hpp): PCM float intercalado no formato do arquivo.
//   4. VideoToolboxExportSink→ `aurea::ExportSink` (export/ExportSink.hpp):
//                              VTCompressionSession + AVAssetWriter — a MESMA
//                              fronteira do MediaCodec/AMediaMuxer do Android.
//   5. fill_platform_info / ios_load_image / ios_default_font_path: os fatos do
//      aparelho e a decodificação de imagem que o motor não pode fazer sozinho.
//
//  No caminho BGRA/Metal o CVPixelBuffer com IOSurface é importado sem cópia
//  pela CPU. AVFoundation faz a conversão de cor. Miniaturas e o caminho P010
//  usam os planos; o fallback BGRA de miniaturas converte para RGBA na CPU.
// =============================================================================
#include "AureaBridge.h"
#if DEBUG
#import "AureaEngine.h"
#if defined(AUREA_GPU_METAL)
#include "MetalBackend.hpp"
#endif
#endif

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>
#import <VideoToolbox/VideoToolbox.h>

#include "aurea/core/Log.hpp"
#include "aurea/media/MediaManager.hpp"

#include <sys/sysctl.h>
#include <unistd.h>
#include <os/proc.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

using namespace aurea;

// Os acessores síncronos de AVAsset (`tracks`, `duration`) estão marcados como
// obsoletos desde o iOS 16 em favor dos `loadValuesAsynchronously…`. A sonda do
// motor é SÍNCRONA de propósito (é chamada fora do lock do modelo, na
// importação), então aqui se usa o caminho síncrono — e o aviso é silenciado
// neste arquivo, não no projeto inteiro.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace aurea::ios {
namespace {

// =============================================================================
// Conversões de tempo
// =============================================================================
inline CMTime cm_time_us(i64 us) {
    return CMTimeMake(us, 1'000'000);
}

inline i64 us_of(CMTime t) {
    if (!CMTIME_IS_VALID(t)) return 0;
    return (i64)llround(CMTimeGetSeconds(t) * 1'000'000.0);
}

// =============================================================================
// Cor: os metadados do arquivo → o vocabulário do motor (VideoTypes.hpp).
//
// Mesma regra do Android: a cor vem do ARQUIVO. Assumir BT.709 limitado desloca
// a cor de metade dos vídeos, e ninguém sabe dizer por quê.
// =============================================================================
VideoColorInfo color_from_format(CMFormatDescriptionRef fmt) {
    VideoColorInfo color;
    if (!fmt) return color;
    CFDictionaryRef ext = CMFormatDescriptionGetExtensions(fmt);
    if (!ext) return color;

    if (CFStringRef matrix = (CFStringRef)CFDictionaryGetValue(ext, kCMFormatDescriptionExtension_YCbCrMatrix)) {
        if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) color.matrix = YCbCrMatrix::BT601;
        else if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2)) color.matrix = YCbCrMatrix::BT709;
        else if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_2020)) color.matrix = YCbCrMatrix::BT2020;
        color.fromStream = true;
    }
    if (CFStringRef prim = (CFStringRef)CFDictionaryGetValue(ext, kCMFormatDescriptionExtension_ColorPrimaries)) {
        if (CFEqual(prim, kCVImageBufferColorPrimaries_ITU_R_709_2)) color.primaries = ColorPrimaries::BT709;
        else if (CFEqual(prim, kCVImageBufferColorPrimaries_ITU_R_2020)) color.primaries = ColorPrimaries::BT2020;
        else if (CFEqual(prim, kCVImageBufferColorPrimaries_P3_D65)) color.primaries = ColorPrimaries::P3;
        else if (CFEqual(prim, kCVImageBufferColorPrimaries_SMPTE_C)) color.primaries = ColorPrimaries::BT601;
        color.fromStream = true;
    }
    if (CFStringRef tf = (CFStringRef)CFDictionaryGetValue(ext, kCMFormatDescriptionExtension_TransferFunction)) {
        if (CFEqual(tf, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ)) color.transfer = TransferFunction::PQ;
        else if (CFEqual(tf, kCVImageBufferTransferFunction_ITU_R_2100_HLG)) color.transfer = TransferFunction::HLG;
        else if (CFEqual(tf, kCVImageBufferTransferFunction_sRGB)) color.transfer = TransferFunction::SRGB;
        else if (CFEqual(tf, kCVImageBufferTransferFunction_Linear)) color.transfer = TransferFunction::Linear;
        color.fromStream = true;
    }
    if (CFBooleanRef full = (CFBooleanRef)CFDictionaryGetValue(ext, kCMFormatDescriptionExtension_FullRangeVideo)) {
        color.fullRange = CFBooleanGetValue(full);
        color.fromStream = true;
    }
    // A PROFUNDIDADE nao sai daqui: o subtipo de midia (hvc1 pode ser 8 ou 10
    // bits) nao e conclusivo. Quem decide e o CVPixelBuffer decodificado, no
    // `next_frame` — o mesmo cuidado do Android, onde o formato real do buffer
    // e quem manda.
    return color;
}

/// Formatos de pixel que o decodificador pede ao VideoToolbox. Sempre com
/// IOSurface e compatível com Metal (é o que faz o buffer virar textura sem
/// cópia).
///
/// POR QUE **BGRA** E NÃO NV12 NO CONTEÚDO DE 8 BITS: o backend Metal amostra
/// `MTLPixelFormat420YpCbCr8BiPlanar*`, mas a conversão embutida no formato usa
/// SEMPRE a matriz BT.601 — um vídeo BT.709 (o padrão de celular) sairia com a
/// cor deslocada, e o motor trata metadado de cor como obrigatório
/// (VideoTypes.hpp). Pedindo BGRA, quem converte é o VideoToolbox, com a matriz
/// DO ARQUIVO, e o backend importa BGRA direto (`MetalResources.mm`): continua
/// zero-copy e a cor fica exata. O 10 bits continua em YCbCr biplanar — ali o
/// que importa é não JOGAR FORA a profundidade (o P010 que o motor espera).
NSDictionary* pixel_attributes(u32 width, u32 height, bool tenBit, bool fullRange) {
    OSType type = tenBit ? (fullRange ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange
                                     : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange)
                         : kCVPixelFormatType_32BGRA;
    NSDictionary* attributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(type),
        (id)kCVPixelBufferWidthKey: @(width),
        (id)kCVPixelBufferHeightKey: @(height),
        // ESTAS DUAS LINHAS SÃO O ZERO-COPY: IOSurface (sem propriedades, o
        // layout que a GPU amostra) e compatibilidade com Metal. Sem elas o
        // VideoToolbox entrega memória que só a CPU lê, e o frame teria que ser
        // copiado plano a plano.
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
    };
    return attributes;
}

/// `DecodedFrame` com um CVPixelBuffer dentro. A contagem de referências do
/// motor (FrameRef) é quem decide quando o buffer volta para o pool.
class IOSDecodedFrame final : public DecodedFrame {
public:
    ~IOSDecodedFrame() override {
        if (pixel && cpuLocked) CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
        if (pixel) CFRelease(pixel);
    }
    CVPixelBufferRef pixel = nullptr;
    bool cpuLocked = false;
    std::vector<u8> rgba;
};

/// Identidade do CVPixelBuffer retido pelo frame e pela textura importada.
/// O cache Metal usa esse mesmo objeto; sua retenção impede reuso prematuro.
u64 buffer_identity(CVPixelBufferRef pixel) {
    return static_cast<u64>(reinterpret_cast<uintptr_t>(pixel));
}

} // namespace

// =============================================================================
// 2. O decodificador de vídeo.
// =============================================================================
class AVFoundationVideoDecoder final : public VideoDecoderBackend {
public:
    AVFoundationVideoDecoder(AVAsset* asset, AVAssetTrack* track, bool zeroCopy) noexcept;
    ~AVFoundationVideoDecoder() override;

    [[nodiscard]] const VideoStreamInfo& info() const noexcept override { return info_; }
    [[nodiscard]] Status seek_to_keyframe(i64 targetUs) noexcept override;
    [[nodiscard]] Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs,
                                    bool& endOfStream) noexcept override;
    // Retained CVPixelBuffers are independent of the reader's next output.
    // Match the Android cache window; the shared byte budget bounds memory.
    [[nodiscard]] u32 max_live_frames() const noexcept override { return 12; }
    [[nodiscard]] i64 keyframe_interval_us() const noexcept override { return keyframeUs_; }
    void suspend() noexcept override;
    [[nodiscard]] Status resume() noexcept override;

private:
    bool start_reader(i64 fromUs) noexcept;
    bool build_timing_index() noexcept;
    void teardown() noexcept;
    [[nodiscard]] Status wrap_sample(CMSampleBufferRef sample, i64 deliverFromUs, FrameRef& out,
                                    i64& outPtsUs) noexcept;

    __strong AVAsset* asset_ = nil;
    __strong AVAssetTrack* track_ = nil;
    __strong AVAssetReader* reader_ = nil;
    __strong AVAssetReaderTrackOutput* output_ = nil;

    VideoStreamInfo info_{};
    i64 keyframeUs_ = 2'000'000;
    i64 positionUs_ = 0;         ///< onde o decoder parou (para o resume)
    i64 seekTargetUs_ = -1;      ///< alvo do último seek (retoma aqui)
    bool zeroCopy_ = true;
    bool suspended_ = false;
    bool tenBit_ = false;
    bool fullRange_ = false;
    std::vector<i64> presentationTimes_;
    bool timingIndexed_ = false;
};

AVFoundationVideoDecoder::AVFoundationVideoDecoder(AVAsset* asset, AVAssetTrack* track,
                                                  bool zeroCopy) noexcept
    : asset_(asset), track_(track), zeroCopy_(zeroCopy) {
    @autoreleasepool {
        const CGSize size = track_.naturalSize;
        info_.codedWidth = (u32)llround(size.width);
        info_.codedHeight = (u32)llround(size.height);
        info_.fps = track_.nominalFrameRate > 0.0f ? (f64)track_.nominalFrameRate : 30.0;
        info_.durationUs = us_of(asset_.duration);
        // Rotação: o container diz (o vídeo de celular é gravado deitado).
        const CGAffineTransform t = track_.preferredTransform;
        if (t.a == 0.0 && t.b == 1.0) info_.rotation = 90;
        else if (t.a == -1.0 && t.b == 0.0) info_.rotation = 180;
        else if (t.a == 0.0 && t.b == -1.0) info_.rotation = 270;
        // O track de vídeo de um HEVC Main10 tem 10 bits; o subtipo do formato
        // confirma. A profundidade real ainda é corrigida pelo CVPixelBuffer.
        NSArray* formats = track_.formatDescriptions;
        if (formats.count > 0) {
            CMFormatDescriptionRef fmt = (__bridge CMFormatDescriptionRef)formats.firstObject;
            info_.color = color_from_format(fmt);
            const FourCharCode sub = CMFormatDescriptionGetMediaSubType(fmt);
            const bool hevc = (sub == 'hvc1' || sub == 'hev1');
            std::snprintf(info_.codec, sizeof(info_.codec), "%s", hevc ? "video/hevc" : "video/avc");
            const CFDictionaryRef ext = CMFormatDescriptionGetExtensions(fmt);
            if (ext) {
                const CFBooleanRef full = (CFBooleanRef)CFDictionaryGetValue(
                    ext, kCMFormatDescriptionExtension_FullRangeVideo);
                fullRange_ = full ? CFBooleanGetValue(full) : false;
            }
            // 10 bits: o que vale é a CURVA. PQ e HLG (o HDR do celular) são
            // 10 bits na prática, e são o caso em que pedir BGRA jogaria fora a
            // profundidade. Um SDR de 10 bits cai em BGRA — perde precisão e
            // não perde cor, e é o compromisso documentado aqui.
            tenBit_ = info_.color.transfer == TransferFunction::PQ
                   || info_.color.transfer == TransferFunction::HLG;
        }
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s",
                      zeroCopy ? "AVFoundation (IOSurface)" : "AVFoundation (planos)");
        // AVAssetReader selects the platform decoder; it does not expose whether
        // that instance uses hardware. Do not claim hardware merely from the API.
        info_.hardwareDecoder = false;
        info_.preciseFrameTiming = true;
    }
}

AVFoundationVideoDecoder::~AVFoundationVideoDecoder() { teardown(); }

void AVFoundationVideoDecoder::teardown() noexcept {
    if (reader_.status == AVAssetReaderStatusReading) [reader_ cancelReading];
    output_ = nil;
    reader_ = nil;
}

bool AVFoundationVideoDecoder::build_timing_index() noexcept {
    if (timingIndexed_) return true;
    // Sample references read container timing without copying/decoding video.
    // Build lazily on the decode worker: initial playback at zero needs no index.
    AVAssetReader* indexReader = [[AVAssetReader alloc] initWithAsset:asset_ error:nullptr];
    AVAssetReaderSampleReferenceOutput* references = [[AVAssetReaderSampleReferenceOutput alloc] initWithTrack:track_];
    if (!indexReader || ![indexReader canAddOutput:references]) return false;
    [indexReader addOutput:references];
    if (![indexReader startReading]) return false;
    std::vector<i64> times;
    while (CMSampleBufferRef sample = [references copyNextSampleBuffer]) {
        const CMTime time = CMSampleBufferGetPresentationTimeStamp(sample);
        if (CMTIME_IS_NUMERIC(time)) times.push_back(us_of(time));
        CFRelease(sample);
    }
    if (indexReader.status != AVAssetReaderStatusCompleted || times.empty()) return false;
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    presentationTimes_ = std::move(times);
    timingIndexed_ = true;
    return true;
}

bool AVFoundationVideoDecoder::start_reader(i64 fromUs) noexcept {
    @autoreleasepool {
        teardown();
        const i64 start = fromUs > 0 ? fromUs : 0;
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset_ error:&error];
        if (!reader) {
            AUREA_LOG_ERROR("videotoolbox: reader recusado (%s)", error.localizedDescription.UTF8String);
            return false;
        }
        // Non-nil settings ask AVFoundation to decode and reorder B-frames.
        // The previous compressed path returned decode-order PTS and overwrote
        // delayed VideoToolbox callbacks in a single slot.
        NSDictionary* outputSettings = pixel_attributes(info_.codedWidth, info_.codedHeight, tenBit_, fullRange_);
        AVAssetReaderTrackOutput* output =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:track_ outputSettings:outputSettings];
        if (!output) return false;
        output.alwaysCopiesSampleData = NO;
        if (![reader canAddOutput:output]) {
            AUREA_LOG_ERROR("videotoolbox: reader nao aceitou a trilha de video");
            return false;
        }
        [reader addOutput:output];
        // Seek to the sample that actually covers the requested time. Subtracting
        // one nominal frame misses long VFR samples that began much earlier.
        i64 begin = 0;
        if (start > 0) {
            if (!build_timing_index()) return false;
            const auto after = std::upper_bound(presentationTimes_.begin(), presentationTimes_.end(), start);
            if (after != presentationTimes_.begin()) begin = *(after - 1);
        }
        reader.timeRange = CMTimeRangeMake(cm_time_us(begin), kCMTimePositiveInfinity);
        if (![reader startReading]) {
            AUREA_LOG_ERROR("videotoolbox: reader nao comecou (%s)", reader.error.localizedDescription.UTF8String);
            return false;
        }
        reader_ = reader;
        output_ = output;
        positionUs_ = begin;
        return true;
    }
}

Status AVFoundationVideoDecoder::seek_to_keyframe(i64 targetUs) noexcept {
    @autoreleasepool {
        seekTargetUs_ = targetUs;
        // A new reader invalidates decoder history and pending output atomically.
        // Frames already retained by Metal keep their CVPixelBuffer ownership.
        if (!start_reader(targetUs)) return Status{Errc::DecodeFailed, "nao foi possivel posicionar"};
        suspended_ = false;
        return OkStatus;
    }
}

Status AVFoundationVideoDecoder::wrap_sample(CMSampleBufferRef sample, i64 deliverFromUs, FrameRef& out,
                                               i64& outPtsUs) noexcept {
    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    if (!CMTIME_IS_NUMERIC(pts)) return Status{Errc::DecodeFailed, "timestamp de video invalido"};
    outPtsUs = us_of(pts);
    const CMTime sampleDuration = CMSampleBufferGetDuration(sample);
    const i64 durationUs = CMTIME_IS_NUMERIC(sampleDuration) && us_of(sampleDuration) > 0
        ? us_of(sampleDuration) : static_cast<i64>(std::llround(1e6 / info_.fps));
    if (outPtsUs < deliverFromUs && deliverFromUs - outPtsUs >= durationUs) return OkStatus;
    CVPixelBufferRef pixel = CMSampleBufferGetImageBuffer(sample);
    if (!pixel) return Status{Errc::DecodeFailed, "amostra decodificada sem imagem"};

    auto* frame = new (std::nothrow) IOSDecodedFrame();
    if (!frame) {
        return Status{Errc::OutOfMemory, "sem memoria para o quadro"};
    }
    frame->pixel = (CVPixelBufferRef)CFRetain(pixel); // outlives the sample and reader
    frame->ptsUs = outPtsUs;
    frame->durationUs = durationUs;
    frame->width = (u32)CVPixelBufferGetWidth(pixel);
    frame->height = (u32)CVPixelBufferGetHeight(pixel);
    frame->cropLeft = 0;
    frame->cropTop = 0;
    frame->visibleWidth = frame->width;
    frame->visibleHeight = frame->height;
    frame->rotation = info_.rotation;
    // O formato REAL do buffer manda (o VideoToolbox pode entregar diferente do
    // pedido quando o stream é de outro perfil).
    const OSType type = CVPixelBufferGetPixelFormatType(pixel);
    const bool tenBit = (type == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
                         || type == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange);
    switch (type) {
        case kCVPixelFormatType_32BGRA:                  frame->format = PixelFormat::RGBA8; break;
        case kCVPixelFormatType_32RGBA:                  frame->format = PixelFormat::RGBA8; break;
        case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
        case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
            frame->format = PixelFormat::P010; break;
        default:                                         frame->format = PixelFormat::NV12; break;
    }
    frame->color = info_.color;
    // Faixa: o metadado do ARQUIVO vale para YCbCr; um buffer RGB já é de faixa
    // completa por definição.
    const bool rgbBuffer = (type == kCVPixelFormatType_32BGRA || type == kCVPixelFormatType_32RGBA);
    frame->color.fullRange = rgbBuffer
        || type == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
        || type == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    frame->color.bitDepth = tenBit ? 10 : 8;
    // ZERO-COPY: o ponteiro do buffer vai para o backend importar como textura.
    // Quando o backend não importa CVPixelBuffer, o frame sai sem ele e o
    // renderer cai no caminho de planos (a CPU lê os planos do mesmo buffer).
    frame->hardwareBuffer = zeroCopy_ && rgbBuffer ? (void*)frame->pixel : nullptr;
    if (!frame->hardwareBuffer) {
        if (CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
            delete frame;
            return Status{Errc::DecodeFailed, "CVPixelBufferLockBaseAddress"};
        }
        frame->cpuLocked = true;
        const size_t count = CVPixelBufferGetPlaneCount(pixel);
        frame->planeCount = count ? static_cast<u32>(std::min<size_t>(count, 3)) : 1;
        for (u32 i = 0; i < frame->planeCount; ++i) {
            frame->planes[i] = static_cast<const u8*>(count ? CVPixelBufferGetBaseAddressOfPlane(pixel, i)
                                                         : CVPixelBufferGetBaseAddress(pixel));
            frame->strides[i] = static_cast<u32>(count ? CVPixelBufferGetBytesPerRowOfPlane(pixel, i)
                                                      : CVPixelBufferGetBytesPerRow(pixel));
        }
    }
    if (!frame->hardwareBuffer && type == kCVPixelFormatType_32BGRA) {
        // The shared CPU upload/thumbnail path consumes RGBA, not BGRA.
        frame->rgba.resize(static_cast<usize>(frame->width) * frame->height * 4);
        for (u32 y = 0; y < frame->height; ++y) {
            const u8* src = frame->planes[0] + static_cast<usize>(y) * frame->strides[0];
            u8* dst = frame->rgba.data() + static_cast<usize>(y) * frame->width * 4;
            for (u32 x = 0; x < frame->width; ++x) {
                dst[x*4] = src[x*4+2]; dst[x*4+1] = src[x*4+1];
                dst[x*4+2] = src[x*4]; dst[x*4+3] = src[x*4+3];
            }
        }
        frame->planes[0] = frame->rgba.data();
        frame->strides[0] = frame->width * 4;
    }
    frame->bufferId = buffer_identity(pixel);
    // `adopt` TOMA a referencia inicial do frame recem-criado (refs_ = 1):
    // somar outra aqui vazaria o quadro.
    out = FrameRef::adopt(frame);
    return OkStatus;
}

Status AVFoundationVideoDecoder::next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs,
                                       bool& endOfStream) noexcept {
    @autoreleasepool {
        endOfStream = false;
        out.reset();
        outPtsUs = positionUs_;
        if (suspended_) return Status{Errc::InvalidState, "decoder suspenso"};
        if (!reader_ && !start_reader(seekTargetUs_ > 0 ? seekTargetUs_ : 0)) {
            return Status{Errc::DecodeFailed, "nao foi possivel abrir o decode"};
        }
        // O reader pode estar "failed" (arquivo truncado no meio): a fonte
        // recebe o fim e mostra o último quadro bom.
        if (reader_.status == AVAssetReaderStatusFailed) {
            endOfStream = true;
            return Status{Errc::DecodeFailed, "leitura do arquivo falhou"};
        }
        CMSampleBufferRef sample = [output_ copyNextSampleBuffer];
        if (!sample) {
            if (reader_.status == AVAssetReaderStatusCompleted) {
                endOfStream = true;
                return OkStatus;
            }
            if (reader_.status == AVAssetReaderStatusCancelled || reader_.status == AVAssetReaderStatusFailed) {
                endOfStream = true;
                return Status{Errc::DecodeFailed, "leitura do arquivo terminou mal"};
            }
            return Status{Errc::DecodeFailed, "reader sem amostra antes do fim"};
        }
        const Status s = wrap_sample(sample, deliverFromUs, out, outPtsUs);
        CFRelease(sample);
        positionUs_ = outPtsUs;
        return s;
    }
}

void AVFoundationVideoDecoder::suspend() noexcept {
    @autoreleasepool {
        // Segundo plano: devolver o decoder de hardware é obrigatório — o
        // sistema dá poucos (regra do VideoSource, §13 do spec da Fase 8).
        suspended_ = true;
        teardown();
    }
}

Status AVFoundationVideoDecoder::resume() noexcept {
    if (!suspended_) return OkStatus;
    suspended_ = false;
    const i64 target = positionUs_ > 0 ? positionUs_ : (seekTargetUs_ > 0 ? seekTargetUs_ : 0);
    return seek_to_keyframe(target);
}

// =============================================================================
// 3. O decodificador de áudio (PCM float no formato do arquivo).
// =============================================================================
class AVFoundationAudioDecoder final : public audio::AudioDecoderBackend {
public:
    explicit AVFoundationAudioDecoder(AVAsset* asset, AVAssetTrack* track) noexcept;
    ~AVFoundationAudioDecoder() override;

    [[nodiscard]] const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    [[nodiscard]] Status seek(i64 us) noexcept override;
    [[nodiscard]] Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override;

private:
    bool start_reader(i64 fromUs) noexcept;

    __strong AVAsset* asset_ = nil;
    __strong AVAssetTrack* track_ = nil;
    __strong AVAssetReader* reader_ = nil;
    __strong AVAssetReaderTrackOutput* output_ = nil;
    audio::AudioStreamInfo info_{};
};

AVFoundationAudioDecoder::AVFoundationAudioDecoder(AVAsset* asset, AVAssetTrack* track) noexcept
    : asset_(asset), track_(track) {
    @autoreleasepool {
        NSArray* formats = track_.formatDescriptions;
        if (formats.count > 0) {
            const AudioStreamBasicDescription* asbd =
                CMAudioFormatDescriptionGetStreamBasicDescription(
                    (__bridge CMAudioFormatDescriptionRef)formats.firstObject);
            if (asbd) {
                info_.sampleRate = (u32)llround(asbd->mSampleRate);
                info_.channels = asbd->mChannelsPerFrame;
            }
        }
        if (info_.sampleRate == 0) info_.sampleRate = 48000;
        if (info_.channels == 0) info_.channels = 2;
        info_.durationUs = us_of(asset_.duration);
    }
}

AVFoundationAudioDecoder::~AVFoundationAudioDecoder() {
    output_ = nil;
    reader_ = nil;
}

bool AVFoundationAudioDecoder::start_reader(i64 fromUs) noexcept {
    @autoreleasepool {
        output_ = nil;
        reader_ = nil;
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset_ error:&error];
        if (!reader) return false;
        // PCM float INTERCALADO nos canais do arquivo: é o contrato do
        // AudioDecoderBackend (audio/Audio.hpp) — a conversão para 48 kHz
        // estéreo é do núcleo, não da plataforma.
        NSDictionary* settings = @{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey: @32,
            AVLinearPCMIsFloatKey: @YES,
            AVLinearPCMIsBigEndianKey: @NO,
            AVLinearPCMIsNonInterleaved: @NO,
            AVSampleRateKey: @(info_.sampleRate),
            AVNumberOfChannelsKey: @(info_.channels),
        };
        AVAssetReaderTrackOutput* output =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:track_ outputSettings:settings];
        if (!output || ![reader canAddOutput:output]) return false;
        [reader addOutput:output];
        if (fromUs > 0) reader.timeRange = CMTimeRangeMake(cm_time_us(fromUs), kCMTimePositiveInfinity);
        if (![reader startReading]) return false;
        reader_ = reader;
        output_ = output;
        return true;
    }
}

Status AVFoundationAudioDecoder::seek(i64 us) noexcept {
    output_ = nil;
    reader_ = nil;
    return start_reader(us) ? OkStatus : Status{Errc::DecodeFailed, "nao foi possivel posicionar o audio"};
}

Status AVFoundationAudioDecoder::read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept {
    @autoreleasepool {
        out.clear();
        ptsUs = 0;
        eos = false;
        if (!reader_ && !start_reader(0)) return Status{Errc::DecodeFailed, "audio nao abriu"};
        CMSampleBufferRef sample = [output_ copyNextSampleBuffer];
        if (!sample) {
            eos = true;
            return reader_.status == AVAssetReaderStatusFailed
                       ? Status{Errc::DecodeFailed, "leitura do audio falhou"}
                       : OkStatus;
        }
        ptsUs = us_of(CMSampleBufferGetPresentationTimeStamp(sample));
        CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
        size_t length = 0;
        char* data = nullptr;
        if (block && CMBlockBufferGetDataPointer(block, 0, nullptr, &length, &data) == noErr && data) {
            const usize samples = length / sizeof(f32);
            out.resize(samples);
            std::memcpy(out.data(), data, samples * sizeof(f32));
        }
        CFRelease(sample);
        return OkStatus;
    }
}

// =============================================================================
// 1. A fábrica: probe + abertura.
// =============================================================================
class VideoToolboxFactory final : public VideoSourceFactory, public MediaFactoryControl {
public:
    VideoToolboxFactory() = default;

    void set_zero_copy(bool enabled) noexcept override { zeroCopy_.store(enabled); }
    [[nodiscard]] bool zero_copy() const noexcept { return zeroCopy_.load(); }

    [[nodiscard]] bool probe(const char* sourcePath, MediaProbe& out) override {
        @autoreleasepool {
            if (!sourcePath || !*sourcePath) return false;
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:sourcePath]];
            if (!url) return false;
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
            if (!asset) return false;
            NSArray<AVAssetTrack*>* video = [asset tracksWithMediaType:AVMediaTypeVideo];
            NSArray<AVAssetTrack*>* audio = [asset tracksWithMediaType:AVMediaTypeAudio];
            if (video.count == 0 && audio.count == 0) return false;

            if (video.count > 0) {
                AVAssetTrack* track = video.firstObject;
                const CGSize size = track.naturalSize;
                out.video.codedWidth = (u32)llround(size.width);
                out.video.codedHeight = (u32)llround(size.height);
                out.video.fps = track.nominalFrameRate > 0.0f ? (f64)track.nominalFrameRate : 30.0;
                out.video.durationUs = us_of(asset.duration);
                const CGAffineTransform t = track.preferredTransform;
                if (t.a == 0.0 && t.b == 1.0) out.video.rotation = 90;
                else if (t.a == -1.0 && t.b == 0.0) out.video.rotation = 180;
                else if (t.a == 0.0 && t.b == -1.0) out.video.rotation = 270;
                NSArray* formats = track.formatDescriptions;
                if (formats.count > 0) {
                    CMFormatDescriptionRef fmt = (__bridge CMFormatDescriptionRef)formats.firstObject;
                    out.video.color = color_from_format(fmt);
                    const FourCharCode sub = CMFormatDescriptionGetMediaSubType(fmt);
                    std::snprintf(out.video.codec, sizeof(out.video.codec), "%s",
                                  (sub == 'hvc1' || sub == 'hev1') ? "video/hevc" : "video/avc");
                } else {
                    std::snprintf(out.video.codec, sizeof(out.video.codec), "%s", "video/avc");
                }
                std::snprintf(out.video.decoderName, sizeof(out.video.decoderName), "%s", "VideoToolbox");
                out.video.hardwareDecoder = true;
                out.hasVideo = true;
            }
            if (audio.count > 0) {
                AVAssetTrack* track = audio.firstObject;
                NSArray* formats = track.formatDescriptions;
                if (formats.count > 0) {
                    const AudioStreamBasicDescription* asbd =
                        CMAudioFormatDescriptionGetStreamBasicDescription(
                            (__bridge CMAudioFormatDescriptionRef)formats.firstObject);
                    if (asbd) {
                        out.audioSampleRate = (u32)llround(asbd->mSampleRate);
                        out.audioChannels = asbd->mChannelsPerFrame;
                    }
                }
                out.audioDurationUs = us_of(asset.duration);
                out.hasAudio = out.audioSampleRate > 0;
            }
            return out.hasVideo || out.hasAudio;
        }
    }

    [[nodiscard]] std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset,
                                                                 MediaPriority priority) override {
        @autoreleasepool {
            if (asset.sourcePath.empty()) return nullptr;
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:asset.sourcePath.c_str()]];
            if (!url) return nullptr;
            AVURLAsset* av = [AVURLAsset URLAssetWithURL:url options:nil];
            NSArray<AVAssetTrack*>* tracks = [av tracksWithMediaType:AVMediaTypeVideo];
            if (tracks.count == 0) return nullptr;
            return std::unique_ptr<VideoDecoderBackend>(
                new (std::nothrow) AVFoundationVideoDecoder(av, tracks.firstObject, priority != MediaPriority::Thumbnail && zeroCopy_.load()));
        }
    }

    [[nodiscard]] std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* sourcePath) override {
        @autoreleasepool {
            if (!sourcePath || !*sourcePath) return nullptr;
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:sourcePath]];
            if (!url) return nullptr;
            AVURLAsset* av = [AVURLAsset URLAssetWithURL:url options:nil];
            NSArray<AVAssetTrack*>* tracks = [av tracksWithMediaType:AVMediaTypeAudio];
            if (tracks.count == 0) return nullptr;
            return std::unique_ptr<audio::AudioDecoderBackend>(
                new (std::nothrow) AVFoundationAudioDecoder(av, tracks.firstObject));
        }
    }

private:
    std::atomic<bool> zeroCopy_{true};
};

// =============================================================================
// 4. O export: VTCompressionSession + AVAssetWriter.
//
//  O encoder é dividido em dois: um host ObjC (dono dos objetos Apple, sob ARC)
//  e a classe C++ que implementa `ExportSink`. O callback do VideoToolbox é uma
//  função C com refcon — ele entra no host, não na classe.
// =============================================================================
} // namespace aurea::ios

#if DEBUG
// Compare production CPU/IOSurface outputs against an independent presentation-
// order reader. No renderer substitution: this is a decoder contract regression.
NSDictionary<NSString*, id>* AureaVerifyVideoDecoder(NSString* path, NSUInteger expectedFrames) {
    @autoreleasepool {
        auto failure = [](NSString* message) -> NSDictionary<NSString*, id>* {
            return @{ @"passed": @NO, @"error": message };
        };
        struct Expected { i64 pts; u64 hash; };
        std::vector<Expected> expected;
        auto hashPixels = [](const u8* bytes, u32 width, u32 height, u32 stride, bool bgra) {
            u64 hash = 14695981039346656037ULL;
            for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
                const u8* p = bytes + static_cast<usize>(y) * stride + x * 4;
                for (u32 c : {bgra ? 2u : 0u, 1u, bgra ? 0u : 2u, 3u}) {
                    hash ^= p[c]; hash *= 1099511628211ULL;
                }
            }
            return hash;
        };
        auto hashBuffer = [&](CVPixelBufferRef pixel, u64& hash) {
            if (!pixel || CVPixelBufferGetPixelFormatType(pixel) != kCVPixelFormatType_32BGRA ||
                CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return false;
            hash = hashPixels(static_cast<const u8*>(CVPixelBufferGetBaseAddress(pixel)),
                static_cast<u32>(CVPixelBufferGetWidth(pixel)), static_cast<u32>(CVPixelBufferGetHeight(pixel)),
                static_cast<u32>(CVPixelBufferGetBytesPerRow(pixel)), true);
            CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
            return true;
        };
        AVURLAsset* movie = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path] options:nil];
        AVAssetTrack* track = [movie tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!track) return failure(@"Reference video track missing");
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:movie error:nullptr];
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track
            outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)}];
        if (![reader canAddOutput:output]) return failure(@"Reference output rejected");
        [reader addOutput:output];
        if (![reader startReading]) return failure(@"Reference reader failed");
        while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
            const CMTime time = CMSampleBufferGetPresentationTimeStamp(sample);
            u64 hash = 0;
            const bool valid = CMTIME_IS_NUMERIC(time) && hashBuffer(CMSampleBufferGetImageBuffer(sample), hash);
            if (valid) expected.push_back({static_cast<i64>(llround(CMTimeGetSeconds(time) * 1e6)), hash});
            CFRelease(sample);
            if (!valid) return failure(@"Invalid reference pixels/timestamp");
        }
        if (reader.status != AVAssetReaderStatusCompleted || expected.size() != expectedFrames || expected.empty())
            return failure(@"Reference frame count or EOS differs");
        for (usize i = 1; i < expected.size(); ++i)
            if (expected[i].pts <= expected[i-1].pts) return failure(@"Reference PTS are not increasing");
        aurea::ios::MediaFactoryControl* control = nullptr;
        auto factory = aurea::ios::make_video_factory(&control);
        Asset asset; asset.kind = AssetKind::Video; asset.sourcePath = path.UTF8String;
        u32 checks = 0;
        for (bool zeroCopy : {false, true}) {
            control->set_zero_copy(zeroCopy);
            auto decoder = factory->open_video(asset, MediaPriority::Preview);
            if (!decoder || !decoder->seek_to_keyframe(0).ok()) return failure(@"Production decoder failed to open");
            auto matches = [&](const FrameRef& frame, const Expected& reference) {
                if (!frame || frame->ptsUs != reference.pts || frame->format != PixelFormat::RGBA8) return false;
                u64 hash = 0;
                if (zeroCopy) {
                    if (!hashBuffer(static_cast<CVPixelBufferRef>(frame->hardwareBuffer), hash)) return false;
                } else {
                    if (frame->hardwareBuffer || !frame->planes[0]) return false;
                    hash = hashPixels(frame->planes[0], frame->width, frame->height, frame->strides[0], false);
                }
                ++checks;
                return hash == reference.hash;
            };
            FrameRef retained;
            for (const auto& reference : expected) {
                FrameRef frame; i64 pts = -1; bool eos = false;
                if (!decoder->next_frame(0, frame, pts, eos).ok() || eos || pts != reference.pts || !matches(frame, reference))
                    return failure(@"Sequential decode pixels/PTS differ");
                if (!retained) retained = frame;
            }
            FrameRef frame; i64 pts = -1; bool eos = false;
            if (!decoder->next_frame(0, frame, pts, eos).ok() || !eos || frame)
                return failure(@"Decoder did not drain to EOS");
#if defined(AUREA_GPU_METAL)
            if (zeroCopy) {
                mtl::Backend gpu;
                BackendConfig config;
                config.enableValidation = false;
                if (!gpu.initialize(config).ok()) return failure(@"Metal memory regression failed to initialize");
                const auto baseline = gpu.memory_stats();
                TextureDesc desc;
                desc.width = 64; desc.height = 32; desc.mipLevels = 7;
                auto owned = gpu.create_texture(desc);
                if (!owned.ok()) return failure(@"Metal mip texture allocation failed");
                const auto allocated = gpu.memory_stats();
                if (allocated.usedBytes != baseline.usedBytes + desc.estimated_bytes())
                    return failure(@"Metal mip texture memory is undercounted");
                ExternalImageDesc image;
                image.nativeHandle = retained->hardwareBuffer;
                image.width = retained->width; image.height = retained->height;
                auto imported = gpu.import_external_image(image);
                if (!imported.ok()) return failure(@"Metal IOSurface import failed");
                gpu.release_external_image(imported->texture);
                gpu.wait_idle();
                const auto released = gpu.memory_stats();
                if (released.usedBytes != allocated.usedBytes || released.textureCount != allocated.textureCount)
                    return failure(@"Metal IOSurface release changed owned texture accounting");
                gpu.destroy_texture(*owned);
                gpu.wait_idle();
                if (gpu.memory_stats().usedBytes != baseline.usedBytes)
                    return failure(@"Metal texture bytes remain after release");
                checks += 3;
            }
#endif
            for (usize index : {expected.size()-1, usize{0}, expected.size()/2}) {
                const i64 end = index + 1 < expected.size() ? expected[index + 1].pts : decoder->info().durationUs;
                const i64 target = expected[index].pts + std::max<i64>(0, end - expected[index].pts) / 2;
                if (!decoder->seek_to_keyframe(target).ok()) return failure(@"Seek failed");
                // The reader may include preroll, which must be discardable without losing PTS.
                bool found = false;
                for (usize step = 0; step <= expected.size(); ++step) {
                    if (!decoder->next_frame(target, frame, pts, eos).ok() || eos) break;
                    if (frame) { found = matches(frame, expected[index]); break; }
                }
                if (!found) return failure(@"Seek returned wrong pixels/PTS");
            }
            decoder->suspend();
            if (!matches(retained, expected.front())) return failure(@"Retained frame changed after suspend");
            if (!decoder->resume().ok()) return failure(@"Resume failed");
            bool found = false;
            const auto& middle = expected[expected.size()/2];
            for (usize step = 0; step <= expected.size(); ++step) {
                if (!decoder->next_frame(middle.pts, frame, pts, eos).ok() || eos) break;
                if (frame) { found = matches(frame, middle); break; }
            }
            if (!found) return failure(@"Resume returned wrong pixels/PTS");
        }
        return @{ @"passed": @YES, @"frames": @(expected.size()), @"pixelChecks": @(checks),
                  @"cpuAndIOSurface": @YES, @"seekAndResume": @YES };
    }
}
#endif

@interface AureaExportHost : NSObject
- (BOOL)openURL:(NSURL*)url
          video:(const VideoStreamConfig&)video
          audio:(const AudioStreamConfig*)audio
          error:(NSError**)error;
- (BOOL)writeVideoY:(const uint8_t*)y yStride:(uint32_t)yStride
                 uv:(const uint8_t*)uv uvStride:(uint32_t)uvStride
              ptsUs:(int64_t)ptsUs;
- (BOOL)writeAudio:(const int16_t*)pcm frames:(uint32_t)frames ptsUs:(int64_t)ptsUs;
- (BOOL)finish;
- (void)abort;
/// Chamado pela thread do encoder (callback C do VideoToolbox). Declarado aqui
/// porque o `@implementation` nao basta para o emissor da mensagem.
- (void)onEncoded:(CMSampleBufferRef)sample status:(OSStatus)status flags:(VTEncodeInfoFlags)flags;
- (void)drainInput:(BOOL)video;
- (void)finishInputIfDrained:(BOOL)video;
- (void)failLocked:(NSError*)error message:(NSString*)message;
- (BOOL)healthyLocked;
- (BOOL)waitForCapacity:(BOOL)video;
- (BOOL)startSessionIfNeeded:(int64_t)ptsUs;
- (BOOL)startWriterIfPossible;
@property (nonatomic, readonly, copy) NSString* encoderName;
@property (nonatomic, readonly) BOOL hardwareEncoder;
@end

@implementation AureaExportHost {
    AVAssetWriter* _writer;
    AVAssetWriterInput* _videoInput;
    AVAssetWriterInput* _audioInput;
    VTCompressionSessionRef _session;
    CVPixelBufferPoolRef _pool;
    dispatch_queue_t _writerQueue;
    NSCondition* _state;
    NSMutableArray* _videoSamples;
    NSMutableArray* _audioSamples;
    NSError* _firstError;
    BOOL _cancelled;
    BOOL _finishing;
    BOOL _videoFinished;
    BOOL _audioFinished;
    BOOL _startedSession;
    BOOL _sessionTimeSet;
    int64_t _sessionStartUs;
    int64_t _frameDurationUs;
    uint32_t _width;
    uint32_t _height;
    uint32_t _audioRate;
    uint32_t _audioChannels;
    BOOL _hasAudio;
    NSUInteger _pendingVideo;
    NSUInteger _pendingAudio;
    NSUInteger _submittedVideo;
    NSUInteger _appendedVideo;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _state = [[NSCondition alloc] init];
        _writerQueue = dispatch_queue_create("com.aurea.export.writer", DISPATCH_QUEUE_SERIAL);
        _videoSamples = [[NSMutableArray alloc] init];
        _audioSamples = [[NSMutableArray alloc] init];
        _encoderName = @"VideoToolbox";
    }
    return self;
}

- (void)dealloc {
    if (_pool) { CVPixelBufferPoolRelease(_pool); _pool = nullptr; }
    if (_session) { VTCompressionSessionInvalidate(_session); CFRelease(_session); _session = nullptr; }
}

static void aurea_encoder_output(void* refcon, void* sourceRefCon, OSStatus status, VTEncodeInfoFlags infoFlags,
                                 CMSampleBufferRef sample) {
    (void)sourceRefCon;
    AureaExportHost* host = (__bridge AureaExportHost*)refcon;
    [host onEncoded:sample status:status flags:infoFlags];
}

/// Caller holds _state. The first failure is terminal; no dropped frame can
/// later be reported as a successful export.
- (void)failLocked:(NSError*)error message:(NSString*)message {
    if (!_firstError) {
        _firstError = error ?: [NSError errorWithDomain:@"aurea.export" code:2
            userInfo:@{NSLocalizedDescriptionKey: message ?: @"falha na exportacao"}];
        AUREA_LOG_ERROR("export: %s", _firstError.localizedDescription.UTF8String);
    }
    [_state broadcast];
}

- (BOOL)healthyLocked {
    if (_writer.status == AVAssetWriterStatusFailed || _writer.status == AVAssetWriterStatusCancelled)
        [self failLocked:_writer.error message:@"o gravador foi interrompido"];
    return !_cancelled && !_firstError;
}

/// This callback may run synchronously inside EncodeFrame/CompleteFrames.
/// Retain the sample and return immediately: waiting here could prevent the
/// same producer from supplying the audio needed by the writer's interleaver.
- (void)onEncoded:(CMSampleBufferRef)sample status:(OSStatus)status flags:(VTEncodeInfoFlags)flags {
    [_state lock];
    if (_cancelled || _firstError) {
        if (_pendingVideo) --_pendingVideo;
    } else if (status != noErr || !sample || (flags & kVTEncodeInfo_FrameDropped)) {
        NSError* error = status == noErr ? nil : [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
        [self failLocked:error message:@"VideoToolbox perdeu ou recusou um quadro"];
        if (_pendingVideo) --_pendingVideo;
    } else {
        // NSMutableArray retains the CF sample beyond the callback's lifetime.
        [_videoSamples addObject:(__bridge id)sample];
    }
    [_state broadcast];
    [_state unlock];
    dispatch_async(_writerQueue, ^{ [self drainInput:YES]; });
}

/// Only the serial writer queue appends or closes inputs. FIFO order preserves
/// VideoToolbox's compressed decode order, including reordered B frames.
- (void)drainInput:(BOOL)video {
    if (![self startWriterIfPossible]) return;
    AVAssetWriterInput* input = video ? _videoInput : _audioInput;
    while (input.readyForMoreMediaData) {
        [_state lock];
        NSMutableArray* samples = video ? _videoSamples : _audioSamples;
        if (![self healthyLocked]) { [_state unlock]; return; }
        if (samples.count == 0) { [_state unlock]; break; }
        CMSampleBufferRef sample = (__bridge CMSampleBufferRef)samples.firstObject;
        CFRetain(sample);
        [samples removeObjectAtIndex:0];
        [_state unlock];

        const BOOL appended = [input appendSampleBuffer:sample];
        CFRelease(sample);
        [_state lock];
        if (!appended) [self failLocked:_writer.error message:@"o gravador recusou uma amostra"];
        if (video) {
            if (_pendingVideo) --_pendingVideo;
            if (appended) ++_appendedVideo;
        } else if (_pendingAudio) --_pendingAudio;
        [_state broadcast];
        [_state unlock];
        if (!appended) return;
    }
    [_state lock];
    (void)[self healthyLocked];
    [_state unlock];
    [self finishInputIfDrained:video];
}

/// MP4 passthrough requires the encoder's actual source format (including its
/// H.264/HEVC configuration). It is first available in a VT output sample.
/// Runs only on _writerQueue; audio remains retained until video supplies it.
- (BOOL)startWriterIfPossible {
    if (_startedSession) return YES;
    [_state lock];
    if (![self healthyLocked] || _videoSamples.count == 0) { [_state unlock]; return NO; }
    CMSampleBufferRef first = (__bridge CMSampleBufferRef)_videoSamples.firstObject;
    CFRetain(first);
    [_state unlock];
    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(first);
    if (format) {
        _videoInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo
            outputSettings:nil sourceFormatHint:format];
        _videoInput.expectsMediaDataInRealTime = NO;
    }
    CFRelease(first);
    if (!_videoInput || ![_writer canAddInput:_videoInput]) {
        [_state lock];
        [self failLocked:_writer.error message:@"o MP4 recusou o formato do video codificado"];
        [_state unlock];
        return NO;
    }
    [_writer addInput:_videoInput];
    if (![_writer startWriting]) {
        [_state lock];
        [self failLocked:_writer.error message:@"o gravador nao iniciou o MP4"];
        [_state unlock];
        return NO;
    }
    [_writer startSessionAtSourceTime:CMTimeMake(_sessionStartUs, 1'000'000)];
    _startedSession = YES;
    __weak AureaExportHost* weakSelf = self;
    [_videoInput requestMediaDataWhenReadyOnQueue:_writerQueue usingBlock:^{ [weakSelf drainInput:YES]; }];
    if (_audioInput)
        [_audioInput requestMediaDataWhenReadyOnQueue:_writerQueue usingBlock:^{ [weakSelf drainInput:NO]; }];
    return YES;
}

/// Signal each track's EOS as soon as its own queue drains. Waiting until both
/// tracks drain before marking either can stall the writer's A/V interleaver.
- (void)finishInputIfDrained:(BOOL)video {
    [_state lock];
    const BOOL done = _finishing && [self healthyLocked] && (video ? _pendingVideo : _pendingAudio) == 0;
    [_state unlock];
    if (!done) return;
    if (video && !_videoFinished) { [_videoInput markAsFinished]; _videoFinished = YES; }
    if (!video && _audioInput && !_audioFinished) { [_audioInput markAsFinished]; _audioFinished = YES; }
}

/// Backpressure belongs to the producer, never the VT callback or writer queue.
/// Cap retained work and use a bounded wait, so a failed/stalled writer cannot
/// trap the core's encoder thread during cancellation/shutdown.
- (BOOL)waitForCapacity:(BOOL)video {
    const NSUInteger capacity = video ? 64 : 256;
    [_state lock];
    BOOL healthy = [self healthyLocked];
    const BOOL full = (video ? _pendingVideo : _pendingAudio) >= capacity;
    [_state unlock];
    if (!healthy) return NO;
    if (full && _session) {
        // Force delayed encoder frames out before waiting for writer credits.
        // Audio can fill first at low FPS; its writer also needs those frames
        // to start/interleave, and the core uses one producer for both tracks.
        // No lock or writer-queue task is held; callbacks only enqueue.
        const OSStatus status = VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
        if (status != noErr) {
            [_state lock];
            [self failLocked:[NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil]
                     message:@"VideoToolbox nao concluiu os quadros pendentes"];
            [_state unlock];
            return NO;
        }
    }
    const double deadline = NSProcessInfo.processInfo.systemUptime + 5.0;
    [_state lock];
    while ([self healthyLocked] && (video ? _pendingVideo : _pendingAudio) >= capacity) {
        const double remaining = deadline - NSProcessInfo.processInfo.systemUptime;
        if (remaining <= 0) {
            [self failLocked:nil message:@"o gravador nao liberou espaco em 5 segundos"];
            break;
        }
        [_state waitUntilDate:[NSDate dateWithTimeIntervalSinceNow:std::min(remaining, 0.1)]];
    }
    healthy = [self healthyLocked];
    [_state unlock];
    return healthy;
}

- (BOOL)openURL:(NSURL*)url
          video:(const VideoStreamConfig&)video
          audio:(const AudioStreamConfig*)audio
          error:(NSError**)error {
    @autoreleasepool {
        _width = video.width ? video.width : 2;
        _height = video.height ? video.height : 2;
        _frameDurationUs = video.fps > 0.0 ? (int64_t)llround(1'000'000.0 / video.fps) : 33'333;
        _hasAudio = audio != nullptr && audio->sampleRate > 0;
        _audioRate = _hasAudio ? audio->sampleRate : 48000;
        _audioChannels = _hasAudio ? audio->channels : 2;

        // O arquivo não pode existir: o AVAssetWriter recusa abrir por cima.
        [NSFileManager.defaultManager removeItemAtURL:url error:nil];
        _writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:error];
        if (!_writer) return NO;

        // The passthrough video input is added on the first VT output sample,
        // when its mandatory MP4 sourceFormatHint is available.

        if (_hasAudio) {
            // O ÁUDIO o AVAssetWriter codifica (AAC-LC), a partir do PCM que o
            // mixer do núcleo entrega. Um AudioConverter próprio aqui não
            // acrescentaria nada.
            NSDictionary* audioSettings = @{
                AVFormatIDKey: @(kAudioFormatMPEG4AAC),
                AVSampleRateKey: @(audio->sampleRate),
                AVNumberOfChannelsKey: @(audio->channels),
                AVEncoderBitRateKey: @(audio->bitrateBps),
            };
            _audioInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio
                                                        outputSettings:audioSettings];
            _audioInput.expectsMediaDataInRealTime = NO;
            if (![_writer canAddInput:_audioInput]) {
                if (error) *error = [NSError errorWithDomain:@"aurea.export" code:3
                    userInfo:@{NSLocalizedDescriptionKey: @"o gravador recusou a trilha AAC"}];
                return NO;
            }
            [_writer addInput:_audioInput];
        }

        // VTCompressionSession: o encoder de vídeo de verdade.
        NSDictionary* encoderSpec = nil;
        if (@available(iOS 17.4, *)) {
            encoderSpec = @{
                (id)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @YES,
            };
        }
        NSDictionary* compressionProps = @{
            (id)kVTCompressionPropertyKey_RealTime: @NO,
            (id)kVTCompressionPropertyKey_AllowFrameReordering: @YES,
            (id)kVTCompressionPropertyKey_MaxKeyFrameInterval:
                @(video.keyframeIntervalFrames > 0 ? (int)video.keyframeIntervalFrames
                                                   : (int)llround(video.fps * 2.0)),
            (id)kVTCompressionPropertyKey_ExpectedFrameRate: @(video.fps > 0.0 ? video.fps : 30.0),
            (id)kVTCompressionPropertyKey_AverageBitRate: @(video.bitrateBps > 0 ? video.bitrateBps : 12'000'000),
        };
        VTCompressionSessionRef session = nullptr;
        const OSStatus status = VTCompressionSessionCreate(
            kCFAllocatorDefault, (int32_t)_width, (int32_t)_height,
            video.codec == ExportCodec::HEVC ? kCMVideoCodecType_HEVC : kCMVideoCodecType_H264,
            (__bridge CFDictionaryRef)encoderSpec, nullptr,
            nullptr, aurea_encoder_output, (__bridge void*)self, &session);
        if (status != noErr || !session) {
            if (error) *error = [NSError errorWithDomain:@"aurea.export" code:(NSInteger)status userInfo:nil];
            return NO;
        }
        _session = session;
        const OSStatus propertyStatus = VTSessionSetProperties(session, (__bridge CFDictionaryRef)compressionProps);
        if (propertyStatus != noErr) {
            if (error) *error = [NSError errorWithDomain:@"aurea.export" code:(NSInteger)propertyStatus userInfo:nil];
            return NO;
        }
        _hardwareEncoder = NO;
        _encoderName = @"VideoToolbox";
        if (@available(iOS 17.4, *)) {
            CFBooleanRef hardware = nullptr;
            if (VTSessionCopyProperty(session, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
                                      kCFAllocatorDefault, &hardware) == noErr && hardware) {
                _hardwareEncoder = CFBooleanGetValue(hardware) ? YES : NO;
                CFRelease(hardware);
                _encoderName = _hardwareEncoder ? @"VideoToolbox (hardware)" : @"VideoToolbox (software)";
            }
        }
        // Etiqueta de cor no bitstream: sem ela player e galeria chutam, e um
        // vídeo de celular vira BT.601 lavado (é o mesmo cuidado do Android).
        CFStringRef primaries = kCVImageBufferColorPrimaries_ITU_R_709_2;
        CFStringRef transfer = kCVImageBufferTransferFunction_ITU_R_709_2;
        CFStringRef matrix = kCVImageBufferYCbCrMatrix_ITU_R_709_2;
        VTSessionSetProperty(session, kVTCompressionPropertyKey_ColorPrimaries, primaries);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_TransferFunction, transfer);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_YCbCrMatrix, matrix);

        // Pool de buffers NV12 do adaptador: reusa memória entre quadros (sem
        // pool, seriam 30 alocações por segundo de 3 MB cada).
        NSDictionary* poolAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (id)kCVPixelBufferWidthKey: @(_width),
            (id)kCVPixelBufferHeightKey: @(_height),
            (id)kCVPixelBufferMetalCompatibilityKey: @YES,
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        };
        CVPixelBufferPoolRef pool = nullptr;
        if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)poolAttributes,
                                    &pool) == kCVReturnSuccess) {
            _pool = pool;
        }

        return YES;
    }
}

/// Remember the first source PTS before submitting to VT. The writer/session
/// starts later, using the first compressed frame's format description.
- (BOOL)startSessionIfNeeded:(int64_t)ptsUs {
    __block BOOL healthy = NO;
    dispatch_sync(_writerQueue, ^{
        [_state lock];
        healthy = [self healthyLocked];
        [_state unlock];
        if (healthy && !_sessionTimeSet) {
            _sessionStartUs = ptsUs;
            _sessionTimeSet = YES;
        }
    });
    return healthy;
}

- (BOOL)writeVideoY:(const uint8_t*)y yStride:(uint32_t)yStride
                 uv:(const uint8_t*)uv uvStride:(uint32_t)uvStride
              ptsUs:(int64_t)ptsUs {
    @autoreleasepool {
        if (!_session || !_pool) return NO;
        if (![self startSessionIfNeeded:ptsUs] || ![self waitForCapacity:YES]) return NO;
        CVPixelBufferRef pixel = nullptr;
        if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, _pool, &pixel) != kCVReturnSuccess
            || !pixel) {
            return NO;
        }
        CVPixelBufferLockBaseAddress(pixel, 0);
        auto* dstY = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel, 0);
        auto* dstUV = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel, 1);
        const size_t dstYStride = CVPixelBufferGetBytesPerRowOfPlane(pixel, 0);
        const size_t dstUVStride = CVPixelBufferGetBytesPerRowOfPlane(pixel, 1);
        const size_t dstUVHeight = CVPixelBufferGetHeightOfPlane(pixel, 1);
        // O stride do destino é do POOL (alinhado pelo driver), raramente igual
        // ao do motor: copiar linha a linha com o mínimo dos dois é o que evita
        // a faixa torta no arquivo.
        const size_t yRows = CVPixelBufferGetHeightOfPlane(pixel, 0);
        const size_t yCopy = yStride < dstYStride ? yStride : dstYStride;
        for (size_t r = 0; r < yRows; ++r) {
            std::memcpy(dstY + r * dstYStride, y + r * yStride, yCopy);
        }
        const size_t uvCopy = uvStride < dstUVStride ? uvStride : dstUVStride;
        for (size_t r = 0; r < dstUVHeight; ++r) {
            std::memcpy(dstUV + r * dstUVStride, uv + r * uvStride, uvCopy);
        }
        CVPixelBufferUnlockBaseAddress(pixel, 0);

        const CMTime pts = CMTimeMake(ptsUs, 1'000'000);
        const CMTime duration = CMTimeMake(_frameDurationUs, 1'000'000);
        [_state lock];
        if (![self healthyLocked]) { [_state unlock]; CVPixelBufferRelease(pixel); return NO; }
        ++_pendingVideo;   // o `finish` espera estes sairem antes de fechar
        ++_submittedVideo;
        [_state unlock];
        VTEncodeInfoFlags flags = 0;
        const OSStatus status = VTCompressionSessionEncodeFrame(_session, pixel, pts, duration, nullptr,
                                                                nullptr, &flags);
        [_state lock];
        if (status != noErr || (flags & kVTEncodeInfo_FrameDropped)) {
            NSError* error = status == noErr ? nil : [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
            [self failLocked:error message:@"VideoToolbox recusou ou perdeu um quadro"];
            if (_pendingVideo > 0) --_pendingVideo;
        }
        const BOOL healthy = [self healthyLocked];
        [_state unlock];
        CVPixelBufferRelease(pixel);
        return healthy;
    }
}

- (BOOL)writeAudio:(const int16_t*)pcm frames:(uint32_t)frames ptsUs:(int64_t)ptsUs {
    @autoreleasepool {
        if (!_hasAudio || !_audioInput) return YES;   // vídeo sem som: não é erro
        if (![self startSessionIfNeeded:ptsUs] || ![self waitForCapacity:NO]) return NO;

        // O formato e o que o sink declarou no `open` (o motor sempre manda
        // 48 kHz estereo, mas o numero vem do contrato, nao de um literal).
        const u32 channels = _audioChannels ? _audioChannels : 2;
        const u32 rate = _audioRate ? _audioRate : 48000;
        AudioStreamBasicDescription asbd{};
        asbd.mSampleRate = rate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        asbd.mBytesPerPacket = channels * sizeof(int16_t);
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerFrame = channels * sizeof(int16_t);
        asbd.mChannelsPerFrame = channels;
        asbd.mBitsPerChannel = 16;
        CMAudioFormatDescriptionRef format = nullptr;
        if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr,
                                           &format) != noErr) {
            return NO;
        }
        const size_t bytes = (size_t)frames * asbd.mBytesPerFrame;
        CMBlockBufferRef block = nullptr;
        if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault,
                                               nullptr, 0, bytes, 0, &block) != kCMBlockBufferNoErr) {
            CFRelease(format);
            return NO;
        }
        if (CMBlockBufferReplaceDataBytes(pcm, block, 0, bytes) != kCMBlockBufferNoErr) {
            CFRelease(block);
            CFRelease(format);
            return NO;
        }
        CMSampleTimingInfo timing{};
        timing.presentationTimeStamp = CMTimeMake(ptsUs, 1'000'000);
        timing.duration = CMTimeMake(1, (int32_t)rate);
        timing.decodeTimeStamp = kCMTimeInvalid;
        CMSampleBufferRef sample = nullptr;
        // One PCM sample is one interleaved frame, not the entire audio chunk.
        const size_t sampleBytes = asbd.mBytesPerFrame;
        const OSStatus status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, format, frames, 1,
                                                          &timing, 1, &sampleBytes, &sample);
        CFRelease(block);
        CFRelease(format);
        if (status != noErr || !sample) return NO;
        [_state lock];
        const BOOL healthy = [self healthyLocked];
        if (healthy) {
            [_audioSamples addObject:(__bridge id)sample];
            ++_pendingAudio;
        }
        [_state unlock];
        CFRelease(sample);
        if (healthy) dispatch_async(_writerQueue, ^{ [self drainInput:NO]; });
        return healthy;
    }
}

- (BOOL)finish {
    @autoreleasepool {
        if (_session) {
            const OSStatus status = VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
            if (status != noErr) {
                [_state lock];
                [self failLocked:[NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil]
                         message:@"VideoToolbox nao finalizou os quadros"];
                [_state unlock];
            }
        }
        [_state lock];
        _finishing = YES;
        [_state unlock];
        dispatch_async(_writerQueue, ^{ [self drainInput:YES]; [self drainInput:NO]; });
        const double deadline = NSProcessInfo.processInfo.systemUptime + 30.0;
        [_state lock];
        while ([self healthyLocked] && (_pendingVideo || _pendingAudio)) {
            const double remaining = deadline - NSProcessInfo.processInfo.systemUptime;
            if (remaining <= 0) {
                [self failLocked:nil message:@"o gravador nao recebeu todas as amostras em 30 segundos"];
                break;
            }
            [_state waitUntilDate:[NSDate dateWithTimeIntervalSinceNow:std::min(remaining, 0.1)]];
        }
        if (!_firstError && _submittedVideo != _appendedVideo)
            [self failLocked:nil message:@"o numero de quadros gravados difere dos quadros enviados"];
        const BOOL healthy = [self healthyLocked];
        [_state unlock];
        if (!healthy) { [self abort]; return NO; }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        dispatch_sync(_writerQueue, ^{
            if (!_startedSession) {
                [_state lock];
                [self failLocked:nil message:@"nenhum quadro iniciou a sessao MP4"];
                [_state unlock];
                dispatch_semaphore_signal(done);
                return;
            }
            [self finishInputIfDrained:YES];
            [self finishInputIfDrained:NO];
            [_writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
        });
        const long waited = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 60ull * NSEC_PER_SEC));
        if (waited != 0 || _writer.status != AVAssetWriterStatusCompleted) {
            [_state lock];
            [self failLocked:_writer.error message:waited ? @"tempo esgotado ao finalizar o MP4" : @"o MP4 nao foi finalizado"];
            [_state unlock];
            [self abort];
            return NO;
        }
        return YES;
    }
}

- (void)abort {
    @autoreleasepool {
        [_state lock];
        _cancelled = YES;
        [_state broadcast];
        [_state unlock];
        // Complete/invalidate outside every lock and outside the writer queue.
        // Callbacks see cancellation, release their credit, and never wait.
        if (_session) {
            (void)VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
            VTCompressionSessionInvalidate(_session);
            CFRelease(_session);
            _session = nullptr;
        }
        dispatch_sync(_writerQueue, ^{
            if (_writer && _writer.status == AVAssetWriterStatusWriting) [_writer cancelWriting];
            [_state lock];
            [_videoSamples removeAllObjects];
            [_audioSamples removeAllObjects];
            _pendingVideo = _pendingAudio = 0;
            [_state broadcast];
            [_state unlock];
        });
        NSURL* url = _writer.outputURL;
        if (url) [NSFileManager.defaultManager removeItemAtURL:url error:nil];
    }
}

@end

namespace aurea::ios {
namespace {

class VideoToolboxExportSink final : public ExportSink {
public:
    VideoToolboxExportSink() = default;
    ~VideoToolboxExportSink() override { abort(); }

    Status open(const char* outputPath, const VideoStreamConfig& video,
                const AudioStreamConfig* audio) noexcept override {
        @autoreleasepool {
            if (!outputPath || !*outputPath) return Status{Errc::InvalidArgument, "sem caminho de saida"};
            if (_host) return Status{Errc::InvalidState, "export ja aberto"};
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:outputPath]];
            if (!url) return Status{Errc::InvalidArgument, "caminho invalido"};
            AureaExportHost* host = [[AureaExportHost alloc] init];
            NSError* error = nil;
            if (![host openURL:url video:video audio:audio error:&error]) {
                AUREA_LOG_ERROR("export: encoder recusado (%s)",
                                error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String : "?");
                return Status{Errc::EncodeFailed, "o encoder recusou o arquivo"};
            }
            _host = (__bridge_retained void*)host;
            return OkStatus;
        }
    }

    Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs) noexcept override {
        @autoreleasepool {
            AureaExportHost* host = host_ref();
            if (!host || !y || !uv) return Status{Errc::InvalidState, "export nao aberto"};
            if (![host writeVideoY:y yStride:yStride uv:uv uvStride:uvStride ptsUs:ptsUs]) {
                return Status{Errc::EncodeFailed, "o encoder recusou o quadro"};
            }
            return OkStatus;
        }
    }

    Status write_audio(const i16* interleaved, u32 frames, i64 ptsUs) noexcept override {
        @autoreleasepool {
            AureaExportHost* host = host_ref();
            if (!host) return Status{Errc::InvalidState, "export nao aberto"};
            if (![host writeAudio:(const int16_t*)interleaved frames:frames ptsUs:ptsUs]) {
                return Status{Errc::EncodeFailed, "o encoder recusou o audio"};
            }
            return OkStatus;
        }
    }

    Status finish() noexcept override {
        @autoreleasepool {
            AureaExportHost* host = host_ref();
            if (!host) return Status{Errc::InvalidState, "export nao aberto"};
            const BOOL ok = [host finish];
            // Solta o host (o ARC libera writer, inputs e sessão).
            (void)(__bridge_transfer AureaExportHost*)release_host();
            return ok ? OkStatus : Status{Errc::EncodeFailed, "o arquivo nao foi finalizado"};
        }
    }

    void abort() noexcept override {
        @autoreleasepool {
            AureaExportHost* host = host_ref();
            if (!host) return;
            [host abort];
            (void)(__bridge_transfer AureaExportHost*)release_host();
        }
    }

    EncoderInfo encoder_info() const noexcept override {
        EncoderInfo info;
        AureaExportHost* host = host_ref();
        if (!host) return info;
        NSString* name = host.encoderName;
        if (name) std::snprintf(info.name, sizeof(info.name), "%s", name.UTF8String);
        info.acceleration = host.hardwareEncoder ? Acceleration::Hardware : Acceleration::Software;
        return info;
    }

private:
    AureaExportHost* host_ref() const noexcept { return (__bridge AureaExportHost*)_host; }
    void* release_host() noexcept {
        void* raw = _host;
        _host = nullptr;
        return raw;
    }

    void* _host = nullptr;
};

} // namespace

// =============================================================================
// Fábricas expostas ao resto da ponte (AureaBridge.h).
// =============================================================================
std::unique_ptr<VideoSourceFactory> make_video_factory(MediaFactoryControl** control) {
    auto factory = std::unique_ptr<VideoToolboxFactory>(new (std::nothrow) VideoToolboxFactory());
    if (control) *control = factory.get();
    return factory;
}

std::unique_ptr<ExportSink> make_export_sink(void* user) {
    (void)user;
    return std::unique_ptr<ExportSink>(new (std::nothrow) VideoToolboxExportSink());
}

// =============================================================================
// A sonda do aparelho.
//
// O que SÓ a plataforma sabe. O resto (classe do aparelho, orçamentos, teto de
// export, quanto de preview) é decisão do núcleo — não se decide nada aqui.
// =============================================================================
void fill_platform_info(PlatformInfo& out) {
    @autoreleasepool {
        // Núcleos: hw.ncpu dá o total; os níveis de desempenho separam grandes
        // de pequenos (A11 em diante). Onde não existem, ficam 0 e o motor usa
        // o total.
        int ncpu = 0;
        size_t len = sizeof(ncpu);
        if (sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0) == 0 && ncpu > 0) {
            out.totalCores = (u32)ncpu;
        }
        int perf = 0;
        len = sizeof(perf);
        if (sysctlbyname("hw.perflevel0.physicalcpu", &perf, &len, nullptr, 0) == 0 && perf > 0) {
            out.performanceCores = (u32)perf;
        }
        int eff = 0;
        len = sizeof(eff);
        if (sysctlbyname("hw.perflevel1.physicalcpu", &eff, &len, nullptr, 0) == 0 && eff > 0) {
            out.efficiencyCores = (u32)eff;
        }
        // Frequência máxima: NÃO existe consulta pública no iOS. Fica 0 =
        // "não medida" — nunca um número inventado para a tela ficar bonita.
        out.maxFrequencyKhz = 0;

        u64 memsize = 0;
        len = sizeof(memsize);
        if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) out.totalMemoryBytes = memsize;
        // `os_proc_available_memory` é o que o app pode usar AGORA (o limite do
        // processo, não a RAM livre do aparelho) — é o número certo para o
        // orçamento, e é o análogo do `availMem` do Android.
        const size_t available = os_proc_available_memory();
        out.availableMemoryBytes = available > 0 ? (u64)available : 0;

        // RefreshRate: quem sabe é a UIScreen, e a ponte ObjC passa esse valor
        // pelo `initWithRefreshRate:` do motor. Aqui fica o padrão conservador.
        if (out.displayRefreshRate == 0) out.displayRefreshRate = 60;

        // --- Codecs ---
        // DECODERS: `VTIsHardwareDecodeSupported` responde pelo chip. H.264 e
        // HEVC são os formatos que o iOS decodifica por hardware desde o A7;
        // AV1/VP9 ficam FORA da tabela de propósito (num aparelho recente eles
        // existem, em outros não, e anunciar o que não existe é pior do que não
        // anunciar).
        auto add_decoder = [&out](u32 tag, const char* name, u8 bitDepth, bool supported) {
            if (out.decoderCount >= 16) return;
            CodecCapability cap;
            cap.supported = supported;
            cap.hardwareAccelerated = supported;
            // 4096×2304 é o teto documentado do decodificador de hardware do
            // iOS para H.264 e HEVC. Não é um "até onde eu acho que vai": é o
            // limite que a Apple publica.
            cap.maxWidth = supported ? 4096u : 0u;
            cap.maxHeight = supported ? 2304u : 0u;
            cap.maxBitDepth = bitDepth;
            // Instâncias simultâneas: o VideoToolbox não publica o número. 0 =
            // desconhecido, e o motor fica no conservador (um decode paralelo).
            cap.concurrentInstances = 0;
            cap.name = name;
            const u32 index = out.decoderCount;
            out.decoders[index] = cap;
            out.set_decoder_tag(tag, index);
            ++out.decoderCount;
        };
        const bool h264 = VTIsHardwareDecodeSupported(kCMVideoCodecType_H264);
        const bool hevc = VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC);
        add_decoder(0x61766331u, h264 ? "VideoToolbox (hardware)" : "VideoToolbox (software)", 8, h264);
        add_decoder(0x68766331u, hevc ? "VideoToolbox (hardware)" : "VideoToolbox (software)", 10, hevc);

        // ENCODERS: `VTCopyVideoEncoderList` é a lista do sistema, com o flag de
        // hardware por encoder. (Obsoleto desde o iOS 17 em favor de
        // VTCopySupportedPropertyDictionaryForEncoder — que existe justamente
        // para PERGUNTAR por uma configuração, não para enumerar; a lista
        // continua sendo a resposta para "o que este aparelho tem".)
        CFArrayRef list = nullptr;
        if (VTCopyVideoEncoderList(nullptr, &list) == noErr && list) {
            const CFIndex count = CFArrayGetCount(list);
            for (CFIndex i = 0; i < count && out.encoderCount < 8; ++i) {
                CFDictionaryRef entry = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
                if (!entry) continue;
                CFNumberRef codecNumber =
                    (CFNumberRef)CFDictionaryGetValue(entry, kVTVideoEncoderList_CodecType);
                int codec = 0;
                if (!codecNumber || !CFNumberGetValue(codecNumber, kCFNumberIntType, &codec)) continue;
                const u32 tag = codec == (int)kCMVideoCodecType_H264 ? 0x61766331u
                              : codec == (int)kCMVideoCodecType_HEVC ? 0x68766331u
                                                                     : 0u;
                if (tag == 0) continue;
                CFBooleanRef hw = (CFBooleanRef)CFDictionaryGetValue(
                    entry, kVTVideoEncoderList_IsHardwareAccelerated);
                const bool hardware = hw ? CFBooleanGetValue(hw) : false;
                if (out.encoderIndexForTag[PlatformInfo::tag_slot_of(tag)] != kInvalidIndex) {
                    continue;   // já achamos o encoder deste codec
                }
                CodecCapability cap;
                cap.supported = true;
                cap.hardwareAccelerated = hardware;
                cap.maxWidth = 3840u;
                cap.maxHeight = 2160u;
                cap.maxBitDepth = tag == 0x68766331u ? 10 : 8;
                cap.concurrentInstances = 0;
                cap.name = hardware ? "VideoToolbox (hardware)" : "VideoToolbox (software)";
                const u32 index = out.encoderCount;
                out.encoders[index] = cap;
                out.set_encoder_tag(tag, index);
                ++out.encoderCount;
            }
            CFRelease(list);
        }
    }
}

// =============================================================================
// A imagem do projeto (ImageIO). Mesmo contrato do `decodeImage` do Android:
// RGBA8 sRGB com alfa RETO.
// =============================================================================
bool ios_load_image(const char* sourcePath, ImagePixels& out, void* ctx) {
    (void)ctx;
    @autoreleasepool {
        if (!sourcePath || !*sourcePath) return false;
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:sourcePath]];
        if (!url) return false;
        CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!src) return false;
        CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
        CFRelease(src);
        if (!image) return false;

        const size_t width = CGImageGetWidth(image);
        const size_t height = CGImageGetHeight(image);
        if (width == 0 || height == 0 || width > 16384 || height > 16384) {
            CGImageRelease(image);
            return false;
        }
        // Desenha num contexto RGBA8 PREMULTIPLICADO e desmultiplica: o motor
        // quer alfa RETO, e o CoreGraphics só entrega premultiplicado quando a
        // imagem tem alfa. Sem desmultiplicar, uma sombra semitransparente
        // chegaria escura.
        std::vector<u8> premul(width * height * 4);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(
            premul.data(), width, height, 8, width * 4, space,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (!context) {
            CGImageRelease(image);
            return false;
        }
        CGContextSetBlendMode(context, kCGBlendModeCopy);
        CGContextDrawImage(context, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height), image);
        CGContextRelease(context);
        CGImageRelease(image);

        out.width = (u32)width;
        out.height = (u32)height;
        out.rgba.resize(width * height * 4);
        for (size_t i = 0; i < width * height; ++i) {
            const u8 a = premul[i * 4 + 3];
            if (a == 0 || a == 255) {
                for (int c = 0; c < 4; ++c) out.rgba[i * 4 + c] = premul[i * 4 + c];
            } else {
                // Desmultiplica com arredondamento: (v * 255 + a/2) / a.
                for (int c = 0; c < 3; ++c) {
                    const u32 v = premul[i * 4 + c];
                    out.rgba[i * 4 + c] = (u8)std::min<u32>(255u, (v * 255u + a / 2u) / a);
                }
                out.rgba[i * 4 + 3] = a;
            }
        }
        return true;
    }
}

const char* ios_default_font_path() {
    // Identical Roboto file to the Android reference emulator (Apache-2.0).
    // Keep the storage alive: the core receives a const char* during startup.
    static const std::string sharedFont = [] {
        NSString* path = [[NSBundle mainBundle] pathForResource:@"Roboto-Regular" ofType:@"ttf" inDirectory:@"Fonts"];
        return path ? std::string(path.UTF8String) : std::string();
    }();
    if (!sharedFont.empty() && access(sharedFont.c_str(), R_OK) == 0) return sharedFont.c_str();
    // A fonte do sistema do iOS mora em /System/Library/Fonts. O FontManager do
    // núcleo já varre essa pasta; passar o caminho explícito quando ele existe
    // evita a varredura inteira na primeira abertura.
    static const char* kCandidates[] = {
        "/System/Library/Fonts/Core/SFUI.ttf",
        "/System/Library/Fonts/SFUI.ttf",
        "/System/Library/Fonts/CoreUI/SFUI.ttf",
    };
    for (const char* path : kCandidates) {
        if (access(path, R_OK) == 0) return path;
    }
    return "";   // o motor procura sozinho (FontManager varre /System/Library/Fonts)
}

} // namespace aurea::ios
