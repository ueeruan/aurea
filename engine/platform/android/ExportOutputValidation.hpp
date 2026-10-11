#pragma once
#include "aurea/export/ExportOutputValidation.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Log.hpp"
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace aurea::android {
// Bounded, constant-memory decode of BOTH finalized tracks. A valid MP4 header
// alone is not evidence that compressed frames or the audio tail are readable.
inline Status validate_android_export(const char* path, const ExportOutputValidation& expected,
    const std::atomic<bool>* cancel, std::atomic<u64>* beat) noexcept {
    if (!path || !expected.width || !expected.height || !expected.frames || !std::isfinite(expected.fps)
        || expected.fps <= 0 || !expected.stallNs) return Errc::InvalidArgument;
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_acquire); };
    auto pulse = [&] { if (beat) beat->store(monotonic_ns(), std::memory_order_release); };
    struct File { int fd = -1; ~File() { if (fd >= 0) ::close(fd); } } file;
    file.fd = ::open(path, O_RDONLY);
    struct stat metadata{};
    if (file.fd < 0 || fstat(file.fd, &metadata) != 0 || metadata.st_size <= 0) return Errc::IoError;
    const i64 durationUs = static_cast<i64>(std::llround(expected.frames * 1e6 / expected.fps));
    const i64 toleranceUs = std::max<i64>(100000, static_cast<i64>(std::ceil(2e6 / expected.fps)));
    bool videoFound = false, audioFound = false;
    for (bool audio : {false, true}) {
        if (audio && !expected.audio) break;
        struct Reader {
            AMediaExtractor* extractor = AMediaExtractor_new();
            AMediaCodec* codec = nullptr; AMediaFormat* format = nullptr; bool started = false;
            ~Reader() {
                if (codec) { if (started) AMediaCodec_stop(codec); AMediaCodec_delete(codec); }
                if (format) AMediaFormat_delete(format);
                if (extractor) AMediaExtractor_delete(extractor);
            }
        } reader;
        if (!reader.extractor || AMediaExtractor_setDataSourceFd(reader.extractor, file.fd, 0, metadata.st_size) != AMEDIA_OK)
            return Status{Errc::EncodeFailed, "validacao: MP4 ilegivel"};
        const char* mime = nullptr;
        for (size_t track = 0; track < AMediaExtractor_getTrackCount(reader.extractor); ++track) {
            AMediaFormat* format = AMediaExtractor_getTrackFormat(reader.extractor, track);
            const char* candidate = nullptr;
            const bool match = format && AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &candidate)
                && std::strncmp(candidate, audio ? "audio/" : "video/", 6) == 0;
            if (match) {
                reader.format = format; AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
                if (AMediaExtractor_selectTrack(reader.extractor, track) != AMEDIA_OK) return Errc::EncodeFailed;
                break;
            }
            if (format) AMediaFormat_delete(format);
        }
        if (!reader.format || !mime) return Status{Errc::EncodeFailed, "validacao: trilha esperada ausente"};
        i64 trackDuration = 0;
        if (!AMediaFormat_getInt64(reader.format, AMEDIAFORMAT_KEY_DURATION, &trackDuration)
            || std::llabs(trackDuration - durationUs) > toleranceUs)
            return Status{Errc::EncodeFailed, "validacao: duracao da trilha difere do projeto"};
        i32 width = 0, height = 0, channels = 2, rate = 48000;
        if (!audio) {
            AMediaFormat_getInt32(reader.format, AMEDIAFORMAT_KEY_WIDTH, &width);
            AMediaFormat_getInt32(reader.format, AMEDIAFORMAT_KEY_HEIGHT, &height);
            if (width != static_cast<i32>(expected.width) || height != static_cast<i32>(expected.height))
                return Status{Errc::EncodeFailed, "validacao: resolucao difere do pedido"};
        } else {
            AMediaFormat_getInt32(reader.format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
            AMediaFormat_getInt32(reader.format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
            if (channels <= 0 || channels > 2 || rate <= 0) return Errc::EncodeFailed;
            AMediaFormat_setInt32(reader.format, "pcm-encoding", 2); // PCM16
        }
        reader.codec = AMediaCodec_createDecoderByType(mime);
        pulse();
        if (!reader.codec || AMediaCodec_configure(reader.codec, reader.format, nullptr, nullptr, 0) != AMEDIA_OK)
            return Status{Errc::UnsupportedCodec, "validacao: decoder indisponivel"};
        if (AMediaCodec_start(reader.codec) != AMEDIA_OK) return Errc::DecodeFailed;
        reader.started = true; pulse();
        bool inputEnd = false, outputEnd = false;
        u32 decoded = 0; i64 lastPts = std::numeric_limits<i64>::min(), decodedEndUs = 0;
        u64 deadline = monotonic_ns() + expected.stallNs;
        while (!outputEnd) {
            if (cancelled()) return Errc::Cancelled;
            bool progressed = false;
            if (!inputEnd) {
                const auto input = AMediaCodec_dequeueInputBuffer(reader.codec, 1000); pulse();
                if (input >= 0) {
                    size_t capacity = 0;
                    u8* bytes = AMediaCodec_getInputBuffer(reader.codec, input, &capacity);
                    if (!bytes || !capacity) return Errc::DecodeFailed;
                    const ssize_t size = AMediaExtractor_readSampleData(reader.extractor, bytes, capacity);
                    const i64 pts = size < 0 ? 0 : AMediaExtractor_getSampleTime(reader.extractor);
                    if (size > static_cast<ssize_t>(capacity) || (size >= 0 && pts < (audio ? -toleranceUs : 0)))
                        return Status{Errc::DecodeFailed, "validacao: pacote fora do intervalo da trilha"};
                    inputEnd = size < 0;
                    if (AMediaCodec_queueInputBuffer(reader.codec, input, 0, inputEnd ? 0 : size, pts,
                        inputEnd ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0) != AMEDIA_OK) return Errc::DecodeFailed;
                    if (!inputEnd) AMediaExtractor_advance(reader.extractor);
                    progressed = true; pulse();
                } else if (input != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return Errc::DecodeFailed;
            }
            AMediaCodecBufferInfo info{};
            const auto output = AMediaCodec_dequeueOutputBuffer(reader.codec, &info, 2000); pulse();
            if (output >= 0) {
                if (info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                    if (info.presentationTimeUs < (audio ? -toleranceUs : 0) || info.presentationTimeUs < lastPts ||
                        (audio && !decoded && info.presentationTimeUs > toleranceUs)) {
                        AMediaCodec_releaseOutputBuffer(reader.codec, output, false); return Errc::DecodeFailed;
                    }
                    if (!audio) {
                        const i64 expectedPts = static_cast<i64>(std::llround(decoded * 1e6 / expected.fps));
                        if (decoded >= expected.frames || std::llabs(info.presentationTimeUs - expectedPts) > 2000) {
                            AMediaCodec_releaseOutputBuffer(reader.codec, output, false);
                            return Status{Errc::EncodeFailed, "validacao: quadro perdido ou timestamp incorreto"};
                        }
                        ++decoded;
                        decodedEndUs = info.presentationTimeUs + static_cast<i64>(std::llround(1e6 / expected.fps));
                    } else {
                        ++decoded;
                        decodedEndUs = std::max(decodedEndUs, info.presentationTimeUs +
                            static_cast<i64>(info.size / (channels * 2)) * 1000000 / rate);
                    }
                    lastPts = info.presentationTimeUs;
                }
                outputEnd = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                if (AMediaCodec_releaseOutputBuffer(reader.codec, output, false) != AMEDIA_OK) return Errc::DecodeFailed;
                progressed = true;
            } else if (output == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* format = AMediaCodec_getOutputFormat(reader.codec);
                if (!format) return Errc::DecodeFailed;
                if (audio) {
                    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
                }
                AMediaFormat_delete(format);
                if (channels <= 0 || rate <= 0) return Errc::DecodeFailed;
                progressed = true;
            } else if (output != AMEDIACODEC_INFO_TRY_AGAIN_LATER && output != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
                return Errc::DecodeFailed;
            if (progressed) deadline = monotonic_ns() + expected.stallNs;
            if (monotonic_ns() > deadline) return Status{Errc::Timeout, "validacao: decoder sem progresso"};
        }
        if (!decoded || (!audio && decoded != expected.frames) || std::llabs(decodedEndUs - durationUs) > toleranceUs)
            return Status{Errc::EncodeFailed, "validacao: video ou audio truncado"};
        (audio ? audioFound : videoFound) = true;
        AUREA_LOG_INFO("export-v2 validated track=%s decoded=%u end_us=%lld", audio ? "audio" : "video", decoded,
            static_cast<long long>(decodedEndUs));
    }
    return videoFound && (!expected.audio || audioFound) ? OkStatus : Status{Errc::EncodeFailed};
}
} // namespace aurea::android
