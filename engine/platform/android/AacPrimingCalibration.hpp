#pragma once
#include "aurea/export/ExportSink.hpp"
#include "aurea/core/Time.hpp"
#include "MediaMuxerPacket.hpp"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace aurea::android {
// Measure the actual component's encode/decode delay, before any user PCM.
// No manufacturer table, guessed AAC delay, or discarded authored samples.
// The caller restarts this same encoder after calibration; all probe data stays
// in bounded memory and never reaches the project or its output container.
inline Result<u32> measure_aac_priming(AMediaCodec* encoder, const AudioStreamConfig& config,
    const std::atomic<bool>* cancel, std::atomic<u64>* beat) noexcept {
    constexpr u32 sourceFrames = 16384, padding = 8192, maxDelay = 8192;
    if (!encoder || !config.channels || config.channels > 2) return Status{Errc::InvalidArgument};
    auto interrupted = [&] { return cancel && cancel->load(std::memory_order_acquire); };
    auto pulse = [&] { if (beat) beat->store(monotonic_ns(), std::memory_order_release); };
    struct Packet { std::vector<u8> bytes; i64 pts; };
    std::vector<Packet> packets;
    std::vector<i16> source(sourceFrames * config.channels);
    u32 seed = 0x41617265; f64 filtered = 0;
    for (u32 i = 0; i < sourceFrames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const f64 signal = static_cast<i32>(seed >> 16) - 32768;
        filtered = filtered * .65 + signal * .35;
        for (u32 c = 0; c < config.channels; ++c) source[i * config.channels + c] = static_cast<i16>(filtered * .65);
    }
    struct Decode {
        AMediaFormat* format = nullptr; AMediaCodec* codec = nullptr; bool started = false;
        ~Decode() {
            if (codec) { if (started) AMediaCodec_stop(codec); AMediaCodec_delete(codec); }
            if (format) AMediaFormat_delete(format);
        }
    } decoder;
    u32 submitted = 0; bool inputEos = false, outputEos = false;
    u64 deadline = monotonic_ns() + 10'000'000'000ull;
    while (!outputEos) {
        if (interrupted()) return Status{Errc::Cancelled};
        bool progressed = false;
        if (!inputEos) {
            const auto index = AMediaCodec_dequeueInputBuffer(encoder, 1000); pulse();
            if (index >= 0) {
                size_t capacity = 0; u8* bytes = AMediaCodec_getInputBuffer(encoder, index, &capacity);
                if (!bytes || capacity < config.channels * sizeof(i16)) return Status{Errc::EncodeFailed};
                const u32 frames = std::min<u32>(1024, std::min<u32>(sourceFrames + padding - submitted,
                    static_cast<u32>(capacity / (config.channels * sizeof(i16)))));
                if (frames) {
                    std::memset(bytes, 0, frames * config.channels * sizeof(i16));
                    if (submitted < sourceFrames) std::memcpy(bytes, source.data() + submitted * config.channels,
                        std::min(frames, sourceFrames - submitted) * config.channels * sizeof(i16));
                }
                inputEos = frames == 0;
                if (AMediaCodec_queueInputBuffer(encoder, index, 0, frames * config.channels * sizeof(i16),
                    static_cast<i64>(submitted) * 1000000 / config.sampleRate,
                    inputEos ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0) != AMEDIA_OK) return Status{Errc::EncodeFailed};
                submitted += frames; progressed = true;
            } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return Status{Errc::EncodeFailed};
        }
        AMediaCodecBufferInfo info{};
        const auto index = AMediaCodec_dequeueOutputBuffer(encoder, &info, 2000); pulse();
        if (index >= 0) {
            if (info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                size_t capacity = 0; const u8* data = AMediaCodec_getOutputBuffer(encoder, index, &capacity);
                if (!normalize_codec_output_packet(data, capacity, info) || capacity > 65536 || packets.size() >= 128) {
                    AMediaCodec_releaseOutputBuffer(encoder, index, false); return Status{Errc::EncodeFailed};
                }
                packets.push_back({std::vector<u8>(data, data + capacity), info.presentationTimeUs});
            }
            outputEos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(encoder, index, false); progressed = true;
        } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (decoder.format) AMediaFormat_delete(decoder.format);
            decoder.format = AMediaCodec_getOutputFormat(encoder); progressed = true;
        } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER && index != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
            return Status{Errc::EncodeFailed};
        if (progressed) deadline = monotonic_ns() + 10'000'000'000ull;
        if (monotonic_ns() > deadline) return Status{Errc::Timeout, "AAC nao respondeu a calibracao"};
    }
    if (!decoder.format || packets.empty()) return Status{Errc::EncodeFailed};
    AMediaFormat_setInt32(decoder.format, "pcm-encoding", 2);
    AMediaFormat_setInt32(decoder.format, "encoder-delay", 0);
    AMediaFormat_setInt32(decoder.format, "encoder-padding", 0);
    decoder.codec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
    if (!decoder.codec || AMediaCodec_configure(decoder.codec, decoder.format, nullptr, nullptr, 0) != AMEDIA_OK ||
        AMediaCodec_start(decoder.codec) != AMEDIA_OK) return Status{Errc::DecodeFailed};
    decoder.started = true;
    size_t sent = 0; inputEos = outputEos = false;
    std::vector<i16> decoded;
    deadline = monotonic_ns() + 10'000'000'000ull;
    while (!outputEos) {
        if (interrupted()) return Status{Errc::Cancelled};
        bool progressed = false;
        if (!inputEos) {
            const auto index = AMediaCodec_dequeueInputBuffer(decoder.codec, 1000); pulse();
            if (index >= 0) {
                size_t capacity = 0; u8* bytes = AMediaCodec_getInputBuffer(decoder.codec, index, &capacity);
                inputEos = sent == packets.size();
                const size_t size = inputEos ? 0 : packets[sent].bytes.size();
                if (!bytes || size > capacity) return Status{Errc::DecodeFailed};
                if (size) std::memcpy(bytes, packets[sent].bytes.data(), size);
                if (AMediaCodec_queueInputBuffer(decoder.codec, index, 0, size, inputEos ? 0 : packets[sent].pts,
                    inputEos ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0) != AMEDIA_OK) return Status{Errc::DecodeFailed};
                if (!inputEos) ++sent;
                progressed = true;
            } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return Status{Errc::DecodeFailed};
        }
        AMediaCodecBufferInfo info{};
        const auto index = AMediaCodec_dequeueOutputBuffer(decoder.codec, &info, 2000); pulse();
        if (index >= 0) {
            if (info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                size_t capacity = 0; const u8* data = AMediaCodec_getOutputBuffer(decoder.codec, index, &capacity);
                if (!data || info.size > static_cast<i32>(capacity) || info.size % (config.channels * sizeof(i16)) ||
                    decoded.size() + info.size / sizeof(i16) > 65536u * config.channels) {
                    AMediaCodec_releaseOutputBuffer(decoder.codec, index, false); return Status{Errc::DecodeFailed};
                }
                const size_t previous = decoded.size(); decoded.resize(previous + info.size / sizeof(i16));
                std::memcpy(decoded.data() + previous, data, info.size);
            }
            outputEos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(decoder.codec, index, false); progressed = true;
        } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* format = AMediaCodec_getOutputFormat(decoder.codec);
            i32 encoding = 2, channels = 0;
            if (format) {
                AMediaFormat_getInt32(format, "pcm-encoding", &encoding);
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                AMediaFormat_delete(format);
            }
            if (encoding != 2 || channels != static_cast<i32>(config.channels)) return Status{Errc::NotSupported};
            progressed = true;
        } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER && index != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
            return Status{Errc::DecodeFailed};
        if (progressed) deadline = monotonic_ns() + 10'000'000'000ull;
        if (monotonic_ns() > deadline) return Status{Errc::Timeout};
    }
    f64 best = 0; u32 delay = 0;
    const u32 available = static_cast<u32>(decoded.size() / config.channels);
    if (available < 8192) return Status{Errc::DecodeFailed};
    for (u32 lag = 0; lag <= std::min(maxDelay, available - 8192); ++lag) {
        f64 dot = 0, aa = 0, bb = 0;
        for (u32 i = 4096; i < 8192; i += 4) {
            const f64 a = source[i * config.channels], b = decoded[(i + lag) * config.channels];
            dot += a * b; aa += a * a; bb += b * b;
        }
        const f64 correlation = aa > 0 && bb > 0 ? dot / std::sqrt(aa * bb) : 0;
        if (correlation > best) { best = correlation; delay = lag; }
        if ((lag & 255u) == 0) { if (interrupted()) return Status{Errc::Cancelled}; pulse(); }
    }
    if (best < .85 || delay == maxDelay) return Status{Errc::NotSupported, "nao foi possivel medir o atraso real do encoder AAC"};
    AUREA_LOG_INFO("export-v2 AAC calibrated delay_samples=%u correlation=%.6f", delay, best);
    return delay;
}
} // namespace aurea::android
