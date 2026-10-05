// =============================================================================
//  Aurea / platform / android / MediaCodecSource.cpp
// =============================================================================
#include "MediaCodecSource.hpp"
#include "GlVideoBridge.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/platform/AndroidVideoPath.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/media/DecodedPlaneBounds.hpp"
#include "aurea/platform/AndroidVideoCompatibility.hpp"

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace aurea::android {
namespace {

// Chaves do MediaFormat como texto: as constantes AMEDIAFORMAT_KEY_COLOR_* e de
// crop só existem a partir da API 28, e o app roda desde a 26. O MediaFormat
// aceita a string em qualquer versão.
constexpr const char* kKeyColorStandard = "color-standard";
constexpr const char* kKeyColorRange    = "color-range";
constexpr const char* kKeyColorTransfer = "color-transfer";
constexpr const char* kKeyRotation      = "rotation-degrees";
constexpr const char* kKeyFrameRate     = "frame-rate";
constexpr const char* kKeyProfile       = "profile";
constexpr const char* kKeyPriority      = "priority";
constexpr const char* kKeyCropLeft      = "crop-left";
constexpr const char* kKeyCropTop       = "crop-top";
constexpr const char* kKeyCropRight     = "crop-right";
constexpr const char* kKeyCropBottom    = "crop-bottom";

/// Imagens do ImageReader. O cache do VideoSource segura `N - 5`: sobram as
/// que estão em voo na GPU (até 3), a que o decoder está escrevendo e uma de
/// folga. Sem a folga o codec trava esperando buffer livre.
constexpr i32 kMaxImages = 12;

// -----------------------------------------------------------------------------
// Origem do arquivo
// -----------------------------------------------------------------------------
struct SourceFd {
    int fd = -1;
    off64_t offset = 0;
    off64_t length = -1;

    SourceFd() = default;
    SourceFd(const SourceFd&) = delete;
    SourceFd& operator=(const SourceFd&) = delete;
    SourceFd(SourceFd&& o) noexcept : fd(o.fd), offset(o.offset), length(o.length) { o.fd = -1; }
    SourceFd& operator=(SourceFd&& o) noexcept {
        if (this != &o) {
            reset();
            fd = o.fd; offset = o.offset; length = o.length;
            o.fd = -1;
        }
        return *this;
    }
    ~SourceFd() { reset(); }
    void reset() noexcept {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
};

bool open_source(const char* path, FdOpener opener, void* ctx, SourceFd& out) {
    if (!path || !*path) return false;
    if (std::strncmp(path, "fd:", 3) == 0) {
        // "fd:<n>[:<offset>:<length>]" — o descritor é da plataforma; aqui
        // trabalhamos com uma cópia, que fecha junto com o decoder.
        char* end = nullptr;
        const long n = std::strtol(path + 3, &end, 10);
        if (end == path + 3 || n < 0) return false;
        out.fd = ::fcntl(static_cast<int>(n), F_DUPFD_CLOEXEC, 0);
        if (end && *end == ':') {
            out.offset = std::strtoll(end + 1, &end, 10);
            if (end && *end == ':') out.length = std::strtoll(end + 1, &end, 10);
        }
    } else if (std::strncmp(path, "content:", 8) == 0) {
        if (!opener) {
            AUREA_LOG_ERROR("URI de conteudo sem resolvedor de descritor");
            return false;
        }
        out.fd = opener(path, ctx);
    } else {
        const char* p = std::strncmp(path, "file://", 7) == 0 ? path + 7 : path;
        out.fd = ::open(p, O_RDONLY | O_CLOEXEC);
    }
    if (out.fd < 0) return false;
    if (out.length < 0) {
        struct stat st{};
        if (::fstat(out.fd, &st) != 0 || st.st_size <= 0) {
            out.reset();
            return false;
        }
        out.length = static_cast<off64_t>(st.st_size) - out.offset;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Formato
// -----------------------------------------------------------------------------
i32 get_i32(AMediaFormat* f, const char* key, i32 fallback) {
    int32_t v = 0;
    return AMediaFormat_getInt32(f, key, &v) ? v : fallback;
}

bool has_key(AMediaFormat* f, const char* key) {
    int32_t v = 0;
    return AMediaFormat_getInt32(f, key, &v);
}

u8 bit_depth_from(const char* mime, i32 profile) {
    if (!mime) return 8;
    // HEVC Main10 = 2, Main10 HDR10 = 0x1000, Main10 HDR10+ = 0x2000.
    if (std::strcmp(mime, "video/hevc") == 0 && (profile == 2 || profile == 0x1000 || profile == 0x2000)) return 10;
    // AVC High10 = 0x10.
    if (std::strcmp(mime, "video/avc") == 0 && profile == 0x10) return 10;
    // VP9 Profile2/3 (HDR incluídos): 0x4, 0x8, 0x1000, 0x2000, 0x4000, 0x8000.
    if (std::strcmp(mime, "video/x-vnd.on2.vp9") == 0 && profile >= 0x4 && profile != 0x1 && profile != 0x2) return 10;
    // AV1 Main10 = 0x2, HDR10 = 0x1000, HDR10+ = 0x2000.
    if (std::strcmp(mime, "video/av01") == 0 && (profile == 0x2 || profile == 0x1000 || profile == 0x2000)) return 10;
    return 8;
}

/// Cor do arquivo. Sem nenhuma das chaves, deduz pelo tamanho (BT.601 abaixo
/// de 720 linhas) — NUNCA assume BT.709 limitado para tudo.
VideoColorInfo color_from(AMediaFormat* f, u32 w, u32 h, u8 bitDepth, const VideoColorInfo* base) {
    VideoColorInfo c = base ? *base : VideoColorInfo::guess(w, h, bitDepth);
    c.bitDepth = bitDepth;
    const i32 standard = get_i32(f, kKeyColorStandard, 0);
    const i32 range = get_i32(f, kKeyColorRange, 0);
    const i32 transfer = get_i32(f, kKeyColorTransfer, 0);
    switch (standard) {
        case 1:  c.matrix = YCbCrMatrix::BT709;  c.primaries = ColorPrimaries::BT709;  break;
        case 2: case 3: case 4: case 5:
                 c.matrix = YCbCrMatrix::BT601;  c.primaries = ColorPrimaries::BT601;  break;
        case 6: case 7:
                 c.matrix = YCbCrMatrix::BT2020; c.primaries = ColorPrimaries::BT2020; break;
        default: break;
    }
    if (range == 1) c.fullRange = true;
    else if (range == 2) c.fullRange = false;
    switch (transfer) {
        case 1: c.transfer = TransferFunction::Linear; break;
        case 3: c.transfer = TransferFunction::SRGB;   break;   // SDR de vídeo
        case 6: c.transfer = TransferFunction::PQ;     break;
        case 7: c.transfer = TransferFunction::HLG;    break;
        default: break;
    }
    if (standard || range || transfer) c.fromStream = true;
    return c;
}

/// Primeira trilha de vídeo, ou -1.
i32 find_video_track(AMediaExtractor* ex, AMediaFormat** outFormat, const char** outMime) {
    const size_t n = AMediaExtractor_getTrackCount(ex);
    for (size_t i = 0; i < n; ++i) {
        AMediaFormat* f = AMediaExtractor_getTrackFormat(ex, i);
        const char* mime = nullptr;
        if (f && AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &mime) && mime
            && std::strncmp(mime, "video/", 6) == 0) {
            *outFormat = f;
            *outMime = mime;   // vive enquanto o formato viver
            return static_cast<i32>(i);
        }
        if (f) AMediaFormat_delete(f);
    }
    return -1;
}

/// fps da trilha: a chave, ou a mediana dos intervalos das primeiras amostras.
f64 frame_rate_of(AMediaExtractor* ex, AMediaFormat* f) {
    int32_t fi = 0;
    if (AMediaFormat_getInt32(f, kKeyFrameRate, &fi) && fi > 0) return static_cast<f64>(fi);
    float ff = 0.0f;
    if (AMediaFormat_getFloat(f, kKeyFrameRate, &ff) && ff > 0.0f) return static_cast<f64>(ff);
    std::vector<i64> t;
    t.reserve(32);
    for (int i = 0; i < 32; ++i) {
        const i64 s = AMediaExtractor_getSampleTime(ex);
        if (s < 0) break;
        t.push_back(s);
        if (!AMediaExtractor_advance(ex)) break;
    }
    AMediaExtractor_seekTo(ex, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    if (t.size() < 3) return 30.0;
    std::sort(t.begin(), t.end());   // ordem de decode ≠ ordem de exibição (quadros B)
    std::vector<i64> d;
    for (size_t i = 1; i < t.size(); ++i) if (t[i] > t[i - 1]) d.push_back(t[i] - t[i - 1]);
    if (d.empty()) return 30.0;
    std::nth_element(d.begin(), d.begin() + static_cast<long>(d.size() / 2), d.end());
    const i64 med = d[d.size() / 2];
    return med > 0 ? 1e6 / static_cast<f64>(med) : 30.0;
}

void fill_stream_info(AMediaExtractor* ex, AMediaFormat* f, const char* mime, VideoStreamInfo& v) {
    const u32 w = static_cast<u32>(std::max(0, get_i32(f, AMEDIAFORMAT_KEY_WIDTH, 0)));
    const u32 h = static_cast<u32>(std::max(0, get_i32(f, AMEDIAFORMAT_KEY_HEIGHT, 0)));
    v.codedWidth = w;
    v.codedHeight = h;
    const i32 rot = ((get_i32(f, kKeyRotation, 0) % 360) + 360) % 360;
    v.rotation = (rot == 90 || rot == 180 || rot == 270) ? static_cast<u32>(rot) : 0u;
    int64_t dur = 0;
    v.durationUs = AMediaFormat_getInt64(f, AMEDIAFORMAT_KEY_DURATION, &dur) ? dur : 0;
    v.fps = frame_rate_of(ex, f);
    const u8 depth = bit_depth_from(mime, get_i32(f, kKeyProfile, 0));
    v.color = color_from(f, w, h, depth, nullptr);
    std::snprintf(v.codec, sizeof(v.codec), "%s", mime ? mime : "");
}

// -----------------------------------------------------------------------------
// ImageReader
// -----------------------------------------------------------------------------
/// Dono do AImageReader. Compartilhado entre o decoder e cada frame vivo: o
/// leitor só é destruído quando o último AImage dele foi devolvido — um frame
/// ainda em voo na GPU depois do decoder fechar não aponta para memória morta.
struct ReaderState {
    AImageReader* reader = nullptr;
    ANativeWindow* window = nullptr;   // pertence ao reader
    std::mutex mutex;
    std::condition_variable cv;
    u64 imageGeneration = 0;

    ~ReaderState() {
        if (reader) AImageReader_delete(reader);
    }

    static void on_image(void* ctx, AImageReader*) {
        auto* s = static_cast<ReaderState*>(ctx);
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            ++s->imageGeneration;
        }
        s->cv.notify_all();
    }
};

class CodecFrame final : public DecodedFrame {
public:
    ~CodecFrame() override {
        if (image) AImage_delete(image);
    }
    AImage* image = nullptr;
    std::shared_ptr<ReaderState> owner;
    std::vector<u8> compact;   // só para layouts de plano que o renderer não lê direto
};

/// Quadro do caminho GL do driver: um alvo RGBA8 (AHardwareBuffer) já com a
/// região visível; o AImage do decoder foi devolvido logo depois do passe GL.
class GlFrame final : public DecodedFrame {
public:
    std::shared_ptr<const GlVideoBridge::Target> target;
};

// Nome do codec: AMediaCodec_getName é da API 28; resolvido em runtime.
using GetNameFn = media_status_t (*)(AMediaCodec*, char**);
using ReleaseNameFn = void (*)(AMediaCodec*, char*);

void codec_name(AMediaCodec* codec, char* out, size_t cap, bool& hardware) {
    static GetNameFn getName = reinterpret_cast<GetNameFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getName"));
    static ReleaseNameFn releaseName = reinterpret_cast<ReleaseNameFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_releaseName"));
    out[0] = '\0';
    hardware = true;
    if (!getName) return;
    char* name = nullptr;
    if (getName(codec, &name) == AMEDIA_OK && name) {
        std::snprintf(out, cap, "%s", name);
        if (releaseName) releaseName(codec, name);
        hardware = !(std::strncmp(out, "OMX.google.", 11) == 0 || std::strncmp(out, "c2.android.", 11) == 0
                     || std::strstr(out, ".sw.") != nullptr);
    }
}

const char* software_decoder_for(const char* mime) {
    return android::software_video_decoder(mime ? mime : "");
}

// -----------------------------------------------------------------------------
// Decoder
// -----------------------------------------------------------------------------
class MediaCodecDecoder final : public VideoDecoderBackend {
public:
    MediaCodecDecoder(SourceFd fd, bool zeroCopy, bool thumbnail, bool softwareOnly, bool driverGl = false,
                      bool diagnosticSoftwareFallback = false, bool diagnosticVideoPixels = false)
        : fd_(std::move(fd)), zeroCopy_((zeroCopy || driverGl) && !softwareOnly), thumbnail_(thumbnail),
          softwareFallback_(softwareOnly), glPath_(driverGl && !softwareOnly && !thumbnail),
          diagnosticSoftwareFallback_(diagnosticSoftwareFallback && !softwareOnly && !thumbnail),
          diagnosticVideoPixels_(diagnosticVideoPixels && !thumbnail) {}
    ~MediaCodecDecoder() override {
        destroy_codec();
        bridge_.reset();
    }

    Status open() {
        if (glPath_) {
            // Caminho GL do driver (o do app antigo): contexto EGL próprio. Sem
            // ele (extensão ausente, EGL recusado), planos pela CPU desde já.
            bridge_ = GlVideoBridge::create(diagnosticVideoPixels_);
            if (!bridge_) {
                glPath_ = false;
                zeroCopy_ = false;
            }
        }
        if (const Status s = create_codec(); !s.ok()) return s;
        if (bridge_) bridge_->set_max_live_targets(driver_gl_live_frames(info_.codedWidth, info_.codedHeight));
        if (!thumbnail_) {
            AUREA_LOG_INFO("video: %s (%s %ux%u, %s)", video_path_name(current_path()),
                           info_.decoderName[0] ? info_.decoderName : "?", info_.codedWidth, info_.codedHeight,
                           info_.hardwareDecoder ? "hardware" : "software");
            scan_keyframes();
        }
        return OkStatus;
    }

    VideoPath current_path() const noexcept {
        return glPath_ ? VideoPath::DriverGl : softwareFallback_ ? VideoPath::SoftwarePlanes : VideoPath::CpuPlanes;
    }

    const VideoStreamInfo& info() const noexcept override { return info_; }
    u32 max_live_frames() const noexcept override {
        return bridge_ && glPath_ ? bridge_->max_live_targets() : static_cast<u32>(kMaxImages);
    }
    i64 keyframe_interval_us() const noexcept override { return keyframeUs_; }

    Status seek_to_keyframe(i64 targetUs) noexcept override {
        clear_rendered_image();
        pendingOutput_ = -1;
        pendingFrame_.reset();
        pendingEos_ = false;
        nextDeliveryUs_ = std::max<i64>(0, targetUs);
        if (!codec_) {
            if (const Status s = create_codec(); !s.ok()) return s;
        }
        if (AMediaExtractor_seekTo(ex_, std::max<i64>(0, targetUs), AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK) {
            return Status{Errc::DecodeFailed, "seek do extractor falhou"};
        }
        if (AMediaCodec_flush(codec_) != AMEDIA_OK) {
            // Codec em estado ruim (erro de hardware): recria do zero.
            destroy_codec();
            if (const Status s = create_codec(); !s.ok()) return s;
            if (AMediaExtractor_seekTo(ex_, std::max<i64>(0, targetUs), AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK) {
                return Status{Errc::DecodeFailed, "seek do extractor recriado falhou"};
            }
        }
        inputEos_ = false;
        outputEos_ = false;
        drain_stray_images();
        return OkStatus;
    }

    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& endOfStream) noexcept override {
        Status result = legacyLookahead_ ? next_image_timed_frame(deliverFromUs, out, outPtsUs, endOfStream)
                                        : next_timed_frame(deliverFromUs, out, outPtsUs, endOfStream);
        // Some surface decoders require each slot to be released before they
        // produce another. Preserve hardware decoding with image lookahead on
        // those drivers instead of mistaking buffer backpressure for codec failure.
        if (!result.ok() && !legacyLookahead_ && pendingOutput_ >= 0 && result.code() == Errc::Timeout) {
            const i64 target = std::max(deliverFromUs, nextDeliveryUs_);
            legacyLookahead_ = true;
            AUREA_LOG_WARN("decoder %s requires immediate output release; using image lookahead", info_.decoderName);
            destroy_codec();
            if (const Status status = create_codec(); !status.ok()) return status;
            if (const Status status = seek_to_keyframe(target); !status.ok()) return status;
            result = next_image_timed_frame(target, out, outPtsUs, endOfStream);
        }
        // Caminho GL do driver falhou neste vídeo (EGLImage recusado, FBO,
        // codec que não anda com saída PRIVATE): planos pela CPU com o MESMO
        // decoder de hardware, a partir do mesmo instante. Só para a frente.
        VideoPath fallbackPath{};
        if (!result.ok() && glPath_
            && next_video_path(VideoPath::DriverGl, result.code(), info_.hardwareDecoder,
                               software_decoder_for(info_.codec) != nullptr, fallbackPath)) {
            const i64 target = std::max(deliverFromUs, nextDeliveryUs_);
            AUREA_LOG_WARN("video: caminho GL do driver falhou (%s); %s em %lld us", result.message().data(),
                           video_path_name(fallbackPath), static_cast<long long>(target));
            glPath_ = false;
            zeroCopy_ = false;
            destroy_codec();
            bridge_.reset();   // quadros ainda vivos seguram os próprios alvos
            if (const Status opened = create_codec(); !opened.ok()) return opened;
            if (const Status seek = seek_to_keyframe(target); !seek.ok()) return seek;
            result = legacyLookahead_ ? next_image_timed_frame(target, out, outPtsUs, endOfStream)
                                      : next_timed_frame(target, out, outPtsUs, endOfStream);
        }
        if (result.ok() || !software_decoder_for(info_.codec)
            || !android::video_software_fallback(result.code(), zeroCopy_, info_.hardwareDecoder, softwareFallback_)) {
            return result;
        }
        // Some drivers accept configuration but stall after flush/recreation;
        // in readable-planes mode (Samsung / safe video mode) a vendor codec can
        // also deliver YUV_420_888 images whose planes the CPU cannot map
        // (video_software_fallback). Retry that exact presentation interval
        // once with the platform software codec; never pretend an empty frame
        // is success or silently restart at 0.
        const i64 target = std::max(deliverFromUs, nextDeliveryUs_);
        AUREA_LOG_WARN("decoder %s falhou (%s%s); tentando software em %lld us", info_.decoderName,
            result.message().data(), zeroCopy_ ? "" : ", planos pela CPU", static_cast<long long>(target));
        softwareFallback_ = true;
        destroy_codec();
        if (const Status opened = create_codec(); !opened.ok()) return opened;
        if (const Status seek = seek_to_keyframe(target); !seek.ok()) return seek;
        return legacyLookahead_ ? next_image_timed_frame(target, out, outPtsUs, endOfStream)
                                : next_timed_frame(target, out, outPtsUs, endOfStream);
    }

    Status next_image_timed_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& endOfStream) noexcept {
        out.reset();
        endOfStream = false;
        if (!pendingFrame_) {
            if (const Status s = next_raw_frame(pendingFrame_, outPtsUs, pendingEos_); !s.ok()) return s;
            if (!pendingFrame_) { endOfStream = pendingEos_; return OkStatus; }
        }
        FrameRef next;
        i64 nextPts = pendingFrame_->ptsUs;
        bool eos = pendingEos_;
        if (!pendingEos_) {
            if (const Status s = next_raw_frame(next, nextPts, eos); !s.ok()) return s;
        }
        // PTS repetido/para trás (gravador de tela, remux ruim): o quadro
        // duplicado sai, o vídeo segue — falhar aqui travava o export inteiro.
        for (u32 dropped = 0; next && next->ptsUs <= pendingFrame_->ptsUs; ++dropped) {
            if (dropped >= 64) return Status{Errc::DecodeFailed, "PTS de video fora de ordem"};
            next.reset();
            if (eos) break;
            if (const Status s = next_raw_frame(next, nextPts, eos); !s.ok()) return s;
        }
        FrameRef current = std::move(pendingFrame_);
        current->durationUs = next ? next->ptsUs - current->ptsUs
            : info_.durationUs > current->ptsUs ? info_.durationUs - current->ptsUs
            : std::max<i64>(1, static_cast<i64>(1e6 / std::max(1.0, info_.fps)));
        outPtsUs = current->ptsUs;
        nextDeliveryUs_ = current->ptsUs + current->durationUs;
        pendingFrame_ = std::move(next);
        pendingEos_ = eos;
        endOfStream = eos && !pendingFrame_;
        if (outPtsUs >= deliverFromUs || current->covers(deliverFromUs)) out = std::move(current);
        return OkStatus;
    }

    Status next_raw_frame(FrameRef& out, i64& outPtsUs, bool& endOfStream) noexcept {
        endOfStream = false;
        out.reset();
        if (renderedPending_) return finish_rendered_image(out, outPtsUs, endOfStream);
        if (!codec_) {
            if (const Status s = create_codec(); !s.ok()) return s;
        }
        if (outputEos_) {
            endOfStream = true;
            outPtsUs = lastPts_;
            return OkStatus;
        }
        const u64 deadline = monotonic_ns() + 3'000'000'000ull;
        for (;;) {
            if (const Status s = feed_input(); !s.ok()) return s;
            AMediaCodecBufferInfo bi{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_, &bi, 4000);
            if (idx >= 0) {
                const bool eos = (bi.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                if (eos && bi.size <= 0) {
                    AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(idx), false);
                    outputEos_ = true;
                    endOfStream = true;
                    outPtsUs = lastPts_;
                    return OkStatus;
                }
                const i64 pts = bi.presentationTimeUs;
                lastPts_ = pts;
                outPtsUs = pts;
                if (eos) {
                    outputEos_ = true;
                    endOfStream = true;
                }
                // Explicit timestamp lets us reject a late image from a previous seek.
                if (AMediaCodec_releaseOutputBufferAtTime(codec_, static_cast<size_t>(idx), pts * 1000) != AMEDIA_OK) {
                    return Status{Errc::DecodeFailed, "releaseOutputBufferAtTime falhou"};
                }
                renderedPending_ = true;
                renderedPts_ = pts; renderedDuration_ = 0; renderedEos_ = eos;
                return finish_rendered_image(out, outPtsUs, endOfStream);
            }
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                update_output_format();
                continue;
            }
            if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                if (monotonic_ns() > deadline) return Status{Errc::Timeout, "decoder parado sem entregar frame"};
                continue;
            }
            return Status{Errc::DecodeFailed, "dequeueOutputBuffer falhou"};
        }
    }

    // Keep one codec output slot for PTS lookahead, not an ImageReader image.
    // Late/preroll frames are released with render=false: they never cross the
    // Surface/ImageReader boundary or allocate/copy GPU/CPU image planes.
    Status next_timed_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& endOfStream) noexcept {
        out.reset(); endOfStream = false;
        if (renderedPending_) return finish_rendered_image(out, outPtsUs, endOfStream);
        if (pendingOutput_ < 0) {
            if (const Status status = next_raw_buffer(pendingOutput_, pendingPts_, pendingEos_); !status.ok()) return status;
            if (pendingOutput_ < 0) { endOfStream = pendingEos_; outPtsUs = lastPts_; return OkStatus; }
        }
        ssize_t next = -1;
        i64 nextPts = pendingPts_;
        bool eos = pendingEos_;
        if (!pendingEos_) {
            if (const Status status = next_raw_buffer(next, nextPts, eos); !status.ok()) return status;
        }
        // PTS repetido/para trás (gravador de tela, remux ruim): o buffer
        // duplicado volta ao codec sem render e o vídeo segue. Falhar aqui
        // fazia o decoder repetir seek+erro no mesmo ponto até o export morrer.
        for (u32 dropped = 0; next >= 0 && nextPts <= pendingPts_; ++dropped) {
            if (dropped >= 64) {
                AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(next), false);
                return Status{Errc::DecodeFailed, "PTS de video fora de ordem"};
            }
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(next), false);
            next = -1;
            if (eos) break;
            if (const Status status = next_raw_buffer(next, nextPts, eos); !status.ok()) return status;
        }
        if (next < 0) nextPts = pendingPts_;
        const ssize_t current = pendingOutput_;
        const i64 pts = pendingPts_;
        pendingOutput_ = next; pendingPts_ = nextPts; pendingEos_ = eos;
        const i64 duration = next >= 0 ? nextPts - pts : info_.durationUs > pts ? info_.durationUs - pts
            : std::max<i64>(1, static_cast<i64>(1e6 / std::max(1.0, info_.fps)));
        outPtsUs = pts;
        endOfStream = eos && next < 0;
        if (pts < deliverFromUs && pts + duration <= deliverFromUs) {
            nextDeliveryUs_ = pts + duration;
            return AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(current), false) == AMEDIA_OK
                ? OkStatus : Status{Errc::DecodeFailed, "descarte de frame atrasado falhou"};
        }
        if (AMediaCodec_releaseOutputBufferAtTime(codec_, static_cast<size_t>(current), pts * 1000) != AMEDIA_OK)
            return Status{Errc::DecodeFailed, "releaseOutputBufferAtTime falhou"};
        renderedPending_ = true;
        renderedPts_ = pts; renderedDuration_ = duration; renderedEos_ = endOfStream;
        return finish_rendered_image(out, outPtsUs, endOfStream);
    }

    // A released codec output may still be waiting for an ImageReader slot or
    // an RGBA target. Preserve it across backpressure; never decode past it.
    Status finish_rendered_image(FrameRef& out, i64& pts, bool& eos) noexcept {
        pts = renderedPts_; eos = renderedEos_;
        if (!renderedImage_) {
            if (const Status s = acquire_image(renderedPts_, renderedImage_); !s.ok()) return s;
        }
        const Status s = wrap(renderedImage_, renderedPts_, out);
        if (s.code() == Errc::BudgetExceeded) return s; // wrap retains this AImage
        renderedImage_ = nullptr; // wrap consumed it (success or permanent failure)
        renderedPending_ = false;
        if (s.ok() && out && renderedDuration_ > 0) {
            out->durationUs = renderedDuration_;
            nextDeliveryUs_ = renderedPts_ + renderedDuration_;
        }
        return s;
    }

    void clear_rendered_image() noexcept {
        if (renderedImage_) AImage_delete(renderedImage_);
        renderedImage_ = nullptr;
        renderedPending_ = false;
    }

    Status next_raw_buffer(ssize_t& out, i64& outPtsUs, bool& endOfStream) noexcept {
        out = -1; endOfStream = false;
        if (!codec_) {
            if (const Status status = create_codec(); !status.ok()) return status;
        }
        if (outputEos_) { endOfStream = true; outPtsUs = lastPts_; return OkStatus; }
        const u64 deadline = monotonic_ns() + 3'000'000'000ull;
        for (;;) {
            if (const Status status = feed_input(); !status.ok()) return status;
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, 4000);
            if (index >= 0) {
                const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                if (eos) { outputEos_ = true; endOfStream = true; }
                if (eos && info.size <= 0) {
                    AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false);
                    outPtsUs = lastPts_; return OkStatus;
                }
                out = index; outPtsUs = lastPts_ = info.presentationTimeUs;
                return OkStatus;
            }
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) { update_output_format(); continue; }
            if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                if (monotonic_ns() > deadline) return Status{Errc::Timeout, "decoder parado sem entregar frame"};
                continue;
            }
            return Status{Errc::DecodeFailed, "dequeueOutputBuffer falhou"};
        }
    }

    void suspend() noexcept override { destroy_codec(); }

    Status resume() noexcept override {
        if (codec_) return OkStatus;
        return create_codec();
    }

