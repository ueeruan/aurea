// Actual device codecs/muxer/extractor: owned buffer prefixes and normalized
// NDK codec output with unreliable pre-36 offset/capacity metadata.
#include "MediaCodecExport.hpp"
#include "MediaMuxerPacket.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/export/BitratePolicy.hpp"
#include "aurea/export/ExportSinkStartup.hpp"
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
using namespace aurea;

static AMediaExtractor* extractor(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat file{}; fstat(fd, &file);
    auto* ex = AMediaExtractor_new();
    auto result = AMediaExtractor_setDataSourceFd(ex, fd, 0, file.st_size); close(fd);
    if (result != AMEDIA_OK) { AMediaExtractor_delete(ex); return nullptr; }
    return ex;
}
static bool check(Status s, const char* stage) {
    if (s.ok()) return true;
    std::fprintf(stderr, "%s failed %d: %.*s\n", stage, s.raw(), static_cast<int>(s.detail().size()), s.detail().data());
    return false;
}
static size_t rss_kib() {
    FILE* status = std::fopen("/proc/self/status", "r");
    if (!status) return 0;
    char line[256]; size_t rss = 0;
    while (std::fgets(line, sizeof(line), status)) if (std::sscanf(line, "VmRSS: %zu kB", &rss) == 1) break;
    std::fclose(status); return rss;
}
static bool verify_video_samples(const std::string& path, int expectedFrames, bool audio) {
    auto* input = extractor(path); if (!input) return false;
    size_t videoTracks = 0, audioTracks = 0;
    for (size_t t = 0; t < AMediaExtractor_getTrackCount(input); ++t) {
        auto* format = AMediaExtractor_getTrackFormat(input, t);
        const char* mime = nullptr;
        if (!format || !AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) return false;
        const bool video = std::strncmp(mime, "video/", 6) == 0;
        if (video) ++videoTracks; else if (std::strncmp(mime, "audio/", 6) == 0) ++audioTracks;
        AMediaFormat_delete(format);
        if (!video) continue;
        AMediaExtractor_selectTrack(input, t);
        int frames = 0;
        while (AMediaExtractor_getSampleTrackIndex(input) >= 0) {
            const i64 expectedPts = std::llround(frames * 1e6 / 30.0);
            if (frames >= expectedFrames || std::llabs(AMediaExtractor_getSampleTime(input) - expectedPts) > 1) return false;
            ++frames; AMediaExtractor_advance(input);
        }
        AMediaExtractor_unselectTrack(input, t);
        if (frames != expectedFrames) return false;
    }
    AMediaExtractor_delete(input);
    std::printf("verified video samples=%d exact PTS, audio_tracks=%zu\n", expectedFrames, audioTracks);
    return videoTracks == 1 && audioTracks == size_t(audio);
}
static bool remux_with_offsets(const std::string& path, bool codecOutput = false) {
    auto* input = extractor(path); if (!input) return false;
    std::string output = path + (codecOutput ? ".codec-output.mp4" : ".offset.mp4");
    int fd = open(output.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    auto* muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!muxer) return false;
    const size_t tracks = AMediaExtractor_getTrackCount(input);
    for (size_t t = 0; t < tracks; ++t) {
        auto* fmt = AMediaExtractor_getTrackFormat(input, t);
        const ssize_t added = AMediaMuxer_addTrack(muxer, fmt); AMediaFormat_delete(fmt);
        if (added != static_cast<ssize_t>(t)) return false;
        AMediaExtractor_selectTrack(input, t);
    }
    if (AMediaMuxer_start(muxer) != AMEDIA_OK) return false;
    std::vector<uint8_t> packet(8u << 20, 0xa5);
    size_t packets = 0;
    while (AMediaExtractor_getSampleTrackIndex(input) >= 0) {
        const int offset = 17 + static_cast<int>(packets % 43);
        const ssize_t size = AMediaExtractor_readSampleData(input, packet.data() + offset, packet.size() - offset);
        if (size <= 0) return false;
        AMediaCodecBufferInfo info{}; info.offset = offset; info.size = static_cast<int32_t>(size);
        info.presentationTimeUs = AMediaExtractor_getSampleTime(input);
        info.flags = AMediaExtractor_getSampleFlags(input) & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME : 0;
        const size_t track = AMediaExtractor_getSampleTrackIndex(input);
        if (android::write_muxer_packet(muxer, track, packet.data(), offset + size - 1, info) != AMEDIA_ERROR_MALFORMED)
            return false; // Reject an out-of-bounds packet before the platform touches it.
        if (codecOutput) {
            // The NDK pointer has already advanced past the prefix. Reproduce
            // both invalid kinds of metadata allowed on API <=35, then prove
            // that normalization writes exactly the original encoded sample.
            const uint8_t* sample = packet.data() + offset;
            size_t capacity = packets % 2 ? 0 : packet.size();
            info.offset = packets % 3 ? offset : -1;
            if (!android::normalize_codec_output_packet(sample, capacity, info) ||
                info.offset != 0 || capacity != static_cast<size_t>(size)) return false;
            if (android::write_muxer_packet(muxer, track, sample, capacity, info) != AMEDIA_OK) return false;
        } else if (android::write_muxer_packet(muxer, track, packet.data(), packet.size(), info) != AMEDIA_OK) return false;
        ++packets; AMediaExtractor_advance(input);
    }
    if (AMediaMuxer_stop(muxer) != AMEDIA_OK) return false;
    AMediaMuxer_delete(muxer); close(fd); AMediaExtractor_delete(input);
    // Verify every original encoded byte and timestamp through a second extractor.
    input = extractor(path); auto* result = extractor(output); if (!input || !result) return false;
    std::vector<uint8_t> actual(packet.size()); size_t compared = 0;
    for (size_t t = 0; t < tracks; ++t) {
        AMediaExtractor_selectTrack(input, t); AMediaExtractor_selectTrack(result, t);
        for (;;) {
            auto n = AMediaExtractor_readSampleData(input, packet.data(), packet.size());
            auto m = AMediaExtractor_readSampleData(result, actual.data(), actual.size());
            if (n < 0 || m < 0) { if (n != m) return false; break; }
            if (n != m || std::memcmp(packet.data(), actual.data(), n) ||
                AMediaExtractor_getSampleTime(input) != AMediaExtractor_getSampleTime(result)) return false;
            ++compared; AMediaExtractor_advance(input); AMediaExtractor_advance(result);
        }
        AMediaExtractor_unselectTrack(input, t); AMediaExtractor_unselectTrack(result, t);
        AMediaExtractor_seekTo(input, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
        AMediaExtractor_seekTo(result, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    }
    AMediaExtractor_delete(input); AMediaExtractor_delete(result);
    std::printf("%s remux: packets=%zu compared=%zu\n", codecOutput ? "codec-output" : "owned-offset", packets, compared);
    return compared == packets && packets >= 30;
}
int main(int argc, char** argv) {
    if (argc < 6 || argc > 9) { std::fprintf(stderr, "usage: probe output.mp4 width height hevc audio [default|software|cbr|software-cbr|gate] [frames] [auto]\n"); return 2; }
    const bool gated = argc >= 7 && std::strcmp(argv[6], "gate") == 0;
    const int frames = argc >= 8 ? std::atoi(argv[7]) : 30;
    if (frames <= 0 || frames > 1000000) return 2;
    VideoStreamConfig vc; vc.width = std::atoi(argv[2]); vc.height = std::atoi(argv[3]); vc.fps = 30;
    vc.codec = std::atoi(argv[4]) ? ExportCodec::HEVC : ExportCodec::H264;
    vc.bitrateBps = 12000000;
    if (argc >= 7) {
        if (std::strcmp(argv[6], "software") == 0 || std::strcmp(argv[6], "software-cbr") == 0)
            vc.preferSoftware = true;
        if (std::strcmp(argv[6], "cbr") == 0 || std::strcmp(argv[6], "software-cbr") == 0) vc.rateMode = 0;
        if (!vc.preferSoftware && vc.rateMode != 0 && std::strcmp(argv[6], "default") != 0 && !gated) return 2;
    }
    if (argc == 9) {
        if (std::strcmp(argv[8], "auto") != 0) return 2;
        vc.bitrateBps = export_video_bitrate_bps(vc.width, vc.height, vc.fps, vc.codec, ExportQuality::Normal);
    }
    AudioStreamConfig ac; const bool audio = std::atoi(argv[5]) != 0;
    auto sink = android::make_mediacodec_export_sink(nullptr);
    std::atomic<bool> cancel{false};
    sink->set_cancel_flag(&cancel);
    VideoStreamConfig invalid = vc; invalid.fps = std::numeric_limits<f64>::quiet_NaN();
    if (sink->open(argv[1], invalid, nullptr).ok() || sink->open(nullptr, vc, nullptr).ok()) return 8;
    if (gated) {
        auto opened = open_export_sink_startup(std::move(sink), argv[1], vc, audio ? &ac : nullptr, &cancel, nullptr);
        if (!check(opened.status(), "gated open")) return 3;
        sink = std::move(opened.sink);
    } else if (!check(sink->open(argv[1], vc, audio ? &ac : nullptr), "open")) return 3;
    std::vector<u8> y(vc.width * vc.height), uv(y.size() / 2, 128);
    std::vector<i16> pcm(1600 * 2);
    cancel = true;
    if (sink->write_video(y.data(), vc.width, uv.data(), vc.width, 0).code() != Errc::Cancelled) return 8;
    cancel = false;
    // Rejected calls must not consume a codec input slot or truncate the file.
    if (sink->open(argv[1], vc, nullptr).ok() ||
        sink->write_video(nullptr, vc.width, uv.data(), vc.width, 0).ok() ||
        sink->write_video(y.data(), vc.width, nullptr, vc.width, 0).ok() ||
        sink->write_video(y.data(), vc.width, uv.data(), vc.width, -1).ok() ||
        (audio && sink->write_audio(nullptr, 1600, 0).ok())) return 8;
    const u64 started = monotonic_ns();
    size_t peakRss = rss_kib();
    std::printf("BEGIN frames=%d output_seconds=%.3f requested_bps=%u rss_kib=%zu\n", frames, frames / vc.fps, vc.bitrateBps, peakRss);
    std::fflush(stdout);
    for (int frame = 0; frame < frames; ++frame) {
        for (u32 row = 0; row < vc.height; ++row) for (u32 col = 0; col < vc.width; ++col)
            y[row * vc.width + col] = static_cast<u8>(16 + ((col / 16 + row / 16 + frame * 2) % 220));
        const i64 pts = std::llround(frame * 1e6 / vc.fps);
        if (!check(sink->write_video(y.data(), vc.width, uv.data(), vc.width, pts), "video")) return 4;
        if (audio) {
            for (int s = 0; s < 1600; ++s) pcm[s * 2] = pcm[s * 2 + 1] = static_cast<i16>(3000 * std::sin((frame * 1600 + s) * 440.0 * 6.283185307 / 48000.0));
            if (!check(sink->write_audio(pcm.data(), 1600, pts), "audio")) return 5;
        }
        if ((frame + 1) % 300 == 0 || frame + 1 == frames) {
            const size_t rss = rss_kib(); peakRss = std::max(peakRss, rss);
            std::printf("PROGRESS frame=%d/%d output_seconds=%.3f elapsed_seconds=%.3f rss_kib=%zu peak_rss_kib=%zu\n",
                frame + 1, frames, (frame + 1) / vc.fps, (monotonic_ns() - started) / 1e9, rss, peakRss);
            std::fflush(stdout);
        }
    }
    if (!check(sink->finish(), "finish")) return 6;
    if (sink->write_video(y.data(), vc.width, uv.data(), vc.width, 1000000).ok() || sink->finish().ok()) return 8;
    if (!verify_video_samples(argv[1], frames, audio)) return 10;
    if (!remux_with_offsets(argv[1]) || !remux_with_offsets(argv[1], true)) return 7;
    // Reusing a completed sink must reset EOS, mux tracks and layout state.
    const std::string reused = std::string(argv[1]) + ".reused.mp4";
    if (gated) {
        sink.reset();
        auto opened = open_export_sink_startup(android::make_mediacodec_export_sink(nullptr), reused.c_str(), vc, nullptr, &cancel, nullptr);
        if (!check(opened.status(), "gated reopen")) return 9;
        sink = std::move(opened.sink);
    } else if (!check(sink->open(reused.c_str(), vc, nullptr), "reopen")) return 9;
    if (
        !check(sink->write_video(y.data(), vc.width, uv.data(), vc.width, 0), "reopened video") ||
        !check(sink->finish(), "reopened finish")) return 9;
    struct stat output{}; ::stat(argv[1], &output);
    std::printf("PASS real export %ux%u codec=%d audio=%d frames=%d elapsed_seconds=%.3f peak_rss_kib=%zu bytes=%lld gate=%d with nonzero packet offsets\n",
        vc.width, vc.height, static_cast<int>(vc.codec), audio, frames, (monotonic_ns() - started) / 1e9,
        peakRss, static_cast<long long>(output.st_size), int(gated));
    return 0;
}
