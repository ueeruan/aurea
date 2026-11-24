#include "aurea/export/PortableVideoSource.hpp"

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
#include "aurea/core/Time.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/intreadwrite.h>
}

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

namespace aurea {
namespace {
constexpr AVRational micros{1, 1'000'000};

bool planar8(AVPixelFormat format) noexcept {
    return format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P;
}

bool supported_pixels(AVPixelFormat format) noexcept {
    return planar8(format) || format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_YUV420P10LE || format == AV_PIX_FMT_P010LE;
}

VideoColorInfo color_info(const AVFrame& frame) noexcept {
    auto color = VideoColorInfo::guess(frame.width, frame.height, planar8(static_cast<AVPixelFormat>(frame.format))
        || frame.format == AV_PIX_FMT_NV12 ? 8 : 10);
    switch (frame.colorspace) {
        case AVCOL_SPC_BT709: color.matrix = YCbCrMatrix::BT709; break;
        case AVCOL_SPC_BT2020_NCL: color.matrix = YCbCrMatrix::BT2020; break;
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_BT470BG: color.matrix = YCbCrMatrix::BT601; break;
        default: break;
    }
    switch (frame.color_primaries) {
        case AVCOL_PRI_BT709: color.primaries = ColorPrimaries::BT709; break;
        case AVCOL_PRI_BT2020: color.primaries = ColorPrimaries::BT2020; break;
        case AVCOL_PRI_SMPTE432: color.primaries = ColorPrimaries::P3; break;
        case AVCOL_PRI_SMPTE170M:
        case AVCOL_PRI_BT470BG: color.primaries = ColorPrimaries::BT601; break;
        default: break;
    }
    switch (frame.color_trc) {
        case AVCOL_TRC_LINEAR: color.transfer = TransferFunction::Linear; break;
        case AVCOL_TRC_SMPTE2084: color.transfer = TransferFunction::PQ; break;
        case AVCOL_TRC_ARIB_STD_B67: color.transfer = TransferFunction::HLG; break;
        default: break;
    }
    color.fullRange = frame.color_range == AVCOL_RANGE_JPEG || frame.format == AV_PIX_FMT_YUVJ420P;
    color.fromStream = frame.colorspace != AVCOL_SPC_UNSPECIFIED || frame.color_range != AVCOL_RANGE_UNSPECIFIED;
    return color;
}

class PortableFrame final : public DecodedFrame {
public:
    ~PortableFrame() override { av_frame_free(&storage); }
    [[nodiscard]] bool owns_cpu_backing() const noexcept override { return true; }
    AVFrame* storage = nullptr;
};

// Repack 10-bit I420 into P010 without scaling the coded values or converting
// color. The compositor already handles both HDR transfer functions in P010.
bool repack_p010(const AVFrame& source, AVFrame& storage) noexcept {
    storage.format = AV_PIX_FMT_P010LE;
    storage.width = source.width;
    storage.height = source.height;
    if (av_frame_get_buffer(&storage, 32) < 0) return false;
    for (int row = 0; row < source.height; ++row) {
        const u8* y = source.data[0] + static_cast<std::ptrdiff_t>(row) * source.linesize[0];
        u8* dest = storage.data[0] + static_cast<std::ptrdiff_t>(row) * storage.linesize[0];
        for (int x = 0; x < source.width; ++x) AV_WL16(dest + x * 2, AV_RL16(y + x * 2) << 6);
    }
    for (int row = 0; row < (source.height + 1) / 2; ++row) {
        const u8* cb = source.data[1] + static_cast<std::ptrdiff_t>(row) * source.linesize[1];
        const u8* cr = source.data[2] + static_cast<std::ptrdiff_t>(row) * source.linesize[2];
        u8* dest = storage.data[1] + static_cast<std::ptrdiff_t>(row) * storage.linesize[1];
        for (int x = 0; x < (source.width + 1) / 2; ++x) {
            AV_WL16(dest + x * 4, AV_RL16(cb + x * 2) << 6);
            AV_WL16(dest + x * 4 + 2, AV_RL16(cr + x * 2) << 6);
        }
    }
    return true;
}

class PortableVideo final : public VideoDecoderBackend {
public:
    ~PortableVideo() override {
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&codec_);
        avformat_close_input(&file_);
    }

