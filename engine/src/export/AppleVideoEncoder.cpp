// =============================================================================
//  Aurea / export / AppleVideoEncoder.cpp
//
//  iOS (18/11/2026): o degrau sem cópia do export — o mesmo contrato do
//  encoder do aparelho do Android (android_video::Encoder, export/
//  AndroidVideoEncoder.hpp), com VTCompressionSession.
//
//  Antes: Metal → blit para buffer compartilhado → cópia/sws_scale na CPU →
//  AVFrame → o h264_videotoolbox do FFmpeg copiava de novo para o pool dele.
//  Agora: o passe `export-rgb` do motor escreve o quadro num CVPixelBuffer
//  IOSurface (Backend::create_shared_render_target do Metal) e ESTE encoder
//  entrega esse buffer ao VideoToolbox, que converte R'G'B' → Y'CbCr BT.709 e
//  codifica no hardware. O FFmpeg só multiplexa os pacotes (como no Android).
//
//  Posse do buffer: o motor reusa o slot (e o CVPixelBuffer) assim que
//  write_video volta. Por isso cada send() termina com
//  VTCompressionSessionCompleteFrames até o PTS do quadro: quando ele volta, o
//  VideoToolbox já devolveu o buffer e o pacote está na fila. O render dos
//  quadros seguintes continua em paralelo nos outros slots.
//
//  Sem B-quadros (AllowFrameReordering = false): o sink exige um pacote por
//  quadro, na ordem. Perfil de qualidade (H.264 High / HEVC Main, nível
//  automático); recusado = o perfil padrão do encoder.
//
//  Só Apple. Nos outros sistemas este arquivo não compila nada.
// =============================================================================
#include "aurea/export/AndroidVideoEncoder.hpp"

#if defined(__APPLE__)

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>

namespace aurea::android_video {

namespace {

constexpr u8 kStartCode[4] = {0, 0, 0, 1};

void append_start_code(std::vector<u8>& out, const u8* nal, usize size) {
    out.insert(out.end(), kStartCode, kStartCode + 4);
    out.insert(out.end(), nal, nal + size);
}

/// Conjuntos de parâmetros da descrição do formato em Annex-B (SPS/PPS ou VPS/SPS/PPS).
bool parameter_sets(CMFormatDescriptionRef format, bool hevc, std::vector<u8>& out, int& nalLength) {
    out.clear();
    nalLength = 4;
    size_t count = 0;
    int header = 4;
    OSStatus status = hevc
        ? CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &header)
        : CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &header);
    if (status != noErr || count == 0) return false;
    nalLength = header;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* set = nullptr;
        size_t size = 0;
        status = hevc
            ? CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(format, i, &set, &size, nullptr, nullptr)
            : CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &set, &size, nullptr, nullptr);
        if (status != noErr || !set || !size) return false;
        append_start_code(out, set, size);
    }
    return true;
}

void set_number(VTCompressionSessionRef session, CFStringRef key, i64 value, const char* what) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value);
    if (!number) return;
    const OSStatus status = VTSessionSetProperty(session, key, number);
    CFRelease(number);
    if (status != noErr) AUREA_LOG_WARN("export: VideoToolbox recusou %s (%d)", what, static_cast<int>(status));
}

void set_double(VTCompressionSessionRef session, CFStringRef key, f64 value, const char* what) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &value);
    if (!number) return;
    const OSStatus status = VTSessionSetProperty(session, key, number);
    CFRelease(number);
    if (status != noErr) AUREA_LOG_WARN("export: VideoToolbox recusou %s (%d)", what, static_cast<int>(status));
}

} // namespace

struct Encoder::Impl {
    VTCompressionSessionRef session = nullptr;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Packet> packets;
    std::vector<u8> config;
    Status failure = OkStatus;
    bool hevc = false, profileUsed = false, cbr = false;
    bool eosSent = false, eosDone = false;
    f64 fps = 30.0;
    void* direct = nullptr;   ///< CVPixelBufferRef do PRÓXIMO send (o motor é o dono)
    std::atomic<u64>* heartbeat = nullptr;
    char description[160]{};

    void beat() noexcept { if (heartbeat) heartbeat->store(monotonic_ns(), std::memory_order_release); }

