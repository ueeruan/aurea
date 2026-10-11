// Production native FFmpeg encoders/muxer, followed by a fresh software decode.
// The source frame number is encoded into pixels: a valid MP4 and the right
// packet count cannot conceal reordered, repeated or missing pictures.
#include "TestFramework.hpp"
#include "aurea/export/FFmpegExportSink.hpp"

#if defined(AUREA_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
namespace {
constexpr u32 kWidth = 192, kHeight = 96, kStride = 256, kRate = 48000;

std::string output(const char* name) {
    std::error_code ec;
    std::filesystem::create_directories("build/reference/astra-native", ec);
    return std::string("build/reference/astra-native/") + name + ".mp4";
}
bool good(Status status) {
    if (!status.ok()) std::printf("\n    native FFmpeg: %d %.*s\n", status.raw(),
        static_cast<int>(status.detail().size()), status.detail().data());
    AUREA_CHECK(status.ok());
    return status.ok();
}
VideoStreamConfig configuration(double fps = 30) {
    VideoStreamConfig video;
    video.width = kWidth; video.height = kHeight; video.fps = fps;
    video.codec = ExportCodec::H264; video.bitrateBps = 2'000'000;
    video.preferSoftware = true;
    return video;
}
struct Picture {
    std::vector<u8> y = std::vector<u8>(kStride * kHeight, 0xA5);
    std::vector<u8> uv = std::vector<u8>(kStride * kHeight / 2, 0xA5);
    void numbered(u32 frame) {
        for (u32 row = 0; row < kHeight; ++row)
            std::fill_n(y.data() + row * kStride, kWidth, u8(96));
        for (u32 row = 0; row < kHeight / 2; ++row)
            std::fill_n(uv.data() + row * kStride, kWidth, u8(128));
        for (u32 bit = 0; bit < 16; ++bit)
            for (u32 row = 8; row < 40; ++row)
                std::fill_n(y.data() + row * kStride + bit * 12 + 2, 8,
                    u8(frame & (1u << bit) ? 224 : 32));
        // Motion marker, independently of the binary identifier.
        for (u32 row = 56; row < 88; ++row)
            std::fill_n(y.data() + row * kStride + frame % (kWidth - 8), 8, u8(220));
    }
};

struct DecodeResult {
    bool opened = false, cleanEof = false, pixelsOrdered = true, exactClock = true;
    u32 videoFrames = 0, width = 0, height = 0, audioChannels = 0, sampleRate = 0;
    u64 audioSamples = 0;
    double audioSquare = 0, stereoDifferenceSquare = 0;
    i64 firstAudioPtsUs = -1, lastVideoPtsUs = -1;
};

// This does not call the sink's validator and never requests a hardware decoder.
DecodeResult decode(const std::string& path, double fps) {
    DecodeResult result;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr; AVCodecContext* audio = nullptr;
    AVFrame* frame = av_frame_alloc(); AVPacket* packet = av_packet_alloc();
    SwsContext* scale = nullptr;
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(format, nullptr) < 0 || !frame || !packet) {
        avformat_close_input(&format); av_frame_free(&frame); av_packet_free(&packet);
        return result;
    }
    const int vi = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int ai = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    auto openDecoder = [&](int index) -> AVCodecContext* {
        if (index < 0) return nullptr;
        const auto* params = format->streams[index]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(params->codec_id);
        AVCodecContext* context = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!context) return nullptr;
        if (avcodec_parameters_to_context(context, params) < 0 || avcodec_open2(context, codec, nullptr) < 0) {
            avcodec_free_context(&context); return nullptr;
        }
        return context;
    };
    video = openDecoder(vi); audio = openDecoder(ai);
    if (!video || (ai >= 0 && !audio)) goto cleanup;
    result.opened = true;
    {
        std::vector<u8> gray;
        bool decoderFailure = false;
        auto receive = [&](AVCodecContext* context, int index) {
            int status;
            while ((status = avcodec_receive_frame(context, frame)) >= 0) {
                const i64 pts = frame->best_effort_timestamp;
                const i64 ptsUs = pts == AV_NOPTS_VALUE ? -1 :
                    av_rescale_q(pts, format->streams[index]->time_base, AVRational{1, 1'000'000});
                if (index == vi) {
                    result.width = frame->width; result.height = frame->height;
                    result.lastVideoPtsUs = ptsUs;
                    const i64 expected = std::llround(result.videoFrames * 1'000'000.0 / fps);
                    if (ptsUs < 0 || std::llabs(ptsUs - expected) > 2) result.exactClock = false;
                    scale = sws_getCachedContext(scale, frame->width, frame->height,
                        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                        AV_PIX_FMT_GRAY8, SWS_POINT, nullptr, nullptr, nullptr);
                    gray.resize(static_cast<size_t>(frame->width) * frame->height);
                    uint8_t* dest[]{gray.data(), nullptr, nullptr, nullptr};
                    const int strides[]{frame->width, 0, 0, 0};
                    if (!scale || sws_scale(scale, frame->data, frame->linesize, 0, frame->height, dest, strides) != frame->height)
                        decoderFailure = true;
                    else if (frame->width != kWidth || frame->height != kHeight) result.pixelsOrdered = false;
                    else {
                        u32 identity = 0;
                        for (u32 bit = 0; bit < 16; ++bit) {
                            const u8 value = gray[24u * kWidth + bit * 12 + 6];
                            if (value >= 160) identity |= 1u << bit;
                            else if (value > 80) result.pixelsOrdered = false;
                        }
                        if (identity != result.videoFrames) {
                            std::printf("\n    picture %u contains identity %u\n", result.videoFrames, identity);
                            result.pixelsOrdered = false;
                        }
                    }
                    ++result.videoFrames;
                } else {
                    result.audioChannels = frame->ch_layout.nb_channels;
                    result.sampleRate = frame->sample_rate;
                    if (result.firstAudioPtsUs < 0) result.firstAudioPtsUs = ptsUs;
                    // Native AAC decoder is planar float. A changed output
                    // format must make this assertion fail, never bypass audio.
                    if (frame->format != AV_SAMPLE_FMT_FLTP || frame->ch_layout.nb_channels != 2) decoderFailure = true;
                    else {
                        const float* left = reinterpret_cast<const float*>(frame->extended_data[0]);
                        const float* right = reinterpret_cast<const float*>(frame->extended_data[1]);
                        for (int n = 0; n < frame->nb_samples; ++n) {
                            result.audioSquare += left[n] * left[n] + right[n] * right[n];
                            const double difference = left[n] - right[n];
                            result.stereoDifferenceSquare += difference * difference;
                        }
                        result.audioSamples += frame->nb_samples;
                    }
                }
                av_frame_unref(frame);
            }
            if (status != AVERROR(EAGAIN) && status != AVERROR_EOF) decoderFailure = true;
        };
        int read;
        while ((read = av_read_frame(format, packet)) >= 0) {
            AVCodecContext* context = packet->stream_index == vi ? video : packet->stream_index == ai ? audio : nullptr;
            if (context) {
                if (avcodec_send_packet(context, packet) < 0) decoderFailure = true;
                receive(context, packet->stream_index);
            }
            av_packet_unref(packet);
        }
        if (avcodec_send_packet(video, nullptr) < 0) decoderFailure = true;
        receive(video, vi);
        if (audio) { if (avcodec_send_packet(audio, nullptr) < 0) decoderFailure = true; receive(audio, ai); }
        result.cleanEof = read == AVERROR_EOF && !decoderFailure;
    }