private:
    Status create_reader(u32 w, u32 h) {
        auto state = std::make_shared<ReaderState>();
        const i32 format = zeroCopy_ ? AIMAGE_FORMAT_PRIVATE : AIMAGE_FORMAT_YUV_420_888;
        const u64 usage = zeroCopy_ ? AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE : AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
        if (AImageReader_newWithUsage(static_cast<int32_t>(w), static_cast<int32_t>(h), format, usage,
                                      kMaxImages, &state->reader) != AMEDIA_OK || !state->reader) {
            state->reader = nullptr;
            return Status{Errc::UnsupportedFormat, "AImageReader nao criado"};
        }
        AImageReader_ImageListener listener{state.get(), &ReaderState::on_image};
        AImageReader_setImageListener(state->reader, &listener);
        if (AImageReader_getWindow(state->reader, &state->window) != AMEDIA_OK || !state->window) {
            return Status{Errc::UnsupportedFormat, "janela do AImageReader indisponivel"};
        }
        reader_ = std::move(state);
        return OkStatus;
    }

    Status create_codec() {
        ex_ = AMediaExtractor_new();
        if (!ex_) return Status{Errc::OutOfMemory, "AMediaExtractor_new"};
        if (AMediaExtractor_setDataSourceFd(ex_, fd_.fd, fd_.offset, fd_.length) != AMEDIA_OK) {
            destroy_codec();
            return Status{Errc::UnsupportedFormat, "container nao reconhecido"};
        }
        AMediaFormat* format = nullptr;
        const char* mime = nullptr;
        track_ = find_video_track(ex_, &format, &mime);
        if (track_ < 0) {
            destroy_codec();
            return Status{Errc::UnsupportedFormat, "sem trilha de video"};
        }
        AMediaExtractor_selectTrack(ex_, static_cast<size_t>(track_));
        if (info_.codedWidth == 0) fill_stream_info(ex_, format, mime, info_);
        AMediaExtractor_seekTo(ex_, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);

        // A rotação do container é aplicada no shader. Deixá-la no formato faria
        // o codec marcar o buffer com uma transformação que o AImage não expõe.
        AMediaFormat_setInt32(format, kKeyRotation, 0);
        AMediaFormat_setInt32(format, kKeyPriority, thumbnail_ ? 1 : 0);

        Status result = OkStatus;
        Status readerStatus = OkStatus;
        // SIGSEGV in vendor planes cannot be caught and retried. In software
        // mode do not create a decoder by MIME, even when no AOSP codec exists.
        if (!softwareFallback_ && !diagnosticSoftwareFallback_) {
            codec_ = AMediaCodec_createDecoderByType(mime);
            if (codec_ && !configure_and_start(format, readerStatus)) {
                AMediaCodec_delete(codec_);
                codec_ = nullptr;
            }
        }
        if (!codec_) {
            for (bool legacy : {false, true}) {
                const char* sw = android::software_video_decoder(mime, legacy);
                if (!sw) continue;
                codec_ = AMediaCodec_createCodecByName(sw);
                if (codec_ && configure_and_start(format, readerStatus, true)) {
                    softwareFallback_ = true;
                    break;
                }
                if (codec_) AMediaCodec_delete(codec_);
                codec_ = nullptr;
            }
            if (!codec_) result = readerStatus.ok()
                ? Status{Errc::UnsupportedCodec, "nenhum decoder seguro aceitou o video"} : readerStatus;
        }
        AMediaFormat_delete(format);
        if (!result.ok()) {
            destroy_codec();
            return result;
        }
        codec_name(codec_, info_.decoderName, sizeof(info_.decoderName), info_.hardwareDecoder);
        if (softwareFallback_) info_.hardwareDecoder = false;
        // gfxstream/goldfish has a single outstanding surface output slot.
        if (std::strstr(info_.decoderName, "goldfish")) legacyLookahead_ = true;
        info_.preciseFrameTiming = true;
        inputEos_ = false;
        outputEos_ = false;
        AUREA_LOG_INFO("decoder %s (%s, %s) %ux%u rot %u, %s", info_.decoderName[0] ? info_.decoderName : "?",
                       info_.hardwareDecoder ? "hardware" : "software", glPath_ ? "caminho GL do driver" : zeroCopy_ ? "zero-copy" : "planos na CPU",
                       info_.codedWidth, info_.codedHeight, info_.rotation, info_.codec);
        return OkStatus;
    }

    bool configure_and_start(AMediaFormat* format, Status& readerStatus, bool knownSoftware = false) {
        char selectedName[128]{};
        bool hardware = true;
        codec_name(codec_, selectedName, sizeof(selectedName), hardware);
        if (knownSoftware || !hardware) {
            // Select the reader AFTER the codec: a MIME lookup can itself
            // return an AOSP software decoder. Its PRIVATE/EGL image may be
            // sampled with BT.601 even when the stream declares BT.709.
            // Read YUV planes and let the shared renderer use stream color
            // metadata instead. Hardware codecs retain their selected path.
            softwareFallback_ = true;
            glPath_ = false;
            zeroCopy_ = false;
            bridge_.reset(); // outstanding RGBA frames own their targets
        }
        // A failed configure/start attempt must not lend its output queue to
        // the next codec. Existing frames retain their own ReaderState.
        reader_.reset();
        const u32 w = info_.codedWidth ? info_.codedWidth : 16;
        const u32 h = info_.codedHeight ? info_.codedHeight : 16;
        readerStatus = create_reader(w, h);
        if (!readerStatus.ok()) return false;
        if (AMediaCodec_configure(codec_, format, reader_->window, nullptr, 0) != AMEDIA_OK) return false;
        return AMediaCodec_start(codec_) == AMEDIA_OK;
    }

    void destroy_codec() noexcept {
        clear_rendered_image();
        pendingOutput_ = -1;
        pendingFrame_.reset();
        pendingEos_ = false;
        if (codec_) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
        }
        if (ex_) {
            AMediaExtractor_delete(ex_);
            ex_ = nullptr;
        }
        track_ = -1;
        // Frames in flight retain their ReaderState. A new codec must get a
        // new queue, never images or callback credits from the retired codec.
        reader_.reset();
        // Os buffers do leitor antigo não voltam: as imagens EGL deles saem.
        if (bridge_) bridge_->forget_sources();
    }

    /// Intervalo entre quadros-chave, pelas primeiras amostras de sync. Decide
    /// quando andar para a frente é mais barato que um seek.
    void scan_keyframes() {
        i64 first = -1, gap = 0;
        const u64 deadline = monotonic_ns() + 50'000'000ull;
        for (int i = 0; i < 1200; ++i) {
            if (monotonic_ns() >= deadline) break;
            const i64 t = AMediaExtractor_getSampleTime(ex_);
            if (t < 0) break;
            if (AMediaExtractor_getSampleFlags(ex_) & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC) {
                if (first >= 0 && t > first) {
                    gap = std::max(gap, t - first);
                    if (gap > 0 && i > 300) break;
                }
                first = t;
            }
            if (!AMediaExtractor_advance(ex_)) break;
        }
        AMediaExtractor_seekTo(ex_, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        if (gap > 0) keyframeUs_ = std::clamp<i64>(gap, 33'000, 10'000'000);
    }

    Status feed_input() {
        while (!inputEos_) {
            const ssize_t in = AMediaCodec_dequeueInputBuffer(codec_, 0);
            if (in == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return OkStatus;
            if (in < 0) return Status{Errc::DecodeFailed, "dequeueInputBuffer falhou"};
            size_t cap = 0;
            uint8_t* buf = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(in), &cap);
            if (!buf || cap == 0) return Status{Errc::DecodeFailed, "buffer de entrada indisponivel"};
            const ssize_t n = AMediaExtractor_readSampleData(ex_, buf, cap);
            if (n < 0) {
                if (AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in), 0, 0, 0,
                                                AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIA_OK) {
                    return Status{Errc::DecodeFailed, "queueInputBuffer EOS falhou"};
                }
                inputEos_ = true;
                return OkStatus;
            }
            if (static_cast<size_t>(n) > cap) return Status{Errc::DecodeFailed, "amostra excede buffer de entrada"};
            const i64 t = AMediaExtractor_getSampleTime(ex_);
            if (AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in), 0, static_cast<size_t>(n),
                                            static_cast<uint64_t>(std::max<i64>(0, t)), 0) != AMEDIA_OK) {
                return Status{Errc::DecodeFailed, "queueInputBuffer falhou"};
            }
            AMediaExtractor_advance(ex_);
        }
        return OkStatus;
    }

    void update_output_format() {
        AMediaFormat* f = AMediaCodec_getOutputFormat(codec_);
        if (!f) return;
        const i32 w = get_i32(f, AMEDIAFORMAT_KEY_WIDTH, 0);
        const i32 h = get_i32(f, AMEDIAFORMAT_KEY_HEIGHT, 0);
        if (has_key(f, kKeyCropRight) && has_key(f, kKeyCropBottom)) {
            outCrop_[0] = get_i32(f, kKeyCropLeft, 0);
            outCrop_[1] = get_i32(f, kKeyCropTop, 0);
            outCrop_[2] = get_i32(f, kKeyCropRight, 0);
            outCrop_[3] = get_i32(f, kKeyCropBottom, 0);
        }
        // A cor do bitstream (VUI) é mais confiável que a do container.
        info_.color = color_from(f, static_cast<u32>(w > 0 ? w : 0), static_cast<u32>(h > 0 ? h : 0),
                                 info_.color.bitDepth, &info_.color);
        if (diagnosticSoftwareFallback_ || diagnosticVideoPixels_) {
            AUREA_LOG_INFO("diagnostico formato video: codec=%s path=%s standard=%d range=%d transfer=%d dataspace=%d tamanho=%dx%d",
                          info_.decoderName, glPath_ ? "GL" : "planos", get_i32(f, kKeyColorStandard, 0), get_i32(f, kKeyColorRange, 0),
                          get_i32(f, kKeyColorTransfer, 0), get_i32(f, "android._dataspace", 0), w, h);
        }
        AMediaFormat_delete(f);
    }

    Status acquire_image(i64 ptsUs, AImage*& out) {
        ReaderState& r = *reader_;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        for (;;) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return Status{Errc::Timeout, "frame renderizado nao chegou ao ImageReader"};
            }
            // Notifications are wakeups, not a count of queued images. Query
            // the queue first; drain/flush can race with an old notification.
            u64 observedGeneration = 0;
            {
                std::lock_guard<std::mutex> lock(r.mutex);
                observedGeneration = r.imageGeneration;
            }
            // Never hold the listener lock across a native ImageReader call.
            const media_status_t s = AImageReader_acquireNextImage(r.reader, &out);
            if (s == AMEDIA_OK && out) {
                int64_t timestampNs = 0;
                const media_status_t ts = AImage_getTimestamp(out, &timestampNs);
                if (ts == AMEDIA_OK && timestampNs == ptsUs * 1000) return OkStatus;
                AImage_delete(out);
                out = nullptr;
                if (ts != AMEDIA_OK) return Status{Errc::DecodeFailed, "timestamp do AImage indisponivel"};
                continue;
            }
            if (s == AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED) {
                return Status{Errc::BudgetExceeded, "todas as imagens do ImageReader estao presas"};
            }
            if (s != AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) {
                return Status{Errc::DecodeFailed, "acquireNextImage falhou"};
            }
            std::unique_lock<std::mutex> lock(r.mutex);
            if (!r.cv.wait_until(lock, deadline, [&] { return r.imageGeneration != observedGeneration; })) {
                return Status{Errc::Timeout, "frame renderizado nao chegou ao ImageReader"};
            }
        }
    }

    void drain_stray_images() {
        // Depois de um erro no meio do caminho, pode ter sobrado imagem na fila
        // do leitor; ela seria entregue com o pts errado no próximo pedido.
        ReaderState& r = *reader_;
        for (;;) {
            AImage* img = nullptr;
            if (AImageReader_acquireNextImage(r.reader, &img) != AMEDIA_OK || !img) break;
            AImage_delete(img);
        }
    }

    /// Caminho GL do driver: AImage PRIVATE → passe GL → alvo RGBA. O AImage
    /// volta ao decoder aqui, salvo pressão temporária: o caller guarda a imagem.
    Status wrap_driver_gl(AImage* image, i64 pts, FrameRef& out) {
        AHardwareBuffer* hb = nullptr;
        if (AImage_getHardwareBuffer(image, &hb) != AMEDIA_OK || !hb) {
            AImage_delete(image);
            return Status{Errc::UnsupportedFeature, "AImage sem AHardwareBuffer"};
        }
        AHardwareBuffer_Desc d{};
        AHardwareBuffer_describe(hb, &d);
        AImageCropRect crop{0, 0, 0, 0};
        AImage_getCropRect(image, &crop);
        const i32 imageCrop[4] = {crop.left, crop.top, crop.right, crop.bottom};
        media::VisibleRegion vis;
        ExternalQuad quad;
        if (!media::visible_region(d.width, d.height, imageCrop, outCrop_, info_.codedWidth, info_.codedHeight, vis)
            || !external_quad(d.width, d.height, vis, 4096, quad)) {
            AImage_delete(image);
            return Status{Errc::UnsupportedFormat, "crop fora do buffer decodificado"};
        }
        std::shared_ptr<const GlVideoBridge::Target> target;
        GlVideoBridge::DiagnosticPixels pixels;
        const bool probe = diagnosticVideoPixels_ && pts == 1'500'000;
        const Status s = bridge_->convert(hb, quad, info_.color, target, probe ? &pixels : nullptr);
        if (s.code() == Errc::BudgetExceeded) return s;
        if (probe && s.ok()) {
            // AImage dataspace is public from API34 only. Resolve it lazily so
            // the API26 minimum and API33 device remain supported; -1 = unknown.
            using GetDataSpace = media_status_t (*)(const AImage*, int32_t*);
            static const auto getDataSpace = reinterpret_cast<GetDataSpace>(dlsym(RTLD_DEFAULT, "AImage_getDataSpace"));
            int32_t dataSpace = -1;
            if (getDataSpace && getDataSpace(image, &dataSpace) != AMEDIA_OK) dataSpace = -1;
            const auto* p = pixels.rgba;
            AUREA_LOG_INFO("diagnostico FBO RGBA: codec=%s pts=%lld source=%p target=%p size=%ux%u matrix=%u range=%u transfer=%u dataspace=%d valid=%d pixels=%u,%u,%u,%u|%u,%u,%u,%u|%u,%u,%u,%u|%u,%u,%u,%u",
                info_.decoderName, static_cast<long long>(pts), static_cast<void*>(hb), static_cast<void*>(target->buffer),
                quad.width, quad.height, static_cast<unsigned>(info_.color.matrix), info_.color.fullRange ? 1u : 0u,
                static_cast<unsigned>(info_.color.transfer), dataSpace, pixels.valid ? 1 : 0,
                p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
        }
        AImage_delete(image);
        if (!s.ok()) return s;
        auto* f = new (std::nothrow) GlFrame();
        if (!f) return Status{Errc::OutOfMemory, "sem memoria para o quadro"};
        f->target = target;
        f->ptsUs = pts;
        f->rotation = info_.rotation;
        f->color = info_.color;
        f->width = target->width;
        f->height = target->height;
        f->visibleWidth = target->width;
        f->visibleHeight = target->height;
        f->hardwareBuffer = target->buffer;
        f->bufferId = reinterpret_cast<u64>(target->buffer);
        f->format = PixelFormat::Opaque;
        out = FrameRef::adopt(f);
        return OkStatus;
    }

    Status wrap(AImage* image, i64 pts, FrameRef& out) {
        if (glPath_ && bridge_) return wrap_driver_gl(image, pts, out);
        auto* f = new (std::nothrow) CodecFrame();
        if (!f) {
            AImage_delete(image);
            return Status{Errc::OutOfMemory, "sem memoria para o quadro"};
        }
        f->image = image;
        f->owner = reader_;
        f->ptsUs = pts;
        f->rotation = info_.rotation;
        f->color = info_.color;

        int32_t iw = 0, ih = 0;
        AImage_getWidth(image, &iw);
        AImage_getHeight(image, &ih);
        AImageCropRect crop{0, 0, 0, 0};
        AImage_getCropRect(image, &crop);

        if (zeroCopy_) {
            AHardwareBuffer* hb = nullptr;
            if (AImage_getHardwareBuffer(image, &hb) != AMEDIA_OK || !hb) {
                f->release();
                return Status{Errc::UnsupportedFeature, "AImage sem AHardwareBuffer"};
            }
            AHardwareBuffer_Desc d{};
            AHardwareBuffer_describe(hb, &d);
            f->width = d.width;
            f->height = d.height;
            f->hardwareBuffer = hb;
            f->bufferId = reinterpret_cast<u64>(hb);
            f->format = info_.color.bitDepth > 8 ? PixelFormat::P010 : PixelFormat::Opaque;
        } else {
            f->width = static_cast<u32>(std::max(0, iw));
            f->height = static_cast<u32>(std::max(0, ih));
            if (!planes_from(image, *f)) {
                // Readable planes the CPU cannot read as 4:2:0: said out loud
                // (it used to be a silent black layer) and reported as a format
                // error, which next_frame() turns into the software-codec retry.
                int32_t planes = 0;
                AImage_getNumberOfPlanes(image, &planes);
                AUREA_LOG_ERROR("decoder %s: imagem YUV_420_888 %dx%d com %d plano(s) nao mapeia para planos na CPU",
                                info_.decoderName, iw, ih, planes);
                f->release();
                return Status{Errc::UnsupportedFormat, "layout YUV do ImageReader nao suportado"};
            }
        }

        // Região visível: crop do AImage ∩ crop do formato de saída, sem a
        // sobra de alinhamento além do tamanho da trilha (media::visible_region,
        // testada no host). Oppo A94: listras nas bordas com a sobra à mostra.
        const i32 imageCrop[4] = {crop.left, crop.top, crop.right, crop.bottom};
        media::VisibleRegion vis;
        if (!media::visible_region(f->width, f->height, imageCrop, outCrop_, info_.codedWidth, info_.codedHeight, vis)) {
            f->release();
            return Status{Errc::UnsupportedFormat, "crop fora do buffer decodificado"};
        }
        f->cropLeft = vis.left;
        f->cropTop = vis.top;
        f->visibleWidth = vis.width;
        f->visibleHeight = vis.height;
        out = FrameRef::adopt(f);
        return OkStatus;
    }

    /// Copy standard YUV_420_888 planes into owned I420 storage.
    static bool planes_from(AImage* image, CodecFrame& f) {
        int32_t n = 0;
        if (AImage_getNumberOfPlanes(image, &n) != AMEDIA_OK || n < 3) return false;
        uint8_t* data[3]{};
        int len[3]{};
        int32_t pixel[3]{}, row[3]{};
        for (int i = 0; i < 3; ++i) {
            if (AImage_getPlaneData(image, i, &data[i], &len[i]) != AMEDIA_OK || !data[i]) return false;
            if (AImage_getPlanePixelStride(image, i, &pixel[i]) != AMEDIA_OK ||
                AImage_getPlaneRowStride(image, i, &row[i]) != AMEDIA_OK) return false;
            const u32 w = i == 0 ? f.width : (f.width + 1) / 2;
            const u32 h = i == 0 ? f.height : (f.height + 1) / 2;
            if (!media::decoded_plane_fits(w, h, row[i], pixel[i], len[i])) return false;
        }
        const uint8_t* input[3]{data[0], data[1], data[2]};
        if (!media::copy_decoded_yuv420(f.width, f.height, input, row, pixel, len, f.compact)) return false;
        const u32 cw = (f.width + 1) / 2, ch = (f.height + 1) / 2;
        f.format = PixelFormat::YUV420P;
        f.planes[0] = f.compact.data();
        f.planes[1] = f.planes[0] + static_cast<size_t>(f.width) * f.height;
        f.planes[2] = f.planes[1] + static_cast<size_t>(cw) * ch;
        f.strides[0] = f.width;
        f.strides[1] = f.strides[2] = cw;
        f.planeCount = 3;
        // No mapped AImage address escapes to the renderer/cache/thumbnail.
        AImage_delete(f.image);
        f.image = nullptr;
        f.owner.reset();
        return true;
    }

    SourceFd fd_;
    bool zeroCopy_ = true;
    bool thumbnail_ = false;
    AMediaExtractor* ex_ = nullptr;
    AMediaCodec* codec_ = nullptr;
    i32 track_ = -1;
    std::shared_ptr<ReaderState> reader_;
    VideoStreamInfo info_{};
    i32 outCrop_[4]{0, 0, 0, 0};
    bool inputEos_ = false;
    bool outputEos_ = false;
    i64 lastPts_ = 0;
    FrameRef pendingFrame_;
    AImage* renderedImage_ = nullptr;
    bool renderedPending_ = false;
    bool renderedEos_ = false;
    i64 renderedPts_ = 0, renderedDuration_ = 0;
    bool legacyLookahead_ = false;
    ssize_t pendingOutput_ = -1;
    i64 pendingPts_ = 0; // Actual PTS lookahead preserves long VFR presentation intervals.
    bool pendingEos_ = false;
    bool softwareFallback_ = false;
    /// Caminho GL do driver (padrão): o AImage PRIVATE vira RGBA num passe GL.
    bool glPath_ = false;
    bool diagnosticSoftwareFallback_ = false;
    bool diagnosticVideoPixels_ = false;
    std::unique_ptr<GlVideoBridge> bridge_;
    i64 nextDeliveryUs_ = 0;
    i64 keyframeUs_ = 2'000'000;
};


