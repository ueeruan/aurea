// =============================================================================
//  Aurea / platform / android / MediaCodecExport.cpp
// =============================================================================
#include "MediaCodecExport.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace aurea::android {
namespace {

// Formatos de cor do MediaCodecInfo.CodecCapabilities.
constexpr i32 kColorFormatI420 = 19;
constexpr i32 kColorFormatNv12 = 21;
constexpr i32 kColorFormatFlexible = 0x7F420888;

// AMediaCodec_getInputFormat é API 28; o minSdk é 26. Sem ela, stride =
// largura e fatia = altura (o que os encoders dessa época fazem).
using GetInputFormatFn = AMediaFormat* (*)(AMediaCodec*);
GetInputFormatFn get_input_format_fn() {
    static GetInputFormatFn fn = reinterpret_cast<GetInputFormatFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputFormat"));
    return fn;
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

    Status open(const char* outputPath, const VideoStreamConfig& video,
                const AudioStreamConfig* audio) noexcept override {
        path_ = outputPath ? outputPath : "";
        video_ = video;
        hasAudio_ = audio != nullptr;
        if (audio) audio_ = *audio;

        fd_ = ::open(path_.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (fd_ < 0) return Status{Errc::IoError, "nao consegui criar o arquivo do export"};
        muxer_ = AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!muxer_) return fail(Errc::IoError, "muxer MP4 indisponivel");

        if (const Status s = open_video(); !s.ok()) return s;
        if (hasAudio_) {
            if (const Status s = open_audio(); !s.ok()) return s;
        }
        return OkStatus;
    }

    Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs) noexcept override {
        const u64 deadline = monotonic_ns() + 5'000'000'000ull;
        ssize_t idx = -1;
        while ((idx = AMediaCodec_dequeueInputBuffer(video__.codec, 2000)) < 0) {
            if (const Status s = drain(video__, false); !s.ok()) return s;
            if (monotonic_ns() > deadline) return Status{Errc::Timeout, "encoder de video parou de aceitar quadros"};
        }
        size_t cap = 0;
        u8* dst = AMediaCodec_getInputBuffer(video__.codec, static_cast<size_t>(idx), &cap);
        const u32 w = video_.width, h = video_.height;
        const usize ySize = static_cast<usize>(stride_) * sliceHeight_;
        const usize need = colorFormat_ == kColorFormatNv12
                         ? ySize + static_cast<usize>(stride_) * (h / 2)
                         : ySize + 2 * static_cast<usize>(stride_ / 2) * (sliceHeight_ / 2);
        if (!dst || cap < need) {
            return Status{Errc::InvalidState, "buffer do encoder menor que o quadro"};
        }
        for (u32 r = 0; r < h; ++r) {
            std::memcpy(dst + static_cast<usize>(r) * stride_, y + static_cast<usize>(r) * yStride, w);
        }
        if (colorFormat_ == kColorFormatNv12) {
            u8* c = dst + ySize;
            for (u32 r = 0; r < h / 2; ++r) {
                std::memcpy(c + static_cast<usize>(r) * stride_, uv + static_cast<usize>(r) * uvStride, w);
            }
        } else {
            // I420: separa o CbCr intercalado em dois planos.
            const u32 cs = stride_ / 2;
            u8* cb = dst + ySize;
            u8* cr = cb + static_cast<usize>(cs) * (sliceHeight_ / 2);
            for (u32 r = 0; r < h / 2; ++r) {
                const u8* src = uv + static_cast<usize>(r) * uvStride;
                u8* ob = cb + static_cast<usize>(r) * cs;
                u8* orr = cr + static_cast<usize>(r) * cs;
                for (u32 x = 0; x < w / 2; ++x) {
                    ob[x] = src[2 * x];
                    orr[x] = src[2 * x + 1];
                }
            }
        }
        const media_status_t ms =
            AMediaCodec_queueInputBuffer(video__.codec, static_cast<size_t>(idx), 0, need, static_cast<u64>(ptsUs), 0);
        if (ms != AMEDIA_OK) return Status{Errc::IoError, "encoder recusou o quadro"};
        lastVideoPts_ = ptsUs;
        return drain(video__, false);
    }

    Status write_audio(const i16* interleaved, u32 frames, i64 ptsUs) noexcept override {
        if (!hasAudio_) return OkStatus;
        const usize bytesPerFrame = sizeof(i16) * audio_.channels;
        usize done = 0;
        const u64 deadline = monotonic_ns() + 5'000'000'000ull;
        while (done < frames) {
            ssize_t idx = AMediaCodec_dequeueInputBuffer(audio__.codec, 2000);
            if (idx < 0) {
                if (const Status s = drain(audio__, false); !s.ok()) return s;
                if (const Status s = drain(video__, false); !s.ok()) return s;
                if (monotonic_ns() > deadline) return Status{Errc::Timeout, "encoder de audio parou"};
                continue;
            }
            size_t cap = 0;
            u8* dst = AMediaCodec_getInputBuffer(audio__.codec, static_cast<size_t>(idx), &cap);
            if (!dst || cap < bytesPerFrame) return Status{Errc::InvalidState, "buffer de audio invalido"};
            const usize n = std::min<usize>(frames - done, cap / bytesPerFrame);
            std::memcpy(dst, interleaved + done * audio_.channels, n * bytesPerFrame);
            const i64 pts = ptsUs + static_cast<i64>(done * 1000000ull / audio_.sampleRate);
            if (AMediaCodec_queueInputBuffer(audio__.codec, static_cast<size_t>(idx), 0, n * bytesPerFrame,
                                             static_cast<u64>(pts), 0) != AMEDIA_OK) {
                return Status{Errc::IoError, "encoder de audio recusou o PCM"};
            }
            done += n;
        }
        return drain(audio__, false);
    }

