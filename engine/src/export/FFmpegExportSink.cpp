#include "aurea/export/FFmpegExportSink.hpp"
#include "aurea/export/NV12Copy.hpp"
#include "aurea/export/ExportPacketLog.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/export/AndroidVideoEncoder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#if defined(__ANDROID__)
#include <libavcodec/jni.h>
#endif
}
#if defined(__ANDROID__)
// android_codecs (degraus legacy/platform-software, 0.1.8 BETA 3) consulta
// MediaCodecList pela JVM.
#include <jni.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if LIBAVCODEC_VERSION_MAJOR < 61 || LIBAVFORMAT_VERSION_MAJOR < 61
#error "AUREA's shared exporter requires FFmpeg 7.1 or newer"
#endif

namespace aurea {
namespace {

constexpr AVRational micros{1, 1'000'000};
constexpr u64 hashSeed = 1469598103934665603ull;
// Tempos (PTS/duracao) dos quadros entregues ao encoder e ainda sem pacote.
// So metadados (16 bytes por quadro), nunca pixels: os pixels vivem nos
// buffers do proprio encoder, que ja faz contrapressao (send_frame = EAGAIN).
// Encoders de hardware (MediaCodec, VideoToolbox) seguram dezenas de quadros
// antes do 1o pacote; 16 derrubava o export no celular com BudgetExceeded (9).
constexpr unsigned maxPendingVideo = 256;
constexpr int audioInputChunk = 4096;
constexpr u64 codecStallNs = 30'000'000'000ull;
// Partida do encoder (escada de abertura): sem o 1o pacote neste prazo
// esperando por ele, o degrau é dado como travado e o próximo assume os
// quadros guardados. Só conta tempo ESPERANDO o encoder (fila cheia, teto da
// Surface, EOS) — um projeto lento de renderizar não dispara.
constexpr u64 startupStallNs = 5'000'000'000ull;
// Quadros guardados até o 1o pacote (para refazer no próximo degrau).
constexpr u64 probationBytes = 64ull << 20;

// Quadro do 1o pacote decodificado × NV12 entregue: média por bloco (Y e
// CbCr, em níveis de 8 bits). Conversão certa fica abaixo de ~1; padrão de
// cor/faixa trocado, quadro invertido ou layout errado passam de longe.
constexpr f64 verifyMeanLimit = 3.0, verifyBlockLimit = 16.0;

// Degraus que falharam neste processo (encoder do aparelho): o próximo export
// começa direto no degrau que funciona, sem esperar a partida travar de novo.
std::mutex failedRungMutex;
std::vector<std::string> failedRungs;
bool rung_failed_before(const char* key) noexcept {
    std::lock_guard lock(failedRungMutex);
    return std::find(failedRungs.begin(), failedRungs.end(), key) != failedRungs.end();
}
void remember_failed_rung(const char* key) noexcept {
    std::lock_guard lock(failedRungMutex);
    try {
        if (std::find(failedRungs.begin(), failedRungs.end(), key) == failedRungs.end() && failedRungs.size() < 32)
            failedRungs.emplace_back(key);
    } catch (...) {}
}

u64 timestamp_hash(u64 hash, i64 value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= (static_cast<u64>(value) >> shift) & 255u;
        hash *= 1099511628211ull;
    }
    return hash;
}

AVCodecID codec_id(ExportCodec codec) noexcept {
    switch (codec) {
        case ExportCodec::H264: return AV_CODEC_ID_H264;
        case ExportCodec::HEVC: return AV_CODEC_ID_HEVC;
        case ExportCodec::AV1: return AV_CODEC_ID_AV1;
        case ExportCodec::ProRes: return AV_CODEC_ID_PRORES;
    }
    return AV_CODEC_ID_NONE;
}

bool has_pixel_format(const AVCodec* codec, AVPixelFormat wanted) noexcept {
    const void* formats = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                     &formats, &count) < 0 || !formats) return false;
    const auto* list = static_cast<const AVPixelFormat*>(formats);
    for (int i = 0; i < count; ++i) if (list[i] == wanted) return true;
    return false;
}

// Never infer hardware acceleration from an OMX/c2 name. API 29's explicit
// platform classification and size/rate support select the exact codec passed
// to FFmpeg. Missing JVM/API/support simply takes the software path.
#if defined(__ANDROID__)
struct AndroidCodecCandidate {
    char name[256]{};
    AVPixelFormat pixel = AV_PIX_FMT_NONE;
};
constexpr usize maxAndroidCandidates = 32;

