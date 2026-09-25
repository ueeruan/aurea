// Actual device codecs/muxer/extractor, including nonzero-offset encoded packets.
#include "MediaCodecExport.hpp"
#include "MediaMuxerPacket.hpp"
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
static bool remux_with_offsets(const std::string& path) {
    auto* input = extractor(path); if (!input) return false;
    std::string output = path + ".offset.mp4";
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
        if (android::write_muxer_packet(muxer, track, packet.data(), packet.size(), info) != AMEDIA_OK) return false;
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
    std::printf("offset remux: packets=%zu compared=%zu\n", packets, compared);
    return compared == packets && packets >= 30;
}
int main(int argc, char** argv) {
    if (argc != 6) { std::fprintf(stderr, "usage: probe output.mp4 width height hevc audio\n"); return 2; }
    VideoStreamConfig vc; vc.width = std::atoi(argv[2]); vc.height = std::atoi(argv[3]); vc.fps = 30;
    vc.codec = std::atoi(argv[4]) ? ExportCodec::HEVC : ExportCodec::H264;
    vc.bitrateBps = 12000000;
    AudioStreamConfig ac; const bool audio = std::atoi(argv[5]) != 0;
    auto sink = android::make_mediacodec_export_sink(nullptr);
    if (!check(sink->open(argv[1], vc, audio ? &ac : nullptr), "open")) return 3;
    std::vector<u8> y(vc.width * vc.height), uv(y.size() / 2, 128);
    std::vector<i16> pcm(1600 * 2);
    for (int frame = 0; frame < 30; ++frame) {
        for (u32 row = 0; row < vc.height; ++row) for (u32 col = 0; col < vc.width; ++col)
            y[row * vc.width + col] = static_cast<u8>(16 + ((col / 16 + row / 16 + frame * 2) % 220));
        const i64 pts = std::llround(frame * 1e6 / vc.fps);
        if (!check(sink->write_video(y.data(), vc.width, uv.data(), vc.width, pts), "video")) return 4;
        if (audio) {
            for (int s = 0; s < 1600; ++s) pcm[s * 2] = pcm[s * 2 + 1] = static_cast<i16>(3000 * std::sin((frame * 1600 + s) * 440.0 * 6.283185307 / 48000.0));
            if (!check(sink->write_audio(pcm.data(), 1600, pts), "audio")) return 5;
        }
    }
    if (!check(sink->finish(), "finish")) return 6;
    if (!remux_with_offsets(argv[1])) return 7;
    std::printf("PASS real export %ux%u codec=%d audio=%d with nonzero packet offsets\n", vc.width, vc.height, static_cast<int>(vc.codec), audio);
    return 0;
}