    bool open(const char* path) noexcept {
        file_ = avformat_alloc_context();
        if (!file_) return false;
        file_->interrupt_callback = {&PortableVideo::interrupt, this};
        AVDictionary* options = nullptr;
        av_dict_set(&options, "protocol_whitelist", "file", 0);
        set_deadline();
        const int opened = avformat_open_input(&file_, path, nullptr, &options);
        av_dict_free(&options);
        if (opened < 0 || !file_->pb || !(file_->pb->seekable & AVIO_SEEKABLE_NORMAL) ||
            avformat_find_stream_info(file_, nullptr) < 0) return false;
        index_ = av_find_best_stream(file_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index_ < 0) return false;
        stream_ = file_->streams[index_];
        const AVCodecParameters& params = *stream_->codecpar;
        if (params.codec_id != AV_CODEC_ID_H264 && params.codec_id != AV_CODEC_ID_HEVC &&
            params.codec_id != AV_CODEC_ID_MPEG4) return false;
        if (params.width <= 0 || params.height <= 0 || !supported_pixels(static_cast<AVPixelFormat>(params.format))) return false;
        const AVCodec* decoder = avcodec_find_decoder(params.codec_id);
        codec_ = decoder ? avcodec_alloc_context3(decoder) : nullptr;
        if (!codec_ || avcodec_parameters_to_context(codec_, &params) < 0) return false;
        codec_->thread_count = sizeof(void*) <= 4 ? 1 : 2;
        codec_->thread_type = FF_THREAD_SLICE;
        codec_->max_pixels = 8192LL * 8192LL;
        if (avcodec_open2(codec_, decoder, nullptr) < 0) return false;
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (!packet_ || !frame_) return false;

        info_.codedWidth = static_cast<u32>(params.width);
        info_.codedHeight = static_cast<u32>(params.height);
        info_.cropRight = info_.codedWidth - 1;
        info_.cropBottom = info_.codedHeight - 1;
        const auto* rotation = av_packet_side_data_get(params.coded_side_data, params.nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        if (rotation && rotation->size >= 9 * sizeof(int32_t)) {
            const auto* matrix = reinterpret_cast<const int32_t*>(rotation->data);
            const double determinant = static_cast<double>(matrix[0]) * matrix[4] - static_cast<double>(matrix[1]) * matrix[3];
            const double angle = -av_display_rotation_get(matrix);
            if (determinant <= 0 || !std::isfinite(angle) || std::abs(angle - std::round(angle / 90.) * 90.) > 0.01) return false;
            info_.rotation = static_cast<u32>((std::lround(angle) % 360 + 360) % 360);
        }
        const double fps = av_q2d(av_guess_frame_rate(file_, stream_, nullptr));
        if (std::isfinite(fps) && fps > 0.) info_.fps = fps;
        if (stream_->duration != AV_NOPTS_VALUE) info_.durationUs = av_rescale_q(stream_->duration, stream_->time_base, micros);
        else if (file_->duration != AV_NOPTS_VALUE) info_.durationUs = file_->duration;
        info_.preciseFrameTiming = true;
        std::snprintf(info_.codec, sizeof(info_.codec), "%s",
            params.codec_id == AV_CODEC_ID_HEVC ? "video/hevc" : params.codec_id == AV_CODEC_ID_H264 ? "video/avc" : "video/mp4v-es");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "FFmpeg %s", decoder->name);
        return true;
    }

    const VideoStreamInfo& info() const noexcept override { return info_; }
    u32 max_live_frames() const noexcept override { return sizeof(void*) <= 4 ? 4 : 8; }

    Status seek_to_keyframe(i64 targetUs) noexcept override {
        set_deadline();
        const i64 start = stream_->start_time == AV_NOPTS_VALUE ? 0 : stream_->start_time;
        const i64 ts = std::max(start, av_rescale_q(std::max<i64>(0, targetUs), micros, stream_->time_base));
        if (av_seek_frame(file_, index_, ts, AVSEEK_FLAG_BACKWARD) < 0)
            return {Errc::DecodeFailed, "FFmpeg: nao foi possivel buscar o quadro-chave da fonte"};
        avcodec_flush_buffers(codec_);
        av_packet_unref(packet_);
        av_frame_unref(frame_);
        draining_ = haveFrame_ = false;
        return OkStatus;
    }

    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& pts, bool& eos) noexcept override {
        set_deadline();
        out.reset();
        eos = false;
        if (!haveFrame_) {
            const Status read = decode(eos);
            if (!read.ok() || eos) { pts = lastPts_; return read; }
            haveFrame_ = true;
        }
        const i64 timestamp = frame_->best_effort_timestamp;
        if (timestamp == AV_NOPTS_VALUE) return {Errc::DecodeFailed, "FFmpeg: quadro sem timestamp de apresentacao"};
        pts = av_rescale_q(timestamp, stream_->time_base, micros);
        const i64 duration = frame_->duration > 0 ? av_rescale_q(frame_->duration, stream_->time_base, micros) : 0;
        if (pts >= deliverFromUs || (duration > 0 && av_sat_add64(pts, duration) > deliverFromUs)) {
            const Status published = publish_frame(pts, duration, out);
            if (!published.ok()) return published;
        }
        haveFrame_ = false;
        lastPts_ = pts;
        av_frame_unref(frame_);
        return OkStatus;
    }

private:
    static int interrupt(void* context) noexcept {
        return monotonic_ns() >= static_cast<PortableVideo*>(context)->deadline_;
    }