    Status finish() noexcept override {
        if (const Status s = signal_eos(video__); !s.ok()) return fail_status(s);
        if (hasAudio_) {
            if (const Status s = signal_eos(audio__); !s.ok()) return fail_status(s);
        }
        const u64 deadline = monotonic_ns() + 10'000'000'000ull;
        while (!video__.eos || (hasAudio_ && !audio__.eos)) {
            if (!video__.eos) {
                if (const Status s = drain(video__, true); !s.ok()) return fail_status(s);
            }
            if (hasAudio_ && !audio__.eos) {
                if (const Status s = drain(audio__, true); !s.ok()) return fail_status(s);
            }
            if (monotonic_ns() > deadline) return fail_status(Status{Errc::Timeout, "encoder nao terminou"});
        }
        if (!muxStarted_) return fail_status(Status{Errc::InvalidState, "nenhum quadro chegou ao arquivo"});
        const media_status_t ms = AMediaMuxer_stop(muxer_);
        muxStarted_ = false;
        release(false);
        if (ms != AMEDIA_OK) {
            ::unlink(path_.c_str());
            return Status{Errc::IoError, "falha ao finalizar o MP4"};
        }
        return OkStatus;
    }

    void abort() noexcept override { release(true); }

private:
    Status open_video() noexcept {
        const char* mime = video_.codec == ExportCodec::HEVC ? "video/hevc" : "video/avc";
        video__.codec = AMediaCodec_createEncoderByType(mime);
        if (!video__.codec) {
            return fail(Errc::NotSupported, video_.codec == ExportCodec::HEVC ? "este aparelho nao codifica HEVC"
                                                                              : "este aparelho nao codifica H.264");
        }
        const i32 order[] = {kColorFormatNv12, kColorFormatI420, kColorFormatFlexible};
        bool configured = false;
        for (i32 cf : order) {
            AMediaFormat* f = AMediaFormat_new();
            AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, mime);
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, static_cast<i32>(video_.width));
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, static_cast<i32>(video_.height));
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, static_cast<i32>(video_.bitrateBps));
            AMediaFormat_setFloat(f, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<f32>(video_.fps));
            const f64 gopSeconds = video_.keyframeIntervalFrames > 0 ? video_.keyframeIntervalFrames / video_.fps : 2.0;
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, std::max(1, static_cast<i32>(gopSeconds + 0.5)));
            AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, cf);
            AMediaFormat_setInt32(f, "bitrate-mode", 1);   // VBR
            // Etiqueta de cor: BT.709, faixa limitada, SDR (MediaFormat.COLOR_*).
            AMediaFormat_setInt32(f, "color-standard", 1);
            AMediaFormat_setInt32(f, "color-range", video_.color.fullRange ? 1 : 2);
            AMediaFormat_setInt32(f, "color-transfer", 3);
            const media_status_t ms = AMediaCodec_configure(video__.codec, f, nullptr, nullptr,
                                                            AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
            AMediaFormat_delete(f);
            if (ms == AMEDIA_OK) {
                colorFormat_ = cf == kColorFormatNv12 ? kColorFormatNv12 : kColorFormatI420;
                configured = true;
                break;
            }
        }
        if (!configured) return fail(Errc::NotSupported, "encoder nao aceita essa resolucao/formato");
        stride_ = video_.width;
        sliceHeight_ = video_.height;
        if (auto fn = get_input_format_fn()) {
            if (AMediaFormat* in = fn(video__.codec)) {
                i32 v = 0;
                if (AMediaFormat_getInt32(in, "stride", &v) && v >= static_cast<i32>(video_.width)) stride_ = static_cast<u32>(v);
                if (AMediaFormat_getInt32(in, "slice-height", &v) && v >= static_cast<i32>(video_.height)) {
                    sliceHeight_ = static_cast<u32>(v);
                }
                AMediaFormat_delete(in);
            }
        }
        if (AMediaCodec_start(video__.codec) != AMEDIA_OK) return fail(Errc::IoError, "encoder de video nao iniciou");
        AUREA_LOG_INFO("export: %s %ux%u @%.2f %u bps, entrada %s stride %u fatia %u", mime, video_.width, video_.height,
                       video_.fps, video_.bitrateBps, colorFormat_ == kColorFormatNv12 ? "NV12" : "I420", stride_,
                       sliceHeight_);
        return OkStatus;
    }

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
        return OkStatus;
    }

    Status signal_eos(Track& t) noexcept {
        const u64 deadline = monotonic_ns() + 5'000'000'000ull;
        ssize_t idx = -1;
        while ((idx = AMediaCodec_dequeueInputBuffer(t.codec, 2000)) < 0) {
            if (const Status s = drain(t, false); !s.ok()) return s;
            if (monotonic_ns() > deadline) return Status{Errc::Timeout, "encoder nao aceitou o fim do fluxo"};
        }
        const i64 pts = t.audio ? lastAudioPts_ : lastVideoPts_;
        if (AMediaCodec_queueInputBuffer(t.codec, static_cast<size_t>(idx), 0, 0, static_cast<u64>(std::max<i64>(0, pts)),
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIA_OK) {
            return Status{Errc::IoError, "encoder recusou o fim do fluxo"};
        }
        return OkStatus;
    }

    /// Esvazia a saída do encoder. `wait` = espera um pouco por buffer (fim do
    /// fluxo); sem ele, só o que já está pronto.
    Status drain(Track& t, bool wait) noexcept {
        for (;;) {
            AMediaCodecBufferInfo info{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(t.codec, &info, wait ? 10000 : 0);
            if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return OkStatus;
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* f = AMediaCodec_getOutputFormat(t.codec);
                t.muxIndex = static_cast<i32>(AMediaMuxer_addTrack(muxer_, f));
                AMediaFormat_delete(f);
                if (t.muxIndex < 0) return Status{Errc::IoError, "muxer recusou a trilha"};
                t.formatKnown = true;
                if (const Status s = maybe_start_muxer(); !s.ok()) return s;
                continue;
            }
            if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (idx < 0) return Status{Errc::IoError, "encoder devolveu erro"};

            size_t cap = 0;
            u8* data = AMediaCodec_getOutputBuffer(t.codec, static_cast<size_t>(idx), &cap);
            const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
            if (data && info.size > 0 && !config) {
                if (t.audio) lastAudioPts_ = std::max<i64>(lastAudioPts_, info.presentationTimeUs);
                if (muxStarted_) {
                    const media_status_t ms = AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(t.muxIndex),
                                                                          data + info.offset, &info);
                    if (ms != AMEDIA_OK) {
                        AMediaCodec_releaseOutputBuffer(t.codec, static_cast<size_t>(idx), false);
                        return Status{Errc::IoError, "falha ao gravar no MP4"};
                    }
                } else {
                    Pending p;
                    p.audio = t.audio;
                    p.data.assign(data + info.offset, data + info.offset + info.size);
                    p.info = info;
                    p.info.offset = 0;
                    pending_.push_back(std::move(p));
                }
            }
            AMediaCodec_releaseOutputBuffer(t.codec, static_cast<size_t>(idx), false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                t.eos = true;
                return OkStatus;
            }
        }
    }

    Status maybe_start_muxer() noexcept {
        if (muxStarted_ || !video__.formatKnown || (hasAudio_ && !audio__.formatKnown)) return OkStatus;
        // Orientação: o vídeo já sai de pé (a composição é renderizada com a
        // proporção final), então nenhuma rotação no contêiner.
        if (AMediaMuxer_start(muxer_) != AMEDIA_OK) return Status{Errc::IoError, "muxer nao iniciou"};
        muxStarted_ = true;
        for (Pending& p : pending_) {
            const Track& t = p.audio ? audio__ : video__;
            if (AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(t.muxIndex), p.data.data(), &p.info) != AMEDIA_OK) {
                return Status{Errc::IoError, "falha ao gravar no MP4"};
            }
        }
        pending_.clear();
        return OkStatus;
    }

    Status fail(Errc code, const char* msg) noexcept {
        release(true);
        return Status{code, msg};
    }
    Status fail_status(const Status& s) noexcept {
        release(true);
        return s;
    }

    void release(bool deleteFile) noexcept {
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
            ::close(fd_);
            fd_ = -1;
            if (deleteFile && !path_.empty()) ::unlink(path_.c_str());
        }
        pending_.clear();
    }

    std::string path_;
    VideoStreamConfig video_{};
    AudioStreamConfig audio_{};
    bool hasAudio_ = false;
    int fd_ = -1;
    AMediaMuxer* muxer_ = nullptr;
    bool muxStarted_ = false;
    Track video__{};
    Track audio__{};
    i32 colorFormat_ = kColorFormatNv12;
    u32 stride_ = 0;
    u32 sliceHeight_ = 0;
    i64 lastVideoPts_ = 0;
    i64 lastAudioPts_ = 0;
    std::vector<Pending> pending_;
};

} // namespace

std::unique_ptr<ExportSink> make_mediacodec_export_sink(void*) {
    return std::make_unique<MediaCodecExportSink>();
}

} // namespace aurea::android