    // Thread do VideoToolbox. Converte o pacote AVCC/HVCC (comprimento + NAL)
    // em Annex-B e o põe na fila; o 1o traz os conjuntos de parâmetros.
    static void output(void* refcon, void*, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
        auto* self = static_cast<Impl*>(refcon);
        if (!self) return;
        std::lock_guard<std::mutex> lock(self->mutex);
        if (status != noErr) {
            if (self->failure.ok()) {
                self->failure = Status{Errc::EncodeFailed, "VideoToolbox falhou ao codificar o quadro"};
                AUREA_LOG_WARN("export: VideoToolbox falhou ao codificar (%d)", static_cast<int>(status));
            }
            self->ready.notify_all();
            return;
        }
        // Export requires every submitted frame. Return the failure in this
        // write so the sink can retry instead of waiting for a packet that
        // VideoToolbox has already said it will not produce.
        if ((flags & kVTEncodeInfo_FrameDropped) || !sample || !CMSampleBufferDataIsReady(sample)) {
            if (self->failure.ok())
                self->failure = Status{Errc::EncodeFailed, "VideoToolbox nao entregou o quadro codificado"};
            self->ready.notify_all();
            return;
        }
        {
            int nalLength = 4;
            CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
            if (self->config.empty() && format) {
                std::vector<u8> sets;
                if (parameter_sets(format, self->hevc, sets, nalLength)) self->config = std::move(sets);
            } else if (format) {
                size_t count = 0;
                int header = 4;
                if ((self->hevc ? CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &header)
                                : CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &header))
                    == noErr) nalLength = header;
            }
            if (nalLength < 1 || nalLength > 4) {
                self->failure = Status{Errc::EncodeFailed, "VideoToolbox com comprimento de NAL invalido"};
                self->ready.notify_all();
                return;
            }
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
            const size_t total = block ? CMBlockBufferGetDataLength(block) : 0;
            if (!total) { self->ready.notify_all(); return; }
            std::vector<u8> avcc(total);
            if (CMBlockBufferCopyDataBytes(block, 0, total, avcc.data()) != kCMBlockBufferNoErr) {
                self->failure = Status{Errc::EncodeFailed, "VideoToolbox sem os bytes do pacote"};
                self->ready.notify_all();
                return;
            }
            Packet packet;
            packet.data.reserve(total + 16);
            for (size_t at = 0; at + static_cast<size_t>(nalLength) <= total;) {
                size_t size = 0;
                for (int b = 0; b < nalLength; ++b) size = (size << 8) | avcc[at + static_cast<size_t>(b)];
                at += static_cast<size_t>(nalLength);
                if (size == 0 || size > total - at) break;
                append_start_code(packet.data, avcc.data() + at, size);
                at += size;
            }
            if (packet.data.empty()) { self->ready.notify_all(); return; }
            // Sem a marca "não é sync" = quadro-chave.
            bool key = true;
            if (CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
                attachments && CFArrayGetCount(attachments) > 0) {
                auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
                const void* notSync = dict ? CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync) : nullptr;
                key = !(notSync && CFGetTypeID(notSync) == CFBooleanGetTypeID() &&
                        CFBooleanGetValue(static_cast<CFBooleanRef>(notSync)));
            }
            packet.key = key;
            const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
            packet.ptsUs = CMTIME_IS_VALID(pts) ? CMTimeConvertScale(pts, 1'000'000, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value
                                                : 0;
            self->packets.push_back(std::move(packet));
        }
        self->ready.notify_all();
    }
};

Encoder::Encoder() noexcept : impl_(new (std::nothrow) Impl()) {}
Encoder::~Encoder() { close(); }

