#include "aurea/export/FFmpegExportSink.hpp"
#include "aurea/export/NV12Copy.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/project/FileIO.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>

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
constexpr unsigned maxPendingVideo = 16;
constexpr u64 pendingVideoBudget = 64ull * 1024 * 1024;
constexpr int audioInputChunk = 4096;
constexpr u64 codecStallNs = 30'000'000'000ull;

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
        return write_video_with_fallback(y, ys, uv, uvs, pts, duration, true);
    }
    Status write_audio(const i16* pcm, u32 frames, i64 pts) noexcept override;
    Status finish() noexcept override;
    Status validate_output(const ExportOutputValidation& expected) noexcept override;
    void abort() noexcept override;

private:
    enum class State { Empty, Open, Finalized, Published, Failed } state_ = State::Empty;
    enum class Timing { Unknown, Constant, Variable } timing_ = Timing::Unknown;
    struct FrameTiming { i64 pts = 0, duration = 0; };
    const std::atomic<bool>* cancel_ = nullptr;
    std::atomic<u64>* heartbeat_ = nullptr;
    std::shared_ptr<ExportDiagnostics> diagnostics_;
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
    Status open_video() noexcept;
    Status try_video(const char* name, bool hardware, const char* nativeName = nullptr,
                     AVPixelFormat requestedPixel = AV_PIX_FMT_NONE) noexcept;
    Status open_audio() noexcept;
    Status write_video_with_fallback(const u8*, u32, const u8*, u32, i64, i64, bool) noexcept;
    Status write_video_impl(const u8*, u32, const u8*, u32, i64, i64, bool) noexcept;
    Status send(AVCodecContext*, AVStream*, const AVFrame*, bool video) noexcept;
    Status drain(AVCodecContext*, AVStream*, bool video, bool flushing, unsigned& produced) noexcept;
    Status mux(AVPacket*, AVCodecContext*, AVStream*) noexcept;
    Status append_audio(const i16* pcm, int samples) noexcept;
    Status encode_audio(bool final) noexcept;
    Status flush_resampler() noexcept;
    Status decode_validate(const ExportOutputValidation& expected) noexcept;
    Status publish() noexcept;
};

void FFmpegSink::release() noexcept {
    if (format_ && format_->pb) avio_closep(&format_->pb);
    avcodec_free_context(&videoCodec_);
    avcodec_free_context(&audioCodec_);
    av_bsf_free(&videoCrop_);
    av_frame_free(&videoFrame_);
    av_frame_free(&audioFrame_);
    av_frame_free(&resampleFrame_);
    av_packet_free(&packet_);
    av_packet_free(&pendingAudio_);
    sws_freeContext(scale_); scale_ = nullptr;
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
    if (!staged_.empty()) fileio::remove_file(staged_);
    staged_.clear();
    state_ = State::Empty;
}