// -----------------------------------------------------------------------------
// Decoder de áudio: MediaExtractor + MediaCodec → PCM float intercalado.
//
// HE-AAC anuncia metade da taxa no formato do contêiner (22,05 kHz) e só no
// primeiro buffer de saída revela a real (44,1 kHz, SBR). Por isso o `open`
// decodifica até o primeiro formato de saída e volta ao início: a taxa que o
// cache usa é a que sai do codec, não a do cabeçalho.
// -----------------------------------------------------------------------------
constexpr const char* kKeyPcmEncoding = "pcm-encoding";
constexpr i32 kPcm16 = 2, kPcmFloat = 4, kPcm8 = 3;

class MediaCodecAudioDecoder final : public audio::AudioDecoderBackend {
public:
    explicit MediaCodecAudioDecoder(SourceFd fd) : fd_(std::move(fd)) {}
    ~MediaCodecAudioDecoder() override {
        if (codec_) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
        }
        if (ex_) AMediaExtractor_delete(ex_);
    }

    Status open() {
        ex_ = AMediaExtractor_new();
        if (!ex_) return Status{Errc::OutOfMemory, "AMediaExtractor_new"};
        if (AMediaExtractor_setDataSourceFd(ex_, fd_.fd, fd_.offset, fd_.length) != AMEDIA_OK) {
            return Status{Errc::UnsupportedFormat, "conteiner ilegivel"};
        }
        const size_t n = AMediaExtractor_getTrackCount(ex_);
        AMediaFormat* fmt = nullptr;
        const char* mime = nullptr;
        for (size_t i = 0; i < n && track_ < 0; ++i) {
            AMediaFormat* f = AMediaExtractor_getTrackFormat(ex_, i);
            const char* m = nullptr;
            if (f && AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) && m && std::strncmp(m, "audio/", 6) == 0) {
                track_ = static_cast<i32>(i);
                fmt = f;
                mime = m;
            } else if (f) {
                AMediaFormat_delete(f);
            }
        }
        if (track_ < 0) return Status{Errc::UnsupportedFormat, "sem trilha de audio"};
        mime_ = mime ? mime : "";
        info_.sampleRate = static_cast<u32>(std::max(0, get_i32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, 0)));
        info_.channels = static_cast<u32>(std::max(0, get_i32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 0)));
        int64_t dur = 0;
        if (AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &dur)) info_.durationUs = dur;
        AMediaExtractor_selectTrack(ex_, static_cast<size_t>(track_));
        codec_ = AMediaCodec_createDecoderByType(mime_.c_str());
        if (!codec_) {
            AMediaFormat_delete(fmt);
            return Status{Errc::UnsupportedFormat, "sem decoder para este audio"};
        }
        // Pede float (menos conversão, sem perda); o decoder pode ignorar e
        // mandar 16 bits — o formato de saída diz o que veio. EXCETO em
        // audio/raw (WAV): ali "pcm-encoding" descreve a ENTRADA e o decoder
        // só repassa os bytes — pedir float rotulava 16 bits como float
        // (lixo, inf) na saída. Fica o encoding que o extrator leu do arquivo.
        const bool raw = mime_ == "audio/raw";
        if (!raw) AMediaFormat_setInt32(fmt, kKeyPcmEncoding, kPcmFloat);
        media_status_t ms = AMediaCodec_configure(codec_, fmt, nullptr, nullptr, 0);
        if (ms != AMEDIA_OK && !raw) {
            AMediaFormat_setInt32(fmt, kKeyPcmEncoding, kPcm16);
            ms = AMediaCodec_configure(codec_, fmt, nullptr, nullptr, 0);
        }
        AMediaFormat_delete(fmt);
        if (ms != AMEDIA_OK || AMediaCodec_start(codec_) != AMEDIA_OK) {
            return Status{Errc::UnsupportedFormat, "decoder de audio nao iniciou"};
        }
        // Descobre a taxa/canais reais (HE-AAC, codecs que mudam no 1º buffer).
        std::vector<f32> tmp;
        i64 pts = 0;
        bool eos = false;
        for (int i = 0; i < 8 && !formatKnown_ && !eos; ++i) {
            if (!read(tmp, pts, eos).ok()) break;
        }
        if (info_.sampleRate == 0 || info_.channels == 0) return Status{Errc::UnsupportedFormat, "audio sem formato"};
        return seek(0);
    }

    const audio::AudioStreamInfo& info() const noexcept override { return info_; }

    Status seek(i64 us) noexcept override {
        AMediaExtractor_seekTo(ex_, std::max<i64>(0, us), AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        // Codec recuperado pelo sistema / em erro: o flush falha. Avisar o
        // cache (que troca o decoder) em vez de seguir com um codec morto.
        if (AMediaCodec_flush(codec_) != AMEDIA_OK) return Status{Errc::DecodeFailed, "flush do audio falhou"};
        inputEos_ = false;
        return OkStatus;
    }

    Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override {
        out.clear();
        eos = false;
        for (int guard = 0; guard < 400; ++guard) {
            // Alimenta o que couber (sem bloquear).
            while (!inputEos_) {
                const ssize_t in = AMediaCodec_dequeueInputBuffer(codec_, 0);
                if (in < AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                    return Status{Errc::DecodeFailed, "codec de audio em erro (entrada)"};
                }
                if (in < 0) break;
                size_t cap = 0;
                u8* buf = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(in), &cap);
                const ssize_t sz = buf ? AMediaExtractor_readSampleData(ex_, buf, cap) : -1;
                if (sz < 0) {
                    AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in), 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputEos_ = true;
                    break;
                }
                const i64 t = AMediaExtractor_getSampleTime(ex_);
                AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in), 0, static_cast<size_t>(sz),
                                             static_cast<u64>(std::max<i64>(0, t)), 0);
                AMediaExtractor_advance(ex_);
            }
            AMediaCodecBufferInfo bi{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_, &bi, 5000);
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* f = AMediaCodec_getOutputFormat(codec_);
                if (f) {
                    const i32 r = get_i32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, 0);
                    const i32 c = get_i32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 0);
                    if (r > 0) info_.sampleRate = static_cast<u32>(r);
                    if (c > 0) info_.channels = static_cast<u32>(c);
                    pcm_ = get_i32(f, kKeyPcmEncoding, kPcm16);
                    AMediaFormat_delete(f);
                    formatKnown_ = true;
                }
                continue;
            }
            // Erro de verdade (codec recuperado/morto): os códigos informativos
            // são -1..-3; abaixo disso é media_status_t. Antes isso girava 2 s
            // e virava "fim do arquivo" — e o trecho ficava mudo no cache.
            if (idx < AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                return Status{Errc::DecodeFailed, "codec de audio em erro (saida)"};
            }
            if (idx < 0) continue;   // TRY_AGAIN / BUFFERS_CHANGED
            size_t cap = 0;
            const u8* data = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(idx), &cap);
            if (data && bi.size > 0) {
                const u8* p = data + bi.offset;
                if (pcm_ == kPcmFloat) {
                    const usize n = static_cast<usize>(bi.size) / sizeof(f32);
                    out.resize(n);
                    std::memcpy(out.data(), p, n * sizeof(f32));
                } else if (pcm_ == kPcm8) {
                    out.resize(static_cast<usize>(bi.size));
                    for (usize i = 0; i < out.size(); ++i) out[i] = (static_cast<f32>(p[i]) - 128.0f) / 128.0f;
                } else {
                    const usize n = static_cast<usize>(bi.size) / sizeof(i16);
                    out.resize(n);
                    const i16* s = reinterpret_cast<const i16*>(p);
                    for (usize i = 0; i < n; ++i) out[i] = static_cast<f32>(s[i]) / 32768.0f;
                }
                formatKnown_ = true;
            }
            ptsUs = bi.presentationTimeUs;
            const bool end = (bi.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(idx), false);
            if (end) eos = true;
            if (!out.empty() || end) return OkStatus;
        }
        // Nada saiu em ~2 s. Com a entrada já no fim, é o fim (decoder que não
        // marca EOS na saída). Antes do fim é codec travado: erro (o cache abre
        // outro decoder), não "fim" — fim falso deixava o trecho mudo no cache.
        if (inputEos_) {
            eos = true;
            return OkStatus;
        }
        return Status{Errc::DecodeFailed, "decoder de audio travado"};
    }

