// Run through adb on a device/emulator. Uses the production decoder, not a mock.
#include "MediaCodecSource.hpp"
#include "aurea/core/Time.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace aurea;

static u64 frame_hash(const FrameRef& frame) {
    if (!frame || !frame->prepare_cpu_planes()) return 0;
    u64 hash = 14695981039346656037ULL;
    if (frame->planeCount == 1 && frame->format == PixelFormat::RGBA8) {
        if (!frame->planes[0] || frame->strides[0] < frame->width * 4) return 0;
        for (u32 y = 0; y < frame->height; ++y) for (u32 x = 0; x < frame->width * 4; ++x) {
            hash ^= frame->planes[0][static_cast<usize>(y) * frame->strides[0] + x];
            hash *= 1099511628211ULL;
        }
        return hash;
    }
    if (frame->planeCount != 3 || frame->format != PixelFormat::YUV420P) return 0;
    for (u32 p = 0; p < 3; ++p) {
        if (!frame->planes[p]) return 0;
        const u32 w = p ? (frame->width + 1) / 2 : frame->width;
        const u32 h = p ? (frame->height + 1) / 2 : frame->height;
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
            hash ^= frame->planes[p][static_cast<usize>(y) * frame->strides[p] + x];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) { std::fprintf(stderr, "usage: aurea_media_probe video timestamps-us.txt [software|gl]\n"); return 2; }
    const bool software = argc == 4 && std::strcmp(argv[3], "software") == 0;
    const bool gl = argc == 4 && std::strcmp(argv[3], "gl") == 0;
    if (argc == 4 && !software && !gl) return 2;
    std::ifstream timing(argv[2]);
    std::vector<i64> expected;
    for (i64 pts; timing >> pts;) expected.push_back(pts);
    if (expected.empty()) return 2;
    android::MediaCodecFactory factory;
    factory.set_zero_copy(false);
    factory.set_software_only(software);
    factory.set_driver_gl(gl);
    Asset asset; asset.kind = AssetKind::Video; asset.sourcePath = argv[1];
    auto decoder = factory.open_video(asset, MediaPriority::Preview);
    if (!decoder || !decoder->seek_to_keyframe(0).ok()) return 3;
    if (software && decoder->info().hardwareDecoder) return 15;
    std::vector<u64> hashes;
    FrameRef retained;
    const u64 started = monotonic_ns();
    bool eos = false;
    while (!eos) {
        FrameRef frame; i64 pts = -1;
        const Status status = decoder->next_frame(0, frame, pts, eos);
        if (!status.ok()) { std::fprintf(stderr, "decode: %.*s\n", static_cast<int>(status.detail().size()), status.detail().data()); return 4; }
        if (!frame) { if (!eos) return 5; break; }
        const usize index = hashes.size();
        const i64 end = index + 1 < expected.size() ? expected[index + 1] : decoder->info().durationUs;
        // Extractor truncates rational timestamps to microseconds; FFmpeg's
        // reference list rounds. Allow only that conversion's one-us error.
        if (index >= expected.size() || std::llabs(pts - expected[index]) > 1 || frame->ptsUs != pts
            || std::llabs(frame->durationUs - (end - pts)) > 1) {
            std::fprintf(stderr, "timing mismatch at %zu: pts=%lld duration=%lld\n", index,
                static_cast<long long>(pts), static_cast<long long>(frame->durationUs)); return 6;
        }
        const u64 hash = frame_hash(frame);
        if (!hash) return 7;
        if (gl && decoder->info().hardwareDecoder &&
            (frame->format != PixelFormat::RGBA8 || !frame->hardwareBuffer || frame->planeCount != 1)) return 19;
        hashes.push_back(hash);
        if (!retained) retained = frame;
    }
    if (hashes.size() != expected.size()) return 8;
    const double sequentialMs = (monotonic_ns() - started) / 1e6;
    for (usize index : {expected.size()-1, usize{0}, expected.size()/2, usize{1}}) {
        if (index >= expected.size()) continue;
        const i64 end = index + 1 < expected.size() ? expected[index + 1] : decoder->info().durationUs;
        const i64 target = expected[index] + (end - expected[index]) / 2;
        if (!decoder->seek_to_keyframe(target).ok()) return 9;
        bool found = false;
        for (usize count = 0; count <= expected.size(); ++count) {
            FrameRef frame; i64 pts = -1; bool ended = false;
            if (!decoder->next_frame(target, frame, pts, ended).ok()) return 10;
            if (frame) {
                found = std::llabs(pts - expected[index]) <= 1 && frame->covers(target) && frame_hash(frame) == hashes[index];
                break;
            }
            if (ended) break;
        }
        if (!found) { std::fprintf(stderr, "seek mismatch at %zu\n", index); return 11; }
    }
    decoder->suspend();
    if (frame_hash(retained) != hashes.front()) return 12;
    if (!decoder->resume().ok() || !decoder->seek_to_keyframe(0).ok()) return 13;
    FrameRef first; i64 pts = -1;
    const Status resumed = decoder->next_frame(0, first, pts, eos);
    if (!resumed.ok() || pts != expected.front() || frame_hash(first) != hashes.front()) {
        std::fprintf(stderr, "resume: status=%d pts=%lld frame=%d hash=%llu expected=%llu detail=%.*s\n",
            resumed.raw(), static_cast<long long>(pts), first ? 1 : 0,
            static_cast<unsigned long long>(frame_hash(first)), static_cast<unsigned long long>(hashes.front()),
            static_cast<int>(resumed.detail().size()), resumed.detail().empty() ? "" : resumed.detail().data());
        return 14;
    }
    auto thumbnail = factory.open_video(asset, MediaPriority::Thumbnail);
    if (!thumbnail || (software && thumbnail->info().hardwareDecoder)) return 16;
    FrameRef thumb; i64 thumbPts = -1; bool thumbEos = false;
    if (!thumbnail->next_frame(0, thumb, thumbPts, thumbEos).ok()) return 17;
    const u64 thumbHash = frame_hash(thumb);
    if (!thumbHash || (!gl && thumbHash != hashes.front())) return 17;
    thumbnail.reset();
    if (frame_hash(thumb) != thumbHash) return 18;
    std::printf("PASS frames=%zu decoder=%s sequential_ms=%.3f path=%s seek_suspend_resume=pass\n",
        hashes.size(), decoder->info().decoderName, sequentialMs, gl ? "driver-gl-rgba-fallback" : "cpu-yuv");
    return 0;
}
