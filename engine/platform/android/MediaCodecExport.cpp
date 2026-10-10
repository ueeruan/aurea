// =============================================================================
//  Aurea / platform / android / MediaCodecExport.cpp
// =============================================================================
#include "MediaCodecExport.hpp"
#include "MediaMuxerPacket.hpp"
#include "ExportOutputValidation.hpp"
#include "AacPrimingCalibration.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/export/ExportWatchdog.hpp"
#include "aurea/export/EncoderRateMode.hpp"
#include "aurea/export/AacOutputIntegrity.hpp"
#include "aurea/media/YuvLayout.hpp"

#include <media/NdkImage.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace aurea::android {
namespace {

// Formatos de cor do MediaCodecInfo.CodecCapabilities.
constexpr i32 kColorFormatI420 = 19;
constexpr i32 kColorFormatNv12 = 21;
constexpr i32 kColorFormatFlexible = 0x7F420888;

// AMediaCodec_getInputFormat é API 28; o minSdk é 26.
using GetInputFormatFn = AMediaFormat* (*)(AMediaCodec*);
GetInputFormatFn get_input_format_fn() {
    static GetInputFormatFn fn = reinterpret_cast<GetInputFormatFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputFormat"));
    return fn;
}

// AMediaCodec_getInputImage é API 21 — está em todo aparelho que o Aurea roda.
// É a ÚNICA fonte que diz o passo REAL do buffer de entrada (rowStride e
// pixelStride por plano), então é ela que decide NV12 x I420 e o passo.
using GetInputImageFn = media_status_t (*)(AMediaCodec*, size_t, AImage**);
GetInputImageFn get_input_image_fn() {
    static GetInputImageFn fn = reinterpret_cast<GetInputImageFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputImage"));
    return fn;
}

// AMediaCodec_getName/releaseName também são API 28. Sem elas o nome fica
// desconhecido e quem decide hardware/software é a tabela do MediaCodecList.
using GetNameFn = media_status_t (*)(AMediaCodec*, char**);
using ReleaseNameFn = void (*)(AMediaCodec*, char*);
bool codec_name(AMediaCodec* codec, char* out, usize cap) {
    static GetNameFn get = reinterpret_cast<GetNameFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getName"));
    static ReleaseNameFn rel = reinterpret_cast<ReleaseNameFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_releaseName"));
    out[0] = '\0';
    if (!get || !codec) return false;
    char* name = nullptr;
    if (get(codec, &name) != AMEDIA_OK || !name) return false;
    std::snprintf(out, cap, "%s", name);
    if (rel) rel(codec, name);
    return true;
}

/// Encoders de software do AOSP (e os de terceiros que se declaram assim). O
/// NDK não expõe `isHardwareAccelerated`; o nome é o que a própria
/// MediaCodecList usa para classificar os do sistema.
bool software_codec_name(const char* n) {
    auto starts = [n](const char* p) { return std::strncmp(n, p, std::strlen(p)) == 0; };
    return starts("OMX.google.") || starts("c2.android.") || starts("c2.google.") || starts("OMX.ffmpeg.")
        || std::strstr(n, ".sw.") != nullptr;
}

/// Uma trilha: encoder + índice no muxer.
struct Track {
    AMediaCodec* codec = nullptr;
    i32 muxIndex = -1;
    bool formatKnown = false;
    bool eos = false;
    bool audio = false;
};

/// Amostra que saiu do encoder antes do muxer poder começar.
struct Pending {
    bool audio = false;
    std::vector<u8> data;
    AMediaCodecBufferInfo info{};
};

class MediaCodecExportSink final : public ExportSink {
public:
    ~MediaCodecExportSink() override { release(true); }
    void set_cancel_flag(const std::atomic<bool>* flag) noexcept override { cancelFlag_ = flag; }
    void set_heartbeat(std::atomic<u64>* beatNs) noexcept override { beat_ = beatNs; }