Status Encoder::open(const Request& r, const CodecInfo& codec) noexcept {
    (void)codec;
    close();
    if (!impl_) impl_.reset(new (std::nothrow) Impl());
    if (!impl_) return Status{Errc::OutOfMemory, "encoder do VideoToolbox"};
    // Só o degrau sem cópia: o NV12 da CPU continua no h264_videotoolbox do FFmpeg.
    if (!r.direct || r.input != Input::Surface)
        return Status{Errc::NotSupported, "VideoToolbox direto exige o CVPixelBuffer do Metal"};
    if (!r.width || !r.height || !(r.fps > 0.0)) return Status{Errc::InvalidArgument, "VideoToolbox sem tamanho ou taxa"};
    Impl& d = *impl_;
    d.hevc = r.hevc;
    d.fps = r.fps;
    d.heartbeat = r.heartbeat;
    d.cbr = r.rateMode == 0;
    VTCompressionSessionRef session = nullptr;
    OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault, static_cast<int32_t>(r.width), static_cast<int32_t>(r.height),
                                                 r.hevc ? kCMVideoCodecType_HEVC : kCMVideoCodecType_H264,
                                                 nullptr, nullptr, nullptr, &Impl::output, &d, &session);
    d.beat();
    if (status != noErr || !session) {
        AUREA_LOG_WARN("export: VTCompressionSessionCreate recusou %ux%u %s (%d)", r.width, r.height,
                       r.hevc ? "HEVC" : "H.264", static_cast<int>(status));
        return Status{Errc::EncoderUnavailable, "VideoToolbox nao criou a sessao de compressao"};
    }
    d.session = session;
    // Trabalho offline: sem tempo real, sem reordenar, um quadro por vez.
    (void)VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
    if (VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse) != noErr) {
        close();
        return Status{Errc::EncoderUnavailable, "VideoToolbox sem desligar a reordenacao de quadros"};
    }
    set_number(session, kVTCompressionPropertyKey_MaxFrameDelayCount, 0, "MaxFrameDelayCount");
    // Perfil de qualidade (recusado = o padrão do encoder, sem falhar).
    if (r.qualityProfile && !r.baseline) {
        const CFStringRef level = r.hevc ? kVTProfileLevel_HEVC_Main_AutoLevel : kVTProfileLevel_H264_High_AutoLevel;
        d.profileUsed = VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, level) == noErr;
        if (!d.profileUsed) AUREA_LOG_WARN("export: VideoToolbox recusou o perfil de qualidade; perfil padrao");
    } else if (r.baseline && !r.hevc) {
        (void)VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Baseline_AutoLevel);
    }
    const i64 bitrate = std::max<i64>(100'000, static_cast<i64>(r.bitrateBps ? r.bitrateBps : 16'000'000u));
    set_number(session, kVTCompressionPropertyKey_AverageBitRate, bitrate, "AverageBitRate");
    {
        // Pico: igual à média no CBR; 1,5× por segundo no VBR (o mesmo teto do FFmpeg).
        const f64 bytesPerSecond = static_cast<f64>(bitrate) / 8.0 * (d.cbr ? 1.0 : 1.5);
        const f64 oneSecond = 1.0;
        CFNumberRef bytes = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &bytesPerSecond);
        CFNumberRef seconds = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &oneSecond);
        if (bytes && seconds) {
            const void* values[2] = {bytes, seconds};
            CFArrayRef limits = CFArrayCreate(kCFAllocatorDefault, values, 2, &kCFTypeArrayCallBacks);
            if (limits) {
                if (VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, limits) != noErr)
                    AUREA_LOG_WARN("export: VideoToolbox recusou o teto de taxa");
                CFRelease(limits);
            }
        }
        if (bytes) CFRelease(bytes);
        if (seconds) CFRelease(seconds);
    }
    const i64 gop = r.keyframeFrames ? static_cast<i64>(r.keyframeFrames) : std::max<i64>(1, std::llround(r.fps * 2.0));
    set_number(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, gop, "MaxKeyFrameInterval");
    set_double(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, static_cast<f64>(gop) / r.fps,
               "MaxKeyFrameIntervalDuration");
    set_double(session, kVTCompressionPropertyKey_ExpectedFrameRate, r.fps, "ExpectedFrameRate");
    // Cor do arquivo: BT.709 de faixa limitada, como o NV12 do motor e as
    // etiquetas do contêiner (o buffer do Metal vai etiquetado igual).
    (void)VTSessionSetProperty(session, kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2);
    (void)VTSessionSetProperty(session, kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_ITU_R_709_2);
    (void)VTSessionSetProperty(session, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2);
    status = VTCompressionSessionPrepareToEncodeFrames(session);
    d.beat();
    if (status != noErr) {
        AUREA_LOG_WARN("export: VideoToolbox nao preparou a sessao (%d)", static_cast<int>(status));
        close();
        return Status{Errc::EncoderUnavailable, "VideoToolbox nao preparou a sessao de compressao"};
    }
    std::snprintf(d.description, sizeof(d.description), "videotoolbox-direct:%s%s%s", r.hevc ? "hevc" : "avc",
                  d.profileUsed ? (r.hevc ? ":main" : ":high") : "", d.cbr ? ":cbr" : "");
    AUREA_LOG_INFO("export: %s %ux%u %.3f fps %lld bps", d.description, r.width, r.height, r.fps,
                   static_cast<long long>(bitrate));
    return OkStatus;
}

