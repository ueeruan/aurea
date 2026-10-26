#pragma once

// A second decode path for composition exports. This helper never calls the
// production sink's validator and never selects a platform/hardware decoder.
#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace aurea::test {
struct NativeExportVerification {
    bool ok = false;
    std::string error;
    std::uint32_t pictures = 0, contrastingFrames = 0, distinctFrameHashes = 0;
    std::uint32_t audioChannels = 0, sampleRate = 0;
    std::uint64_t samples = 0;
    double meanSquare = 0;
    std::int64_t firstPtsUs = -1, lastPtsUs = -1;
};

/// Opcional: o que o export retomável compara com o export sem interrupção
/// (cada quadro decodificado em cinza, a lista de PTS, a duração declarada da
/// faixa de vídeo e as amostras lógicas de áudio).
struct NativeExportDetail {
    std::vector<std::vector<std::uint8_t>> grayFrames;
    std::vector<std::int64_t> ptsUs;
    std::int64_t videoDurationUs = -1;
    std::uint64_t audioSamples = 0;
};

inline NativeExportVerification verify_native_export(const std::string& path,
    std::uint32_t expectedFrames, std::uint32_t width, std::uint32_t height,
    double fps, bool expectAudio, NativeExportDetail* detail = nullptr) {
    NativeExportVerification result;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr; AVCodecContext* audio = nullptr;
    AVFrame* frame = av_frame_alloc(); AVPacket* packet = av_packet_alloc();
    SwsContext* scale = nullptr;
    auto error = [&](const char* message) { if (result.error.empty()) result.error = message; };
    do {
        if (!frame || !packet || avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format, nullptr) < 0) {
            error("independent decoder could not open exported file"); break;
        }
        const int vi = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int ai = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (vi < 0 || (ai >= 0) != expectAudio) { error("exported stream layout differs from request"); break; }
        auto decoder = [&](int index) -> AVCodecContext* {
            const AVCodecParameters* parameters = format->streams[index]->codecpar;
            const char* name = parameters->codec_id == AV_CODEC_ID_H264 ? "h264" :
                parameters->codec_id == AV_CODEC_ID_HEVC ? "hevc" :
                parameters->codec_id == AV_CODEC_ID_AAC ? "aac" :
                parameters->codec_id == AV_CODEC_ID_PRORES ? "prores" : nullptr;
            const AVCodec* codec = name ? avcodec_find_decoder_by_name(name) : nullptr;
            AVCodecContext* context = codec ? avcodec_alloc_context3(codec) : nullptr;
            if (!context) return nullptr;
            context->thread_count = 2;
            context->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;
            if (avcodec_parameters_to_context(context, parameters) < 0 || avcodec_open2(context, codec, nullptr) < 0)
                avcodec_free_context(&context);
            return context;
        };
        video = decoder(vi); if (ai >= 0) audio = decoder(ai);
        if (!video || (expectAudio && !audio)) { error("required independent software decoder unavailable"); break; }
        std::vector<std::uint8_t> gray(static_cast<std::size_t>(width) * height);
        std::uint64_t previousHash = 0;
        std::int64_t previousAudioEnd = AV_NOPTS_VALUE;
        double audioSquare = 0;
        std::uint64_t physicalSamples = 0;
        auto receive = [&](AVCodecContext* context, int index) -> bool {
            int code;
            while ((code = avcodec_receive_frame(context, frame)) >= 0) {
                if (frame->flags & AV_FRAME_FLAG_CORRUPT || frame->decode_error_flags ||
                    frame->best_effort_timestamp == AV_NOPTS_VALUE) {
                    error("corrupt frame or missing presentation timestamp"); av_frame_unref(frame); return false;
                }
                const auto pts = frame->best_effort_timestamp;
                if (index == vi) {
                    const auto ptsUs = av_rescale_q(pts, format->streams[vi]->time_base, AVRational{1, 1'000'000});
                    const auto expected = std::llround(result.pictures * 1'000'000.0 / fps);
                    if (frame->width != static_cast<int>(width) || frame->height != static_cast<int>(height) ||
                        std::llabs(ptsUs - expected) > 2 || result.pictures >= expectedFrames) {
                        error("video dimensions, order, frame count or exact clock mismatch");
                        av_frame_unref(frame); return false;
                    }
                    scale = sws_getCachedContext(scale, frame->width, frame->height,
                        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                        AV_PIX_FMT_GRAY8, SWS_POINT, nullptr, nullptr, nullptr);
                    std::uint8_t* destination[]{gray.data(), nullptr, nullptr, nullptr};
                    const int strides[]{frame->width, 0, 0, 0};
                    if (!scale || sws_scale(scale, frame->data, frame->linesize, 0, frame->height,
                        destination, strides) != frame->height) {
                        error("independent video pixel conversion failed"); av_frame_unref(frame); return false;
                    }
                    std::uint64_t hash = 14695981039346656037ull;
                    std::uint8_t low = 255, high = 0;
                    for (const auto pixel : gray) {
                        low = std::min(low, pixel); high = std::max(high, pixel);
                        hash = (hash ^ pixel) * 1099511628211ull;
                    }
                    if (high - low >= 24) ++result.contrastingFrames;
                    if (!result.pictures || hash != previousHash) ++result.distinctFrameHashes;
                    previousHash = hash;
                    if (detail) { detail->grayFrames.push_back(gray); detail->ptsUs.push_back(ptsUs); }
                    if (!result.pictures) result.firstPtsUs = ptsUs;
                    result.lastPtsUs = ptsUs;
                    ++result.pictures;
                } else {
                    if (frame->format != AV_SAMPLE_FMT_FLTP || frame->sample_rate <= 0 ||
                        frame->ch_layout.nb_channels < 1 || frame->ch_layout.nb_channels > 2) {
                        error("AAC output format or channel layout invalid"); av_frame_unref(frame); return false;
                    }
                    const auto samplePts = av_rescale_q(pts, format->streams[ai]->time_base,
                        AVRational{1, frame->sample_rate});
                    if (previousAudioEnd != AV_NOPTS_VALUE && samplePts != previousAudioEnd) {
                        error("decoded audio has a gap or repeated samples"); av_frame_unref(frame); return false;
                    }
                    previousAudioEnd = samplePts + frame->nb_samples;
                    result.audioChannels = frame->ch_layout.nb_channels; result.sampleRate = frame->sample_rate;
                    const auto declaredDuration = format->streams[ai]->duration;
                    if (declaredDuration == AV_NOPTS_VALUE || declaredDuration <= 0) {
                        error("audio stream has no finite duration"); av_frame_unref(frame); return false;
                    }
                    const auto logicalSamples = av_rescale_q(declaredDuration, format->streams[ai]->time_base,
                        AVRational{1, frame->sample_rate});
                    const auto begin = std::max<std::int64_t>(0, samplePts);
                    const auto end = std::min<std::int64_t>(logicalSamples, samplePts + frame->nb_samples);
                    if (end > begin) result.samples += end - begin;
                    for (int channel = 0; channel < frame->ch_layout.nb_channels; ++channel) {
                        const auto* samples = reinterpret_cast<const float*>(frame->extended_data[channel]);
                        for (int n = 0; n < frame->nb_samples; ++n) audioSquare += samples[n] * samples[n];
                    }
                    physicalSamples += frame->nb_samples;
                }
                av_frame_unref(frame);
            }
            if (code != AVERROR_EOF && code != AVERROR(EAGAIN)) { error("independent frame decode failed"); return false; }
            return true;
        };
        auto send = [&](AVCodecContext* context, int index, const AVPacket* input) -> bool {
            int code = avcodec_send_packet(context, input);
            if (code == AVERROR(EAGAIN)) {
                if (!receive(context, index)) return false;
                code = avcodec_send_packet(context, input);
            }
            if (code < 0) { error("independent decoder rejected packet or flush"); return false; }
            return receive(context, index);
        };
        int read = AVERROR_EOF;
        while ((read = av_read_frame(format, packet)) >= 0) {
            if (packet->flags & AV_PKT_FLAG_CORRUPT) { error("demuxer marked a corrupt packet"); av_packet_unref(packet); break; }
            const int index = packet->stream_index;
            AVCodecContext* context = index == vi ? video : index == ai ? audio : nullptr;
            const bool accepted = !context || send(context, index, packet);
            av_packet_unref(packet);
            if (!accepted) break;
        }
        if (!result.error.empty()) break;
        if (read != AVERROR_EOF || !send(video, vi, nullptr) || (audio && !send(audio, ai, nullptr))) {
            error("file ended without clean decoder drain"); break;
        }
        if (result.pictures != expectedFrames) { error("not every requested video frame decoded"); break; }
        if (detail) {
            const AVStream* stream = format->streams[vi];
            detail->videoDurationUs = stream->duration == AV_NOPTS_VALUE ? -1
                : av_rescale_q(stream->duration, stream->time_base, AVRational{1, 1'000'000});
            detail->audioSamples = result.samples;
        }
        if (audio) {
            const auto expectedSamples = std::llround(expectedFrames * static_cast<double>(result.sampleRate) / fps);
            if (std::llabs(static_cast<std::int64_t>(result.samples) - expectedSamples) > 2) {
                error("audio logical duration differs from complete video duration"); break;
            }
            if (physicalSamples) result.meanSquare = audioSquare / (physicalSamples * result.audioChannels);
        }
        result.ok = true;
    } while (false);
    sws_freeContext(scale); avcodec_free_context(&video); avcodec_free_context(&audio);
    av_frame_free(&frame); av_packet_free(&packet); avformat_close_input(&format);
    std::printf("\n    independent composition decode %s: %u pictures, %llu logical PCM samples, %u contrasting pictures, %u successive hashes%s%s\n",
        path.c_str(), result.pictures, static_cast<unsigned long long>(result.samples), result.contrastingFrames,
        result.distinctFrameHashes, result.ok ? "" : "; ", result.error.c_str());
    return result;
}
} // namespace aurea::test
#endif