private:
    SourceFd fd_;
    AMediaExtractor* ex_ = nullptr;
    AMediaCodec* codec_ = nullptr;
    i32 track_ = -1;
    std::string mime_;
    audio::AudioStreamInfo info_{};
    i32 pcm_ = kPcm16;
    bool inputEos_ = false;
    bool formatKnown_ = false;
};

} // namespace

// =============================================================================
// Fábrica
// =============================================================================
std::string MediaCodecFactory::cache_identity(const char* sourcePath) {
    SourceFd fd;
    if (!open_source(sourcePath, opener_, openerCtx_, fd)) return {};
    struct stat st{};
    if (::fstat(fd.fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return {};
    return std::to_string(static_cast<i64>(st.st_dev)) + ':' + std::to_string(static_cast<i64>(st.st_ino)) + ':' +
        std::to_string(static_cast<i64>(st.st_size)) + ':' + std::to_string(static_cast<i64>(st.st_mtim.tv_sec)) + ':' +
        std::to_string(static_cast<i64>(st.st_mtim.tv_nsec)) + ':' + std::to_string(fd.offset) + ':' + std::to_string(fd.length);
}

bool MediaCodecFactory::probe(const char* sourcePath, MediaProbe& out) {
    SourceFd fd;
    if (!open_source(sourcePath, opener_, openerCtx_, fd)) {
        AUREA_LOG_ERROR("nao foi possivel abrir a midia para sondar");
        return false;
    }
    AMediaExtractor* ex = AMediaExtractor_new();
    if (!ex) return false;
    bool ok = false;
    if (AMediaExtractor_setDataSourceFd(ex, fd.fd, fd.offset, fd.length) == AMEDIA_OK) {
        const size_t n = AMediaExtractor_getTrackCount(ex);
        for (size_t i = 0; i < n; ++i) {
            AMediaFormat* f = AMediaExtractor_getTrackFormat(ex, i);
            if (!f) continue;
            const char* mime = nullptr;
            AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &mime);
            if (mime && std::strncmp(mime, "video/", 6) == 0 && !out.hasVideo) {
                AMediaExtractor_selectTrack(ex, i);
                fill_stream_info(ex, f, mime, out.video);
                AMediaExtractor_unselectTrack(ex, i);
                out.hasVideo = out.video.codedWidth > 0 && out.video.codedHeight > 0;
            } else if (mime && std::strncmp(mime, "audio/", 6) == 0 && !out.hasAudio) {
                out.hasAudio = true;
                out.audioSampleRate = static_cast<u32>(std::max(0, get_i32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, 0)));
                out.audioChannels = static_cast<u32>(std::max(0, get_i32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 0)));
                int64_t dur = 0;
                if (AMediaFormat_getInt64(f, AMEDIAFORMAT_KEY_DURATION, &dur)) out.audioDurationUs = dur;
            }
            AMediaFormat_delete(f);
        }
        ok = out.hasVideo || out.hasAudio;
    }
    AMediaExtractor_delete(ex);
    return ok;
}