usize android_codecs(AVCodecID id, const VideoStreamConfig& config, bool wantHardware,
                     std::array<AndroidCodecCandidate, maxAndroidCandidates>& output) noexcept {
    usize found = 0;
    auto* vm = static_cast<JavaVM*>(av_jni_get_java_vm(nullptr));
    if (!vm) return 0;
    JNIEnv* env = nullptr;
    bool detach = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0;
        detach = true;
    } else if (state != JNI_OK || !env) return 0;
    if (env->PushLocalFrame(64) < 0) {
        env->ExceptionClear();
        if (detach) vm->DetachCurrentThread();
        return 0;
    }
    do {
        const jclass version = env->FindClass("android/os/Build$VERSION");
        if (!version) break;
        const jfieldID sdk = env->GetStaticFieldID(version, "SDK_INT", "I");
        if (!sdk || env->GetStaticIntField(version, sdk) < 29) break;
        const jclass listClass = env->FindClass("android/media/MediaCodecList");
        const jclass infoClass = env->FindClass("android/media/MediaCodecInfo");
        const jclass capClass = env->FindClass("android/media/MediaCodecInfo$CodecCapabilities");
        const jclass videoClass = env->FindClass("android/media/MediaCodecInfo$VideoCapabilities");
        const jclass encoderClass = env->FindClass("android/media/MediaCodecInfo$EncoderCapabilities");
        const jclass formatClass = env->FindClass("android/media/MediaFormat");
        if (!listClass || !infoClass || !capClass || !videoClass || !encoderClass || !formatClass) break;
        const auto ctor = env->GetMethodID(listClass, "<init>", "(I)V");
        const auto infos = env->GetMethodID(listClass, "getCodecInfos", "()[Landroid/media/MediaCodecInfo;");
        const auto encoder = env->GetMethodID(infoClass, "isEncoder", "()Z");
        const auto hardware = env->GetMethodID(infoClass, "isHardwareAccelerated", "()Z");
        const auto software = env->GetMethodID(infoClass, "isSoftwareOnly", "()Z");
        const auto alias = env->GetMethodID(infoClass, "isAlias", "()Z");
        const auto types = env->GetMethodID(infoClass, "getSupportedTypes", "()[Ljava/lang/String;");
        const auto name = env->GetMethodID(infoClass, "getName", "()Ljava/lang/String;");
        const auto capabilities = env->GetMethodID(infoClass, "getCapabilitiesForType",
            "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
        const auto videoCaps = env->GetMethodID(capClass, "getVideoCapabilities",
            "()Landroid/media/MediaCodecInfo$VideoCapabilities;");
        const auto encoderCaps = env->GetMethodID(capClass, "getEncoderCapabilities",
            "()Landroid/media/MediaCodecInfo$EncoderCapabilities;");
        const auto supports = env->GetMethodID(videoClass, "areSizeAndRateSupported", "(IID)Z");
        const auto modeSupported = env->GetMethodID(encoderClass, "isBitrateModeSupported", "(I)Z");
        const auto formatSupported = env->GetMethodID(capClass, "isFormatSupported", "(Landroid/media/MediaFormat;)Z");
        const auto formatCtor = env->GetMethodID(formatClass, "<init>", "()V");
        const auto setInteger = env->GetMethodID(formatClass, "setInteger", "(Ljava/lang/String;I)V");
        const auto setString = env->GetMethodID(formatClass, "setString", "(Ljava/lang/String;Ljava/lang/String;)V");
        if (!ctor || !infos || !encoder || !hardware || !software || !alias || !types || !name ||
            !capabilities || !videoCaps || !encoderCaps || !supports || !modeSupported ||
            !formatSupported || !formatCtor || !setInteger || !setString) break;
        const jobject list = env->NewObject(listClass, ctor, 1 /* ALL_CODECS */);
        if (!list) break;
        const auto array = static_cast<jobjectArray>(env->CallObjectMethod(list, infos));
        if (!array || env->ExceptionCheck()) break;
        const char* mime = id == AV_CODEC_ID_HEVC ? "video/hevc" : "video/avc";
        // FFmpeg's byte-buffer adapter configures 16-pixel storage, then uses
        // an SPS conformance window for the exact visible dimensions. Probe
        // the configuration actually sent to MediaCodec, including its color.
        const jint width = static_cast<jint>((config.width + 15u) & ~15u);
        const jint height = static_cast<jint>((config.height + 15u) & ~15u);
        const jint fps = config.fps >= 1 ? static_cast<jint>(config.fps) : 30;
        const jint bitrate = static_cast<jint>(config.bitrateBps ? config.bitrateBps : 12'000'000);
        const jint mode = config.rateMode == 0 ? 2 /* CBR */ : 1 /* VBR */;
        const jsize n = env->GetArrayLength(array);
        for (jsize i = 0; i < n && found < output.size(); ++i) {
            if (env->PushLocalFrame(32) < 0) { env->ExceptionClear(); break; }
            const jobject info = env->GetObjectArrayElement(array, i);
            if (info && env->CallBooleanMethod(info, encoder) && !env->CallBooleanMethod(info, alias) &&
                (wantHardware ? env->CallBooleanMethod(info, hardware) : env->CallBooleanMethod(info, software)) &&
                !env->ExceptionCheck()) {
                const auto supported = static_cast<jobjectArray>(env->CallObjectMethod(info, types));
                if (supported && !env->ExceptionCheck()) {
                    for (jsize t = 0; t < env->GetArrayLength(supported); ++t) {
                        const auto type = static_cast<jstring>(env->GetObjectArrayElement(supported, t));
                        const char* text = type ? env->GetStringUTFChars(type, nullptr) : nullptr;
                        const bool matches = text && std::strcmp(text, mime) == 0;
                        if (text) env->ReleaseStringUTFChars(type, text);
                        if (matches) {
                            const jobject cap = env->CallObjectMethod(info, capabilities, type);
                            const jobject vc = cap ? env->CallObjectMethod(cap, videoCaps) : nullptr;
                            const jobject ec = cap ? env->CallObjectMethod(cap, encoderCaps) : nullptr;
                            if (vc && ec && env->CallBooleanMethod(vc, supports, width, height, config.fps) &&
                                env->CallBooleanMethod(ec, modeSupported, mode) && !env->ExceptionCheck()) {
                                const auto codecName = static_cast<jstring>(env->CallObjectMethod(info, name));
                                const char* textName = codecName ? env->GetStringUTFChars(codecName, nullptr) : nullptr;
                                for (jint color : {21 /* NV12 */, 19 /* I420 */}) {
                                    const jobject format = env->NewObject(formatClass, formatCtor);
                                    auto integer = [&](const char* key, jint value) noexcept {
                                        const jstring k = env->NewStringUTF(key);
                                        if (k) { env->CallVoidMethod(format, setInteger, k, value); env->DeleteLocalRef(k); }
                                    };
                                    if (!format || env->ExceptionCheck()) break;
                                    const jstring mimeKey = env->NewStringUTF("mime");
                                    if (!mimeKey) break;
                                    env->CallVoidMethod(format, setString, mimeKey, type);
                                    env->DeleteLocalRef(mimeKey);
                                    integer("width", width); integer("height", height);
                                    integer("frame-rate", fps); integer("bitrate", bitrate);
                                    integer("color-format", color); integer("bitrate-mode", mode);
                                    if (id == AV_CODEC_ID_H264 && config.profile == 1) integer("profile", 1 /* AVC Baseline */);
                                    const bool accepted = !env->ExceptionCheck() && env->CallBooleanMethod(cap, formatSupported, format);
                                    env->DeleteLocalRef(format);
                                    if (accepted && !env->ExceptionCheck() && textName && std::strlen(textName) < sizeof(output[found].name)) {
                                        std::snprintf(output[found].name, sizeof(output[found].name), "%s", textName);
                                        output[found].pixel = color == 21 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
                                        ++found;
                                        break;
                                    }
                                    if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
                                }
                                if (textName) env->ReleaseStringUTFChars(codecName, textName);
                            }
                            break;
                        }
                        if (type) env->DeleteLocalRef(type);
                    }
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->PopLocalFrame(nullptr);
        }
    } while (false);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->PopLocalFrame(nullptr);
    if (detach) vm->DetachCurrentThread();
    return found;
}
#endif


// Parâmetros globais (SPS/PPS, e VPS no HEVC) dentro de um pacote Annex-B:
// encoders que não mandam BUFFER_FLAG_CODEC_CONFIG os trazem no 1o pacote.
std::vector<u8> annexb_parameter_sets(const u8* data, usize size, bool hevc) {
    std::vector<u8> out;
    usize i = 0;
    auto start_code = [&](usize at) -> usize {
        if (at + 3 <= size && data[at] == 0 && data[at + 1] == 0 && data[at + 2] == 1) return 3;
        if (at + 4 <= size && data[at] == 0 && data[at + 1] == 0 && data[at + 2] == 0 && data[at + 3] == 1) return 4;
        return 0;
    };
    while (i < size) {
        const usize sc = start_code(i);
        if (!sc) { ++i; continue; }
        usize j = i + sc;
        while (j < size && !start_code(j)) ++j;
        if (i + sc < size) {
            const u8 header = data[i + sc];
            const int type = hevc ? (header >> 1) & 63 : header & 31;
            const bool parameters = hevc ? (type >= 32 && type <= 34) : (type == 7 || type == 8);
            if (parameters) {
                static constexpr u8 code[4] = {0, 0, 0, 1};
                out.insert(out.end(), code, code + 4);
                out.insert(out.end(), data + i + sc, data + j);
            }
        }
        i = j;
    }
    return out;
}

class FFmpegSink final : public ExportSink {
public:
    ~FFmpegSink() override { abort(); }
    void set_cancel_flag(const std::atomic<bool>* flag) noexcept override { cancel_ = flag; }
    void set_heartbeat(std::atomic<u64>* beat) noexcept override { heartbeat_ = beat; }
    void set_diagnostics(std::shared_ptr<ExportDiagnostics> diagnostics) noexcept override {
        diagnostics_ = std::move(diagnostics);
    }
    EncoderInfo encoder_info() const noexcept override { return encoder_; }

    Status open(const char* output, const VideoStreamConfig& video,
                const AudioStreamConfig* audio) noexcept override;
    Status write_video(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts) noexcept override {
        return write_video_with_fallback(y, ys, uv, uvs, pts, 0, false);
    }
    Status write_video_timed(const u8* y, u32 ys, const u8* uv, u32 uvs,
                            i64 pts, i64 duration) noexcept override {
        if (logMode_) return Status{Errc::NotSupported, "registro de pacotes aceita so taxa constante"};
        return write_video_with_fallback(y, ys, uv, uvs, pts, duration, true);
    }
    Status write_audio(const i16* pcm, u32 frames, i64 pts) noexcept override;
    Status finish() noexcept override;
    Status end_video() noexcept override;
    Status begin_assembly(const AudioStreamConfig* audio) noexcept override;
    Status replay_spilled_audio() noexcept override;
    Status validate_output(const ExportOutputValidation& expected) noexcept override;
    const char* validation_warnings() const noexcept override { return warningText_; }
    void abort() noexcept override;

private:
    enum class State { Empty, Open, VideoLogged, Finalized, Published, Failed } state_ = State::Empty;
    enum class Timing { Unknown, Constant, Variable } timing_ = Timing::Unknown;
    struct FrameTiming { i64 pts = 0, duration = 0, us = 0; };   ///< `us`: o tempo que o encoder do aparelho recebeu
    const std::atomic<bool>* cancel_ = nullptr;
    std::atomic<u64>* heartbeat_ = nullptr;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* videoCodec_ = nullptr;
    AVCodecContext* audioCodec_ = nullptr;
    AVBSFContext* videoCrop_ = nullptr;
    AVStream* videoStream_ = nullptr;
    AVStream* audioStream_ = nullptr;
    AVFrame* videoFrame_ = nullptr;
    AVFrame* audioFrame_ = nullptr;
    AVFrame* resampleFrame_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVPacket* pendingAudio_ = nullptr;
    SwsContext* scale_ = nullptr;
    SwrContext* resample_ = nullptr;
    AVAudioFifo* audioFifo_ = nullptr;
    VideoStreamConfig video_{};
    AudioStreamConfig audio_{};
    bool hasAudio_ = false;
    AVRational rate_{30, 1}, videoTimebase_{1, 90'000};
    int audioRate_ = 0, audioFrameSamples_ = 0;
    u64 videoFrames_ = 0, videoPackets_ = 0, videoHash_ = hashSeed;
    u64 completedVideoWrites_ = 0;
    i64 lastVideoPts_ = AV_NOPTS_VALUE, lastVideoEnd_ = 0;
    i64 audioInputSamples_ = 0, audioConvertedSamples_ = 0, audioSubmittedSamples_ = 0;
    std::array<FrameTiming, maxPendingVideo> pendingVideo_{};
    unsigned pendingHead_ = 0, pendingCount_ = 0, pendingLimit_ = maxPendingVideo;
    EncoderInfo encoder_{};
    std::string target_, staged_;
    char detail_[256]{};
    Status lastError_{};
    // Registro de pacotes (export retomável). `logMode_`: o vídeo vai para o
    // registro; `assembling_`: o MP4 final está sendo montado a partir dele.
    bool logMode_ = false, assembling_ = false;
    std::string logPath_;
    export_log::Writer log_;
    export_log::Reader logReader_;
    export_log::StreamInfo logInfo_{};
    AVCodecParameters* videoParams_ = nullptr;   ///< parâmetros do encoder liberado (end_video)
    u64 assembledVideo_ = 0;
    export_log::Packet logged_;
    // PCM do render guardado ao lado do registro (`<registro>.pcm`, s16
    // intercalado): o áudio é mixado em paralelo aos quadros e só codificado
    // na montagem. Nulo = sessão retomada (o motor mixa de novo no fim).
    std::FILE* spill_ = nullptr;
    std::string spillPath_;
    i64 spilledSamples_ = 0;
    bool spillAudio_ = false;
    bool wantsAudio_ = false;
    bool logStarted_ = false;                ///< o registro abre no 1o pacote (com os parâmetros do encoder que deu certo)
    export_log::ScanResult resumeScan_{};    ///< varredura da retomada, feita antes de qualquer degrau mexer no registro

    // --- Escada de abertura do encoder de vídeo -----------------------------
    // Android: Surface do encoder do aparelho → ByteBuffer no formato que ele
    // anuncia → o caminho FFmpeg/MediaCodec antigo → software do app. iOS:
    // VideoToolbox NV12 → VideoToolbox I420 → software. Cada degrau cai para o
    // próximo quando não abre OU quando o 1o pacote não chega/não confere
    // ("liberdade condicional"): os quadros entregues até o 1o pacote ficam
    // guardados e o próximo degrau os recebe de novo — sem erro na tela.
    enum class Rung : u8 { Surface, Buffer, Legacy, VideoToolbox, VideoToolboxPlanar, Software, PlatformSoftware };
    /// `pixel`: formato do caminho FFmpeg/MediaCodec que o encoder ACEITOU no
    /// isFormatSupported (NV12 ou I420; NONE = o padrão do try_video).
    struct RungEntry { Rung kind = Rung::Software; bool hardware = false; char codec[128]{};
                       AVPixelFormat pixel = AV_PIX_FMT_NONE; };
    struct Retained { std::vector<u8> y, uv; i64 pts = 0, duration = 0; bool timed = false; };
    std::vector<RungEntry> rungs_;
    usize rung_ = 0, reopenFrom_ = 0;
    std::unique_ptr<android_video::Encoder> native_;
    android_video::CodecInfo deviceCodec_{};
    android_video::Packet nativePacket_;
    bool nativeActive_ = false, nativeEos_ = false;
    std::vector<u8> extradata_;              ///< parâmetros globais do encoder atual (Annex-B ou avcC)
    std::vector<Retained> retained_;
    bool probation_ = false;                 ///< quadros guardados; falha aqui = próximo degrau
    bool firstPacketPending_ = false;        ///< o 1o pacote ainda confere/abre o registro
    u32 maxRetained_ = 8;
    char rungName_[192]{};
    char attempts_[384]{};                   ///< degraus que falharam nesta sessão (diagnóstico)
    char rungDetail_[256]{};

    std::shared_ptr<ExportDiagnostics> diagnostics_;

    void beat() noexcept {
        if (heartbeat_) heartbeat_->store(monotonic_ns(), std::memory_order_release);
    }
    void phase(ExportDiagnostics::Stage stage) noexcept {
        if (diagnostics_) diagnostics_->stage(ExportDiagnostics::Lane::Encoder, stage);
    }
    void event(ExportDiagnostics::Counter counter) noexcept {
        if (diagnostics_) diagnostics_->event(counter);
    }
    bool cancelled() const noexcept { return cancel_ && cancel_->load(std::memory_order_acquire); }
    static int interrupted(void* user) noexcept { return static_cast<FFmpegSink*>(user)->cancelled() ? 1 : 0; }
    Status error(Errc code, const char* what, int result = 0) noexcept {
        char fferror[AV_ERROR_MAX_STRING_SIZE]{};
        if (result < 0) av_strerror(result, fferror, sizeof(fferror));
        std::snprintf(detail_, sizeof(detail_), result < 0 ? "%s: %s" : "%s%s", what, fferror);
        if (result == AVERROR(ENOSPC)) code = Errc::StorageFull;
#ifdef EDQUOT
        if (result == AVERROR(EDQUOT)) code = Errc::StorageFull;
#endif
        if (cancelled() || result == AVERROR_EXIT) code = Errc::Cancelled;
        state_ = State::Failed;
        lastError_ = Status{code, detail_};
        phase(ExportDiagnostics::Stage::Failed);
        if (diagnostics_) diagnostics_->result(static_cast<i32>(code));
        return lastError_;
    }
    Status ready() noexcept {
        if (cancelled()) return Status{Errc::Cancelled, "exportacao FFmpeg cancelada"};
        if (state_ == State::Failed) return lastError_;
        if (state_ != State::Open) return Status{Errc::InvalidState, "exportacao FFmpeg nao esta aberta"};
        return OkStatus;
    }
    void release() noexcept;
    Status open_video(usize firstRung) noexcept;
    Status try_video(const char* name, bool hardware, const char* nativeName = nullptr,
                     AVPixelFormat pixel = AV_PIX_FMT_NONE) noexcept;
    Status prepare_ffmpeg_video() noexcept;
    Status open_audio() noexcept;
    Status write_video_with_fallback(const u8*, u32, const u8*, u32, i64, i64, bool) noexcept;
    Status write_video_impl(const u8*, u32, const u8*, u32, i64, i64, bool) noexcept;
    Status send(AVCodecContext*, AVStream*, const AVFrame*, bool video) noexcept;
    Status drain(AVCodecContext*, AVStream*, bool video, bool flushing, unsigned& produced) noexcept;
    Status video_packet(AVRational from) noexcept;
    // Escada de abertura (ver os membros acima).
    void build_rungs() noexcept;
    Status open_rung(const RungEntry& rung) noexcept;
    void close_video() noexcept;
    void reset_video_clock() noexcept;
    void describe_rung(const RungEntry& rung, char* out, usize capacity) const noexcept;
    void note_failure(const char* why) noexcept;
    void publish_path(const char* state) const noexcept;
    bool fallback_allowed(Status status) const noexcept;
    Status fallback(Status reason) noexcept;
    Status retain(const u8*, u32, const u8*, u32, i64, i64, bool) noexcept;
    Status resubmit_retained() noexcept;
    Status wait_first_packet() noexcept;
    Status first_packet() noexcept;
    Status verify_first_frame() noexcept;
    Status start_log() noexcept;
    Status finish_video_stream() noexcept;
    Status drain_native(bool flushing, unsigned& produced) noexcept;
    Status send_native(const u8*, u32, const u8*, u32, i64 us) noexcept;
    void fill_native_parameters(AVCodecParameters* par) const noexcept;
    void fill_log_info(export_log::StreamInfo& info) const noexcept;
    u64 stall_ns() const noexcept { return probation_ ? startupStallNs : codecStallNs; }
    Status mux(AVPacket*, AVRational from, AVStream*) noexcept;
    Status append_audio(const i16* pcm, int samples) noexcept;
    Status encode_audio(bool final) noexcept;
    Status flush_resampler() noexcept;
    Status decode_validate(const ExportOutputValidation& expected) noexcept;
    // Avisos da validação tolerante: cada divergência (texto estático) com a
    // contagem, sem alocar; o texto final é montado no fim da validação.
    struct Warning { const char* what = nullptr; u32 count = 0; };
    std::array<Warning, 8> warnings_{};
    char warningText_[384]{};
    void clear_warnings() noexcept;
    void note_warning(const char* what) noexcept;
    void finish_warnings(u64 decodedFrames) noexcept;
    Status publish() noexcept;
    i64 frame_pts(u64 frame) const noexcept { return av_rescale_q(static_cast<i64>(frame), av_inv_q(rate_), videoTimebase_); }
    Status open_log() noexcept;
    Status open_container() noexcept;
    Status mux_logged_video(u64 untilFrame) noexcept;
};

void FFmpegSink::close_video() noexcept {
    avcodec_free_context(&videoCodec_);
    av_bsf_free(&videoCrop_);
    av_frame_free(&videoFrame_);
    sws_freeContext(scale_); scale_ = nullptr;
    if (native_) native_->close();
    nativeActive_ = nativeEos_ = false;
    extradata_.clear();
}

void FFmpegSink::release() noexcept {
    if (format_ && format_->pb) avio_closep(&format_->pb);
    close_video();
    std::vector<Retained>().swap(retained_);
    probation_ = false;
    avcodec_free_context(&audioCodec_);
    av_frame_free(&audioFrame_);
    av_frame_free(&resampleFrame_);
    av_packet_free(&packet_);
    av_packet_free(&pendingAudio_);
    avcodec_parameters_free(&videoParams_);
    if (spill_) { std::fclose(spill_); spill_ = nullptr; }
    logReader_.close();
    (void)log_.close();   // o registro é a recuperação: fechado, nunca apagado aqui
    swr_free(&resample_);
    if (audioFifo_) av_audio_fifo_free(audioFifo_);
    audioFifo_ = nullptr;
    if (format_) avformat_free_context(format_);
    format_ = nullptr;
    videoStream_ = audioStream_ = nullptr;
}

void FFmpegSink::abort() noexcept {
    if (state_ != State::Empty && state_ != State::Published)
        phase(ExportDiagnostics::Stage::Abort);
    release();
    clear_warnings();
    if (!staged_.empty()) fileio::remove_file(staged_);
    staged_.clear();
    state_ = State::Empty;
}

Status FFmpegSink::open(const char* output, const VideoStreamConfig& video,
                       const AudioStreamConfig* audio) noexcept {
    // Reabertura pela escada (contêiner direto, sem registro): continua do
    // degrau seguinte, com a mesma lista.
    const usize firstRung = reopenFrom_;
    reopenFrom_ = 0;
    abort();
    if (!output || !*output || !video.width || !video.height ||
        video.width > 16'384 || video.height > 16'384 ||
        (video.width & 1u) || (video.height & 1u) ||
        !std::isfinite(video.fps) || video.fps < 0.000001 || video.fps > 480.0 ||
        video.rateMode > 1 || video.bitrateBps > static_cast<u32>(INT_MAX / 2) ||
        (audio && (!audio->sampleRate || audio->sampleRate > 192'000 ||
                   !audio->channels || audio->channels > 8 || !audio->bitrateBps)))
        return Status{Errc::InvalidArgument, "configuracao de exportacao FFmpeg invalida"};
    if (codec_id(video.codec) == AV_CODEC_ID_NONE)
        return Status{Errc::UnsupportedCodec, "codec de exportacao FFmpeg desconhecido"};
    if (cancelled()) return Errc::Cancelled;
    try { logPath_ = video.packetLogPath ? video.packetLogPath : ""; }
    catch (...) { return error(Errc::OutOfMemory, "caminho do registro de pacotes"); }
    logMode_ = !logPath_.empty(); assembling_ = false; assembledVideo_ = 0;
    if (!logMode_ && video.resumeFrame)
        return Status{Errc::InvalidArgument, "retomada de exportacao exige registro de pacotes"};
    video_ = video; video_.packetLogPath = nullptr;
    // Com registro, o encoder de áudio só abre na montagem (begin_assembly);
    // durante o render o PCM vai para o arquivo lateral.
    hasAudio_ = audio != nullptr && !logMode_;
    wantsAudio_ = audio != nullptr;
    audio_ = audio ? *audio : AudioStreamConfig{0, 0, 0};
    timing_ = Timing::Unknown; encoder_ = {};
    videoFrames_ = videoPackets_ = 0; videoHash_ = hashSeed;
    completedVideoWrites_ = 0;
    lastVideoPts_ = AV_NOPTS_VALUE; lastVideoEnd_ = 0;
    audioInputSamples_ = audioConvertedSamples_ = audioSubmittedSamples_ = 0;
    pendingHead_ = pendingCount_ = 0; lastError_ = OkStatus;
    const u64 frameBytes = static_cast<u64>(video.width) * video.height * 3 / 2;
    maxRetained_ = static_cast<u32>(std::clamp<u64>(probationBytes / std::max<u64>(frameBytes, 1), 3, 8));
    pendingLimit_ = maxPendingVideo;
    rate_ = av_d2q(video.fps, 1'000'000);
    if (rate_.num <= 0 || rate_.den <= 0)
        return Status{Errc::InvalidArgument, "taxa de quadros FFmpeg nao possui representacao racional valida"};
    // The timescale is an integer multiple of the exact rational frame rate.
    // Every CFR PTS is derived from its absolute index; no accumulated rounding.
    // Also keep the timescale <= 1 MHz: Android's encoder transports PTS in
    // microseconds. A finer tick would lose exact CFR PTS on that round trip.
    const int multiplier = std::max(1, 1'000'000 / rate_.num);
    videoTimebase_ = AVRational{1, rate_.num * multiplier};
    try {
        target_ = output;
        static std::atomic<u64> generation{0};
        staged_ = target_ + ".aurea-ffmpeg-" + std::to_string(monotonic_ns()) + "-" +
                  std::to_string(++generation) + ".tmp";
    } catch (...) { return error(Errc::OutOfMemory, "caminho temporario da exportacao FFmpeg"); }
    int result = avformat_alloc_output_context2(&format_, nullptr,
        video.codec == ExportCodec::ProRes ? "mov" : "mp4", staged_.c_str());
    beat();
    if (result < 0 || !format_) return error(Errc::OutOfMemory, "criar container FFmpeg", result);
    format_->interrupt_callback = AVIOInterruptCB{&FFmpegSink::interrupted, this};
    format_->max_interleave_delta = 100'000; // bounded muxer wait: 100 ms
    format_->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    if (logMode_ && video_.resumeFrame) {
        // A varredura vem ANTES de qualquer degrau: o registro só é cortado
        // quando um encoder provou que continua o mesmo fluxo (1o pacote).
        const Status scanned = export_log::scan(logPath_, resumeScan_);
        if (!scanned.ok()) return error(scanned.code(), "varrer registro de pacotes");
        if (resumeScan_.resumeFrame != video_.resumeFrame)
            return error(Errc::InvalidState, "registro de pacotes mudou desde a varredura");
    }
    if (!firstRung) {
        build_rungs();
        attempts_[0] = '\0';
    }
    if (const Status status = open_video(firstRung); !status.ok()) return status;
    if (logMode_) return open_log();
    if (audio) if (const Status status = open_audio(); !status.ok()) return status;
    return open_container();
}

Status FFmpegSink::open_container() noexcept {
    videoStream_ = avformat_new_stream(format_, nullptr);
    if (!videoStream_) return error(Errc::OutOfMemory, "criar faixa de video FFmpeg");
    videoStream_->time_base = videoTimebase_;
    videoStream_->avg_frame_rate = rate_;
    int result = 0;
    if (videoParams_) result = avcodec_parameters_copy(videoStream_->codecpar, videoParams_);
    else if (nativeActive_) fill_native_parameters(videoStream_->codecpar);   // SPS/PPS chegam no 1o pacote
    else result = videoCrop_ ? avcodec_parameters_copy(videoStream_->codecpar, videoCrop_->par_out)
                             : avcodec_parameters_from_context(videoStream_->codecpar, videoCodec_);
    if (result < 0)
        return error(Errc::EncodeFailed, "parametros de video FFmpeg", result);
    videoStream_->codecpar->width = static_cast<int>(video_.width);
    videoStream_->codecpar->height = static_cast<int>(video_.height);
    if (video_.codec == ExportCodec::HEVC) videoStream_->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    if (hasAudio_) {
        audioStream_ = avformat_new_stream(format_, nullptr);
        if (!audioStream_) return error(Errc::OutOfMemory, "criar faixa de audio FFmpeg");
        audioStream_->time_base = audioCodec_->time_base;
        if ((result = avcodec_parameters_from_context(audioStream_->codecpar, audioCodec_)) < 0)
            return error(Errc::EncodeFailed, "parametros de audio FFmpeg", result);
    }
    if (!packet_) packet_ = av_packet_alloc();
    if (!pendingAudio_) pendingAudio_ = av_packet_alloc();
    if (!packet_ || !pendingAudio_) return error(Errc::OutOfMemory, "pacotes da exportacao FFmpeg");
    result = avio_open2(&format_->pb, staged_.c_str(), AVIO_FLAG_WRITE, &format_->interrupt_callback, nullptr);
    beat();
    if (result < 0) return error(Errc::IoError, "abrir arquivo temporario FFmpeg", result);
    AVDictionary* options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    av_dict_set(&options, "use_editlist", "1", 0);
    av_dict_set_int(&options, "video_track_timescale", videoTimebase_.den, 0);
    av_dict_set_int(&options, "movie_timescale", hasAudio_ ? audioRate_ : videoTimebase_.den, 0);
    result = avformat_write_header(format_, &options);
    av_dict_free(&options); beat();
    if (result < 0) return error(Errc::EncodeFailed, "escrever cabecalho FFmpeg", result);
    state_ = State::Open;
    phase(ExportDiagnostics::Stage::Idle);
    return OkStatus;
}

Status FFmpegSink::try_video(const char* name, bool hardware, const char* nativeName, AVPixelFormat wanted) noexcept {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) return Status{Errc::EncoderUnavailable, "encoder FFmpeg ausente nesta compilacao"};
    avcodec_free_context(&videoCodec_);
    videoCodec_ = avcodec_alloc_context3(codec);
    if (!videoCodec_) return error(Errc::OutOfMemory, "contexto de video FFmpeg");
    AVPixelFormat pixel = AV_PIX_FMT_NV12;
    if (wanted != AV_PIX_FMT_NONE) {
        // Degrau com formato alternativo (VideoToolbox I420): só ele, sem trocar.
        if (!has_pixel_format(codec, wanted)) {
            avcodec_free_context(&videoCodec_);
            return Status{Errc::UnsupportedFormat, "encoder FFmpeg sem o formato alternativo pedido"};
        }
        pixel = wanted;
    } else if (!has_pixel_format(codec, pixel)) {
        pixel = video_.codec == ExportCodec::ProRes ? AV_PIX_FMT_YUV422P10LE : AV_PIX_FMT_YUV420P;
        if (!has_pixel_format(codec, pixel)) {
            avcodec_free_context(&videoCodec_);
            return Status{Errc::UnsupportedFormat, "encoder FFmpeg sem entrada de pixels CPU compativel"};
        }
    }
    auto* ctx = videoCodec_;
    // Kvazaar's FFmpeg wrapper requires 8-pixel storage dimensions. Keep the
    // requested picture intact inside that storage and crop only its SPS
    // conformance window; the published and decoded dimensions stay exact.
    const bool kvazaar = std::strcmp(name, "libkvazaar") == 0;
    ctx->width = static_cast<int>(kvazaar ? (video_.width + 7u) & ~7u : video_.width);
    ctx->height = static_cast<int>(kvazaar ? (video_.height + 7u) & ~7u : video_.height);
    ctx->pix_fmt = pixel; ctx->time_base = videoTimebase_; ctx->framerate = rate_;
    ctx->sample_aspect_ratio = AVRational{1, 1};
    ctx->bit_rate = video_.bitrateBps ? video_.bitrateBps : 12'000'000;
    ctx->rc_max_rate = video_.rateMode == 0 ? ctx->bit_rate : ctx->bit_rate * 3 / 2;
    ctx->rc_min_rate = video_.rateMode == 0 ? ctx->bit_rate : 0;
    ctx->rc_buffer_size = static_cast<int>(std::min<i64>(ctx->bit_rate * 2, INT_MAX));
    ctx->gop_size = video_.keyframeIntervalFrames ? static_cast<int>(std::min<u32>(video_.keyframeIntervalFrames, INT_MAX)) :
        std::max(1, static_cast<int>(std::lround(video_.fps * 2)));
    ctx->max_b_frames = 0; ctx->thread_count = 4; ctx->thread_type = FF_THREAD_SLICE;
    ctx->color_primaries = static_cast<AVColorPrimaries>(video_.color.primaries);
    ctx->color_trc = static_cast<AVColorTransferCharacteristic>(video_.color.transfer);
    ctx->colorspace = static_cast<AVColorSpace>(video_.color.matrix);
    ctx->color_range = video_.color.fullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    ctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
    if (video_.profile == 1 && video_.codec == ExportCodec::H264) ctx->profile = AV_PROFILE_H264_BASELINE;
    if (format_->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = nullptr;
    if (std::strstr(name, "videotoolbox")) {
        // Desktop FFmpeg enforces this; iOS acceleration remains Unknown below.
        av_dict_set(&options, "allow_sw", "0", 0);
        av_dict_set(&options, "realtime", "0", 0);
    } else if (std::strstr(name, "mediacodec")) {
        av_dict_set(&options, "codec_name", nativeName, 0);
        av_dict_set(&options, "ndk_codec", "1", 0);
        // Modo de taxa só quando o encoder o anuncia (consultado na escada).
        const bool known = nativeName && std::strcmp(nativeName, deviceCodec_.name) == 0;
        if (!known || (video_.rateMode == 0 ? deviceCodec_.cbr : deviceCodec_.vbr))
            av_dict_set(&options, "bitrate_mode", video_.rateMode == 0 ? "cbr" : "vbr", 0);
    } else if (std::strcmp(name, "libx264") == 0) {
        av_dict_set(&options, "preset", "veryfast", 0);
        av_dict_set(&options, "tune", "zerolatency", 0);
        av_dict_set(&options, "x264-params", "rc-lookahead=0:sync-lookahead=0:scenecut=0", 0);
    } else if (kvazaar) {
        char parameters[160]{};
        // gop=0 disables B-picture reordering; owf=0 bounds frame buffering.
        // The wrapper copies the requested AVCodecContext bitrate and VUI.
        std::snprintf(parameters, sizeof(parameters),
            "preset=ultrafast,threads=4,owf=0,gop=0,period=%d,open-gop=0", ctx->gop_size);
        av_dict_set(&options, "kvazaar-params", parameters, 0);
    } else if (std::strcmp(name, "libx265") == 0) {
        av_dict_set(&options, "preset", "veryfast", 0);
        av_dict_set(&options, "x265-params", "rc-lookahead=0:bframes=0:pools=4:frame-threads=1:log-level=error", 0);
    } else if (std::strcmp(name, "libopenh264") == 0) {
        // OpenH264's "buffer" mode explicitly has no bitrate control. Both
        // fallback rate modes therefore use bitrate control, with the requested
        // CBR/VBR ceiling already carried by rc_max_rate above.
        av_dict_set(&options, "rc_mode", "bitrate", 0);
        av_dict_set(&options, "allow_skip_frames", "0", 0);
        // OpenH264 só paraleliza ENTRE fatias: com uma fatia por quadro as 4
        // threads ficavam paradas (moto g52, 4K do upscale: 306 ms/quadro num
        // núcleo). 4 fatias = 4 núcleos; a perda de compressão é pequena.
        if (ctx->height >= 720) ctx->slices = 4;
        if (video_.profile == 1) av_dict_set(&options, "profile", "constrained_baseline", 0);
    } else if (std::strcmp(name, "libsvtav1") == 0) {
        av_dict_set(&options, "preset", "10", 0);
        av_dict_set(&options, "svtav1-params", "lookahead=0:lp=4", 0);
    } else if (std::strcmp(name, "libaom-av1") == 0) {
        av_dict_set(&options, "cpu-used", "8", 0);
        av_dict_set(&options, "lag-in-frames", "0", 0);
    }
    if (diagnostics_) diagnostics_->codec(nativeName ? nativeName : name, name,
        std::strstr(name, "mediacodec") ? pixel == AV_PIX_FMT_NV12 ? 21 : 19 : -1,
        std::strstr(name, "mediacodec") ? (ctx->width + 15u) & ~15u : ctx->width,
        std::strstr(name, "mediacodec") ? (ctx->height + 15u) & ~15u : ctx->height);
    phase(ExportDiagnostics::Stage::CodecOpen);
    const int result = avcodec_open2(ctx, codec, &options);
    const bool unused = av_dict_count(options) != 0;
    av_dict_free(&options); beat();
    if (result < 0 || unused) {
        AUREA_LOG_WARN("export encoder=%s backend=%s input=%s size=%dx%d fps=%.6f open=%d unused=%d",
            nativeName ? nativeName : name, name, av_get_pix_fmt_name(pixel),
            ctx->width, ctx->height, video_.fps, result, unused ? 1 : 0);
        avcodec_free_context(&videoCodec_);
        return Status{Errc::EncoderUnavailable, "encoder FFmpeg recusou configuracao solicitada"};
    }
    std::snprintf(encoder_.name, sizeof(encoder_.name), "%s", nativeName ? nativeName : name);
    encoder_.acceleration = hardware ? Acceleration::Hardware : Acceleration::Software;
    AUREA_LOG_INFO("export encoder=%s backend=%s input=%s size=%dx%d fps=%.6f acceleration=%d",
        encoder_.name, name, av_get_pix_fmt_name(pixel), ctx->width, ctx->height,
        video_.fps, static_cast<int>(encoder_.acceleration));
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // Upstream FFmpeg does not enforce allow_sw=0 on iOS. Do not claim an
    // encoder-session hardware property that this API cannot actually query.
    if (hardware) encoder_.acceleration = Acceleration::Unknown;
#endif
    return OkStatus;
}

void FFmpegSink::describe_rung(const RungEntry& rung, char* out, usize capacity) const noexcept {
    const char* kind = "software";
    switch (rung.kind) {
        case Rung::Surface: kind = "surface"; break;
        case Rung::Buffer: kind = "buffer"; break;
        case Rung::Legacy: kind = "legacy"; break;
        case Rung::VideoToolbox: kind = "videotoolbox"; break;
        case Rung::VideoToolboxPlanar: kind = "videotoolbox-i420"; break;
        case Rung::Software: kind = "software"; break;
        case Rung::PlatformSoftware: kind = "platform-software"; break;
    }
    std::snprintf(out, capacity, "%s:%s", kind, rung.codec);
}

void FFmpegSink::build_rungs() noexcept {
    rungs_.clear();
    rung_ = 0;
    auto add = [&](Rung kind, const char* codec, bool hardware, AVPixelFormat pixel = AV_PIX_FMT_NONE) {
        RungEntry entry;
        entry.kind = kind;
        entry.hardware = hardware;
        entry.pixel = pixel;
        std::snprintf(entry.codec, sizeof(entry.codec), "%s", codec);
        char key[192];
        describe_rung(entry, key, sizeof(key));
        if ((kind == Rung::Surface || kind == Rung::Buffer) && rung_failed_before(key)) {
            AUREA_LOG_INFO("export: degrau %s falhou antes neste processo; pulado", key);
            return;
        }
        try { rungs_.push_back(entry); } catch (...) {}
    };
    const bool h26x = video_.codec == ExportCodec::H264 || video_.codec == ExportCodec::HEVC;
    [[maybe_unused]] const bool hevc = video_.codec == ExportCodec::HEVC;
    if (!video_.preferSoftware && h26x) {
#if defined(__ANDROID__)
        namespace avenc = android_video;
        bool found = avenc::find_encoder(hevc, video_.width, video_.height, video_.fps, true, deviceCodec_);
        // Emulador/teste: os degraus do aparelho com o encoder de software do sistema.
        if (!found && avenc::debug_flag("debug.aurea.enc.allowsw", nullptr))
            found = avenc::find_encoder(hevc, video_.width, video_.height, video_.fps, false, deviceCodec_);
        if (found) {
            // A Surface recebe R'G'B' de 8 bits (SDR); proxy HDR vai pelo buffer.
            const u8 t = video_.color.transfer;
            const bool sdr = t == 1 || t == 6 || t == 14 || t == 15;
            if (deviceCodec_.surface && sdr && !avenc::debug_flag("debug.aurea.enc.skip", "surface"))
                add(Rung::Surface, deviceCodec_.name, deviceCodec_.hardware);
            if (!avenc::debug_flag("debug.aurea.enc.skip", "buffer"))
                add(Rung::Buffer, deviceCodec_.name, deviceCodec_.hardware);
        }
        // O caminho de sempre (FFmpeg + MediaCodec por buffer): TODOS os
        // encoders de hardware que aceitam a configuração (0.1.8 BETA 3:
        // sem alias, isFormatSupported com NV12 e depois I420). Nada que
        // funcionava antes deixa de ser tentado.
        if (!avenc::debug_flag("debug.aurea.enc.skip", "legacy")) {
            std::array<AndroidCodecCandidate, maxAndroidCandidates> legacy{};
            const usize count = android_codecs(codec_id(video_.codec), video_, true, legacy);
            for (usize i = 0; i < count; ++i) add(Rung::Legacy, legacy[i].name, true, legacy[i].pixel);
            if (!count && found) add(Rung::Legacy, deviceCodec_.name, deviceCodec_.hardware);
        }
#elif defined(__APPLE__)
        const char* vt = hevc ? "hevc_videotoolbox" : "h264_videotoolbox";
        add(Rung::VideoToolbox, vt, true);
        add(Rung::VideoToolboxPlanar, vt, true);
#endif
    }
    const char* candidates[4]{};
    switch (video_.codec) {
        case ExportCodec::H264: candidates[0] = "libopenh264"; candidates[1] = "libx264"; break;
        case ExportCodec::HEVC: candidates[0] = "libkvazaar"; candidates[1] = "libx265"; break;
        case ExportCodec::AV1: candidates[0] = "libsvtav1"; candidates[1] = "libaom-av1"; candidates[2] = "librav1e"; break;
        case ExportCodec::ProRes: candidates[0] = "prores_ks"; candidates[1] = "prores_aw"; break;
    }
    for (const char* candidate : candidates)
        if (candidate && avcodec_find_encoder_by_name(candidate)) add(Rung::Software, candidate, false);
#if defined(__ANDROID__)
    // Android can provide an explicitly classified software encoder even when
    // this FFmpeg distribution has no libx265. It is still encoded/muxed
    // through the same shared FFmpeg pipeline.
    if (h26x) {
        std::array<AndroidCodecCandidate, maxAndroidCandidates> software{};
        const usize count = android_codecs(codec_id(video_.codec), video_, false, software);
        for (usize i = 0; i < count; ++i) add(Rung::PlatformSoftware, software[i].name, false, software[i].pixel);
        android_video::CodecInfo platform;
        if (!count && android_video::find_encoder(hevc, video_.width, video_.height, video_.fps, false, platform))
            add(Rung::PlatformSoftware, platform.name, false);
    }
#endif
}

Status FFmpegSink::prepare_ffmpeg_video() noexcept {
    auto fail = [&](Errc code, const char* what, int result = 0) noexcept {
        char fferror[AV_ERROR_MAX_STRING_SIZE]{};
        if (result < 0) av_strerror(result, fferror, sizeof(fferror));
        std::snprintf(rungDetail_, sizeof(rungDetail_), result < 0 ? "%s: %s" : "%s%s", what, fferror);
        return Status{code, rungDetail_};
    };
    if (videoCodec_->width != static_cast<int>(video_.width) ||
        videoCodec_->height != static_cast<int>(video_.height)) {
        const AVBitStreamFilter* crop = av_bsf_get_by_name("hevc_metadata");
        int result = crop ? av_bsf_alloc(crop, &videoCrop_) : AVERROR_BSF_NOT_FOUND;
        if (result < 0) return fail(Errc::EncoderUnavailable, "filtro de dimensoes HEVC FFmpeg ausente", result);
        if ((result = avcodec_parameters_from_context(videoCrop_->par_in, videoCodec_)) < 0)
            return fail(Errc::EncodeFailed, "parametros do crop HEVC FFmpeg", result);
        videoCrop_->time_base_in = videoTimebase_;
        if ((result = av_opt_set_int(videoCrop_->priv_data, "crop_left", 0, 0)) < 0 ||
            (result = av_opt_set_int(videoCrop_->priv_data, "crop_top", 0, 0)) < 0 ||
            (result = av_opt_set_int(videoCrop_->priv_data, "crop_right", videoCodec_->width - video_.width, 0)) < 0 ||
            (result = av_opt_set_int(videoCrop_->priv_data, "crop_bottom", videoCodec_->height - video_.height, 0)) < 0 ||
            (result = av_bsf_init(videoCrop_)) < 0)
            return fail(Errc::EncodeFailed, "preservar dimensoes visiveis HEVC FFmpeg", result);
    }
    videoFrame_ = av_frame_alloc();
    if (!videoFrame_) return fail(Errc::OutOfMemory, "frame de video FFmpeg");
    videoFrame_->format = videoCodec_->pix_fmt;
    videoFrame_->width = videoCodec_->width; videoFrame_->height = videoCodec_->height;
    videoFrame_->color_primaries = videoCodec_->color_primaries;
    videoFrame_->color_trc = videoCodec_->color_trc; videoFrame_->colorspace = videoCodec_->colorspace;
    videoFrame_->color_range = videoCodec_->color_range;
    const int result = av_frame_get_buffer(videoFrame_, 32);
    if (result < 0) return fail(Errc::OutOfMemory, "pixels do frame FFmpeg", result);
    if (videoCodec_->pix_fmt != AV_PIX_FMT_NV12) {
        scale_ = sws_getContext(static_cast<int>(video_.width), static_cast<int>(video_.height), AV_PIX_FMT_NV12,
            static_cast<int>(video_.width), static_cast<int>(video_.height), videoCodec_->pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale_) return fail(Errc::OutOfMemory, "conversor libswscale");
        const int* coefficients = sws_getCoefficients(SWS_CS_ITU709);
        if (sws_setColorspaceDetails(scale_, coefficients, video_.color.fullRange ? 1 : 0,
            coefficients, video_.color.fullRange ? 1 : 0, 0, 1 << 16, 1 << 16) < 0)
            return fail(Errc::EncodeFailed, "configurar cor libswscale");
    }
    return OkStatus;
}

Status FFmpegSink::open_rung(const RungEntry& rung) noexcept {
    close_video();
    describe_rung(rung, rungName_, sizeof(rungName_));
    const bool hevc = video_.codec == ExportCodec::HEVC;
    Status opened = OkStatus;
    switch (rung.kind) {
    case Rung::Surface:
    case Rung::Buffer: {
#if defined(__ANDROID__)
        namespace avenc = android_video;
        const bool surface = rung.kind == Rung::Surface;
        const char* tag = surface ? "surface" : "buffer";
        char flag[32];
        std::snprintf(flag, sizeof(flag), "%s-open", tag);
        if (avenc::debug_flag("debug.aurea.enc.fail", flag))
            return Status{Errc::EncoderUnavailable, "falha simulada na abertura (debug.aurea.enc.fail)"};
        if (!native_) native_.reset(new (std::nothrow) avenc::Encoder());
        if (!native_) return Status{Errc::OutOfMemory, "encoder do aparelho"};
        avenc::Request request;
        request.hevc = hevc;
        request.width = video_.width;
        request.height = video_.height;
        request.fps = video_.fps;
        request.bitrateBps = video_.bitrateBps;
        request.rateMode = video_.rateMode;
        request.keyframeFrames = video_.keyframeIntervalFrames;
        request.baseline = video_.profile == 1;
        request.color = video_.color;
        request.input = surface ? avenc::Input::Surface : avenc::Input::Buffer;
        std::snprintf(flag, sizeof(flag), "%s-stall", tag);
        request.simulateStall = avenc::debug_flag("debug.aurea.enc.fail", flag);
        request.heartbeat = heartbeat_;
        request.simulateHang = surface && avenc::debug_flag("debug.aurea.enc.fail", "surface-hang");
        opened = native_->open(request, deviceCodec_);
        beat();
        if (!opened.ok()) {
            const auto text = opened.detail().empty() ? opened.message() : opened.detail();
            std::snprintf(rungDetail_, sizeof(rungDetail_), "%.*s", static_cast<int>(text.size()), text.data());
            native_->close();
            return Status{opened.code(), rungDetail_};
        }
        std::snprintf(rungName_, sizeof(rungName_), "%s", native_->describe());
        nativeActive_ = true;
        std::snprintf(encoder_.name, sizeof(encoder_.name), "%s", rung.codec);
        encoder_.acceleration = rung.hardware ? Acceleration::Hardware : Acceleration::Software;
        return OkStatus;
#else
        return Status{Errc::NotSupported, "encoder do aparelho so no Android"};
#endif
    }
    case Rung::Legacy:
        opened = try_video(hevc ? "hevc_mediacodec" : "h264_mediacodec", rung.hardware, rung.codec, rung.pixel);
        break;
    case Rung::PlatformSoftware:
        opened = try_video(hevc ? "hevc_mediacodec" : "h264_mediacodec", false, rung.codec, rung.pixel);
        break;
    case Rung::VideoToolbox:
        opened = try_video(rung.codec, true);
        break;
    case Rung::VideoToolboxPlanar:
        opened = try_video(rung.codec, true, nullptr, AV_PIX_FMT_YUV420P);
        break;
    case Rung::Software:
        opened = try_video(rung.codec, false);
        break;
    }
    if (opened.ok()) opened = prepare_ffmpeg_video();
    if (!opened.ok()) {
        close_video();
        return opened;
    }
    // Parâmetros globais do contêiner/registro (o crop do HEVC reescreve o SPS).
    const AVCodecParameters* cropped = videoCrop_ ? videoCrop_->par_out : nullptr;
    const u8* extra = cropped ? cropped->extradata : videoCodec_->extradata;
    const int extraBytes = cropped ? cropped->extradata_size : videoCodec_->extradata_size;
    try {
        if (extra && extraBytes > 0) extradata_.assign(extra, extra + extraBytes);
    } catch (...) {
        close_video();
        return Status{Errc::OutOfMemory, "parametros do encoder"};
    }
    return OkStatus;
}

Status FFmpegSink::open_video(usize firstRung) noexcept {
    Status last{Errc::EncoderUnavailable, "nenhum encoder FFmpeg compativel disponivel"};
    for (usize i = firstRung; i < rungs_.size(); ++i) {
        if (cancelled()) break;
        rung_ = i;
        last = open_rung(rungs_[i]);
        if (last.ok() && logMode_ && video_.resumeFrame && !extradata_.empty()) {
            // Retomada: outro SPS/PPS não continua o registro. Encoder do
            // aparelho só mostra os parâmetros no 1o pacote (start_log).
            export_log::StreamInfo candidate;
            fill_log_info(candidate);
            try { candidate.extradata = extradata_; } catch (...) { candidate.extradata.clear(); }
            if (!resumeScan_.info.same_stream(candidate)) {
                close_video();
                last = Status{Errc::UnsupportedFormat, "o encoder desta sessao difere do registro"};
            }
        }
        if (last.ok()) {
            probation_ = firstPacketPending_ = true;
            AUREA_LOG_INFO("export: encoder de video %s (degrau %u de %u)", rungName_,
                           static_cast<unsigned>(i + 1), static_cast<unsigned>(rungs_.size()));
            publish_path("abrindo");
            return OkStatus;
        }
        const auto text = last.detail().empty() ? last.message() : last.detail();
        char why[160];
        std::snprintf(why, sizeof(why), "%.*s", static_cast<int>(text.size()), text.data());
        note_failure(why);
        if (last.code() == Errc::OutOfMemory) break;
    }
    rung_ = rungs_.size();
    return error(cancelled() ? Errc::Cancelled :
        last.code() == Errc::OutOfMemory ? Errc::OutOfMemory : Errc::EncoderUnavailable,
        "FFmpeg nao abriu encoder solicitado nem fallback por software");
}

void FFmpegSink::note_failure(const char* why) noexcept {
    AUREA_LOG_WARN("export: degrau %s falhou: %s", rungName_, why);
    const usize used = std::strlen(attempts_);
    if (used + 8 < sizeof(attempts_))
        std::snprintf(attempts_ + used, sizeof(attempts_) - used, "%s%s=%.60s", used ? "; " : "", rungName_, why);
    if (rung_ < rungs_.size() && (rungs_[rung_].kind == Rung::Surface || rungs_[rung_].kind == Rung::Buffer) && !cancelled()) {
        char key[192];
        describe_rung(rungs_[rung_], key, sizeof(key));
        remember_failed_rung(key);
    }
    publish_path("falhou");
}

void FFmpegSink::publish_path(const char* state) const noexcept {
    char text[640];
    std::snprintf(text, sizeof(text), "%s %s%s%s", rungName_, state, attempts_[0] ? " | falhas: " : "", attempts_);
    // Diagnóstico da sessão (JSON do relato de bug, Android e iOS): o degrau
    // em uso e os que falharam, como no 0.1.8 BETA 2.
    if (diagnostics_) diagnostics_->encoder_path(text);
    // Só o export (a prévia em proxy usa o mesmo sink e não conta).
    if (!logMode_ && !video_.validateBeforePublish) return;
    set_last_export_encoder_path(text);
}

void FFmpegSink::reset_video_clock() noexcept {
    pendingHead_ = pendingCount_ = 0;
    const u64 resume = logMode_ ? video_.resumeFrame : 0;
    videoFrames_ = videoPackets_ = resume;
    videoHash_ = hashSeed;
    for (u64 frame = 0; frame < resume; ++frame) videoHash_ = timestamp_hash(videoHash_, frame_pts(frame));
    lastVideoPts_ = resume ? frame_pts(resume - 1) : AV_NOPTS_VALUE;
    lastVideoEnd_ = resume ? frame_pts(resume) : 0;
    timing_ = logMode_ ? Timing::Constant : Timing::Unknown;
    nativeEos_ = false;
}

bool FFmpegSink::fallback_allowed(Status status) const noexcept {
    if (!probation_ || cancelled() || rung_ + 1 >= rungs_.size()) return false;
    // Contêiner direto: trocar o encoder reabre o arquivo; só sem áudio gravado.
    if (!logMode_ && audioInputSamples_) return false;
    switch (status.code()) {
        case Errc::EncodeFailed: case Errc::EncoderUnavailable: case Errc::UnsupportedFormat:
        case Errc::UnsupportedCodec: case Errc::Timeout: case Errc::CorruptData: case Errc::DecodeFailed:
        case Errc::NotSupported:
            return true;
        default:
            return false;
    }
}

Status FFmpegSink::fallback(Status reason) noexcept {
    for (;;) {
        const auto text = reason.detail().empty() ? reason.message() : reason.detail();
        char why[160];
        std::snprintf(why, sizeof(why), "%.*s", static_cast<int>(text.size()), text.data());
        if (!fallback_allowed(reason)) {
            if (probation_ && !cancelled() && reason.code() != Errc::Cancelled) note_failure(why);
            return reason;
        }
        note_failure(why);
        const usize next = rung_ + 1;
        Status opened = OkStatus;
        if (logMode_) {
            // Registro: nada foi gravado antes do 1o pacote; só o encoder muda.
            close_video();
            opened = open_video(next);
            if (opened.ok()) {
                state_ = State::Open;
                lastError_ = OkStatus;
                reset_video_clock();
            }
        } else {
            std::string path;
            try { path = target_; } catch (...) { return error(Errc::OutOfMemory, "reabrir exportacao no proximo encoder"); }
            const VideoStreamConfig video = video_;
            const AudioStreamConfig audio = audio_;
            const bool withAudio = hasAudio_;
            std::vector<Retained> keep = std::move(retained_);
            reopenFrom_ = next;
            opened = open(path.c_str(), video, withAudio ? &audio : nullptr);
            retained_ = std::move(keep);
        }
        if (!opened.ok()) return opened;
        AUREA_LOG_WARN("export: seguindo no degrau %s com %u quadros guardados", rungName_,
                       static_cast<unsigned>(retained_.size()));
        const Status resent = resubmit_retained();
        if (resent.ok()) return OkStatus;
        reason = resent;
    }
}

Status FFmpegSink::retain(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts, i64 duration, bool timed) noexcept {
    try {
        Retained frame;
        const usize w = video_.width, h = video_.height;
        frame.y.resize(w * h);
        frame.uv.resize(w * (h / 2));
        export_detail::copy_nv12_plane(frame.y.data(), w, y, ys, video_.width, video_.height);
        export_detail::copy_nv12_plane(frame.uv.data(), w, uv, uvs, video_.width, video_.height / 2);
        frame.pts = pts;
        frame.duration = duration;
        frame.timed = timed;
        retained_.push_back(std::move(frame));
    } catch (...) {
        // Sem memória para guardar: o degrau atual segue sem rede (sem troca).
        AUREA_LOG_WARN("export: sem memoria para guardar quadros da partida; %s segue sem troca de degrau", rungName_);
        std::vector<Retained>().swap(retained_);
        probation_ = false;
    }
    return OkStatus;
}

Status FFmpegSink::resubmit_retained() noexcept {
    std::vector<Retained> frames = std::move(retained_);
    retained_.clear();
    try { retained_.reserve(frames.size()); } catch (...) { return error(Errc::OutOfMemory, "quadros da partida"); }
    const u32 stride = video_.width;
    for (usize i = 0; i < frames.size(); ++i) {
        Retained* frame = &frames[i];
        if (probation_) {
            retained_.push_back(std::move(frames[i]));   // capacidade reservada: não realoca
            frame = &retained_.back();
        }
        const Status written = write_video_impl(frame->y.data(), stride, frame->uv.data(), stride,
                                                frame->pts, frame->duration, frame->timed);
        if (!written.ok()) {
            if (probation_)
                for (usize j = i + 1; j < frames.size(); ++j) retained_.push_back(std::move(frames[j]));
            return written;
        }
    }
    return OkStatus;
}

Status FFmpegSink::wait_first_packet() noexcept {
    const u64 started = monotonic_ns();
    while (probation_) {
        if (cancelled()) return error(Errc::Cancelled, "exportacao FFmpeg cancelada");
        unsigned produced = 0;
        if (const Status status = drain(videoCodec_, videoStream_, true, false, produced); !status.ok()) return status;
        if (!probation_ || produced) continue;
        if (monotonic_ns() - started > startupStallNs)
            return error(Errc::Timeout, "o encoder nao entregou o primeiro pacote (partida travada)");
        beat();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return OkStatus;
}

void FFmpegSink::fill_native_parameters(AVCodecParameters* par) const noexcept {
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = codec_id(video_.codec);
    par->codec_tag = 0;
    par->format = AV_PIX_FMT_YUV420P;
    par->width = static_cast<int>(video_.width);
    par->height = static_cast<int>(video_.height);
    par->bit_rate = video_.bitrateBps;
    par->sample_aspect_ratio = AVRational{1, 1};
    par->framerate = rate_;
    par->color_primaries = static_cast<AVColorPrimaries>(video_.color.primaries);
    par->color_trc = static_cast<AVColorTransferCharacteristic>(video_.color.transfer);
    par->color_space = static_cast<AVColorSpace>(video_.color.matrix);
    par->color_range = video_.color.fullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    par->chroma_location = AVCHROMA_LOC_LEFT;
    av_freep(&par->extradata);
    par->extradata_size = 0;
    if (!extradata_.empty()) {
        par->extradata = static_cast<u8*>(av_mallocz(extradata_.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (par->extradata) {
            std::memcpy(par->extradata, extradata_.data(), extradata_.size());
            par->extradata_size = static_cast<int>(extradata_.size());
        }
    }
}

Status FFmpegSink::first_packet() noexcept {
    if (extradata_.empty() && nativeActive_) {
        try { extradata_ = native_->codec_config(); } catch (...) { extradata_.clear(); }
    }
    if (extradata_.empty() && packet_->data && packet_->size > 0) {
        try { extradata_ = annexb_parameter_sets(packet_->data, static_cast<usize>(packet_->size), video_.codec == ExportCodec::HEVC); }
        catch (...) { extradata_.clear(); }
    }
    // Encoder do aparelho (os dois degraus novos e o caminho FFmpeg/MediaCodec
    // antigo, que presume o layout do buffer): o 1o quadro decodificado tem de
    // bater com o NV12 entregue. Software do app é confiável: não confere.
    const bool legacy = rung_ < rungs_.size() && rungs_[rung_].kind == Rung::Legacy;
    if (nativeActive_ || legacy) {
#if defined(__ANDROID__)
        const bool surface = rung_ < rungs_.size() && rungs_[rung_].kind == Rung::Surface;
        if (nativeActive_ && android_video::debug_flag("debug.aurea.enc.fail", surface ? "surface-verify" : "buffer-verify"))
            return error(Errc::CorruptData, "verificacao do 1o quadro reprovada (simulada, debug.aurea.enc.fail)");
#endif
        if (const Status verified = verify_first_frame(); !verified.ok()) return verified;
    }
    if (logMode_) {
        if (const Status started = start_log(); !started.ok()) return started;
    } else if (videoStream_ && videoStream_->codecpar->extradata_size <= 0 && !extradata_.empty()) {
        // O cabeçalho já foi escrito sem SPS/PPS (encoder do aparelho): o MP4
        // os copia da faixa no 1o pacote.
        videoStream_->codecpar->extradata = static_cast<u8*>(av_mallocz(extradata_.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!videoStream_->codecpar->extradata) return error(Errc::OutOfMemory, "parametros de video do contêiner");
        std::memcpy(videoStream_->codecpar->extradata, extradata_.data(), extradata_.size());
        videoStream_->codecpar->extradata_size = static_cast<int>(extradata_.size());
    }
    probation_ = firstPacketPending_ = false;
    AUREA_LOG_INFO("export: encoder de video %s confirmado no 1o pacote", rungName_);
    publish_path("ok");
    return OkStatus;
}

Status FFmpegSink::verify_first_frame() noexcept {
    if (retained_.empty()) return OkStatus;
    const AVCodec* decoder = avcodec_find_decoder(codec_id(video_.codec));
    if (!decoder) {
        AUREA_LOG_WARN("export: sem decoder para conferir o 1o quadro de %s", rungName_);
        return OkStatus;
    }
    struct Resources {
        AVCodecContext* context = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* frame = nullptr;
        ~Resources() { avcodec_free_context(&context); av_packet_free(&packet); av_frame_free(&frame); }
    } r;
    r.context = avcodec_alloc_context3(decoder);
    r.packet = av_packet_alloc();
    r.frame = av_frame_alloc();
    if (!r.context || !r.packet || !r.frame) return OkStatus;   // sem memória para conferir: segue
    if (!extradata_.empty()) {
        r.context->extradata = static_cast<u8*>(av_mallocz(extradata_.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!r.context->extradata) return OkStatus;
        std::memcpy(r.context->extradata, extradata_.data(), extradata_.size());
        r.context->extradata_size = static_cast<int>(extradata_.size());
    }
    r.context->thread_count = 1;
    if (avcodec_open2(r.context, decoder, nullptr) < 0 || av_packet_ref(r.packet, packet_) < 0) return OkStatus;
    int code = avcodec_send_packet(r.context, r.packet);
    if (code >= 0) code = avcodec_send_packet(r.context, nullptr);
    if (code >= 0) code = avcodec_receive_frame(r.context, r.frame);
    beat();
    if (code < 0) return error(Errc::CorruptData, "o 1o pacote do encoder nao decodifica");
    const AVFrame* f = r.frame;
    const u32 w = video_.width, h = video_.height;
    if (f->width != static_cast<int>(w) || f->height != static_cast<int>(h)) {
        char why[128];
        std::snprintf(why, sizeof(why), "1o quadro do encoder saiu %dx%d em vez de %ux%u", f->width, f->height, w, h);
        return error(Errc::CorruptData, why);
    }
    const bool planar = f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P;
    if (!planar && f->format != AV_PIX_FMT_NV12) {
        AUREA_LOG_WARN("export: 1o quadro de %s em formato %d; conferencia de cor pulada", rungName_, f->format);
        return OkStatus;
    }
    // Média por bloco (≈ 16 × 9 blocos): ruído de compressão some na média;
    // matriz/faixa trocada, quadro de cabeça para baixo ou layout errado não.
    const Retained& source = retained_.front();
    const u32 blocksX = std::clamp<u32>(w / 16u, 1u, 16u), blocksY = std::clamp<u32>(h / 16u, 1u, 9u);
    const u32 bw = std::max<u32>(2, (w / blocksX) & ~1u), bh = std::max<u32>(2, (h / blocksY) & ~1u);
    f64 sumY = 0.0, sumC = 0.0, worstY = 0.0, worstC = 0.0;
    u32 blocks = 0;
    for (u32 by = 0; by + bh <= h; by += bh) {
        for (u32 bx = 0; bx + bw <= w; bx += bw) {
            f64 ys = 0.0, yd = 0.0, cbs = 0.0, cbd = 0.0, crs = 0.0, crd = 0.0;
            for (u32 y = by; y < by + bh; ++y) {
                const u8* s = source.y.data() + static_cast<usize>(y) * w;
                const u8* d = f->data[0] + static_cast<usize>(y) * static_cast<usize>(f->linesize[0]);
                for (u32 x = bx; x < bx + bw; ++x) { ys += s[x]; yd += d[x]; }
            }
            for (u32 y = by / 2; y < (by + bh) / 2; ++y) {
                const u8* s = source.uv.data() + static_cast<usize>(y) * w;
                for (u32 x = bx / 2; x < (bx + bw) / 2; ++x) {
                    cbs += s[2 * x];
                    crs += s[2 * x + 1];
                    if (planar) {
                        cbd += f->data[1][static_cast<usize>(y) * static_cast<usize>(f->linesize[1]) + x];
                        crd += f->data[2][static_cast<usize>(y) * static_cast<usize>(f->linesize[2]) + x];
                    } else {
                        const u8* d = f->data[1] + static_cast<usize>(y) * static_cast<usize>(f->linesize[1]);
                        cbd += d[2 * x];
                        crd += d[2 * x + 1];
                    }
                }
            }
            const f64 lumaCount = static_cast<f64>(bw) * bh, chromaCount = lumaCount / 4.0;
            const f64 ey = std::abs(ys - yd) / lumaCount;
            const f64 ec = std::max(std::abs(cbs - cbd), std::abs(crs - crd)) / chromaCount;
            sumY += ey; sumC += ec;
            worstY = std::max(worstY, ey); worstC = std::max(worstC, ec);
            ++blocks;
        }
    }
    if (!blocks) return OkStatus;
    const f64 meanY = sumY / blocks, meanC = sumC / blocks;
    AUREA_LOG_INFO("export: 1o quadro de %s x NV12 entregue: Y media %.2f pior %.1f, CbCr media %.2f pior %.1f (%u blocos)",
                   rungName_, meanY, worstY, meanC, worstC, blocks);
    if (meanY > verifyMeanLimit || meanC > verifyMeanLimit || worstY > verifyBlockLimit || worstC > verifyBlockLimit) {
        char why[160];
        std::snprintf(why, sizeof(why), "1o quadro do encoder diverge do original (Y %.1f/%.0f, CbCr %.1f/%.0f)",
                      meanY, worstY, meanC, worstC);
        return error(Errc::CorruptData, why);
    }
    return OkStatus;
}

Status FFmpegSink::start_log() noexcept {
    if (logStarted_) return OkStatus;
    if (extradata_.empty() || extradata_.size() > export_log::kMaxExtradataBytes)
        return error(Errc::EncoderUnavailable, "encoder sem parametros globais para o registro de pacotes");
    try { logInfo_.extradata = extradata_; }
    catch (...) { return error(Errc::OutOfMemory, "parametros do registro de pacotes"); }
    const Status opened = video_.resumeFrame ? log_.resume(logPath_, logInfo_, resumeScan_)
                                             : log_.create(logPath_, logInfo_);
    beat();
    if (!opened.ok()) {
        char why[200];
        const auto text = opened.detail().empty() ? std::string_view{"abrir registro de pacotes"} : opened.detail();
        std::snprintf(why, sizeof(why), "%.*s", static_cast<int>(text.size()), text.data());
        return error(opened.code(), why);
    }
    logStarted_ = true;
    return OkStatus;
}

Status FFmpegSink::send_native(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 us) noexcept {
    auto failed = [&](Status status) noexcept {
        const auto text = status.detail().empty() ? status.message() : status.detail();
        char why[200];
        std::snprintf(why, sizeof(why), "%.*s", static_cast<int>(text.size()), text.data());
        return error(status.code(), why);
    };
    // Surface: no máximo `limit` quadros sem pacote. Passar disso travaria
    // DENTRO do EGL se o encoder parasse; aqui a espera tem prazo.
    const u32 limit = native_->max_in_flight();
    u64 waited = monotonic_ns();
    while (limit && pendingCount_ >= limit) {
        if (cancelled()) return error(Errc::Cancelled, "encoder do aparelho cancelado");
        unsigned produced = 0;
        if (const Status status = drain_native(false, produced); !status.ok()) return status;
        if (produced) { waited = monotonic_ns(); continue; }
        if (monotonic_ns() - waited > stall_ns())
            return error(Errc::Timeout, probation_ ? "o encoder nao entregou o primeiro pacote (partida travada)"
                                                   : "o encoder do aparelho parou de devolver quadros");
        beat();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    u64 started = monotonic_ns();
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "encoder do aparelho cancelado");
        const Status sent = native_->send(y, ys, uv, uvs, us, stall_ns());
        beat();
        if (sent.ok()) return OkStatus;
        if (sent.code() != Errc::Busy) return failed(sent);
        unsigned produced = 0;
        if (const Status status = drain_native(false, produced); !status.ok()) return status;
        if (produced) { started = monotonic_ns(); continue; }
        if (monotonic_ns() - started > stall_ns())
            return error(Errc::Timeout, probation_ ? "o encoder nao entregou o primeiro pacote (partida travada)"
                                                   : "encoder do aparelho sem entrada livre");
    }
}

Status FFmpegSink::drain_native(bool flushing, unsigned& produced) noexcept {
    produced = 0;
    u64 progress = monotonic_ns();
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "drenagem do encoder cancelada");
        if (nativeEos_) return OkStatus;
        bool got = false, eos = false;
        const Status received = native_->receive(nativePacket_, got, eos, flushing ? 10'000 : 0);
        beat();
        if (received.code() == Errc::Busy) {
            if (!flushing) return OkStatus;
            if (monotonic_ns() - progress > stall_ns())
                return error(Errc::Timeout, "encoder do aparelho nao concluiu EOS");
            continue;
        }
        if (!received.ok()) {
            const auto text = received.detail().empty() ? received.message() : received.detail();
            char why[200];
            std::snprintf(why, sizeof(why), "%.*s", static_cast<int>(text.size()), text.data());
            return error(received.code(), why);
        }
        if (got) {
            ++produced;
            progress = monotonic_ns();
            if (!pendingCount_ || nativePacket_.ptsUs != pendingVideo_[pendingHead_].us)
                return error(Errc::EncodeFailed, "encoder do aparelho omitiu, duplicou ou reordenou quadro");
            av_packet_unref(packet_);
            const int made = av_new_packet(packet_, static_cast<int>(nativePacket_.data.size()));
            if (made < 0) return error(Errc::OutOfMemory, "pacote do encoder do aparelho", made);
            std::memcpy(packet_->data, nativePacket_.data.data(), nativePacket_.data.size());
            packet_->pts = packet_->dts = pendingVideo_[pendingHead_].pts;
            if (nativePacket_.key) packet_->flags |= AV_PKT_FLAG_KEY;
            if (const Status status = video_packet(videoTimebase_); !status.ok()) return status;
            av_packet_unref(packet_);
        }
        if (eos) {
            nativeEos_ = true;
            return OkStatus;
        }
    }
}

Status FFmpegSink::finish_video_stream() noexcept {
    for (;;) {
        Status result = OkStatus;
        if (nativeActive_) {
            const u64 started = monotonic_ns();
            for (;;) {
                if (cancelled()) { result = error(Errc::Cancelled, "EOS do encoder cancelado"); break; }
                const Status eos = native_->end_of_stream();
                beat();
                if (eos.ok()) break;
                if (eos.code() != Errc::Busy) { result = error(eos.code(), "encoder do aparelho recusou o EOS"); break; }
                unsigned produced = 0;
                if (const Status status = drain_native(false, produced); !status.ok()) { result = status; break; }
                if (!produced && monotonic_ns() - started > stall_ns()) {
                    result = error(Errc::Timeout, "encoder do aparelho sem entrada para o EOS");
                    break;
                }
            }
            unsigned produced = 0;
            if (result.ok()) result = drain_native(true, produced);
            if (result.ok() && !nativeEos_) result = error(Errc::EncodeFailed, "encoder do aparelho terminou sem EOS");
        } else {
            unsigned produced = 0;
            result = send(videoCodec_, videoStream_, nullptr, true);
            if (result.ok()) result = drain(videoCodec_, videoStream_, true, true, produced);
        }
        if (result.ok() && (pendingCount_ || videoPackets_ != videoFrames_))
            result = error(Errc::EncodeFailed, "encoder FFmpeg nao entregou todos os frames submetidos");
        if (result.ok() && videoCrop_) {
            int code = av_bsf_send_packet(videoCrop_, nullptr);
            if (code >= 0) code = av_bsf_receive_packet(videoCrop_, packet_);
            if (code != AVERROR_EOF)
                result = error(Errc::EncodeFailed, "crop HEVC FFmpeg nao concluiu EOS sem frames adicionais", code);
        }
        if (result.ok()) return OkStatus;
        // Clipe curto: o encoder travou/recusou antes do 1o pacote — o
        // próximo degrau recebe todos os quadros guardados e fecha de novo.
        if (!probation_) return result;
        if (const Status moved = fallback(result); !moved.ok()) return moved;
    }
}

Status FFmpegSink::open_audio() noexcept {
    const AVCodec* codec = avcodec_find_encoder_by_name("aac");
    if (!codec) return error(Errc::EncoderUnavailable, "encoder AAC FFmpeg ausente");
    audioCodec_ = avcodec_alloc_context3(codec);
    if (!audioCodec_) return error(Errc::OutOfMemory, "contexto AAC FFmpeg");
    audioRate_ = static_cast<int>(audio_.sampleRate);
    const void* supported = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(audioCodec_, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0,
                                     &supported, &count) >= 0 && supported && count) {
        const auto* rates = static_cast<const int*>(supported);
        audioRate_ = rates[0];
        for (int i = 0; i < count; ++i)
            if (std::abs(rates[i] - static_cast<int>(audio_.sampleRate)) <
                std::abs(audioRate_ - static_cast<int>(audio_.sampleRate))) audioRate_ = rates[i];
    }
    audioCodec_->sample_rate = audioRate_;
    audioCodec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audioCodec_->time_base = AVRational{1, audioRate_};
    audioCodec_->bit_rate = audio_.bitrateBps;
    av_channel_layout_default(&audioCodec_->ch_layout, static_cast<int>(audio_.channels));
    if (format_->oformat->flags & AVFMT_GLOBALHEADER) audioCodec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int result = avcodec_open2(audioCodec_, codec, nullptr); beat();
    if (result < 0) return error(Errc::EncoderUnavailable, "abrir AAC FFmpeg", result);
    audioFrameSamples_ = audioCodec_->frame_size;
    if (audioFrameSamples_ <= 0 || audioFrameSamples_ > 8192)
        return error(Errc::EncodeFailed, "tamanho de frame AAC FFmpeg invalido");
    AVChannelLayout input{};
    av_channel_layout_default(&input, static_cast<int>(audio_.channels));
    result = swr_alloc_set_opts2(&resample_, &audioCodec_->ch_layout, audioCodec_->sample_fmt, audioRate_,
                               &input, AV_SAMPLE_FMT_S16, static_cast<int>(audio_.sampleRate), 0, nullptr);
    av_channel_layout_uninit(&input);
    if (result < 0 || !resample_) return error(Errc::OutOfMemory, "criar libswresample", result);
    if ((result = swr_init(resample_)) < 0) return error(Errc::EncodeFailed, "iniciar libswresample", result);
    audioFifo_ = av_audio_fifo_alloc(audioCodec_->sample_fmt, audioCodec_->ch_layout.nb_channels,
                                   audioFrameSamples_ * 2);
    audioFrame_ = av_frame_alloc(); resampleFrame_ = av_frame_alloc();
    if (!audioFifo_ || !audioFrame_ || !resampleFrame_) return error(Errc::OutOfMemory, "fila AAC limitada");
    for (AVFrame* frame : {audioFrame_, resampleFrame_}) {
        frame->format = audioCodec_->sample_fmt; frame->sample_rate = audioRate_;
        if ((result = av_channel_layout_copy(&frame->ch_layout, &audioCodec_->ch_layout)) < 0)
            return error(Errc::OutOfMemory, "layout AAC FFmpeg", result);
        frame->nb_samples = audioFrameSamples_;
        if ((result = av_frame_get_buffer(frame, 0)) < 0)
            return error(Errc::OutOfMemory, "amostras AAC FFmpeg", result);
    }
    return OkStatus;
}

Status FFmpegSink::mux(AVPacket* packet, AVRational from, AVStream* stream) noexcept {
    if (cancelled()) return error(Errc::Cancelled, "exportacao FFmpeg cancelada");
    av_packet_rescale_ts(packet, from, stream->time_base);
    packet->stream_index = stream->index;
    const int result = av_interleaved_write_frame(format_, packet); beat();
    if (result < 0) return error(Errc::EncodeFailed, "multiplexar pacote FFmpeg", result);
    if (format_->pb && format_->pb->error < 0)
        return error(Errc::IoError, "gravar pacote FFmpeg", format_->pb->error);
    event(stream == videoStream_ ? ExportDiagnostics::Counter::VideoPackets : ExportDiagnostics::Counter::AudioPackets);
    return OkStatus;
}

Status FFmpegSink::video_packet(AVRational from) noexcept {
    packet_->duration = pendingVideo_[pendingHead_].duration;
    pendingHead_ = (pendingHead_ + 1) % maxPendingVideo; --pendingCount_;
    ++videoPackets_;
    if (videoCrop_) {
        int filtered = av_bsf_send_packet(videoCrop_, packet_);
        if (filtered >= 0) filtered = av_bsf_receive_packet(videoCrop_, packet_);
        if (filtered < 0) return error(Errc::EncodeFailed, "aplicar dimensoes HEVC FFmpeg ao frame", filtered);
    }
    // 1o pacote do degrau: confere o quadro e abre o registro com os
    // parâmetros deste encoder. Falha aqui ainda troca de degrau.
    if (firstPacketPending_)
        if (const Status status = first_packet(); !status.ok()) return status;
    if (logMode_) {
        // Export retomável: o pacote vai para o registro durável, em ordem.
        const u64 frame = videoPackets_ - 1;
        if (packet_->pts != frame_pts(frame) || packet_->size <= 0)
            return error(Errc::EncodeFailed, "pacote do encoder fora do relogio do registro");
        const Status logged = log_.append(static_cast<u32>(frame), packet_->data,
            static_cast<usize>(packet_->size), (packet_->flags & AV_PKT_FLAG_KEY) != 0);
        beat();
        if (!logged.ok()) return error(logged.code(), logged.detail().empty() ? "registro de pacotes" : logged.detail().data());
    } else if (const Status status = mux(packet_, from, videoStream_); !status.ok()) return status;
    if (videoCrop_) {
        const int extra = av_bsf_receive_packet(videoCrop_, packet_);
        if (extra != AVERROR(EAGAIN))
            return error(Errc::EncodeFailed, "crop HEVC FFmpeg alterou a quantidade de frames", extra);
    }
    return OkStatus;
}

Status FFmpegSink::drain(AVCodecContext* codec, AVStream* stream, bool video,
                         bool flushing, unsigned& produced) noexcept {
    if (video && nativeActive_) return drain_native(flushing, produced);
    produced = 0;
    const u64 started = monotonic_ns();
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "drenagem FFmpeg cancelada");
        const int result = avcodec_receive_packet(codec, packet_); beat();
        if (result == AVERROR_EOF) return OkStatus;
        if (result == AVERROR(EAGAIN)) {
            event(ExportDiagnostics::Counter::OutputAgain);
            if (!flushing) return OkStatus;
            // Real-time hardware wrappers may need another poll after EOS.
            // A bounded timeout is an error, never a discarded final frame.
            if (monotonic_ns() - started > (video ? stall_ns() : codecStallNs))
                return error(Errc::Timeout, "encoder FFmpeg nao concluiu EOS");
            std::this_thread::yield();
            continue;
        }
        if (result < 0) return error(Errc::EncodeFailed, "receber pacote FFmpeg", result);
        ++produced;
        if (video) {
            if (!pendingCount_ || packet_->pts != pendingVideo_[pendingHead_].pts) {
                av_packet_unref(packet_);
                return error(Errc::EncodeFailed, "encoder FFmpeg omitiu, duplicou ou reordenou frame sem autorizacao");
            }
            if (const Status status = video_packet(codec->time_base); !status.ok()) return status;
        } else {
            // Keep only the final AAC packet so its exact duration/tail padding
            // can be written after all resampler samples have been accounted.
            if (pendingAudio_->size)
                if (const Status status = mux(pendingAudio_, codec->time_base, stream); !status.ok()) return status;
            av_packet_move_ref(pendingAudio_, packet_);
        }
        av_packet_unref(packet_);
    }
}

Status FFmpegSink::send(AVCodecContext* codec, AVStream* stream, const AVFrame* frame,
                        bool video) noexcept {
    const u64 started = monotonic_ns();
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "encoder FFmpeg cancelado");
        const int result = avcodec_send_frame(codec, frame); beat();
        if (result == AVERROR(EAGAIN)) {
            event(ExportDiagnostics::Counter::InputAgain);
            unsigned produced = 0;
            if (const Status status = drain(codec, stream, video, false, produced); !status.ok()) return status;
            if (!produced && monotonic_ns() - started > (video ? stall_ns() : codecStallNs))
                return error(Errc::Timeout, "encoder FFmpeg bloqueou sem produzir pacote");
            if (!produced) std::this_thread::yield();
            continue; // retry exactly the same frame; no skip/advance
        }
        if (result < 0) return error(Errc::EncodeFailed, "enviar frame FFmpeg", result);
        return OkStatus;
    }
}

Status FFmpegSink::write_video_with_fallback(const u8* y, u32 ys, const u8* uv, u32 uvs,
                                           i64 pts, i64 duration, bool timed) noexcept {
    if (const Status status = ready(); !status.ok()) return status;
    if (assembling_) return Status{Errc::InvalidState, "montagem do MP4 nao aceita quadros novos"};
    if (!y || !uv || ys < video_.width || uvs < video_.width || ys > INT_MAX || uvs > INT_MAX)
        return Status{Errc::InvalidArgument, "pixels ou tempo de frame FFmpeg invalidos"};
    if (!probation_ && !retained_.empty()) std::vector<Retained>().swap(retained_);
    if (probation_ && retained_.size() >= maxRetained_) {
        // Guardados no limite e nenhum pacote ainda: o encoder tem um prazo
        // curto para mostrar que anda; senão o próximo degrau assume.
        Status waited = wait_first_packet();
        if (!waited.ok()) waited = fallback(waited);
        if (!waited.ok()) return waited;
        if (!probation_) std::vector<Retained>().swap(retained_);
    }
    if (probation_)
        if (const Status kept = retain(y, ys, uv, uvs, pts, duration, timed); !kept.ok()) return kept;
    Status result = write_video_impl(y, ys, uv, uvs, pts, duration, timed);
    // Recusa/travada do encoder antes do 1o pacote: o quadro atual já está
    // guardado e vai junto com os outros para o próximo degrau.
    if (!result.ok() && probation_) result = fallback(result);
    if (result.ok()) ++completedVideoWrites_;
    if (!probation_ && !retained_.empty()) std::vector<Retained>().swap(retained_);
    return result;
}

Status FFmpegSink::write_video_impl(const u8* y, u32 ys, const u8* uv, u32 uvs,
                                   i64 pts, i64 duration, bool timed) noexcept {
    phase(ExportDiagnostics::Stage::VideoInput);
    if (const Status status = ready(); !status.ok()) return status;
    if (assembling_) return Status{Errc::InvalidState, "montagem do MP4 nao aceita quadros novos"};
    if (!y || !uv || ys < video_.width || uvs < video_.width || ys > INT_MAX || uvs > INT_MAX || pts < 0 ||
        (timed && (duration <= 0 || duration > INT64_MAX - pts)) || (videoFrames_ == 0 && pts != 0) ||
        (timing_ != Timing::Unknown && (timed != (timing_ == Timing::Variable))))
        return Status{Errc::InvalidArgument, "pixels ou tempo de frame FFmpeg invalidos"};
    i64 framePts = 0, frameDuration = 0;
    if (timed) {
        framePts = av_rescale_q(pts, micros, videoTimebase_);
        frameDuration = av_rescale_q(duration, micros, videoTimebase_);
    } else {
        if (videoFrames_ > static_cast<u64>(INT64_MAX / std::max(rate_.den, 1)))
            return Status{Errc::OutOfRange, "contador de frames FFmpeg excedido"};
        const i64 expectedUs = av_rescale_q(static_cast<i64>(videoFrames_), av_inv_q(rate_), micros);
        if (expectedUs < 0) return Status{Errc::OutOfRange, "tempo absoluto FFmpeg excedido"};
        if (std::abs(pts - expectedUs) > 2)
            return Status{Errc::InvalidArgument, "frame FFmpeg ausente ou PTS fora do relogio absoluto"};
        framePts = av_rescale_q(static_cast<i64>(videoFrames_), av_inv_q(rate_), videoTimebase_);
        const i64 next = av_rescale_q(static_cast<i64>(videoFrames_ + 1), av_inv_q(rate_), videoTimebase_);
        frameDuration = next - framePts;
    }
    if (framePts < 0 || frameDuration <= 0 || frameDuration > INT64_MAX - framePts ||
        (lastVideoPts_ != AV_NOPTS_VALUE && framePts <= lastVideoPts_))
        return Status{Errc::InvalidArgument, "PTS de frame FFmpeg duplicado ou nao monotono"};
    if (pendingCount_ >= pendingLimit_) {
        // Fila cheia: espera o encoder devolver pacotes (o mesmo prazo de
        // travamento do resto do sink) em vez de abortar na hora com 9.
        const u64 waitStarted = monotonic_ns();
        while (pendingCount_ >= pendingLimit_) {
            unsigned produced = 0;
            if (const Status status = drain(videoCodec_, videoStream_, true, false, produced); !status.ok()) return status;
            if (produced) continue;
            if (monotonic_ns() - waitStarted > stall_ns())
                return error(Errc::Timeout, "encoder FFmpeg nao devolveu pacotes com a fila de quadros cheia");
            beat();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // O tempo que o encoder do aparelho recebe (us) e devolve no pacote.
    const i64 frameUs = timed ? pts : av_rescale_q(framePts, videoTimebase_, micros);
    if (nativeActive_) {
        if (const Status status = send_native(y, ys, uv, uvs, frameUs); !status.ok()) return status;
        event(ExportDiagnostics::Counter::Submitted);
        pendingVideo_[(pendingHead_ + pendingCount_) % maxPendingVideo] = {framePts, frameDuration, frameUs};
        ++pendingCount_; ++videoFrames_;
        videoHash_ = timestamp_hash(videoHash_, framePts);
        lastVideoPts_ = framePts; lastVideoEnd_ = framePts + frameDuration;
        timing_ = timed ? Timing::Variable : Timing::Constant;
        unsigned produced = 0;
        return drain_native(false, produced);
    }
    int result = av_frame_make_writable(videoFrame_);
    if (result < 0) return error(Errc::OutOfMemory, "tornar frame FFmpeg gravavel", result);
    if (scale_) {
        const u8* source[4]{y, uv, nullptr, nullptr};
        const int strides[4]{static_cast<int>(ys), static_cast<int>(uvs), 0, 0};
        result = sws_scale(scale_, source, strides, 0, static_cast<int>(video_.height),
            videoFrame_->data, videoFrame_->linesize);
        if (result != static_cast<int>(video_.height))
            return error(Errc::EncodeFailed, "libswscale nao converteu todo o frame", result);
        if (videoCrop_) for (int plane = 0; plane < 3; ++plane) {
            const int shift = plane ? 1 : 0;
            const int pictureWidth = static_cast<int>(video_.width) >> shift;
            const int pictureHeight = static_cast<int>(video_.height) >> shift;
            const int storageWidth = videoCodec_->width >> shift;
            const int storageHeight = videoCodec_->height >> shift;
            const int black = plane ? 128 : video_.color.fullRange ? 0 : 16;
            for (int row = 0; row < pictureHeight; ++row)
                std::memset(videoFrame_->data[plane] + static_cast<usize>(row) * videoFrame_->linesize[plane] + pictureWidth,
                    black, static_cast<usize>(storageWidth - pictureWidth));
            for (int row = pictureHeight; row < storageHeight; ++row)
                std::memset(videoFrame_->data[plane] + static_cast<usize>(row) * videoFrame_->linesize[plane],
                    black, static_cast<usize>(storageWidth));
        }
    } else {
        export_detail::copy_nv12_plane(videoFrame_->data[0], videoFrame_->linesize[0],
                                      y, ys, video_.width, video_.height);
        export_detail::copy_nv12_plane(videoFrame_->data[1], videoFrame_->linesize[1],
                                      uv, uvs, video_.width, video_.height / 2);
    }
    videoFrame_->pts = framePts; videoFrame_->duration = frameDuration;
    if (const Status status = send(videoCodec_, videoStream_, videoFrame_, true); !status.ok()) return status;
    event(ExportDiagnostics::Counter::Submitted);
    pendingVideo_[(pendingHead_ + pendingCount_) % maxPendingVideo] = {framePts, frameDuration, frameUs};
    ++pendingCount_; ++videoFrames_;
    videoHash_ = timestamp_hash(videoHash_, framePts);
    lastVideoPts_ = framePts; lastVideoEnd_ = framePts + frameDuration;
    timing_ = timed ? Timing::Variable : Timing::Constant;
    unsigned produced = 0;
    return drain(videoCodec_, videoStream_, true, false, produced);
}

Status FFmpegSink::encode_audio(bool final) noexcept {
    while (av_audio_fifo_size(audioFifo_) >= audioFrameSamples_ ||
           (final && av_audio_fifo_size(audioFifo_) > 0)) {
        const int available = av_audio_fifo_size(audioFifo_);
        const int real = std::min(available, audioFrameSamples_);
        const bool small = (audioCodec_->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) != 0;
        const int samples = real < audioFrameSamples_ && small ? real : audioFrameSamples_;
        audioFrame_->nb_samples = audioFrameSamples_;
        int result = av_frame_make_writable(audioFrame_);
        if (result < 0) return error(Errc::OutOfMemory, "frame AAC gravavel", result);
        if (av_audio_fifo_read(audioFifo_, reinterpret_cast<void**>(audioFrame_->extended_data), real) != real)
            return error(Errc::EncodeFailed, "fila AAC nao entregou todas as amostras");
        if (samples > real && av_samples_set_silence(audioFrame_->extended_data, real, samples - real,
            audioCodec_->ch_layout.nb_channels, audioCodec_->sample_fmt) < 0)
            return error(Errc::EncodeFailed, "padding AAC FFmpeg");
        audioFrame_->nb_samples = samples; audioFrame_->pts = audioSubmittedSamples_;
        audioFrame_->duration = real;
        if (const Status status = send(audioCodec_, audioStream_, audioFrame_, false); !status.ok()) return status;
        audioSubmittedSamples_ += samples;
        unsigned produced = 0;
        if (const Status status = drain(audioCodec_, audioStream_, false, false, produced); !status.ok()) return status;
    }
    return OkStatus;
}

Status FFmpegSink::append_audio(const i16* pcm, int samples) noexcept {
    const u8* source[1]{reinterpret_cast<const u8*>(pcm)};
    int remaining = samples;
    bool first = true;
    do {
        resampleFrame_->nb_samples = audioFrameSamples_;
        int result = av_frame_make_writable(resampleFrame_);
        if (result < 0) return error(Errc::OutOfMemory, "buffer libswresample gravavel", result);
        result = swr_convert(resample_, resampleFrame_->extended_data, audioFrameSamples_,
                             source, first ? remaining : 0);
        first = false; remaining = 0; beat();
        if (result < 0) return error(Errc::EncodeFailed, "converter PCM por libswresample", result);
        if (result > 0) {
            // The FIFO contains less than one AAC frame before each append.
            // Conversion outputs at most one frame, so its allocation never grows.
            if (av_audio_fifo_size(audioFifo_) + result > audioFrameSamples_ * 2)
                return error(Errc::BudgetExceeded, "fila de audio FFmpeg excedeu limite");
            if (av_audio_fifo_write(audioFifo_, reinterpret_cast<void**>(resampleFrame_->extended_data), result) != result)
                return error(Errc::OutOfMemory, "enfileirar PCM FFmpeg");
            audioConvertedSamples_ += result;
            if (const Status status = encode_audio(false); !status.ok()) return status;
        }
        // Do not flush the resampling filter between source chunks. A zero-size
        // input with a nonnull pointer drains only output already buffered.
        if (swr_get_out_samples(resample_, 0) < audioFrameSamples_) break;
        source[0] = reinterpret_cast<const u8*>(pcm);
    } while (true);
    return OkStatus;
}

Status FFmpegSink::write_audio(const i16* pcm, u32 frames, i64 pts) noexcept {
    phase(ExportDiagnostics::Stage::AudioInput);
    if (const Status status = ready(); !status.ok()) return status;
    if (!hasAudio_ && !(logMode_ && !assembling_ && wantsAudio_))
        return Status{Errc::InvalidState, "exportacao FFmpeg nao possui faixa de audio"};
    if ((!pcm && frames) || pts < 0 || audioInputSamples_ > INT64_MAX - static_cast<i64>(frames))
        return Status{Errc::InvalidArgument, "PCM de exportacao FFmpeg invalido"};
    const i64 expected = av_rescale_q(audioInputSamples_, AVRational{1, static_cast<int>(audio_.sampleRate)}, micros);
    if (expected < 0) return Status{Errc::OutOfRange, "tempo absoluto de audio FFmpeg excedido"};
    if (std::abs(pts - expected) > 2)
        return Status{Errc::InvalidArgument, "PCM FFmpeg descontinuo; amostras ausentes ou duplicadas"};
    if (logMode_ && !assembling_) {
        // Durante o render: o PCM vai para o arquivo lateral (codificado na montagem).
        if (!spillAudio_ || !spill_) {
            // Sem arquivo lateral: o PCM é descartado e a montagem mixa o áudio de novo.
            audioInputSamples_ += frames;
            return OkStatus;
        }
        const usize bytes = static_cast<usize>(frames) * audio_.channels * sizeof(i16);
        if (frames && std::fwrite(pcm, 1, bytes, spill_) != bytes) {
            std::fclose(spill_); spill_ = nullptr; spillAudio_ = false;
            return error(errno == ENOSPC ? Errc::StorageFull : Errc::IoError, "gravar PCM do registro");
        }
        audioInputSamples_ += frames;
        spilledSamples_ += frames;
        beat();
        return OkStatus;
    }
    if (assembling_) {
        // Montagem: os quadros do registro que começam antes do fim deste
        // trecho de som entram primeiro — o arquivo sai intercalado.
        const i64 endSample = audioInputSamples_ + static_cast<i64>(frames);
        const i64 until = av_rescale_q_rnd(endSample, AVRational{1, static_cast<int>(audio_.sampleRate)},
            av_inv_q(rate_), static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
        if (const Status status = mux_logged_video(static_cast<u64>(std::clamp<i64>(until, 0, static_cast<i64>(videoFrames_))));
            !status.ok()) return status;
    }
    u32 consumed = 0;
    while (consumed < frames) {
        if (cancelled()) return error(Errc::Cancelled, "conversao de audio FFmpeg cancelada");
        const int count = static_cast<int>(std::min<u32>(frames - consumed, audioInputChunk));
        if (const Status status = append_audio(pcm + static_cast<usize>(consumed) * audio_.channels, count); !status.ok())
            return status;
        audioInputSamples_ += count; consumed += static_cast<u32>(count);
    }
    return OkStatus;
}

Status FFmpegSink::flush_resampler() noexcept {
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "EOS de audio FFmpeg cancelado");
        resampleFrame_->nb_samples = audioFrameSamples_;
        int result = av_frame_make_writable(resampleFrame_);
        if (result < 0) return error(Errc::OutOfMemory, "buffer EOS libswresample", result);
        result = swr_convert(resample_, resampleFrame_->extended_data, audioFrameSamples_, nullptr, 0);
        beat();
        if (result < 0) return error(Errc::EncodeFailed, "EOS libswresample", result);
        if (!result) break;
        if (av_audio_fifo_size(audioFifo_) + result > audioFrameSamples_ * 2)
            return error(Errc::BudgetExceeded, "EOS libswresample excedeu fila limitada");
        if (av_audio_fifo_write(audioFifo_, reinterpret_cast<void**>(resampleFrame_->extended_data), result) != result)
            return error(Errc::OutOfMemory, "fila EOS libswresample");
        audioConvertedSamples_ += result;
        if (const Status status = encode_audio(false); !status.ok()) return status;
    }
    const i64 expected = av_rescale_rnd(audioInputSamples_, audioRate_, audio_.sampleRate, AV_ROUND_NEAR_INF);
    if (std::abs(audioConvertedSamples_ - expected) > 1)
        return error(Errc::EncodeFailed, "libswresample alterou a duracao total do audio");
    return encode_audio(true);
}

Status FFmpegSink::finish() noexcept {
    phase(ExportDiagnostics::Stage::Drain);
    if (const Status status = ready(); !status.ok()) return status;
    if (logMode_ && !assembling_)
        return Status{Errc::InvalidState, "registro de pacotes: end_video e begin_assembly antes de finish"};
    if (!videoFrames_ || (hasAudio_ && !audioInputSamples_))
        return error(Errc::EncodeFailed, "exportacao FFmpeg sem frames ou amostras esperadas");
    if (hasAudio_) {
        const i64 expectedSamples = av_rescale_q(lastVideoEnd_, videoTimebase_,
            AVRational{1, static_cast<int>(audio_.sampleRate)});
        if (expectedSamples < 0 || std::abs(audioInputSamples_ - expectedSamples) > 2)
            return error(Errc::EncodeFailed, "audio e video FFmpeg possuem duracoes divergentes");
    }
    unsigned produced = 0;
    if (assembling_) {
        if (const Status status = mux_logged_video(videoFrames_); !status.ok()) return status;
        export_log::Packet extra; bool end = false;
        if (const Status status = logReader_.next(extra, end); !status.ok() || !end)
            return error(Errc::CorruptData, "registro de pacotes tem quadros alem do video exportado");
    } else if (const Status status = finish_video_stream(); !status.ok()) {
        return status;
    }
    videoStream_->duration = av_rescale_q(lastVideoEnd_, videoTimebase_, videoStream_->time_base);
    if (hasAudio_) {
        if (const Status status = flush_resampler(); !status.ok()) return status;
        if (const Status status = send(audioCodec_, audioStream_, nullptr, false); !status.ok()) return status;
        if (const Status status = drain(audioCodec_, audioStream_, false, true, produced); !status.ok()) return status;
        if (!pendingAudio_->size || pendingAudio_->pts == AV_NOPTS_VALUE ||
            pendingAudio_->pts < 0 || pendingAudio_->pts >= audioConvertedSamples_)
            return error(Errc::EncodeFailed, "AAC FFmpeg nao entregou o pacote final esperado");
        const i64 real = audioConvertedSamples_ - pendingAudio_->pts;
        const i64 packetSamples = std::max<i64>(pendingAudio_->duration, audioFrameSamples_);
        const u32 padding = static_cast<u32>(std::max<i64>(0, packetSamples - real));
        pendingAudio_->duration = real;
        if (padding) {
            size_t size = 0;
            u8* side = av_packet_get_side_data(pendingAudio_, AV_PKT_DATA_SKIP_SAMPLES, &size);
            if (!side) side = av_packet_new_side_data(pendingAudio_, AV_PKT_DATA_SKIP_SAMPLES, 10);
            if (!side) return error(Errc::OutOfMemory, "metadados de padding AAC");
            if (!size) std::memset(side, 0, 10);
            if (size && size < 10) return error(Errc::EncodeFailed, "metadados AAC de padding invalidos");
            AV_WL32(side + 4, padding); side[9] = 0;
            audioStream_->codecpar->trailing_padding = static_cast<int>(padding);
        }
        audioStream_->duration = av_rescale_q(audioConvertedSamples_, audioCodec_->time_base, audioStream_->time_base);
        if (const Status status = mux(pendingAudio_, audioCodec_->time_base, audioStream_); !status.ok()) return status;
    }
    int result = av_write_trailer(format_); beat();
    if (result < 0) return error(Errc::EncodeFailed, "finalizar container FFmpeg", result);
    avio_flush(format_->pb); beat();
    if (format_->pb->error < 0) return error(Errc::IoError, "flush do arquivo FFmpeg", format_->pb->error);
    result = avio_closep(&format_->pb); beat();
    if (result < 0) return error(Errc::IoError, "fechar arquivo FFmpeg", result);
    release(); state_ = State::Finalized;
    if (video_.validateBeforePublish) return OkStatus;
    if (videoFrames_ > UINT32_MAX) return error(Errc::OutOfRange, "validacao FFmpeg excedeu contador de frames");
    ExportOutputValidation expected{video_.width, video_.height, static_cast<u32>(videoFrames_), video_.fps, hasAudio_};
    expected.strict = video_.strictValidation;
    if (const Status status = decode_validate(expected); !status.ok()) return status;
    return publish();
}

void FFmpegSink::fill_log_info(export_log::StreamInfo& info) const noexcept {
    info.codec = static_cast<u32>(video_.codec);
    info.width = video_.width; info.height = video_.height;
    info.rateNum = rate_.num; info.rateDen = rate_.den;
    info.timebaseNum = videoTimebase_.num; info.timebaseDen = videoTimebase_.den;
    info.sessionKey = video_.sessionKey;
    info.totalFrames = video_.totalFrames;
}

Status FFmpegSink::open_log() noexcept {
    logInfo_ = {};
    fill_log_info(logInfo_);
    // O registro abre no 1o pacote (start_log), com os parâmetros globais
    // (avcC/hvcC) do degrau que provou funcionar — o encoder do aparelho só os
    // entrega junto com a saída. A retomada já foi varrida em open().
    logStarted_ = false;
    if (!packet_) packet_ = av_packet_alloc();
    if (!packet_) return error(Errc::OutOfMemory, "pacotes da exportacao FFmpeg");
    // PCM do render: só numa sessão do zero (retomada mixa tudo de novo no fim).
    spillAudio_ = false; spilledSamples_ = 0;
    try { spillPath_ = logPath_ + ".pcm"; }
    catch (...) { return error(Errc::OutOfMemory, "caminho do PCM do registro"); }
    (void)fileio::remove_file(spillPath_);
    if (audio_.sampleRate && audio_.channels && !video_.resumeFrame && wantsAudio_) {
        spill_ = fileio::open_file(spillPath_, "wb");
        spillAudio_ = spill_ != nullptr;
    }
    // A sessão continua o relógio absoluto do registro: quadro i -> PTS de i.
    reset_video_clock();
    state_ = State::Open;
    return OkStatus;
}

Status FFmpegSink::end_video() noexcept {
    if (!logMode_ || assembling_) return Status{Errc::InvalidState, "end_video so vale para o registro de pacotes"};
    if (const Status status = ready(); !status.ok()) return status;
    if (!videoFrames_) return error(Errc::EncodeFailed, "registro de pacotes sem quadros");
    if (const Status status = finish_video_stream(); !status.ok()) return status;
    if (!logStarted_ || log_.frames() != videoFrames_) return error(Errc::EncodeFailed, "registro de pacotes perdeu quadros");
    if (spill_) {
        const bool flushed = std::fflush(spill_) == 0;
        const bool closed = std::fclose(spill_) == 0;
        spill_ = nullptr;
        if (!flushed || !closed) spillAudio_ = false;   // sem PCM confiável: o motor mixa de novo
    }
    videoParams_ = avcodec_parameters_alloc();
    if (!videoParams_) return error(Errc::OutOfMemory, "parametros de video FFmpeg");
    int copied = 0;
    if (nativeActive_) fill_native_parameters(videoParams_);
    else copied = videoCrop_ ? avcodec_parameters_copy(videoParams_, videoCrop_->par_out)
                             : avcodec_parameters_from_context(videoParams_, videoCodec_);
    if (copied < 0) return error(Errc::EncodeFailed, "parametros de video FFmpeg", copied);
    // O encoder do aparelho é solto antes da montagem (memória e hardware).
    close_video();
    if (const Status closed = log_.close(); !closed.ok())
        return error(closed.code(), "fechar registro de pacotes");
    beat();
    lastVideoEnd_ = frame_pts(videoFrames_);
    state_ = State::VideoLogged;
    return OkStatus;
}

Status FFmpegSink::begin_assembly(const AudioStreamConfig* audio) noexcept {
    if (cancelled()) return error(Errc::Cancelled, "montagem FFmpeg cancelada");
    if (state_ != State::VideoLogged || !videoParams_ || !format_)
        return Status{Errc::InvalidState, "montagem exige o video inteiro no registro"};
    if (audio && (!audio->sampleRate || audio->sampleRate > 192'000 || !audio->channels ||
                  audio->channels > 8 || !audio->bitrateBps))
        return Status{Errc::InvalidArgument, "configuracao de audio FFmpeg invalida"};
    hasAudio_ = audio != nullptr;
    if (audio) {
        // O PCM do render só vale se veio na MESMA configuração de áudio.
        if (audio->sampleRate != audio_.sampleRate || audio->channels != audio_.channels) spillAudio_ = false;
        audio_ = *audio;
    }
    audioInputSamples_ = audioConvertedSamples_ = audioSubmittedSamples_ = 0;
    if (const Status opened = logReader_.open(logPath_); !opened.ok())
        return error(opened.code(), "reabrir registro de pacotes");
    if (!logReader_.info().same_stream(logInfo_))
        return error(Errc::CorruptData, "registro de pacotes mudou antes da montagem");
    if (hasAudio_) if (const Status status = open_audio(); !status.ok()) return status;
    state_ = State::Open;
    assembling_ = true;
    assembledVideo_ = 0;
    return open_container();
}

Status FFmpegSink::replay_spilled_audio() noexcept {
    if (!assembling_ || !hasAudio_) return Status{Errc::InvalidState, "PCM do render so na montagem com audio"};
    const i64 expected = av_rescale_q(lastVideoEnd_, videoTimebase_, AVRational{1, static_cast<int>(audio_.sampleRate)});
    if (!spillAudio_ || audioInputSamples_ != 0 || std::abs(spilledSamples_ - expected) > 2)
        return Status{Errc::NotFound, "sem PCM completo do render"};
    std::unique_ptr<std::FILE, decltype(&std::fclose)> f(fileio::open_file(spillPath_, "rb"), &std::fclose);
    if (!f) return Status{Errc::NotFound, "PCM do render ausente"};
    std::vector<i16> chunk(static_cast<usize>(audioInputChunk) * audio_.channels);
    i64 done = 0;
    while (done < spilledSamples_) {
        const u32 n = static_cast<u32>(std::min<i64>(audioInputChunk, spilledSamples_ - done));
        if (std::fread(chunk.data(), sizeof(i16) * audio_.channels, n, f.get()) != n)
            return error(Errc::IoError, "ler PCM do render");
        const i64 ptsUs = av_rescale_q(done, AVRational{1, static_cast<int>(audio_.sampleRate)}, micros);
        if (const Status status = write_audio(chunk.data(), n, ptsUs); !status.ok()) return status;
        done += n;
    }
    f.reset();
    (void)fileio::remove_file(spillPath_);
    return OkStatus;
}

Status FFmpegSink::mux_logged_video(u64 untilFrame) noexcept {
    while (assembledVideo_ < untilFrame) {
        if (cancelled()) return error(Errc::Cancelled, "montagem FFmpeg cancelada");
        bool end = false;
        if (const Status status = logReader_.next(logged_, end); !status.ok())
            return error(status.code(), "ler registro de pacotes");
        if (end || logged_.frame != assembledVideo_)
            return error(Errc::CorruptData, "registro de pacotes terminou antes do video");
        av_packet_unref(packet_);
        const int result = av_new_packet(packet_, static_cast<int>(logged_.data.size()));
        if (result < 0) return error(Errc::OutOfMemory, "pacote da montagem FFmpeg", result);
        std::memcpy(packet_->data, logged_.data.data(), logged_.data.size());
        packet_->pts = packet_->dts = frame_pts(assembledVideo_);
        packet_->duration = frame_pts(assembledVideo_ + 1) - packet_->pts;
        if (logged_.key) packet_->flags |= AV_PKT_FLAG_KEY;
        if (const Status status = mux(packet_, videoTimebase_, videoStream_); !status.ok()) return status;
        ++assembledVideo_;
    }
    return OkStatus;
}

Status FFmpegSink::publish() noexcept {
    phase(ExportDiagnostics::Stage::Publish);
    if (cancelled()) return error(Errc::Cancelled, "publicacao FFmpeg cancelada");
    const Status result = fileio::commit_file(staged_, target_);
    beat();
    if (!result.ok()) return error(result.code(), "publicar exportacao FFmpeg atomicamente");
    staged_.clear(); state_ = State::Published;
    phase(ExportDiagnostics::Stage::Complete);
    return OkStatus;
}

Status FFmpegSink::validate_output(const ExportOutputValidation& expected) noexcept {
    if (state_ != State::Finalized && state_ != State::Published)
        return Status{Errc::InvalidState, "FFmpeg exige container finalizado para validar"};
    if (const Status status = decode_validate(expected); !status.ok()) return status;
    return state_ == State::Published ? OkStatus : publish();
}

Status FFmpegSink::decode_validate(const ExportOutputValidation& expected) noexcept {
    phase(ExportDiagnostics::Stage::Validation);
    clear_warnings();
    const bool strict = expected.strict;
    const State entryState = state_;
    // Divergência que um encoder de hardware produz legitimamente (PTS,
    // duração, contagem, dimensão de 16, amostras de áudio): estrito = erro;
    // tolerante = aviso registrado e a validação segue. O que torna o arquivo
    // inútil (não abre, vazio, sem vídeo, nenhum quadro) reprova nos dois.
    auto soft = [&](Errc code, const char* what, int result = 0) noexcept -> Status {
        if (strict) return error(code, what, result);
        note_warning(what);
        return OkStatus;
    };
    if (!expected.frames || expected.frames != videoFrames_ || expected.width != video_.width ||
        expected.height != video_.height || expected.audio != hasAudio_ ||
        !std::isfinite(expected.fps) || std::abs(expected.fps - video_.fps) > 0.000001)
        if (const Status s = soft(Errc::CorruptData, "expectativa de validacao FFmpeg diverge dos frames submetidos"); !s.ok())
            return s;
    struct DecoderResources {
        AVFormatContext* format = nullptr;
        AVCodecContext* video = nullptr;
        AVCodecContext* audio = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* frame = nullptr;
        ~DecoderResources() {
            avcodec_free_context(&video); avcodec_free_context(&audio);
            av_packet_free(&packet); av_frame_free(&frame);
            avformat_close_input(&format);
        }
    } decoded;
    decoded.format = avformat_alloc_context();
    if (!decoded.format) return error(Errc::OutOfMemory, "contexto de validacao FFmpeg");
    decoded.format->interrupt_callback = AVIOInterruptCB{&FFmpegSink::interrupted, this};
    const std::string& path = state_ == State::Published ? target_ : staged_;
    int result = avformat_open_input(&decoded.format, path.c_str(), nullptr, nullptr); beat();
    if (result < 0) return error(Errc::CorruptData, "reabrir arquivo exportado FFmpeg", result);
    result = avformat_find_stream_info(decoded.format, nullptr); beat();
    if (result < 0) return error(Errc::CorruptData, "inspecionar arquivo exportado FFmpeg", result);
    int videoIndex = -1, audioIndex = -1;
    for (unsigned i = 0; i < decoded.format->nb_streams; ++i) {
        const AVMediaType type = decoded.format->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO) {
            if (videoIndex >= 0) {
                if (const Status s = soft(Errc::CorruptData, "exportacao FFmpeg com video duplicado"); !s.ok()) return s;
                continue;
            }
            videoIndex = static_cast<int>(i);
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            if (audioIndex >= 0) {
                if (const Status s = soft(Errc::CorruptData, "exportacao FFmpeg com audio duplicado"); !s.ok()) return s;
                continue;
            }
            audioIndex = static_cast<int>(i);
        }
    }
    if (videoIndex < 0) return error(Errc::CorruptData, "arquivo FFmpeg sem faixa de video");
    if ((audioIndex >= 0) != hasAudio_) {
        if (const Status s = soft(Errc::CorruptData, "arquivo FFmpeg sem faixas esperadas"); !s.ok()) return s;
    }
    // Tolerante: o áudio só é conferido quando o arquivo tem a faixa pedida.
    const bool checkAudio = hasAudio_ && audioIndex >= 0;
    auto openDecoder = [&](int index, AVCodecContext*& context) noexcept -> Status {
        const AVStream* stream = decoded.format->streams[index];
        const char* software = nullptr;
        switch (stream->codecpar->codec_id) {
            case AV_CODEC_ID_H264: software = "h264"; break;
            case AV_CODEC_ID_HEVC: software = "hevc"; break;
            case AV_CODEC_ID_AV1: software = "libdav1d"; break;
            case AV_CODEC_ID_PRORES: software = "prores"; break;
            case AV_CODEC_ID_AAC: software = "aac"; break;
            default: return error(Errc::CorruptData, "codec inesperado na exportacao FFmpeg");
        }
        const AVCodec* codec = avcodec_find_decoder_by_name(software);
        if (!codec && stream->codecpar->codec_id == AV_CODEC_ID_AV1)
            codec = avcodec_find_decoder_by_name("libaom-av1");
        if (!codec) return error(Errc::NotSupported, "decoder de software independente ausente para validacao FFmpeg");
        context = avcodec_alloc_context3(codec);
        if (!context) return error(Errc::OutOfMemory, "decoder independente FFmpeg");
        int code = avcodec_parameters_to_context(context, stream->codecpar);
        if (code < 0) return error(Errc::DecodeFailed, "parametros do decoder independente FFmpeg", code);
        context->pkt_timebase = stream->time_base;
        context->thread_count = 4;
        // Tolerante: o decoder esconde o erro do pacote (o quadro sai marcado)
        // em vez de explodir — a contagem de quadros decodificáveis é o que vale.
        context->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | (strict ? AV_EF_EXPLODE : 0);
        code = avcodec_open2(context, codec, nullptr); beat();
        return code < 0 ? error(Errc::DecodeFailed, "abrir decoder independente FFmpeg", code) : OkStatus;
    };
    if (decoded.format->streams[videoIndex]->codecpar->codec_id != codec_id(video_.codec))
        if (const Status s = soft(Errc::CorruptData, "arquivo FFmpeg mudou o codec de video solicitado"); !s.ok()) return s;
    if (const Status status = openDecoder(videoIndex, decoded.video); !status.ok()) return status;
    bool audioDecoding = checkAudio;
    if (checkAudio) if (const Status status = openDecoder(audioIndex, decoded.audio); !status.ok()) {
        if (strict || status.code() == Errc::Cancelled) return status;
        // Arquivo com vídeo bom e áudio que o validador não abre: aviso.
        state_ = entryState;
        note_warning("decoder de audio da validacao FFmpeg nao abriu");
        avcodec_free_context(&decoded.audio);
        audioDecoding = false;
    }
    decoded.packet = av_packet_alloc(); decoded.frame = av_frame_alloc();
    if (!decoded.packet || !decoded.frame) return error(Errc::OutOfMemory, "buffers do decoder independente FFmpeg");
    u64 frames = 0, hash = hashSeed;
    i64 previousVideo = AV_NOPTS_VALUE, audioCovered = 0, previousAudioEnd = 0;
    i64 previousAudioRawEnd = AV_NOPTS_VALUE;
    u64 progressAt = monotonic_ns();
    const u64 stall = expected.stallNs ? expected.stallNs : codecStallNs;
    auto receive = [&](AVCodecContext* context, int index, bool video, bool final, unsigned& obtained) noexcept -> Status {
        obtained = 0;
        for (;;) {
            if (cancelled()) return error(Errc::Cancelled, "validacao FFmpeg cancelada");
            if (monotonic_ns() - progressAt > stall) return error(Errc::Timeout, "decoder independente FFmpeg parou");
            const int code = avcodec_receive_frame(context, decoded.frame); beat();
            if (code == AVERROR_EOF) return OkStatus;
            if (code == AVERROR(EAGAIN)) {
                if (final) return soft(Errc::DecodeFailed, "decoder FFmpeg nao concluiu drenagem EOS");
                return OkStatus;
            }
            // Tolerante: um pacote que não decodifica é aviso; o que conta no
            // fim é haver quadros decodificados.
            if (code < 0) return soft(Errc::DecodeFailed, "decodificar arquivo exportado FFmpeg", code);
            progressAt = monotonic_ns(); ++obtained;
            const AVFrame* frame = decoded.frame;
            const AVStream* stream = decoded.format->streams[index];
            Status checked = OkStatus;
            if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags)
                checked = soft(Errc::CorruptData, "frame exportado FFmpeg corrompido");
            const i64 pts = frame->best_effort_timestamp;
            if (checked.ok() && pts == AV_NOPTS_VALUE) {
                checked = soft(Errc::CorruptData, "frame exportado FFmpeg sem PTS");
                if (checked.ok() && video) ++frames;   // decodificou; só não tem relógio
            } else if (checked.ok() && video) {
                if (frame->width != static_cast<int>(expected.width) || frame->height != static_cast<int>(expected.height))
                    checked = soft(Errc::CorruptData, "dimensoes exportadas FFmpeg divergentes");
                const i64 normalized = av_rescale_q(pts, stream->time_base, videoTimebase_);
                if (checked.ok() && (normalized < 0 || (frames == 0 && normalized != 0) ||
                    (previousVideo != AV_NOPTS_VALUE && normalized <= previousVideo)))
                    checked = soft(Errc::CorruptData, "PTS de video FFmpeg duplicado, ausente ou fora de ordem");
                if (checked.ok() && timing_ == Timing::Constant && normalized !=
                    av_rescale_q(static_cast<i64>(frames), av_inv_q(rate_), videoTimebase_))
                    checked = soft(Errc::CorruptData, "arquivo FFmpeg perdeu um frame do relogio absoluto");
                if (checked.ok()) {
                    hash = timestamp_hash(hash, normalized); previousVideo = normalized;
                    if (++frames > videoFrames_) checked = soft(Errc::CorruptData, "arquivo FFmpeg possui frames adicionais");
                }
            } else if (checked.ok()) {
                if (frame->sample_rate != audioRate_ || frame->ch_layout.nb_channels != static_cast<int>(audio_.channels) ||
                    frame->nb_samples <= 0) {
                    checked = soft(Errc::CorruptData, "layout de audio FFmpeg divergente");
                } else {
                    const i64 start = av_rescale_q(pts, stream->time_base, AVRational{1, audioRate_});
                    if (start > INT64_MAX - frame->nb_samples) {
                        checked = soft(Errc::CorruptData, "PTS de audio FFmpeg excedeu faixa de amostras");
                    } else {
                        const i64 end = start + frame->nb_samples;
                        if (start < -audioFrameSamples_ || end > audioConvertedSamples_ + audioFrameSamples_ ||
                            (previousAudioRawEnd != AV_NOPTS_VALUE && std::abs(start - previousAudioRawEnd) > 1))
                            checked = soft(Errc::CorruptData, "AAC FFmpeg possui pacote repetido, lacuna ou padding excessivo");
                        previousAudioRawEnd = end;
                        // AAC priming is removed by the MP4 edit list / decoder. The
                        // final coded frame may extend past the exact stream duration.
                        const i64 clippedStart = std::max<i64>(0, start);
                        const i64 clippedEnd = std::min<i64>(audioConvertedSamples_, end);
                        if (checked.ok() && clippedEnd > clippedStart) {
                            if (std::abs(clippedStart - previousAudioEnd) > 1)
                                checked = soft(Errc::CorruptData, "audio exportado FFmpeg possui lacuna ou sobreposicao");
                            audioCovered += clippedEnd - clippedStart; previousAudioEnd = clippedEnd;
                        }
                    }
                }
            }
            av_frame_unref(decoded.frame);
            if (!checked.ok()) return checked;
        }
    };
    auto submit = [&](AVCodecContext* context, int index, bool video, const AVPacket* packet) noexcept -> Status {
        for (;;) {
            const int code = avcodec_send_packet(context, packet); beat();
            if (code == AVERROR(EAGAIN)) {
                unsigned obtained = 0;
                if (const Status status = receive(context, index, video, false, obtained); !status.ok()) return status;
                if (!obtained) return soft(Errc::DecodeFailed, "decoder FFmpeg recusou entrada sem produzir frame");
                continue;
            }
            if (code < 0) {
                if (!strict && code == AVERROR_EOF && !packet) return OkStatus;   // tolerante: já drenado
                return soft(Errc::DecodeFailed, "alimentar decoder independente FFmpeg", code);
            }
            unsigned obtained = 0;
            return receive(context, index, video, packet == nullptr, obtained);
        }
    };
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "leitura para validacao FFmpeg cancelada");
        result = av_read_frame(decoded.format, decoded.packet); beat();
        if (result == AVERROR_EOF) break;
        if (result < 0) {
            // Tolerante: arquivo truncado no fim ainda serve se tem quadros.
            if (const Status s = soft(Errc::CorruptData, "ler todos os pacotes exportados FFmpeg", result); !s.ok()) return s;
            if (cancelled()) return error(Errc::Cancelled, "leitura para validacao FFmpeg cancelada");
            break;
        }
        if (decoded.packet->flags & AV_PKT_FLAG_CORRUPT)
            if (const Status s = soft(Errc::CorruptData, "pacote exportado FFmpeg corrompido"); !s.ok()) return s;
        if (decoded.packet->stream_index == videoIndex) {
            if (const Status status = submit(decoded.video, videoIndex, true, decoded.packet); !status.ok()) return status;
        } else if (audioDecoding && decoded.packet->stream_index == audioIndex) {
            if (const Status status = submit(decoded.audio, audioIndex, false, decoded.packet); !status.ok()) return status;
        }
        av_packet_unref(decoded.packet);
    }
    if (const Status status = submit(decoded.video, videoIndex, true, nullptr); !status.ok()) return status;
    if (audioDecoding) if (const Status status = submit(decoded.audio, audioIndex, false, nullptr); !status.ok()) return status;
    // Nos dois modos: um arquivo sem nenhum quadro decodificável não é vídeo.
    if (frames == 0) return error(Errc::CorruptData, "arquivo exportado FFmpeg sem nenhum quadro decodificavel");
    if (frames != videoFrames_ || hash != videoHash_)
        if (const Status s = soft(Errc::CorruptData, "validacao FFmpeg encontrou frames ausentes ou PTS alterados"); !s.ok())
            return s;
    const AVStream* videoStream = decoded.format->streams[videoIndex];
    const i64 videoDuration = videoStream->duration == AV_NOPTS_VALUE ? -1 :
        av_rescale_q(videoStream->duration, videoStream->time_base, videoTimebase_);
    const i64 durationTolerance = hasAudio_ ?
        std::max<i64>(2, av_rescale_q(1, AVRational{1, audioRate_}, videoTimebase_) + 1) : 2;
    if (videoDuration < 0 || std::abs(videoDuration - lastVideoEnd_) > durationTolerance)
        if (const Status s = soft(Errc::CorruptData, "container FFmpeg alterou a duracao final do video"); !s.ok()) return s;
    if (audioDecoding) {
        const AVStream* stream = decoded.format->streams[audioIndex];
        const i64 declared = stream->duration == AV_NOPTS_VALUE ? -1 :
            av_rescale_q(stream->duration, stream->time_base, AVRational{1, audioRate_});
        if (std::abs(declared - audioConvertedSamples_) > 1 ||
            std::abs(audioCovered - audioConvertedSamples_) > 1 ||
            std::abs(previousAudioEnd - audioConvertedSamples_) > 1)
            if (const Status s = soft(Errc::CorruptData, "duracao ou amostras de audio FFmpeg divergentes apos decode"); !s.ok())
                return s;
    }
    finish_warnings(frames);
    return OkStatus;
}