Status Encoder::send(const u8*, u32, const u8*, u32, i64 ptsUs, u64 timeoutNs) noexcept {
    (void)timeoutNs;
    if (!impl_ || !impl_->session) return Status{Errc::InvalidState, "VideoToolbox fechado"};
    Impl& d = *impl_;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (!d.failure.ok()) return d.failure;
    }
    auto* buffer = static_cast<CVPixelBufferRef>(d.direct);
    d.direct = nullptr;
    if (!buffer) return Status{Errc::NotSupported, "quadro sem o CVPixelBuffer do Metal"};
    const CMTime pts = CMTimeMake(ptsUs, 1'000'000);
    const CMTime duration = CMTimeMake(std::max<i64>(1, std::llround(1e6 / d.fps)), 1'000'000);
    VTEncodeInfoFlags info = 0;
    OSStatus status = VTCompressionSessionEncodeFrame(d.session, buffer, pts, duration, nullptr, nullptr, &info);
    d.beat();
    if (status != noErr) {
        AUREA_LOG_WARN("export: VTCompressionSessionEncodeFrame falhou (%d)", static_cast<int>(status));
        return Status{Errc::EncodeFailed, "VideoToolbox recusou o quadro"};
    }
    // O motor reusa este CVPixelBuffer no próximo quadro do slot: só volta
    // quando o VideoToolbox terminou com ele (o pacote já está na fila).
    status = VTCompressionSessionCompleteFrames(d.session, pts);
    d.beat();
    if (status != noErr) {
        AUREA_LOG_WARN("export: VTCompressionSessionCompleteFrames falhou (%d)", static_cast<int>(status));
        return Status{Errc::EncodeFailed, "VideoToolbox nao concluiu o quadro"};
    }
    std::lock_guard<std::mutex> lock(d.mutex);
    return d.failure;
}

Status Encoder::end_of_stream() noexcept {
    if (!impl_ || !impl_->session) return Status{Errc::InvalidState, "VideoToolbox fechado"};
    Impl& d = *impl_;
    const OSStatus status = VTCompressionSessionCompleteFrames(d.session, kCMTimeInvalid);
    d.beat();
    std::lock_guard<std::mutex> lock(d.mutex);
    d.eosSent = true;
    d.ready.notify_all();
    if (status != noErr) return Status{Errc::EncodeFailed, "VideoToolbox nao concluiu o fluxo"};
    return d.failure;
}

Status Encoder::receive(Packet& out, bool& got, bool& eos, i64 timeoutUs) noexcept {
    got = eos = false;
    if (!impl_) return Status{Errc::InvalidState, "VideoToolbox fechado"};
    Impl& d = *impl_;
    std::unique_lock<std::mutex> lock(d.mutex);
    if (d.packets.empty() && !d.eosSent && d.failure.ok() && timeoutUs > 0)
        d.ready.wait_for(lock, std::chrono::microseconds(timeoutUs),
                         [&] { return !d.packets.empty() || d.eosSent || !d.failure.ok(); });
    if (!d.packets.empty()) {
        out = std::move(d.packets.front());
        d.packets.pop_front();
        got = true;
        eos = d.eosSent && d.packets.empty();
        return OkStatus;
    }
    if (!d.failure.ok()) return d.failure;
    if (d.eosSent) { eos = true; return OkStatus; }
    return Status{Errc::Busy};
}

const std::vector<u8>& Encoder::codec_config() const noexcept {
    static const std::vector<u8> empty;
    if (!impl_) return empty;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->config;
}

// Cada send() já devolve o pacote (CompleteFrames): sem teto de quadros sem pacote.
u32 Encoder::max_in_flight() const noexcept { return 0; }
const char* Encoder::describe() const noexcept { return impl_ ? impl_->description : "videotoolbox-direct"; }
bool Encoder::offline_hints() const noexcept { return false; }
bool Encoder::constant_rate() const noexcept { return impl_ && impl_->cbr; }
bool Encoder::quality_profile() const noexcept { return impl_ && impl_->profileUsed; }
void Encoder::set_direct_buffer(void* buffer) noexcept { if (impl_) impl_->direct = buffer; }

void Encoder::close() noexcept {
    if (!impl_) return;
    Impl& d = *impl_;
    if (d.session) {
        // Invalidate espera os callbacks pendentes; depois dele a sessão não chama mais `output`.
        VTCompressionSessionInvalidate(d.session);
        CFRelease(d.session);
        d.session = nullptr;
    }
    std::lock_guard<std::mutex> lock(d.mutex);
    d.packets.clear();
    d.config.clear();
    d.failure = OkStatus;
    d.eosSent = d.eosDone = d.profileUsed = false;
    d.direct = nullptr;
    d.description[0] = '\0';
}

} // namespace aurea::android_video

#endif // __APPLE__