std::unique_ptr<VideoDecoderBackend> MediaCodecFactory::open_video(const Asset& asset, MediaPriority priority) {
    // Miniatura: planos na CPU sempre (a conversão para RGBA pequeno é na CPU).
    SourceFd fd;
    if (!open_source(asset.sourcePath.c_str(), opener_, openerCtx_, fd)) {
        AUREA_LOG_ERROR("midia do asset '%s' inacessivel", asset.name.c_str());
        return nullptr;
    }
    // Caminho de partida (AndroidVideoPath.hpp): GL do driver em todo aparelho
    // cujo backend importa AHardwareBuffer RGBA; modo seguro = software.
    VideoPathInputs in;
    in.rgbaImport = driverGl_.load();
    in.safeMode = softwareOnly_.load();
    in.thumbnail = priority == MediaPriority::Thumbnail;
    const VideoPath path = initial_video_path(in);
    const bool zeroCopy = priority != MediaPriority::Thumbnail && zeroCopy_.load();
    auto decoder = std::make_unique<MediaCodecDecoder>(std::move(fd), zeroCopy, priority == MediaPriority::Thumbnail,
                                                       path == VideoPath::SoftwarePlanes, path == VideoPath::DriverGl,
                                                       diagnosticSoftwareFallback_.load(), diagnosticVideoPixels_.load());
    if (const Status s = decoder->open(); !s.ok()) {
        AUREA_LOG_ERROR("decoder nao abriu: %s", s.message().data());
        return nullptr;
    }
    return decoder;
}

std::unique_ptr<audio::AudioDecoderBackend> MediaCodecFactory::open_audio(const char* sourcePath) {
    SourceFd fd;
    if (!open_source(sourcePath, opener_, openerCtx_, fd)) {
        AUREA_LOG_ERROR("audio inacessivel");
        return nullptr;
    }
    auto d = std::make_unique<MediaCodecAudioDecoder>(std::move(fd));
    if (const Status s = d->open(); !s.ok()) {
        AUREA_LOG_WARN("decoder de audio nao abriu: %s", s.message().data());
        return nullptr;
    }
    AUREA_LOG_INFO("audio: %u Hz, %u canais", d->info().sampleRate, d->info().channels);
    return d;
}

} // namespace aurea::android