cleanup:
    sws_freeContext(scale); avcodec_free_context(&video); avcodec_free_context(&audio);
    av_packet_free(&packet); av_frame_free(&frame); avformat_close_input(&format);
    return result;
}

bool exportNumbered(ExportSink& sink, const std::string& path, u32 frames, double fps, bool withAudio) {
    const auto video = configuration(fps); AudioStreamConfig audio;
    if (!good(sink.open(path.c_str(), video, withAudio ? &audio : nullptr))) return false;
    AUREA_CHECK(sink.encoder_info().acceleration == ExportSink::Acceleration::Software);
    Picture picture;
    std::vector<i16> pcm(4096 * 2);
    u64 samples = 0;
    for (u32 n = 0; n < frames; ++n) {
        picture.numbered(n);
        if (!good(sink.write_video(picture.y.data(), kStride, picture.uv.data(), kStride,
            std::llround(n * 1'000'000.0 / fps)))) return false;
        if (withAudio) {
            const u64 target = static_cast<u64>(std::llround((n + 1) * kRate / fps));
            while (samples < target) {
                const u32 count = static_cast<u32>(std::min<u64>(4096, target - samples));
                for (u32 s = 0; s < count; ++s) {
                    const double t = double(samples + s) / kRate;
                    pcm[s * 2] = static_cast<i16>(12000 * std::sin(t * 440 * 6.283185307179586));
                    pcm[s * 2 + 1] = static_cast<i16>(12000 * std::sin(t * 880 * 6.283185307179586));
                }
                if (!good(sink.write_audio(pcm.data(), count, std::llround(samples * 1'000'000.0 / kRate)))) return false;
                samples += count;
            }
        }
    }
    if (!good(sink.finish())) return false;
    ExportOutputValidation expected{kWidth, kHeight, frames, fps, withAudio};
    if (!good(sink.validate_output(expected))) return false;
    const auto decoded = decode(path, fps);
    AUREA_CHECK(decoded.opened && decoded.cleanEof);
    AUREA_CHECK(decoded.pixelsOrdered && decoded.exactClock);
    AUREA_CHECK_EQ(decoded.videoFrames, frames);
    AUREA_CHECK_EQ(decoded.width, kWidth); AUREA_CHECK_EQ(decoded.height, kHeight);
    if (withAudio) {
        const i64 expectedSamples = std::llround(frames * kRate / fps);
        AUREA_CHECK_EQ(decoded.audioChannels, 2u); AUREA_CHECK_EQ(decoded.sampleRate, kRate);
        AUREA_CHECK(std::llabs(static_cast<i64>(decoded.audioSamples) - expectedSamples) <= 2048);
        AUREA_CHECK(decoded.firstAudioPtsUs >= -22'000 && decoded.firstAudioPtsUs <= 22'000);
        AUREA_CHECK(decoded.audioSamples > 0 && decoded.audioSquare / (decoded.audioSamples * 2) > .01);
        AUREA_CHECK(decoded.audioSamples > 0 && decoded.stereoDifferenceSquare / decoded.audioSamples > .01);
    } else AUREA_CHECK_EQ(decoded.audioSamples, 0ull);
    std::printf("\n    verified %s: %u decoded numbered pictures, %.9g fps, %llu PCM samples\n",
        path.c_str(), decoded.videoFrames, fps, static_cast<unsigned long long>(decoded.audioSamples));
    return true;
}
}

