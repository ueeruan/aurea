#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <iterator>
#include <mutex>
#include <string>
#include "aurea/core/Time.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {

/// Session-owned, path-free export evidence. Its mutex is never held across
/// provider/codec/GPU calls. Reading diagnostics does not acquire model/render
/// locks and a stalled platform call cannot block the reader.
class ExportDiagnostics {
public:
    enum class Lane : u32 { Session, Producer, Encoder };
    enum class Stage : u32 {
        Idle, Admission, Preparation, Snapshot, Sources, AudioSnapshot, CodecOpen,
        GpuSetup, Decode, Render, Readback, VideoInput, AudioInput, Drain,
        Validation, Publish, Abort, Complete, Failed, Cancelled
    };
    enum class Counter : u32 { Rendered, Submitted, VideoPackets, AudioPackets, InputAgain, OutputAgain, Count };
    struct Format {
        u32 width = 0, height = 0, bitrate = 0, profile = 0, safeMode = 0;
        f64 fps = 0;
        i32 color = -1; // requested MediaCodec color; -1 means unknown/not applicable
    };

    ExportDiagnostics() noexcept : start_(monotonic_ns()) {}
    void stage(Lane lane, Stage stage) noexcept {
        std::lock_guard lock(mutex_);
        if (state_.ended) return;
        auto& target = state_.lanes[static_cast<usize>(lane)];
        if (target.stage != stage) { target.stage = stage; target.since = monotonic_ns(); }
        if (lane == Lane::Session && (stage == Stage::Complete || stage == Stage::Failed || stage == Stage::Cancelled)) {
            state_.ended = monotonic_ns();
            for (usize i = 0; i < state_.finalCounts.size(); ++i) state_.finalCounts[i] = counters_[i].load(std::memory_order_relaxed);
            state_.firstInput = firstInput_.load(std::memory_order_relaxed);
            state_.firstPacket = firstPacket_.load(std::memory_order_relaxed);
        }
    }
    void format(Format format) noexcept { std::lock_guard lock(mutex_); if (!state_.ended) state_.format = format; }
    void codec(const char* name, const char* backend, i32 color = -1, u32 codedWidth = 0, u32 codedHeight = 0) noexcept {
        std::lock_guard lock(mutex_);
        if (state_.ended) return;
        copy_token(state_.codec, name); copy_token(state_.backend, backend);
        state_.format.color = color;
        state_.codedWidth = codedWidth; state_.codedHeight = codedHeight;
        ++state_.candidates;
    }
    void event(Counter counter) noexcept {
        const auto index = static_cast<usize>(counter);
        counters_[index].fetch_add(1, std::memory_order_relaxed);
        if (counter == Counter::Submitted) first(firstInput_);
        if (counter == Counter::VideoPackets) first(firstPacket_);
    }
    void result(i32 code) noexcept {
        std::lock_guard lock(mutex_); if (!state_.ended) { state_.result = code; state_.hasResult = true; }
    }
    /// Degrau da escada de encoders (FFmpegExportSink): o que está em uso e os
    /// que falharam antes ("surface:c2.x abrindo | falhas: ..."). ASCII seguro.
    void encoder_path(const char* text) noexcept {
        std::lock_guard lock(mutex_);
        if (state_.ended) return;
        usize n = 0;
        if (text) for (; text[n] && n + 1 < sizeof(state_.path); ++n) {
            const unsigned char c = static_cast<unsigned char>(text[n]);
            // Sem aspas, barra invertida nem controle: o texto entra cru no JSON.
            state_.path[n] = c >= 0x20 && c < 0x7f && c != '"' && c != '\\' ? static_cast<char>(c) : '_';
        }
        state_.path[n] = '\0';
    }
    /// CheckpointManager (export/ExportCheckpoint.hpp): "on", "resumed",
    /// "on_restarted" (o encoder não continuou o registro), "off",
    /// "off_low_storage", "off_sources", "off_temporal", "off_project",
    /// "off_directory". `resumeFrame`: de onde esta sessão continuou.
    void checkpoint(const char* state, u32 resumeFrame) noexcept {
        std::lock_guard lock(mutex_);
        if (state_.ended) return;
        copy_token(state_.checkpoint, state);
        state_.resumeFrame = resumeFrame;
    }
    [[nodiscard]] std::string json() const noexcept {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
            State snapshot;
            { std::unique_lock lock(mutex_, std::try_to_lock);
              if (!lock.owns_lock()) return "{\"schema\":1,\"busy\":true}";
              snapshot = state_; }
            const u64 now = snapshot.ended ? snapshot.ended : monotonic_ns();
            std::array<u64, static_cast<usize>(Counter::Count)> counts{};
            for (usize i = 0; i < counts.size(); ++i) counts[i] = snapshot.ended ? snapshot.finalCounts[i] : counters_[i].load(std::memory_order_relaxed);
            char buffer[2048];
            const auto input = snapshot.ended ? snapshot.firstInput : firstInput_.load(std::memory_order_relaxed);
            const auto packet = snapshot.ended ? snapshot.firstPacket : firstPacket_.load(std::memory_order_relaxed);
            const int written = std::snprintf(buffer, sizeof(buffer),
                "{\"schema\":1,\"sessionToken\":\"%llu\",\"elapsedMs\":%llu,\"sessionStage\":\"%s\",\"sessionWaitMs\":%llu,"
                "\"producerStage\":\"%s\",\"producerWaitMs\":%llu,\"encoderStage\":\"%s\",\"encoderWaitMs\":%llu,"
                "\"encoder\":\"%s\",\"backend\":\"%s\",\"candidateCount\":%u,\"requestedColor\":%d,\"codedWidth\":%u,\"codedHeight\":%u,"
                "\"width\":%u,\"height\":%u,\"fpsNumerator\":%llu,\"fpsDenominator\":1000000,\"bitrate\":%u,\"requestedProfile\":%u,\"safeMode\":%u,"
                "\"rendered\":%llu,\"submitted\":%llu,\"videoPackets\":%llu,\"audioPackets\":%llu,"
                "\"inputAgain\":%llu,\"outputAgain\":%llu,\"firstInputMs\":%lld,\"firstPacketMs\":%lld,"
                "\"hasResult\":%s,\"result\":%d,\"negotiatedColor\":null,\"encoderPath\":\"%s\","
                "\"checkpoint\":\"%s\",\"resumeFrame\":%u}",
                static_cast<unsigned long long>(start_), ms(now, start_), name(snapshot.lanes[0].stage), ms(now, snapshot.lanes[0].since),
                name(snapshot.lanes[1].stage), ms(now, snapshot.lanes[1].since),
                name(snapshot.lanes[2].stage), ms(now, snapshot.lanes[2].since),
                snapshot.codec, snapshot.backend, snapshot.candidates, snapshot.format.color, snapshot.codedWidth, snapshot.codedHeight,
                snapshot.format.width, snapshot.format.height,
                std::isfinite(snapshot.format.fps) && snapshot.format.fps > 0 && snapshot.format.fps <= 1000 ? static_cast<unsigned long long>(std::llround(snapshot.format.fps * 1e6)) : 0ull,
                snapshot.format.bitrate,
                snapshot.format.profile, snapshot.format.safeMode,
                static_cast<unsigned long long>(counts[0]), static_cast<unsigned long long>(counts[1]),
                static_cast<unsigned long long>(counts[2]), static_cast<unsigned long long>(counts[3]),
                static_cast<unsigned long long>(counts[4]), static_cast<unsigned long long>(counts[5]),
                input ? static_cast<long long>(ms(input, start_)) : -1ll,
                packet ? static_cast<long long>(ms(packet, start_)) : -1ll,
                snapshot.hasResult ? "true" : "false", snapshot.result, snapshot.path,
                snapshot.checkpoint[0] ? snapshot.checkpoint : "off", snapshot.resumeFrame);
            return written > 0 && written < static_cast<int>(sizeof(buffer)) ? std::string(buffer, written) : "{}";
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        } catch (...) { return "{}"; }
#endif
    }
