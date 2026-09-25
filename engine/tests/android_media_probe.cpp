// Run through adb on a device/emulator. Uses the production decoder, not a mock.
#include "MediaCodecSource.hpp"
#include "aurea/core/Time.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace aurea;

static u64 luma_hash(const FrameRef& frame) {
    if (!frame || !frame->planes[0]) return 0;
    u64 hash = 14695981039346656037ULL;
    for (u32 y = 0; y < frame->visibleHeight; ++y) for (u32 x = 0; x < frame->visibleWidth; ++x) {
        hash ^= frame->planes[0][static_cast<usize>(y + frame->cropTop) * frame->strides[0] + x + frame->cropLeft];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: aurea_media_probe video timestamps-us.txt\n"); return 2; }
    std::ifstream timing(argv[2]);
    std::vector<i64> expected;
    for (i64 pts; timing >> pts;) expected.push_back(pts);
    if (expected.empty()) return 2;
    android::MediaCodecFactory factory;
    factory.set_zero_copy(false);
    Asset asset; asset.kind = AssetKind::Video; asset.sourcePath = argv[1];
    auto decoder = factory.open_video(asset, MediaPriority::Preview);
    if (!decoder || !decoder->seek_to_keyframe(0).ok()) return 3;
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
        const u64 hash = luma_hash(frame);
        if (!hash) return 7;
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
                found = std::llabs(pts - expected[index]) <= 1 && frame->covers(target) && luma_hash(frame) == hashes[index];
                break;
            }
            if (ended) break;
        }
        if (!found) { std::fprintf(stderr, "seek mismatch at %zu\n", index); return 11; }
    }
    decoder->suspend();
    if (luma_hash(retained) != hashes.front()) return 12;
    if (!decoder->resume().ok() || !decoder->seek_to_keyframe(0).ok()) return 13;
    FrameRef first; i64 pts = -1;
    const Status resumed = decoder->next_frame(0, first, pts, eos);
    if (!resumed.ok() || pts != expected.front() || luma_hash(first) != hashes.front()) {
        std::fprintf(stderr, "resume: status=%d pts=%lld frame=%d hash=%llu expected=%llu detail=%.*s\n",
            resumed.raw(), static_cast<long long>(pts), first ? 1 : 0,
            static_cast<unsigned long long>(luma_hash(first)), static_cast<unsigned long long>(hashes.front()),
            static_cast<int>(resumed.detail().size()), resumed.detail().empty() ? "" : resumed.detail().data());
        return 14;
    }
    std::printf("PASS frames=%zu decoder=%s sequential_ms=%.3f seek_suspend_resume=pass\n",
        hashes.size(), decoder->info().decoderName, sequentialMs);
    return 0;
}