    Status open(const char* outputPath, const VideoStreamConfig& video,
                const AudioStreamConfig* audio) noexcept override {
        if (fd_ >= 0 || muxer_) return Status{Errc::InvalidState, "export ja esta aberto"};
        if (!outputPath || !*outputPath || video.width == 0 || video.height == 0 ||
            (video.width & 1u) || (video.height & 1u) || video.width > 16384 || video.height > 16384 ||
            !std::isfinite(video.fps) || video.fps <= 0.0 || video.fps > 240.0 ||
            (video.codec != ExportCodec::H264 && video.codec != ExportCodec::HEVC) ||
            (audio && (audio->sampleRate == 0 || audio->channels == 0 || audio->channels > 2)))
            return Status{Errc::InvalidArgument, "configuracao de exportacao invalida"};
        video__ = Track{};
        audio__ = Track{};
        layoutReady_ = false;
        plan_ = media::YuvCopyPlan{};
        videoQueued_ = 0;
        videoOut_ = 0;
        audioIntegrity_ = {};
        audioPrimingCalibrated_ = false;
        audioDelaySamples_ = 0;
        audioContentEndUs_ = -1;
        audioPadding_ = false;
        audioSubmittedFrames_ = 0;
        audioCodecName_[0] = '\0';
        triedSoftwareReopen_ = false;
        lastVideoPts_ = lastAudioPts_ = 0;
        explicitLastVideoDurationUs_ = 0;
        lastAudioInputPts_ = -1;
        pendingBytes_ = 0;
        path_ = outputPath ? outputPath : "";
        video_ = video;
        hasAudio_ = audio != nullptr;
        if (audio) audio_ = *audio;

        fd_ = ::open(path_.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (fd_ < 0) return codec_error("nao consegui criar o arquivo do export", -errno, true);
        muxer_ = AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!muxer_) return fail(Errc::IoError, "muxer MP4 indisponivel");

        if (const Status s = open_video(); !s.ok()) return s;
        if (hasAudio_) {
            if (const Status s = open_audio(); !s.ok()) return s;
        }
        return OkStatus;
    }

    Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs) noexcept override {
        if (cancelled()) return Errc::Cancelled;
        if (!video__.codec || video__.eos) return Status{Errc::InvalidState, "encoder de video fechado"};
        // Listras diagonais nascem AQUI. O quadro chegava com passo = largura e
        // era escrito no buffer do encoder com ESSE passo: se o encoder pede
        // linhas de 1152 bytes para uma imagem de 1080, sobram 72 bytes por
        // linha e a imagem desliza — listra diagonal. O plano abaixo usa o
        // passo REAL do encoder (Image API → getInputFormat → capacidade do
        // buffer) e recusa o que não dá para escrever com segurança.
        if (!y || !uv || yStride < video_.width || uvStride < video_.width) {
            return Status{Errc::InvalidState, "quadro com passo de linha menor que a largura"};
        }
        if (ptsUs < 0 || (videoQueued_ > 0 && ptsUs <= lastVideoPts_))
            return Status{Errc::InvalidArgument, "tempo do quadro fora de ordem"};
        u64 deadline = monotonic_ns() + stall_timeout_ns();
        u64 generation = progressGeneration_;
        bool recovering = false;
        ssize_t idx = -1;
        while ((idx = AMediaCodec_dequeueInputBuffer(video__.codec, 2000)) < 0) {
            pulse();
            if (cancelled()) return Errc::Cancelled;
            if (idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return codec_error("encoder de video recusou entrada", idx);
            if (const Status s = drain(video__, false); !s.ok()) return s;
            if (generation != progressGeneration_) { generation = progressGeneration_; deadline = monotonic_ns() + stall_timeout_ns(); }
            if (monotonic_ns() > deadline) {
                // Watchdog (ExportWatchdog.hpp): antes de desistir, UMA rodada
                // de recuperação para o MESMO quadro — esvaziar as duas saídas
                // esperando de verdade (encoder de fabricante que só devolve
                // entrada depois de a saída ser consumida com espera) e tentar
                // de novo por meio prazo. Sem resposta, Timeout: o motor
                // sugere refazer no modo de segurança (Baseline / software).
                if (recovering) return Status{Errc::Timeout, "encoder de video parou de aceitar quadros"};
                recovering = true;
                AUREA_LOG_WARN("export: encoder %s sem buffer de entrada ha %.0f s no quadro %u (%lld us); recuperando",
                               info_.name, stall_timeout_ns() / 1e9, videoQueued_, static_cast<long long>(ptsUs));
                for (int k = 0; k < 20; ++k) {
                    if (cancelled()) return Errc::Cancelled;
                    if (const Status s = drain(video__, true); !s.ok()) return s;
                    if (hasAudio_ && audio__.codec && !audio__.eos) {
                        if (const Status s = drain(audio__, true); !s.ok()) return s;
                    }
                }
                deadline = monotonic_ns() + stall_timeout_ns() / 2;
            }
        }
        pulse();
        size_t cap = 0;
        u8* dst = AMediaCodec_getInputBuffer(video__.codec, static_cast<size_t>(idx), &cap);
        if (!dst) return Status{Errc::InvalidState, "encoder nao entregou buffer de entrada"};
        const bool layoutOk = layoutReady_ || resolve_layout(static_cast<std::size_t>(idx), cap);
        if (!layoutOk || !plan_.valid() || cap < plan_.totalBytes) {
            const char* why = !layoutOk ? layoutWhy_ : "buffer do encoder menor que o quadro";
            // Vivo Y30 (Helio P35): o encoder MediaTek declarou um passo que o
            // buffer dele não comporta. Antes do primeiro quadro nada foi
            // gravado: troca para o encoder de SOFTWARE do sistema (layout
            // contíguo e conhecido) e escreve ESTE quadro nele — mais lento,
            // mesma resolução e taxa — em vez de falhar o export.
            if (videoQueued_ == 0 && reopen_software_video(why)) {
                return write_video(y, yStride, uv, uvStride, ptsUs);
            }
            return Status{Errc::InvalidState, why};
        }
        const media::YuvCopyPlan& p = plan_;
        for (u32 r = 0; r < p.yRows; ++r) {
            std::memcpy(dst + p.yOffset + static_cast<usize>(r) * p.yPitch,
                        y + static_cast<usize>(r) * yStride, p.yRowBytes);
        }
        if (p.cSecondOffset == 0) {
            // NV12: a croma da fonte já vem intercalada (U,V por amostra 2×2).
            for (u32 r = 0; r < p.cRows; ++r) {
                std::memcpy(dst + p.cOffset + static_cast<usize>(r) * p.cPitch,
                            uv + static_cast<usize>(r) * uvStride, p.cRowBytes);
            }
        } else {
            // I420: separa o CbCr intercalado em dois planos de meia largura.
            u8* cb = dst + p.cOffset;
            u8* cr = cb + p.cSecondOffset;
            for (u32 r = 0; r < p.cRows; ++r) {
                const u8* src = uv + static_cast<usize>(r) * uvStride;
                u8* ob = cb + static_cast<usize>(r) * p.cPitch;
                u8* orr = cr + static_cast<usize>(r) * p.cSecondPitch;
                for (u32 x = 0; x < p.cRowBytes; ++x) {
                    ob[x] = src[2 * x];
                    orr[x] = src[2 * x + 1];
                }
            }
        }
        const media_status_t ms = AMediaCodec_queueInputBuffer(video__.codec, static_cast<size_t>(idx), 0,
                                                               p.totalBytes, static_cast<u64>(ptsUs), 0);
        pulse();
        if (ms != AMEDIA_OK) return codec_error("encoder recusou o quadro", ms);
        ++videoQueued_;
        lastVideoPts_ = ptsUs;
        explicitLastVideoDurationUs_ = 0;
        return drain(video__, false);
    }

    Status write_audio(const i16* interleaved, u32 frames, i64 ptsUs) noexcept override {
        if (cancelled()) return Errc::Cancelled;
        if (!hasAudio_) return OkStatus;
        if (!audio__.codec || audio__.eos) return Status{Errc::InvalidState, "encoder de audio fechado"};
        if (frames == 0) return OkStatus;
        if (!interleaved || ptsUs < 0 || ptsUs < lastAudioInputPts_)
            return Status{Errc::InvalidArgument, "bloco de audio invalido"};
        const usize bytesPerFrame = sizeof(i16) * audio_.channels;
        usize done = 0;
        u64 deadline = monotonic_ns() + stall_timeout_ns();
        u64 generation = progressGeneration_;
        while (done < frames) {
            if (cancelled()) return Errc::Cancelled;
            ssize_t idx = AMediaCodec_dequeueInputBuffer(audio__.codec, 2000);
            pulse();
            if (idx < 0) {
                if (idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return codec_error("encoder de audio recusou entrada", idx);
                if (const Status s = drain(audio__, false); !s.ok()) return s;
                if (const Status s = drain(video__, false); !s.ok()) return s;
                if (generation != progressGeneration_) { generation = progressGeneration_; deadline = monotonic_ns() + stall_timeout_ns(); }
                if (monotonic_ns() > deadline) return Status{Errc::Timeout, "encoder de audio parou"};
                continue;
            }
            size_t cap = 0;
            u8* dst = AMediaCodec_getInputBuffer(audio__.codec, static_cast<size_t>(idx), &cap);
            if (!dst || cap < bytesPerFrame) return Status{Errc::InvalidState, "buffer de audio invalido"};
            const usize n = std::min<usize>(frames - done, cap / bytesPerFrame);
            std::memcpy(dst, interleaved + done * audio_.channels, n * bytesPerFrame);
            const i64 pts = ptsUs + static_cast<i64>(done * 1000000ull / audio_.sampleRate);
            const media_status_t queued = AMediaCodec_queueInputBuffer(audio__.codec, static_cast<size_t>(idx), 0,
                n * bytesPerFrame, static_cast<u64>(pts), 0);
            if (queued != AMEDIA_OK) {
                return codec_error("encoder de audio recusou o PCM", queued);
            }
            lastAudioInputPts_ = pts;
            if (!audioPadding_) audioIntegrity_.accepted_pcm(n);
            audioSubmittedFrames_ += n;
            done += n;
            deadline = monotonic_ns() + stall_timeout_ns();
        }
        return drain(audio__, false);
    }

    Status finish() noexcept override {
        if (!video__.codec || !muxer_) return Status{Errc::InvalidState, "export nao esta aberto"};
        if (const Status s = signal_eos(video__); !s.ok()) return fail_status(s);
        if (hasAudio_) {
            if (video_.validateBeforePublish && audioIntegrity_.accepted_frames()) {
                const u64 authored = audioIntegrity_.accepted_frames();
                audioContentEndUs_ = static_cast<i64>(authored * 1000000ull / audio_.sampleRate);
                const u32 block = audioIntegrity_.frame_samples();
                if (!block) return fail_status(Status{Errc::NotSupported, "encoder AAC nao informou tamanho do bloco"});
                const u32 padding = audioDelaySamples_ + block + static_cast<u32>((block - authored % block) % block);
                std::vector<i16> zeros(static_cast<usize>(padding) * audio_.channels, 0);
                audioPadding_ = true;
                const Status padded = write_audio(zeros.data(), padding, audioContentEndUs_);
                audioPadding_ = false;
                if (!padded.ok()) return fail_status(padded);
            }
            if (const Status s = signal_eos(audio__); !s.ok()) return fail_status(s);
        }
        const u64 hardDeadline = monotonic_ns() + 120'000'000'000ull;
        u64 deadline = monotonic_ns() + stall_timeout_ns();
        u64 generation = progressGeneration_;
        bool resentEos = false;
        while (!video__.eos || (hasAudio_ && !audio__.eos)) {
            pulse();
            if (cancelled()) return fail_status(Errc::Cancelled);
            if (!video__.eos) {
                if (const Status s = drain(video__, true); !s.ok()) return fail_status(s);
            }
            if (hasAudio_ && !audio__.eos) {
                if (const Status s = drain(audio__, true); !s.ok()) return fail_status(s);
            }
            if (generation != progressGeneration_) { generation = progressGeneration_; deadline = monotonic_ns() + stall_timeout_ns(); }
            const u64 now = monotonic_ns();
            if (now <= deadline && now <= hardDeadline) continue;
            // Sem saída por um prazo inteiro. Encoder de fabricante que engole a
            // marca de fim: 1) manda o EOS de novo (uma vez); 2) se TODO quadro
            // já saiu (um pacote por quadro) e o áudio chegou até o último PCM,
            // o arquivo está completo — fecha sem a marca em vez de jogar fora
            // um export inteiro (ExportWatchdog.hpp).
            if (!resentEos && now <= hardDeadline) {
                resentEos = true;
                AUREA_LOG_WARN("export: encoder %s sem fim de fluxo apos %.0f s (video %llu/%u, audio %s); EOS de novo",
                               info_.name, stall_timeout_ns() / 1e9, static_cast<unsigned long long>(videoOut_),
                               videoQueued_, !hasAudio_ ? "-" : audio__.eos ? "ok" : "pendente");
                if (!video__.eos) (void)signal_eos(video__, 50'000'000ull);
                if (hasAudio_ && !audio__.eos) (void)signal_eos(audio__, 50'000'000ull);
                deadline = monotonic_ns() + std::min<u64>(stall_timeout_ns(), 5'000'000'000ull);
                continue;
            }
            const bool videoDone = video__.eos || export_video_complete_without_eos(videoQueued_, videoOut_);
            const bool audioDone = !hasAudio_ || audio__.eos
                || (audio__.formatKnown && export_audio_complete_without_eos(lastAudioInputPts_, lastAudioPts_));
            if (muxStarted_ && videoDone && audioDone) {
                AUREA_LOG_WARN("export: encoder %s nao marcou o fim, mas todos os %u quadros sairam; fechando o MP4",
                               info_.name, videoQueued_);
                break;
            }
            return fail_status(Status{Errc::Timeout, "encoder nao terminou"});
        }
        if (cancelled()) return fail_status(Errc::Cancelled);
        if (!muxStarted_) return fail_status(Status{Errc::InvalidState, "nenhum quadro chegou ao arquivo"});
        // EOS alone is not proof of a complete stream: a broken encoder may
        // acknowledge the end after silently dropping submitted input frames.
        if (!export_video_complete_without_eos(videoQueued_, videoOut_))
            return fail_status(Status{Errc::EncodeFailed, "encoder terminou sem entregar todos os quadros"});
        if (hasAudio_) {
            AUREA_LOG_INFO("export AAC: %s, PCM=%llu, buffers=%llu, amostras/AU=%u, capacidade de AU unica=%s",
                audioCodecName_[0] ? audioCodecName_ : "desconhecido",
                static_cast<unsigned long long>(audioIntegrity_.accepted_frames()),
                static_cast<unsigned long long>(audioIntegrity_.data_buffers()), audioIntegrity_.frame_samples(),
                audioIntegrity_.capacity_known() ? "conhecida" : "desconhecida");
            // This proves only insufficient encoded capacity, not correct
            // priming/tail alignment. Never subtract a guessed codec delay.
            if (audioIntegrity_.proves_insufficient())
                return fail_status(Status{Errc::EncodeFailed, "encoder terminou sem representar todas as amostras de audio aceitas"});
        }
        if (video_.validateBeforePublish) {
            // MP4 cannot infer the duration of a single sample. An explicit
            // empty EOS sample also preserves the final frame at fractional
            // rates (MediaMuxer contract), instead of duplicating a guessed delta.
            const u8 empty = 0;
            AMediaCodecBufferInfo end{};
            end.flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            end.presentationTimeUs = explicitLastVideoDurationUs_ > 0
                ? lastVideoPts_ + explicitLastVideoDurationUs_
                : static_cast<i64>(std::llround(videoQueued_ * 1e6 / video_.fps));
            const auto marked = AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(video__.muxIndex), &empty, &end);
            pulse();
            if (marked != AMEDIA_OK) return fail_status(codec_error("muxer recusou duracao do ultimo quadro", marked));
            if (hasAudio_ && audioContentEndUs_ > 0) {
                end.presentationTimeUs = audioContentEndUs_;
                const auto audioMarked = AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(audio__.muxIndex), &empty, &end);
                pulse();
                if (audioMarked != AMEDIA_OK) return fail_status(codec_error("muxer recusou duracao do audio", audioMarked));
            }
        }
        const media_status_t ms = AMediaMuxer_stop(muxer_);
        muxStarted_ = false;
        if (ms != AMEDIA_OK) {
            // Diagnose disk space while the descriptor still exists and before
            // unlinking a partial file can free the space that caused failure.
            const Status error = codec_error("falha ao finalizar o MP4", ms, true);
            release(true);
            return error;
        }
        // A successful muxer stop can still leave delayed filesystem errors.
        // Flush while this descriptor and its free-space diagnosis are valid,
        // before a staging proxy can publish the finalized filename.
        if (::fsync(fd_) != 0) {
            const Status error = codec_error("falha ao sincronizar o MP4", -errno, true);
            release(true);
            return error;
        }
        pulse();
        if (cancelled()) return fail_status(Errc::Cancelled);
        return release(false);
    }

    void abort() noexcept override { release(true); }

    Status validate_output(const ExportOutputValidation& expected) noexcept override {
        if (fd_ >= 0 || muxer_) return Errc::InvalidState;
        return validate_android_export(path_.c_str(), expected, cancelFlag_, beat_);
    }

    Status write_video_timed(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts, i64 duration) noexcept override {
        const Status written = write_video(y, ys, uv, uvs, pts);
        if (written.ok()) explicitLastVideoDurationUs_ = std::max<i64>(0, duration);
        return written;
    }

private:
    bool cancelled() const noexcept { return cancelFlag_ && cancelFlag_->load(std::memory_order_acquire); }
    /// Batida de vida: uma chamada da plataforma acabou de voltar (o watchdog
    /// do motor só desiste de quem fica PRESO dentro dela).
    void pulse() noexcept {
        if (beat_) beat_->store(monotonic_ns(), std::memory_order_release);
    }
    u64 stall_timeout_ns() const noexcept {
        // Software H.264/HEVC can legitimately need >5 seconds on a busy or
        // throttled phone. Only time without codec progress counts as a stall.
        return info_.acceleration == Acceleration::Software ? 30'000'000'000ull : 10'000'000'000ull;
    }
    /// Descobre o layout REAL do buffer de entrada, na ordem de confiança:
    /// `AMediaCodec_getInputImage` (API 21, dá rowStride/pixelStride por plano),
    /// `AMediaCodec_getInputFormat` (API 28, dá passo/fatia/formato) e, por
    /// último, a capacidade do buffer — nunca a suposição de que o passo é a
    /// largura. O que foi usado vai para o log: é o primeiro dado quando um
    /// export sai errado.
    bool resolve_layout(std::size_t idx, size_t capacity) noexcept {
        media::YuvInputLayout in;
        in.width = video_.width;
        in.height = video_.height;
        in.stride = 0;
        in.sliceHeight = 0;
        in.chroma = colorFormat_ == kColorFormatNv12 ? media::ChromaLayout::SemiPlanar : media::ChromaLayout::Planar;
        in.capacity = capacity;

        const char* fonte = "capacidade do buffer";
        bool achou = false;
        bool viaImage = false;
        // 1) Image API: o passo e o entrelaçamento verdadeiros.
        if (GetInputImageFn fn = get_input_image_fn()) {
            AImage* img = nullptr;
            if (fn(video__.codec, idx, &img) == AMEDIA_OK && img) {
                int32_t n = 0;
                if (AImage_getNumberOfPlanes(img, &n) == AMEDIA_OK && n >= 1) {
                    int32_t yRow = 0, yPix = 0;
                    if (AImage_getPlaneRowStride(img, 0, &yRow) == AMEDIA_OK
                        && AImage_getPlanePixelStride(img, 0, &yPix) == AMEDIA_OK && yRow > 0) {
                        in.stride = static_cast<u32>(yRow);
                        in.sliceHeight = video_.height;   // a Image API não expõe fatia; o passo é o que desloca
                        if (n >= 3) {
                            int32_t uPix = 0;
                            if (AImage_getPlanePixelStride(img, 1, &uPix) == AMEDIA_OK) {
                                // U com passo de 2 bytes = U,V intercalados (NV12);
                                // passo 1 = dois planos separados (I420).
                                in.chroma = uPix >= 2 ? media::ChromaLayout::SemiPlanar : media::ChromaLayout::Planar;
                            }
                        }
                        achou = true;
                        viaImage = true;
                        fonte = "Image API";
                    }
                }
                AImage_delete(img);
            }
        }
        // 2) Formato de entrada (API 28): completa o que a Image API não deu
        //    (fatia e formato de cor) e serve de fonte quando ela não existe.
        if (auto fn = get_input_format_fn()) {
            if (AMediaFormat* fmt = fn(video__.codec)) {
                i32 v = 0;
                if (AMediaFormat_getInt32(fmt, "stride", &v) && v >= static_cast<i32>(video_.width)) {
                    // O passo da Image API é o do buffer de verdade: só cai para
                    // o do formato quando ela não respondeu.
                    if (!viaImage) {
                        in.stride = static_cast<u32>(v);
                        achou = true;
                        fonte = "getInputFormat";
                    }
                }
                if (AMediaFormat_getInt32(fmt, "slice-height", &v) && v >= static_cast<i32>(video_.height)) {
                    in.sliceHeight = static_cast<u32>(v);
                }
                if (AMediaFormat_getInt32(fmt, "color-format", &v)) {
                    if (v == kColorFormatNv12) in.chroma = media::ChromaLayout::SemiPlanar;
                    else if (v == kColorFormatI420) in.chroma = media::ChromaLayout::Planar;
                }
                AMediaFormat_delete(fmt);
            }
        }
        // 3) Último recurso (Android < 8.1): deduz o passo da capacidade do
        //    buffer que o codec entregou — e recusa se não der para deduzir.
        if (!achou) {
            u32 s = 0, fatia = 0;
            if (media::stride_from_capacity(video_.width, video_.height, in.chroma, capacity, 128, s, fatia)) {
                in.stride = s;
                in.sliceHeight = fatia;
                achou = true;
            } else {
                layoutWhy_ = "nao consegui descobrir o passo do encoder de video";
                AUREA_LOG_ERROR("export: %s (buffer %zu bytes, %ux%u) — export recusado em vez de sair listrado",
                                layoutWhy_, capacity, video_.width, video_.height);
                return false;
            }
        }
        in.fromCodec = true;
        const char* why = nullptr;
        if (!plan_yuv_copy(in, plan_, &why)) {
            layoutWhy_ = why ? why : "layout do encoder invalido";
            AUREA_LOG_ERROR("export: %s (passo %u, fatia %u, %ux%u, %s)", layoutWhy_, in.stride, in.sliceHeight,
                            video_.width, video_.height,
                            in.chroma == media::ChromaLayout::SemiPlanar ? "NV12" : "I420");
            return false;
        }
        layoutReady_ = true;
        AUREA_LOG_INFO("export: entrada %s passo %u fatia %u (%ux%u), quadro %zu bytes [fonte: %s]",
                       in.chroma == media::ChromaLayout::SemiPlanar ? "NV12" : "I420", in.stride, in.sliceHeight,
                       video_.width, video_.height, plan_.totalBytes, fonte);
        return true;
    }

    /// Configura o codec com a MESMA receita (resolução, taxa, GOP, cor) nos
    /// formatos de entrada que o motor sabe entregar. Nada de baixar resolução
    /// ou taxa para "caber": se não aceita, quem chama decide o que fazer.
    bool configure_video(AMediaCodec*& codec, const char* mime, const char* preferredName = nullptr) noexcept {
        // Modo de segurança (ExportWatchdog.hpp): H.264 Baseline — sem
        // B-quadros, o perfil que todo encoder de fabricante implementa. Se o
        // encoder recusar a chave, a mesma receita sem ela.
        const bool baseline = video_.profile == kExportProfileBaseline && video_.codec == ExportCodec::H264;
        char name[128]{};
        if (preferredName) std::snprintf(name, sizeof(name), "%s", preferredName);
        else (void)codec_name(codec, name, sizeof(name));
        bool attempted = false;
        const u32 requestedMode = video_.rateMode == 0 ? 0u : 1u;
        u32 acceptedMode = requestedMode;
        const bool configured = configure_encoder_rate_mode(requestedMode, [&](u32 mode) noexcept {
            for (int withProfile = baseline ? 1 : 0; withProfile >= 0; --withProfile) {
                if (configure_video_formats(codec, mime, withProfile != 0, name, attempted, mode)) return true;
            }
            return false;
        }, acceptedMode);
        if (configured) {
            video_.rateMode = acceptedMode;
            if (acceptedMode != requestedMode)
                AUREA_LOG_WARN("export: encoder %s recusou %s; usando %s com os mesmos %u bps", name,
                    requestedMode == 0 ? "CBR" : "VBR", acceptedMode == 0 ? "CBR" : "VBR", video_.bitrateBps);
        }
        return configured;
    }

    bool configure_video_formats(AMediaCodec*& codec, const char* mime, bool baselineProfile,
                                 const char* name, bool& attempted, u32 rateMode) noexcept {
        const i32 order[] = {kColorFormatNv12, kColorFormatI420, kColorFormatFlexible};
        for (i32 cf : order) {
            // configure() can leave a vendor codec in its error state. Each
            // format/profile retry starts with a new instance of the same codec.
            if (attempted) {
                if (codec) AMediaCodec_delete(codec);
                codec = name[0] ? AMediaCodec_createCodecByName(name) : AMediaCodec_createEncoderByType(mime);
                if (!codec) return false;
            }
            attempted = true;
            AMediaFormat* f = AMediaFormat_new();
            // MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline = 1.
            if (baselineProfile) AMediaFormat_setInt32(f, "profile", 1);
            AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, mime);
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, static_cast<i32>(video_.width));
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, static_cast<i32>(video_.height));
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, static_cast<i32>(video_.bitrateBps));
            AMediaFormat_setFloat(f, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<f32>(video_.fps));
            const f64 gopSeconds = video_.keyframeIntervalFrames > 0 ? video_.keyframeIntervalFrames / video_.fps : 2.0;
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, static_cast<i32>(
                std::clamp(std::floor(gopSeconds + 0.5), 1.0, static_cast<f64>(std::numeric_limits<i32>::max()))));
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, cf);
            // MediaCodecInfo.EncoderCapabilities: 0 = CQ, 1 = VBR, 2 = CBR. CQ nunca:
            // ignora a taxa (era o "1 minuto = 1 GB"). CBR quando o encoder do
            // aparelho não anuncia VBR (o app confere no MediaCodecList).
            AMediaFormat_setInt32(f, "bitrate-mode", rateMode == 0 ? 2 : 1);
            // Etiqueta de cor: BT.709, faixa limitada, SDR (MediaFormat.COLOR_*).
            AMediaFormat_setInt32(f, "color-standard", video_.color.matrix == 9 ? 6 : video_.color.matrix == 6 ? 4 : 1);
            AMediaFormat_setInt32(f, "color-range", video_.color.fullRange ? 1 : 2);
            AMediaFormat_setInt32(f, "color-transfer", video_.color.transfer == 16 ? 6 : video_.color.transfer == 18 ? 7 : video_.color.transfer == 8 ? 1 : 3);
            const media_status_t ms = AMediaCodec_configure(codec, f, nullptr, nullptr,
                                                            AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
            AMediaFormat_delete(f);
            if (ms == AMEDIA_OK) {
                colorFormat_ = cf == kColorFormatNv12 ? kColorFormatNv12 : kColorFormatI420;
                return true;
            }
        }
        return false;
    }

    /// O encoder de SOFTWARE do sistema (Codec2 e, antes dele, OMX.google) com
    /// a MESMA receita. Configurado, ainda não iniciado.
    bool open_software_video(const char* mime, const char* hwName, const char* motivo) noexcept {
        const bool hevc = video_.codec == ExportCodec::HEVC;
        const char* const avc[] = {"c2.android.avc.encoder", "OMX.google.h264.encoder"};
        const char* const hvc[] = {"c2.android.hevc.encoder", "OMX.google.hevc.encoder"};
        const char* const* names = hevc ? hvc : avc;
        for (int k = 0; k < 2; ++k) {
            const char* name = names[k];
            AMediaCodec* sw = AMediaCodec_createCodecByName(name);
            if (!sw) continue;
            if (configure_video(sw, mime, name)) {
                video__.codec = sw;
                std::snprintf(info_.name, sizeof(info_.name), "%s", name);
                info_.acceleration = Acceleration::Software;
                AUREA_LOG_WARN("export: encoder de hardware %s %s (%ux%u @%.2f); usando o de SOFTWARE %s "
                               "(mais lento, mesma resolucao e taxa)",
                               hwName, motivo, video_.width, video_.height, video_.fps, name);
                return true;
            }
            if (sw) AMediaCodec_delete(sw);
        }
        return false;
    }

    /// O encoder de hardware abriu, mas o buffer de entrada dele não serve
    /// (passo/fatia declarados maiores que o buffer). Só antes do primeiro
    /// quadro e antes de a trilha de vídeo entrar no muxer: troca pelo de
    /// software, que tem layout contíguo.
    bool reopen_software_video(const char* why) noexcept {
        if (triedSoftwareReopen_ || info_.acceleration == Acceleration::Software || video__.formatKnown) return false;
        triedSoftwareReopen_ = true;
        char hwName[64];
        std::snprintf(hwName, sizeof(hwName), "%s", info_.name[0] ? info_.name : "?");
        AUREA_LOG_WARN("export: %s no encoder %s (%ux%u); tentando o encoder de software", why, hwName,
                       video_.width, video_.height);
        if (video__.codec) {
            AMediaCodec_stop(video__.codec);
            AMediaCodec_delete(video__.codec);
            video__.codec = nullptr;
        }
        layoutReady_ = false;
        plan_ = media::YuvCopyPlan{};
        const char* mime = video_.codec == ExportCodec::HEVC ? "video/hevc" : "video/avc";
        if (!open_software_video(mime, hwName, why)) return false;
        if (AMediaCodec_start(video__.codec) != AMEDIA_OK) {
            AMediaCodec_delete(video__.codec);
            video__.codec = nullptr;
            return false;
        }
        return true;
    }

    void note_codec(AMediaCodec* codec) noexcept {
        if (codec_name(codec, info_.name, sizeof(info_.name))) {
            info_.acceleration = software_codec_name(info_.name) ? Acceleration::Software : Acceleration::Hardware;
        } else {
            info_.acceleration = Acceleration::Unknown;
        }
    }

    Status open_video() noexcept {
        const bool hevc = video_.codec == ExportCodec::HEVC;
        const char* mime = hevc ? "video/hevc" : "video/avc";
        // O sistema devolve o encoder PREFERIDO do tipo — o de hardware, quando
        // existe. Qual veio de fato fica registrado (nome + hardware/software).
        // Modo de segurança nível 2: o encoder de hardware já travou duas vezes
        // neste projeto — direto o de SOFTWARE do sistema (mais lento, nunca
        // trava no HAL do fabricante). Sem ele, o preferido de sempre.
        if (video_.preferSoftware && open_software_video(mime, "(modo de seguranca)", "travou antes")) {
            if (AMediaCodec_start(video__.codec) == AMEDIA_OK) {
                triedSoftwareReopen_ = true;
                AUREA_LOG_INFO("export: %s %s (SOFTWARE, modo de seguranca) %ux%u @%.2f %u bps", mime, info_.name,
                               video_.width, video_.height, video_.fps, video_.bitrateBps);
                return OkStatus;
            }
            AMediaCodec_delete(video__.codec);
            video__.codec = nullptr;
            info_ = EncoderInfo{};
        }
        video__.codec = AMediaCodec_createEncoderByType(mime);
        bool configured = false;
        if (!video__.codec) {
            configured = open_software_video(mime, "(indisponivel)", "nao abriu");
            if (!configured)
                return fail(Errc::NotSupported, hevc ? "este aparelho nao codifica HEVC" : "este aparelho nao codifica H.264");
        }
        note_codec(video__.codec);
        if (!configured) configured = configure_video(video__.codec, mime);
        if (!configured && info_.acceleration != Acceleration::Software) {
            // O de hardware recusou a receita (resolução/fps acima do bloco do
            // aparelho). Fallback: o encoder de software do sistema, com a MESMA
            // receita — mais lento, nunca menor. Avisado no log e na UI.
            char hwName[64];
            std::snprintf(hwName, sizeof(hwName), "%s", info_.name[0] ? info_.name : "?");
            AMediaCodec_delete(video__.codec);
            video__.codec = nullptr;
            configured = open_software_video(mime, hwName, "recusou a configuracao");
        }
        if (!configured) return fail(Errc::NotSupported, "encoder nao aceita essa resolucao/formato");
        // O passo/fatia REAIS do buffer de entrada sao descobertos no primeiro
        // quadro (resolve_layout): so depois do start o formato de entrada e o
        // buffer tem o tamanho de verdade, e o passo nunca e suposto igual a
        // largura.
        const media_status_t started = AMediaCodec_start(video__.codec);
        if (started != AMEDIA_OK && !reopen_software_video("nao conseguiu iniciar"))
            return fail_status(codec_error("encoder de video nao iniciou", started));
        AUREA_LOG_INFO("export: %s %s (%s) %ux%u @%.2f %u bps %s, formato pedido %s", mime,
                       info_.name[0] ? info_.name : "?",
                       info_.acceleration == Acceleration::Hardware ? "hardware"
                       : info_.acceleration == Acceleration::Software ? "SOFTWARE" : "aceleracao desconhecida",
                       video_.width, video_.height, video_.fps, video_.bitrateBps, video_.rateMode == 0 ? "CBR" : "VBR",
                       colorFormat_ == kColorFormatNv12 ? "NV12" : "I420");
        return OkStatus;
    }