Status FFmpegSink::open(const char* output, const VideoStreamConfig& video,
                       const AudioStreamConfig* audio) noexcept {
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
    video_ = video; hasAudio_ = audio != nullptr;
    if (audio) audio_ = *audio;
    timing_ = Timing::Unknown; encoder_ = {};
    videoFrames_ = videoPackets_ = 0; videoHash_ = hashSeed;
    completedVideoWrites_ = 0;
    lastVideoPts_ = AV_NOPTS_VALUE; lastVideoEnd_ = 0;
    audioInputSamples_ = audioConvertedSamples_ = audioSubmittedSamples_ = 0;
    pendingHead_ = pendingCount_ = 0; lastError_ = OkStatus;
    const u64 frameBytes = static_cast<u64>(video.width) * video.height * 3 / 2;
    pendingLimit_ = static_cast<unsigned>(std::max<u64>(1,
        std::min<u64>(maxPendingVideo, pendingVideoBudget / frameBytes)));
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
    if (const Status status = open_video(); !status.ok()) return status;
    if (audio) if (const Status status = open_audio(); !status.ok()) return status;
    videoStream_ = avformat_new_stream(format_, nullptr);
    if (!videoStream_) return error(Errc::OutOfMemory, "criar faixa de video FFmpeg");
    videoStream_->time_base = videoTimebase_;
    videoStream_->avg_frame_rate = rate_;
    result = videoCrop_ ? avcodec_parameters_copy(videoStream_->codecpar, videoCrop_->par_out)
                        : avcodec_parameters_from_context(videoStream_->codecpar, videoCodec_);
    if (result < 0)
        return error(Errc::EncodeFailed, "parametros de video FFmpeg", result);
    videoStream_->codecpar->width = static_cast<int>(video_.width);
    videoStream_->codecpar->height = static_cast<int>(video_.height);
    if (video.codec == ExportCodec::HEVC) videoStream_->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    if (audio) {
        audioStream_ = avformat_new_stream(format_, nullptr);
        if (!audioStream_) return error(Errc::OutOfMemory, "criar faixa de audio FFmpeg");
        audioStream_->time_base = audioCodec_->time_base;
        if ((result = avcodec_parameters_from_context(audioStream_->codecpar, audioCodec_)) < 0)
            return error(Errc::EncodeFailed, "parametros de audio FFmpeg", result);
    }
    packet_ = av_packet_alloc(); pendingAudio_ = av_packet_alloc();
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

Status FFmpegSink::try_video(const char* name, bool hardware, const char* nativeName, AVPixelFormat requestedPixel) noexcept {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) return Status{Errc::EncoderUnavailable, "encoder FFmpeg ausente nesta compilacao"};
    avcodec_free_context(&videoCodec_);
    videoCodec_ = avcodec_alloc_context3(codec);
    if (!videoCodec_) return error(Errc::OutOfMemory, "contexto de video FFmpeg");
    AVPixelFormat pixel = requestedPixel == AV_PIX_FMT_NONE ? AV_PIX_FMT_NV12 : requestedPixel;
    if (!has_pixel_format(codec, pixel)) {
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

Status FFmpegSink::open_video() noexcept {
    Status selected{Errc::EncoderUnavailable, "nenhum encoder FFmpeg compativel disponivel"};
    if (!video_.preferSoftware && (video_.codec == ExportCodec::H264 || video_.codec == ExportCodec::HEVC)) {
#if defined(__ANDROID__)
        std::array<AndroidCodecCandidate, maxAndroidCandidates> hardware{};
        const usize count = android_codecs(codec_id(video_.codec), video_, true, hardware);
        for (usize i = 0; i < count && !cancelled(); ++i) {
            selected = try_video(video_.codec == ExportCodec::HEVC ? "hevc_mediacodec" : "h264_mediacodec",
                                 true, hardware[i].name, hardware[i].pixel);
            if (selected.ok() || selected.code() == Errc::OutOfMemory) break;
        }
#elif defined(__APPLE__)
        selected = try_video(video_.codec == ExportCodec::HEVC ? "hevc_videotoolbox" : "h264_videotoolbox", true);
#endif
    }
    if (!selected.ok() && selected.code() != Errc::OutOfMemory) {
        const char* candidates[4]{};
        switch (video_.codec) {
            case ExportCodec::H264: candidates[0] = "libopenh264"; candidates[1] = "libx264"; break;
            case ExportCodec::HEVC: candidates[0] = "libkvazaar"; candidates[1] = "libx265"; break;
            case ExportCodec::AV1: candidates[0] = "libsvtav1"; candidates[1] = "libaom-av1"; candidates[2] = "librav1e"; break;
            case ExportCodec::ProRes: candidates[0] = "prores_ks"; candidates[1] = "prores_aw"; break;
        }
        for (const char* candidate : candidates) {
            if (!candidate || cancelled()) break;
            selected = try_video(candidate, false);
            if (selected.ok() || selected.code() == Errc::OutOfMemory) break;
        }
#if defined(__ANDROID__)
        // Android can provide an explicitly classified software HEVC encoder
        // even when this FFmpeg distribution has no libx265. It is still
        // encoded/muxed through the same shared FFmpeg pipeline.
        if (!selected.ok() && selected.code() != Errc::OutOfMemory && !cancelled() &&
            (video_.codec == ExportCodec::H264 || video_.codec == ExportCodec::HEVC)) {
            std::array<AndroidCodecCandidate, maxAndroidCandidates> software{};
            const usize count = android_codecs(codec_id(video_.codec), video_, false, software);
            for (usize i = 0; i < count && !cancelled(); ++i) {
                selected = try_video(video_.codec == ExportCodec::HEVC ? "hevc_mediacodec" : "h264_mediacodec",
                                     false, software[i].name, software[i].pixel);
                if (selected.ok() || selected.code() == Errc::OutOfMemory) break;
            }
        }
#endif
    }
    if (!selected.ok()) return error(cancelled() ? Errc::Cancelled :
        selected.code() == Errc::OutOfMemory ? Errc::OutOfMemory : Errc::EncoderUnavailable,
        "FFmpeg nao abriu encoder solicitado nem fallback por software");
    if (videoCodec_->width != static_cast<int>(video_.width) ||
        videoCodec_->height != static_cast<int>(video_.height)) {
        const AVBitStreamFilter* crop = av_bsf_get_by_name("hevc_metadata");
        int result = crop ? av_bsf_alloc(crop, &videoCrop_) : AVERROR_BSF_NOT_FOUND;
        if (result < 0) return error(Errc::EncoderUnavailable, "filtro de dimensoes HEVC FFmpeg ausente", result);
        if ((result = avcodec_parameters_from_context(videoCrop_->par_in, videoCodec_)) < 0)
            return error(Errc::EncodeFailed, "parametros do crop HEVC FFmpeg", result);
        videoCrop_->time_base_in = videoTimebase_;
        if ((result = av_opt_set_int(videoCrop_->priv_data, "crop_left", 0, 0)) < 0 ||
            (result = av_opt_set_int(videoCrop_->priv_data, "crop_top", 0, 0)) < 0 ||
            (result = av_opt_set_int(videoCrop_->priv_data, "crop_right", videoCodec_->width - video_.width, 0)) < 0 ||
            (result = av_opt_set_int(videoCrop_->priv_data, "crop_bottom", videoCodec_->height - video_.height, 0)) < 0 ||
            (result = av_bsf_init(videoCrop_)) < 0)
            return error(Errc::EncodeFailed, "preservar dimensoes visiveis HEVC FFmpeg", result);
    }
    videoFrame_ = av_frame_alloc();
    if (!videoFrame_) return error(Errc::OutOfMemory, "frame de video FFmpeg");
    videoFrame_->format = videoCodec_->pix_fmt;
    videoFrame_->width = videoCodec_->width; videoFrame_->height = videoCodec_->height;
    videoFrame_->color_primaries = videoCodec_->color_primaries;
    videoFrame_->color_trc = videoCodec_->color_trc; videoFrame_->colorspace = videoCodec_->colorspace;
    videoFrame_->color_range = videoCodec_->color_range;
    const int result = av_frame_get_buffer(videoFrame_, 32);
    if (result < 0) return error(Errc::OutOfMemory, "pixels do frame FFmpeg", result);
    if (videoCodec_->pix_fmt != AV_PIX_FMT_NV12) {
        scale_ = sws_getContext(static_cast<int>(video_.width), static_cast<int>(video_.height), AV_PIX_FMT_NV12,
            static_cast<int>(video_.width), static_cast<int>(video_.height), videoCodec_->pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale_) return error(Errc::OutOfMemory, "conversor libswscale");
        const int* coefficients = sws_getCoefficients(SWS_CS_ITU709);
        if (sws_setColorspaceDetails(scale_, coefficients, video_.color.fullRange ? 1 : 0,
            coefficients, video_.color.fullRange ? 1 : 0, 0, 1 << 16, 1 << 16) < 0)
            return error(Errc::EncodeFailed, "configurar cor libswscale");
    }
    return OkStatus;
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

Status FFmpegSink::mux(AVPacket* packet, AVCodecContext* codec, AVStream* stream) noexcept {
    if (cancelled()) return error(Errc::Cancelled, "exportacao FFmpeg cancelada");
    av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
    packet->stream_index = stream->index;
    const int result = av_interleaved_write_frame(format_, packet); beat();
    if (result < 0) return error(Errc::EncodeFailed, "multiplexar pacote FFmpeg", result);
    if (format_->pb && format_->pb->error < 0)
        return error(Errc::IoError, "gravar pacote FFmpeg", format_->pb->error);
    event(codec == videoCodec_ ? ExportDiagnostics::Counter::VideoPackets : ExportDiagnostics::Counter::AudioPackets);
    return OkStatus;
}

Status FFmpegSink::drain(AVCodecContext* codec, AVStream* stream, bool video,
                         bool flushing, unsigned& produced) noexcept {
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
            if (monotonic_ns() - started > codecStallNs)
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
            packet_->duration = pendingVideo_[pendingHead_].duration;
            pendingHead_ = (pendingHead_ + 1) % maxPendingVideo; --pendingCount_;
            ++videoPackets_;
            if (videoCrop_) {
                int filtered = av_bsf_send_packet(videoCrop_, packet_);
                if (filtered >= 0) filtered = av_bsf_receive_packet(videoCrop_, packet_);
                if (filtered < 0) return error(Errc::EncodeFailed, "aplicar dimensoes HEVC FFmpeg ao frame", filtered);
            }
            if (const Status status = mux(packet_, codec, stream); !status.ok()) return status;
            if (videoCrop_) {
                const int extra = av_bsf_receive_packet(videoCrop_, packet_);
                if (extra != AVERROR(EAGAIN))
                    return error(Errc::EncodeFailed, "crop HEVC FFmpeg alterou a quantidade de frames", extra);
            }
        } else {
            // Keep only the final AAC packet so its exact duration/tail padding
            // can be written after all resampler samples have been accounted.
            if (pendingAudio_->size)
                if (const Status status = mux(pendingAudio_, codec, stream); !status.ok()) return status;
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
            if (!produced && monotonic_ns() - started > codecStallNs)
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
    Status result = write_video_impl(y, ys, uv, uvs, pts, duration, timed);
    if (result.ok()) { ++completedVideoWrites_; return result; }
    if (state_ != State::Failed || completedVideoWrites_ || audioInputSamples_ ||
        video_.preferSoftware || encoder_.acceleration == Acceleration::Software ||
        (result.code() != Errc::EncodeFailed && result.code() != Errc::EncoderUnavailable &&
         result.code() != Errc::UnsupportedFormat && result.code() != Errc::Timeout)) return result;
    // A late hardware rejection is still part of startup. Delete only our own
    // temporary container, open a fresh software session and resubmit the exact
    // caller-owned frame. Never change encoder or advance PTS mid-session.
    const VideoStreamConfig requested = video_;
    VideoStreamConfig software = requested; software.preferSoftware = true;
    const AudioStreamConfig audio = audio_;
    const bool withAudio = hasAudio_;
    std::string path;
    try { path = target_; }
    catch (...) { return error(Errc::OutOfMemory, "reinicio de exportacao FFmpeg por software"); }
    if (const Status opened = open(path.c_str(), software, withAudio ? &audio : nullptr); !opened.ok()) return opened;
    result = write_video_impl(y, ys, uv, uvs, pts, duration, timed);
    if (result.ok()) ++completedVideoWrites_;
    return result;
}

Status FFmpegSink::write_video_impl(const u8* y, u32 ys, const u8* uv, u32 uvs,
                                   i64 pts, i64 duration, bool timed) noexcept {
    phase(ExportDiagnostics::Stage::VideoInput);
    if (const Status status = ready(); !status.ok()) return status;
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
    if (pendingCount_ >= pendingLimit_)
        return error(Errc::BudgetExceeded, "encoder FFmpeg excedeu a fila limitada de frames");
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
    pendingVideo_[(pendingHead_ + pendingCount_) % maxPendingVideo] = {framePts, frameDuration};
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
    if (!hasAudio_) return Status{Errc::InvalidState, "exportacao FFmpeg nao possui faixa de audio"};
    if ((!pcm && frames) || pts < 0 || audioInputSamples_ > INT64_MAX - static_cast<i64>(frames))
        return Status{Errc::InvalidArgument, "PCM de exportacao FFmpeg invalido"};
    const i64 expected = av_rescale_q(audioInputSamples_, AVRational{1, static_cast<int>(audio_.sampleRate)}, micros);
    if (expected < 0) return Status{Errc::OutOfRange, "tempo absoluto de audio FFmpeg excedido"};
    if (std::abs(pts - expected) > 2)
        return Status{Errc::InvalidArgument, "PCM FFmpeg descontinuo; amostras ausentes ou duplicadas"};
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
    if (!videoFrames_ || (hasAudio_ && !audioInputSamples_))
        return error(Errc::EncodeFailed, "exportacao FFmpeg sem frames ou amostras esperadas");
    if (hasAudio_) {
        const i64 expectedSamples = av_rescale_q(lastVideoEnd_, videoTimebase_,
            AVRational{1, static_cast<int>(audio_.sampleRate)});
        if (expectedSamples < 0 || std::abs(audioInputSamples_ - expectedSamples) > 2)
            return error(Errc::EncodeFailed, "audio e video FFmpeg possuem duracoes divergentes");
    }
    unsigned produced = 0;
    if (const Status status = send(videoCodec_, videoStream_, nullptr, true); !status.ok()) return status;
    if (const Status status = drain(videoCodec_, videoStream_, true, true, produced); !status.ok()) return status;
    if (pendingCount_ || videoPackets_ != videoFrames_)
        return error(Errc::EncodeFailed, "encoder FFmpeg nao entregou todos os frames submetidos");
    if (videoCrop_) {
        int result = av_bsf_send_packet(videoCrop_, nullptr);
        if (result >= 0) result = av_bsf_receive_packet(videoCrop_, packet_);
        if (result != AVERROR_EOF)
            return error(Errc::EncodeFailed, "crop HEVC FFmpeg nao concluiu EOS sem frames adicionais", result);
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
        if (const Status status = mux(pendingAudio_, audioCodec_, audioStream_); !status.ok()) return status;
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
    const ExportOutputValidation expected{video_.width, video_.height, static_cast<u32>(videoFrames_), video_.fps, hasAudio_};
    if (const Status status = decode_validate(expected); !status.ok()) return status;
    return publish();
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
    if (!expected.frames || expected.frames != videoFrames_ || expected.width != video_.width ||
        expected.height != video_.height || expected.audio != hasAudio_ ||
        !std::isfinite(expected.fps) || std::abs(expected.fps - video_.fps) > 0.000001)
        return error(Errc::CorruptData, "expectativa de validacao FFmpeg diverge dos frames submetidos");
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
            if (videoIndex >= 0) return error(Errc::CorruptData, "exportacao FFmpeg com video duplicado");
            videoIndex = static_cast<int>(i);
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            if (audioIndex >= 0) return error(Errc::CorruptData, "exportacao FFmpeg com audio duplicado");
            audioIndex = static_cast<int>(i);
        }
    }
    if (videoIndex < 0 || ((audioIndex >= 0) != hasAudio_))
        return error(Errc::CorruptData, "arquivo FFmpeg sem faixas esperadas");
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
        context->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;
        code = avcodec_open2(context, codec, nullptr); beat();
        return code < 0 ? error(Errc::DecodeFailed, "abrir decoder independente FFmpeg", code) : OkStatus;
    };
    if (decoded.format->streams[videoIndex]->codecpar->codec_id != codec_id(video_.codec))
        return error(Errc::CorruptData, "arquivo FFmpeg mudou o codec de video solicitado");
    if (const Status status = openDecoder(videoIndex, decoded.video); !status.ok()) return status;
    if (hasAudio_) if (const Status status = openDecoder(audioIndex, decoded.audio); !status.ok()) return status;
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
                if (final) return error(Errc::DecodeFailed, "decoder FFmpeg nao concluiu drenagem EOS");
                return OkStatus;
            }
            if (code < 0) return error(Errc::DecodeFailed, "decodificar arquivo exportado FFmpeg", code);
            progressAt = monotonic_ns(); ++obtained;
            const AVFrame* frame = decoded.frame;
            if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags)
                return error(Errc::CorruptData, "frame exportado FFmpeg corrompido");
            const i64 pts = frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) return error(Errc::CorruptData, "frame exportado FFmpeg sem PTS");
            const AVStream* stream = decoded.format->streams[index];
            if (video) {
                if (frame->width != static_cast<int>(expected.width) || frame->height != static_cast<int>(expected.height))
                    return error(Errc::CorruptData, "dimensoes exportadas FFmpeg divergentes");
                const i64 normalized = av_rescale_q(pts, stream->time_base, videoTimebase_);
                if (normalized < 0 || (frames == 0 && normalized != 0) ||
                    (previousVideo != AV_NOPTS_VALUE && normalized <= previousVideo))
                    return error(Errc::CorruptData, "PTS de video FFmpeg duplicado, ausente ou fora de ordem");
                if (timing_ == Timing::Constant && normalized !=
                    av_rescale_q(static_cast<i64>(frames), av_inv_q(rate_), videoTimebase_))
                    return error(Errc::CorruptData, "arquivo FFmpeg perdeu um frame do relogio absoluto");
                hash = timestamp_hash(hash, normalized); previousVideo = normalized;
                if (++frames > videoFrames_) return error(Errc::CorruptData, "arquivo FFmpeg possui frames adicionais");
            } else {
                if (frame->sample_rate != audioRate_ || frame->ch_layout.nb_channels != static_cast<int>(audio_.channels) ||
                    frame->nb_samples <= 0)
                    return error(Errc::CorruptData, "layout de audio FFmpeg divergente");
                const i64 start = av_rescale_q(pts, stream->time_base, AVRational{1, audioRate_});
                if (start > INT64_MAX - frame->nb_samples)
                    return error(Errc::CorruptData, "PTS de audio FFmpeg excedeu faixa de amostras");
                const i64 end = start + frame->nb_samples;
                if (start < -audioFrameSamples_ || end > audioConvertedSamples_ + audioFrameSamples_ ||
                    (previousAudioRawEnd != AV_NOPTS_VALUE && std::abs(start - previousAudioRawEnd) > 1))
                    return error(Errc::CorruptData, "AAC FFmpeg possui pacote repetido, lacuna ou padding excessivo");
                previousAudioRawEnd = end;
                // AAC priming is removed by the MP4 edit list / decoder. The
                // final coded frame may extend past the exact stream duration.
                const i64 clippedStart = std::max<i64>(0, start);
                const i64 clippedEnd = std::min<i64>(audioConvertedSamples_, end);
                if (clippedEnd > clippedStart) {
                    if (std::abs(clippedStart - previousAudioEnd) > 1)
                        return error(Errc::CorruptData, "audio exportado FFmpeg possui lacuna ou sobreposicao");
                    audioCovered += clippedEnd - clippedStart; previousAudioEnd = clippedEnd;
                }
            }
            av_frame_unref(decoded.frame);
        }
    };
    auto submit = [&](AVCodecContext* context, int index, bool video, const AVPacket* packet) noexcept -> Status {
        for (;;) {
            const int code = avcodec_send_packet(context, packet); beat();
            if (code == AVERROR(EAGAIN)) {
                unsigned obtained = 0;
                if (const Status status = receive(context, index, video, false, obtained); !status.ok()) return status;
                if (!obtained) return error(Errc::DecodeFailed, "decoder FFmpeg recusou entrada sem produzir frame");
                continue;
            }
            if (code < 0) return error(Errc::DecodeFailed, "alimentar decoder independente FFmpeg", code);
            unsigned obtained = 0;
            return receive(context, index, video, packet == nullptr, obtained);
        }
    };
    for (;;) {
        if (cancelled()) return error(Errc::Cancelled, "leitura para validacao FFmpeg cancelada");
        result = av_read_frame(decoded.format, decoded.packet); beat();
        if (result == AVERROR_EOF) break;
        if (result < 0) return error(Errc::CorruptData, "ler todos os pacotes exportados FFmpeg", result);
        if (decoded.packet->flags & AV_PKT_FLAG_CORRUPT)
            return error(Errc::CorruptData, "pacote exportado FFmpeg corrompido");
        if (decoded.packet->stream_index == videoIndex) {
            if (const Status status = submit(decoded.video, videoIndex, true, decoded.packet); !status.ok()) return status;
        } else if (decoded.packet->stream_index == audioIndex) {
            if (const Status status = submit(decoded.audio, audioIndex, false, decoded.packet); !status.ok()) return status;
        }
        av_packet_unref(decoded.packet);
    }
    if (const Status status = submit(decoded.video, videoIndex, true, nullptr); !status.ok()) return status;
    if (hasAudio_) if (const Status status = submit(decoded.audio, audioIndex, false, nullptr); !status.ok()) return status;
    if (frames != videoFrames_ || hash != videoHash_)
        return error(Errc::CorruptData, "validacao FFmpeg encontrou frames ausentes ou PTS alterados");
    const AVStream* videoStream = decoded.format->streams[videoIndex];
    const i64 videoDuration = videoStream->duration == AV_NOPTS_VALUE ? -1 :
        av_rescale_q(videoStream->duration, videoStream->time_base, videoTimebase_);
    const i64 durationTolerance = hasAudio_ ?
        std::max<i64>(2, av_rescale_q(1, AVRational{1, audioRate_}, videoTimebase_) + 1) : 2;
    if (videoDuration < 0 || std::abs(videoDuration - lastVideoEnd_) > durationTolerance)
        return error(Errc::CorruptData, "container FFmpeg alterou a duracao final do video");
    if (hasAudio_) {
        const AVStream* stream = decoded.format->streams[audioIndex];
        const i64 declared = stream->duration == AV_NOPTS_VALUE ? -1 :
            av_rescale_q(stream->duration, stream->time_base, AVRational{1, audioRate_});
        if (std::abs(declared - audioConvertedSamples_) > 1 ||
            std::abs(audioCovered - audioConvertedSamples_) > 1 ||
            std::abs(previousAudioEnd - audioConvertedSamples_) > 1)
            return error(Errc::CorruptData, "duracao ou amostras de audio FFmpeg divergentes apos decode");
    }
    return OkStatus;
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