AUREA_TEST(FFmpegExport, NativeSoftwareCfrAllPicturesAndStereoAudio) {
    AUREA_CHECK(ffmpeg_export_available());
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    (void)exportNumbered(*sink, output("cfr-stereo"), 90, 30, true);
}
AUREA_TEST(FFmpegExport, FractionalClockSurvivesAllFramesAndReopen) {
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    if (!exportNumbered(*sink, output("fractional-2997"), 300, 30000.0 / 1001, true)) return;
    (void)exportNumbered(*sink, output("reopened-5994"), 120, 60000.0 / 1001, false);
}
AUREA_TEST(FFmpegExport, LongNativeEncodePreservesEveryPictureWithFixedInputBuffers) {
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    // Two minutes / 3600 frames. The input buffers remain 36 KiB throughout.
    (void)exportNumbered(*sink, output("long-120s"), 3600, 30, true);
}
AUREA_TEST(FFmpegExport, RejectsDiscontinuousVideoAudioAndWrongValidationCount) {
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    Picture picture; picture.numbered(0); const auto video = configuration();
    if (!good(sink->open(output("bad-clock").c_str(), video, nullptr))) return;
    AUREA_CHECK(!sink->write_video(picture.y.data(), kStride, picture.uv.data(), kStride, 33333).ok());
    sink->abort();
    if (!good(sink->open(output("duplicate-clock").c_str(), video, nullptr))) return;
    if (!good(sink->write_video(picture.y.data(), kStride, picture.uv.data(), kStride, 0))) return;
    AUREA_CHECK(!sink->write_video(picture.y.data(), kStride, picture.uv.data(), kStride, 0).ok());
    sink->abort();
    AudioStreamConfig audio; const i16 pcm[3200]{};
    if (!good(sink->open(output("bad-audio-clock").c_str(), video, &audio))) return;
    AUREA_CHECK(!sink->write_audio(pcm, 1600, 1'000'000).ok()); sink->abort();
    const auto path = output("wrong-count");
    if (!good(sink->open(path.c_str(), video, nullptr))) return;
    if (!good(sink->write_video(picture.y.data(), kStride, picture.uv.data(), kStride, 0))) return;
    if (!good(sink->finish())) return;
    AUREA_CHECK(!sink->validate_output(ExportOutputValidation{kWidth, kHeight, 2, 30, false}).ok());
}
AUREA_TEST(FFmpegExport, CancellationAbortsPartialFileAndAllowsNewSession) {
    auto sink = make_ffmpeg_export_sink(nullptr); AUREA_CHECK(sink != nullptr); if (!sink) return;
    std::atomic<bool> cancel{false}; sink->set_cancel_flag(&cancel);
    const auto path = output("cancelled"); const auto video = configuration(); Picture picture;
    if (!good(sink->open(path.c_str(), video, nullptr))) return;
    cancel = true;
    const auto began = std::chrono::steady_clock::now();
    AUREA_CHECK(sink->write_video(picture.y.data(), kStride, picture.uv.data(), kStride, 0).code() == Errc::Cancelled);
    sink->abort();
    AUREA_CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(1));
    AUREA_CHECK(!std::filesystem::exists(path));
    cancel = false;
    (void)exportNumbered(*sink, output("after-cancel"), 15, 30, false);
}
#endif