void FFmpegSink::clear_warnings() noexcept {
    warnings_ = {};
    warningText_[0] = '\0';
}

void FFmpegSink::note_warning(const char* what) noexcept {
    for (Warning& w : warnings_) {
        if (w.what == what) { ++w.count; return; }
        if (!w.what) { w.what = what; w.count = 1; return; }
    }
    ++warnings_.back().count;   // lista cheia: conta no último (o texto já diz que houve mais)
}

void FFmpegSink::finish_warnings(u64 decodedFrames) noexcept {
    warningText_[0] = '\0';
    usize used = 0;
    for (const Warning& w : warnings_) {
        if (!w.what || used + 1 >= sizeof(warningText_)) break;
        const int n = std::snprintf(warningText_ + used, sizeof(warningText_) - used, "%s%s (x%u)",
                                    used ? "; " : "", w.what, w.count);
        if (n <= 0) break;
        used = std::min(sizeof(warningText_) - 1, used + static_cast<usize>(n));
    }
    if (used) {
        const int n = std::snprintf(warningText_ + used, sizeof(warningText_) - used, " [%llu/%llu quadros]",
                                    static_cast<unsigned long long>(decodedFrames),
                                    static_cast<unsigned long long>(videoFrames_));
        (void)n;
    }
}

} // namespace

bool ffmpeg_export_available() noexcept { return true; }
std::unique_ptr<ExportSink> make_ffmpeg_export_sink(void*) noexcept {
    return std::unique_ptr<ExportSink>(new (std::nothrow) FFmpegSink());
}
} // namespace aurea

#else
namespace aurea {
bool ffmpeg_export_available() noexcept { return false; }
std::unique_ptr<ExportSink> make_ffmpeg_export_sink(void*) noexcept { return nullptr; }
} // namespace aurea
#endif

namespace aurea {
namespace {
std::mutex exportPathMutex;
char exportPath[640]{};
} // namespace

void set_last_export_encoder_path(const char* text) noexcept {
    std::lock_guard lock(exportPathMutex);
    std::snprintf(exportPath, sizeof(exportPath), "%s", text ? text : "");
}

usize last_export_encoder_path(char* out, usize capacity) noexcept {
    std::lock_guard lock(exportPathMutex);
    const usize length = std::strlen(exportPath);
    if (out && capacity) std::snprintf(out, capacity, "%s", exportPath);
    return length;
}
} // namespace aurea