    void set_deadline() noexcept { deadline_ = monotonic_ns() + 10'000'000'000ull; }

    Status decode(bool& eos) noexcept {
        while (!interrupt(this)) {
            const int received = avcodec_receive_frame(codec_, frame_);
            if (received >= 0) return OkStatus;
            if (received == AVERROR_EOF) { eos = true; return OkStatus; }
            if (received != AVERROR(EAGAIN) || draining_) break;
            const int read = av_read_frame(file_, packet_);
            if (read == AVERROR_EOF) {
                draining_ = true;
                if (avcodec_send_packet(codec_, nullptr) < 0) break;
                continue;
            }
            if (read < 0) break;
            const int sent = packet_->stream_index == index_ ? avcodec_send_packet(codec_, packet_) : 0;
            av_packet_unref(packet_);
            if (sent < 0) break;
        }
        return {Errc::DecodeFailed, "FFmpeg: nao foi possivel decodificar o proximo quadro da fonte"};
    }

    Status publish_frame(i64 pts, i64 duration, FrameRef& out) noexcept {
        const auto format = static_cast<AVPixelFormat>(frame_->format);
        if (!supported_pixels(format) || frame_->width <= 0 || frame_->height <= 0)
            return {Errc::UnsupportedFormat, "FFmpeg: formato de pixel da fonte mudou"};
        auto frame = std::unique_ptr<PortableFrame>(new (std::nothrow) PortableFrame());
        if (!frame) return Errc::BudgetExceeded;
        if (format == AV_PIX_FMT_YUV420P10LE) {
            frame->storage = av_frame_alloc();
            if (!frame->storage || !repack_p010(*frame_, *frame->storage)) return Errc::BudgetExceeded;
        } else {
            frame->storage = av_frame_clone(frame_);
            if (!frame->storage) return Errc::BudgetExceeded;
        }
        const bool planar = planar8(format);
        frame->format = planar ? PixelFormat::YUV420P : format == AV_PIX_FMT_NV12 ? PixelFormat::NV12 : PixelFormat::P010;
        frame->planeCount = planar ? 3 : 2;
        for (u32 i = 0; i < frame->planeCount; ++i) {
            if (frame->storage->linesize[i] <= 0 || !frame->storage->data[i]) return Errc::UnsupportedFormat;
            frame->planes[i] = frame->storage->data[i];
            frame->strides[i] = static_cast<u32>(frame->storage->linesize[i]);
        }
        frame->width = frame->visibleWidth = static_cast<u32>(frame_->width);
        frame->height = frame->visibleHeight = static_cast<u32>(frame_->height);
        frame->ptsUs = pts;
        frame->durationUs = duration;
        frame->rotation = info_.rotation;
        frame->color = info_.color = color_info(*frame_);
        out = FrameRef::adopt(frame.release());
        return OkStatus;
    }

    AVFormatContext* file_ = nullptr;
    AVStream* stream_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    VideoStreamInfo info_{};
    int index_ = -1;
    u64 deadline_ = 0;
    i64 lastPts_ = 0;
    bool draining_ = false, haveFrame_ = false;
};
} // namespace

std::unique_ptr<VideoDecoderBackend> open_portable_video_source(const char* path) noexcept {
    if (!path || !*path || std::strstr(path, "://")) return nullptr;
    auto source = std::unique_ptr<PortableVideo>(new (std::nothrow) PortableVideo());
    if (!source || !source->open(path)) return nullptr;
    return source;
}
} // namespace aurea
#else
namespace aurea {
std::unique_ptr<VideoDecoderBackend> open_portable_video_source(const char*) noexcept { return nullptr; }
} // namespace aurea
#endif