public:
    EncoderInfo encoder_info() const noexcept override { return info_; }

private:

    Status open_audio() noexcept {
        audio__.audio = true;
        audio__.codec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
        if (!audio__.codec) return fail(Errc::NotSupported, "este aparelho nao codifica AAC");
        AMediaFormat* f = AMediaFormat_new();
        AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, static_cast<i32>(audio_.sampleRate));
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, static_cast<i32>(audio_.channels));
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, static_cast<i32>(audio_.bitrateBps));
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);   // AAC-LC
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);
        const media_status_t ms = AMediaCodec_configure(audio__.codec, f, nullptr, nullptr,
                                                        AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaFormat_delete(f);
        if (ms != AMEDIA_OK) return fail(Errc::NotSupported, "encoder AAC recusou a configuracao");
        if (AMediaCodec_start(audio__.codec) != AMEDIA_OK) return fail(Errc::IoError, "encoder AAC nao iniciou");
        // createEncoderByType does not request an alias: getName identifies
        // the allocated component. Unknown/older APIs disable strict counting.
        (void)codec_name(audio__.codec, audioCodecName_, sizeof(audioCodecName_));
        if (video_.validateBeforePublish && !audioPrimingCalibrated_) {
            const std::string measuredComponent = audioCodecName_;
            const auto measured = measure_aac_priming(audio__.codec, audio_, cancelFlag_, beat_);
            AMediaCodec_stop(audio__.codec);
            AMediaCodec_delete(audio__.codec);
            audio__.codec = nullptr;
            if (!measured.ok()) return measured.status();
            audioDelaySamples_ = *measured;
            audioPrimingCalibrated_ = true;
            if (const Status reopened = open_audio(); !reopened.ok()) return reopened;
            if (!measuredComponent.empty() && measuredComponent != audioCodecName_)
                return Status{Errc::UnsupportedCodec, "encoder AAC mudou depois da calibracao"};
        }
        return OkStatus;
    }

    /// Marca o fim do fluxo. `budgetNs` 0 = o prazo de travamento inteiro (o
    /// primeiro EOS); a segunda tentativa (finish) só espera um pouco.
    Status signal_eos(Track& t, u64 budgetNs = 0) noexcept {
        u64 deadline = monotonic_ns() + (budgetNs ? budgetNs : stall_timeout_ns());
        u64 generation = progressGeneration_;
        ssize_t idx = -1;
        while ((idx = AMediaCodec_dequeueInputBuffer(t.codec, 2000)) < 0) {
            pulse();
            if (cancelled()) return Errc::Cancelled;
            if (idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return codec_error("encoder recusou entrada de fim", idx);
            if (const Status s = drain(t, false); !s.ok()) return s;
            if (!budgetNs && generation != progressGeneration_) {
                generation = progressGeneration_;
                deadline = monotonic_ns() + stall_timeout_ns();
            }
            if (monotonic_ns() > deadline) return Status{Errc::Timeout, "encoder nao aceitou o fim do fluxo"};
        }
        pulse();
        // Vídeo: o fim com o carimbo do quadro SEGUINTE. Encoder que recebe o
        // EOS com o mesmo carimbo do último quadro pode tratá-lo como repetido
        // e nunca devolver a marca (o export "parado" no fim).
        const i64 frameUs = video_.fps > 0.0 ? static_cast<i64>(std::llround(1e6 / video_.fps)) : 33'333;
        const i64 pts = t.audio ? (video_.validateBeforePublish
                                    ? static_cast<i64>(audioSubmittedFrames_ * 1000000ull / audio_.sampleRate)
                                    : std::max(lastAudioPts_, lastAudioInputPts_))
                                : (videoQueued_ > 0 ? lastVideoPts_ + frameUs : 0);
        const media_status_t queued = AMediaCodec_queueInputBuffer(t.codec, static_cast<size_t>(idx), 0, 0,
            static_cast<u64>(std::max<i64>(0, pts)), AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        pulse();
        if (queued != AMEDIA_OK) return Status{Errc::IoError, "encoder recusou o fim do fluxo"};
        return OkStatus;
    }

    /// Esvazia a saída do encoder. `wait` = espera um pouco por buffer (fim do
    /// fluxo); sem ele, só o que já está pronto.
    Status drain(Track& t, bool wait) noexcept {
        for (u32 packets = 0; packets < 256; ++packets) {
            if (cancelled()) return Errc::Cancelled;
            AMediaCodecBufferInfo info{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(t.codec, &info, wait ? 10000 : 0);
            pulse();
            if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return OkStatus;
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (t.formatKnown) return Status{Errc::EncodeFailed, "encoder mudou o formato durante a exportacao"};
                AMediaFormat* f = AMediaCodec_getOutputFormat(t.codec);
                if (!f) return Status{Errc::EncodeFailed, "encoder nao entregou o formato de saida"};
                if (t.audio) {
                    void* asc = nullptr;
                    size_t ascBytes = 0;
                    (void)AMediaFormat_getBuffer(f, "csd-0", &asc, &ascBytes);
                    i32 maxBatch = -1, threshold = -1;
                    const bool maxKnown = AMediaFormat_getInt32(f, "buffer-batch-max-output-size", &maxBatch);
                    const bool thresholdKnown = AMediaFormat_getInt32(f, "buffer-batch-threshold-output-size", &threshold);
                    const bool singleAu = aac_c2_single_au_contract(audioCodecName_, maxKnown, maxBatch, thresholdKnown, threshold);
                    audioIntegrity_.set_format(asc ? std::span<const u8>{static_cast<const u8*>(asc), ascBytes}
                                                  : std::span<const u8>{}, audio_.sampleRate, audio_.channels, singleAu);
                }
                t.muxIndex = static_cast<i32>(AMediaMuxer_addTrack(muxer_, f));
                AMediaFormat_delete(f);
                if (t.muxIndex < 0) return codec_error("muxer recusou a trilha", t.muxIndex);
                t.formatKnown = true;
                ++progressGeneration_;
                if (const Status s = maybe_start_muxer(); !s.ok()) return s;
                continue;
            }
            if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (idx < 0) return codec_error(t.audio ? "encoder AAC devolveu erro" : "encoder de video devolveu erro", idx);

            size_t cap = 0;
            u8* data = AMediaCodec_getOutputBuffer(t.codec, static_cast<size_t>(idx), &cap);
            const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
            if (info.size != 0 && !config) {
                // NDK already adjusted data to the valid sample. Applying its
                // old BufferInfo.offset again skips bytes, and its pre-36
                // reported capacity cannot be used to reject a valid packet.
                if (!normalize_codec_output_packet(data, cap, info)) {
                    AMediaCodec_releaseOutputBuffer(t.codec, static_cast<size_t>(idx), false);
                    return Status{Errc::EncodeFailed, "encoder entregou pacote invalido"};
                }
                ++progressGeneration_;
                if (t.audio) {
                    lastAudioPts_ = std::max<i64>(lastAudioPts_, info.presentationTimeUs);
                    audioIntegrity_.encoded_buffer(static_cast<u64>(info.size), info.flags, info.presentationTimeUs);
                }
                else ++videoOut_;
                if (t.audio && video_.validateBeforePublish)
                    info.presentationTimeUs -= static_cast<i64>(std::llround(audioDelaySamples_ * 1e6 / audio_.sampleRate));
                const bool paddingOnly = t.audio && video_.validateBeforePublish && audioContentEndUs_ >= 0 &&
                    info.presentationTimeUs >= audioContentEndUs_;
                if (paddingOnly) {
                    // Keep drain/EOS accounting, but never extend authored audio
                    // with the extra PCM needed to flush the encoder's history.
                } else if (muxStarted_) {
                    const media_status_t ms = write_muxer_packet(muxer_, static_cast<size_t>(t.muxIndex), data, cap, info);
                    if (ms != AMEDIA_OK) {
                        AMediaCodec_releaseOutputBuffer(t.codec, static_cast<size_t>(idx), false);
                        return codec_error("falha ao gravar no MP4", ms, true);
                    }
                } else {
                    // A broken encoder can emit packets forever without the
                    // other track exposing its format. Bound that startup
                    // queue instead of growing RAM until Android kills us.
                    constexpr usize kMaxPendingBytes = 32u << 20;
                    constexpr usize kMaxPendingPackets = 512;
                    if (pending_.size() >= kMaxPendingPackets ||
                        static_cast<usize>(info.size) > kMaxPendingBytes - pendingBytes_) {
                        AMediaCodec_releaseOutputBuffer(t.codec, static_cast<size_t>(idx), false);
                        return Status{Errc::Timeout, "encoder nao entregou as trilhas para iniciar o MP4"};
                    }
                    Pending p;
                    p.audio = t.audio;
                    p.data.assign(data, data + cap);
                    p.info = info;
                    p.info.offset = 0;
                    pendingBytes_ += p.data.size();
                    pending_.push_back(std::move(p));
                }
            } else if (t.audio) {
                audioIntegrity_.encoded_buffer(0, info.flags, info.presentationTimeUs);
            }
            AMediaCodec_releaseOutputBuffer(t.codec, static_cast<size_t>(idx), false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                t.eos = true;
                return OkStatus;
            }
        }
        return OkStatus;
    }

    Status maybe_start_muxer() noexcept {
        if (muxStarted_ || !video__.formatKnown || (hasAudio_ && !audio__.formatKnown)) return OkStatus;
        // Orientação: o vídeo já sai de pé (a composição é renderizada com a
        // proporção final), então nenhuma rotação no contêiner.
        const media_status_t started = AMediaMuxer_start(muxer_);
        if (started != AMEDIA_OK) return codec_error("muxer nao iniciou", started);
        muxStarted_ = true;
        for (Pending& p : pending_) {
            const Track& t = p.audio ? audio__ : video__;
            const media_status_t written = write_muxer_packet(muxer_, static_cast<size_t>(t.muxIndex), p.data.data(), p.data.size(), p.info);
            if (written != AMEDIA_OK) {
                return codec_error("falha ao gravar no MP4", written, true);
            }
        }
        pending_.clear();
        pendingBytes_ = 0;
        return OkStatus;
    }

    Status codec_error(const char* message, i64 platformCode, bool storage = false) noexcept {
        struct statvfs space{};
        const bool full = storage && (platformCode == -ENOSPC || platformCode == -EDQUOT ||
            (fd_ >= 0 && ::fstatvfs(fd_, &space) == 0 && space.f_bavail == 0));
        std::snprintf(errorDetail_, sizeof(errorDetail_), "%s (codigo %lld)",
            full ? "armazenamento cheio ao gravar o MP4" : message, static_cast<long long>(platformCode));
        AUREA_LOG_ERROR("export: %s; encoder=%s, ultimo quadro=%lld us", errorDetail_, info_.name,
            static_cast<long long>(lastVideoPts_));
        return Status{full ? Errc::StorageFull : Errc::IoError, errorDetail_};
    }

    Status fail(Errc code, const char* msg) noexcept {
        release(true);
        return Status{code, msg};
    }
    Status fail_status(const Status& s) noexcept {
        release(true);
        return s;
    }

    Status release(bool deleteFile) noexcept {
        Status closed = OkStatus;
        for (Track* t : {&video__, &audio__}) {
            if (t->codec) {
                AMediaCodec_stop(t->codec);
                AMediaCodec_delete(t->codec);
                t->codec = nullptr;
            }
        }
        if (muxer_) {
            if (muxStarted_) AMediaMuxer_stop(muxer_);
            AMediaMuxer_delete(muxer_);
            muxer_ = nullptr;
            muxStarted_ = false;
        }
        if (fd_ >= 0) {
            const int closeError = ::close(fd_) == 0 ? 0 : errno;
            fd_ = -1;
            if ((deleteFile || closeError) && !path_.empty()) ::unlink(path_.c_str());
            // Preserve an earlier encode/mux/flush diagnostic during abort.
            // A successful finish, however, must surface a deferred close error.
            if (closeError && !deleteFile)
                closed = codec_error("falha ao fechar o MP4", -closeError, true);
        }
        pending_.clear();
        pendingBytes_ = 0;
        return closed;
    }

    std::string path_;
    i64 explicitLastVideoDurationUs_ = 0;
    char errorDetail_[192]{};
    VideoStreamConfig video_{};
    AudioStreamConfig audio_{};
    bool hasAudio_ = false;
    int fd_ = -1;
    AMediaMuxer* muxer_ = nullptr;
    bool muxStarted_ = false;
    Track video__{};
    Track audio__{};
    EncoderInfo info_{};
    i32 colorFormat_ = kColorFormatNv12;
    /// Layout REAL do buffer de entrada (descoberto no primeiro quadro).
    media::YuvCopyPlan plan_{};
    bool layoutReady_ = false;
    const char* layoutWhy_ = "";
    /// Quadros de vídeo já entregues ao encoder (a troca para software só vale
    /// antes do primeiro).
    u32 videoQueued_ = 0;
    /// Pacotes de vídeo que saíram do encoder (um por quadro): fecha o MP4
    /// mesmo se a marca de fim se perder (export_video_complete_without_eos).
    u64 videoOut_ = 0;
    AacOutputIntegrity audioIntegrity_{};
    bool audioPrimingCalibrated_ = false, audioPadding_ = false;
    u32 audioDelaySamples_ = 0;
    u64 audioSubmittedFrames_ = 0;
    i64 audioContentEndUs_ = -1;
    char audioCodecName_[64]{};
    std::atomic<u64>* beat_ = nullptr;
    bool triedSoftwareReopen_ = false;
    i64 lastVideoPts_ = 0;
    i64 lastAudioPts_ = 0;
    i64 lastAudioInputPts_ = -1;
    usize pendingBytes_ = 0;
    u64 progressGeneration_ = 0;
    const std::atomic<bool>* cancelFlag_ = nullptr;
    std::vector<Pending> pending_;
};

} // namespace

std::unique_ptr<ExportSink> make_mediacodec_export_sink(void*) {
    return std::make_unique<MediaCodecExportSink>();
}

} // namespace aurea::android