private:
    struct Phase { Stage stage = Stage::Idle; u64 since = 0; };
    struct State {
        std::array<Phase, 3> lanes{};
        Format format{};
        char codec[96]{}, backend[48]{};
        char path[320]{};   ///< encoderPath: degrau da escada em uso + falhas
        char checkpoint[24]{};   ///< estado do checkpoint (ver checkpoint())
        u32 resumeFrame = 0;
        u32 candidates = 0;
        u32 codedWidth = 0, codedHeight = 0;
        i32 result = 0;
        bool hasResult = false;
        u64 ended = 0, firstInput = 0, firstPacket = 0;
        std::array<u64, static_cast<usize>(Counter::Count)> finalCounts{};
    };
    const u64 start_;
    mutable std::mutex mutex_;
    State state_{};
    std::array<std::atomic<u64>, static_cast<usize>(Counter::Count)> counters_{};
    std::atomic<u64> firstInput_{0}, firstPacket_{0};
    static unsigned long long ms(u64 now, u64 since) noexcept {
        return since && now >= since ? static_cast<unsigned long long>((now - since) / 1'000'000) : 0;
    }
    static void first(std::atomic<u64>& target) noexcept {
        u64 zero = 0; target.compare_exchange_strong(zero, monotonic_ns(), std::memory_order_relaxed);
    }
    template<usize N> static void copy_token(char (&out)[N], const char* text) noexcept {
        usize count = 0;
        if (text) for (; text[count] && count + 1 < N; ++count) {
            const unsigned char c = static_cast<unsigned char>(text[count]);
            out[count] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ? c : '_';
        }
        out[count] = '\0';
    }
    static const char* name(Stage stage) noexcept {
        static constexpr const char* names[] = {"idle", "admission", "preparation", "snapshot", "sources", "audio_snapshot", "codec_open",
            "gpu_setup", "decode", "render", "readback", "video_input", "audio_input", "drain", "validation", "publish", "abort", "complete", "failed", "cancelled"};
        const auto index = static_cast<usize>(stage);
        return index < std::size(names) ? names[index] : "unknown";
    }
};
} // namespace aurea
