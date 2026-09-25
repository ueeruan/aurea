#include "TestFramework.hpp"
#include "aurea/media/PreviewProxy.hpp"
#include "aurea/media/MediaManager.hpp"
#include <cmath>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <algorithm>

using namespace aurea;

AUREA_TEST(PreviewProxy, AreaResamplingPreservesCropRotationAndChromaOrder) {
    DecodedFrame frame;
    const u8 y[] = {0, 0, 0, 0, 0, 0,
                    0, 10, 10, 50, 50, 0,
                    0, 10, 10, 50, 50, 0,
                    0, 90, 90, 130, 130, 0,
                    0, 90, 90, 130, 130, 0,
                    0, 0, 0, 0, 0, 0};
    const u8 uv[] = {200, 30, 200, 30, 200, 30, 200, 30, 200, 30, 200, 30, 200, 30, 200, 30, 200, 30};
    frame.width = frame.height = 6; frame.cropLeft = frame.cropTop = 1;
    frame.visibleWidth = frame.visibleHeight = 4; frame.rotation = 90;
    frame.format = PixelFormat::NV21; frame.planeCount = 2;
    frame.planes[0] = y; frame.planes[1] = uv; frame.strides[0] = frame.strides[1] = 6;
    std::vector<u8> result;
    AUREA_CHECK(proxy_frame_nv12(frame, 2, 2, result));
    AUREA_CHECK(result == std::vector<u8>({90, 10, 130, 50, 30, 200}));
    frame.cropLeft = 3;
    AUREA_CHECK(!proxy_frame_nv12(frame, 2, 2, result));
}

AUREA_TEST(PreviewProxy, P010KeepsEncodedHdrValuesAndMetadata) {
    DecodedFrame frame;
    const u16 y[] = {64 << 6, 64 << 6, 940 << 6, 940 << 6};
    const u16 uv[] = {512 << 6, 512 << 6};
    frame.width = frame.height = 2; frame.format = PixelFormat::P010; frame.planeCount = 2;
    frame.planes[0] = reinterpret_cast<const u8*>(y); frame.planes[1] = reinterpret_cast<const u8*>(uv);
    frame.strides[0] = frame.strides[1] = 4;
    frame.color.transfer = TransferFunction::PQ; frame.color.bitDepth = 10;
    std::vector<u8> result;
    AUREA_CHECK(proxy_frame_nv12(frame, 2, 2, result));
    AUREA_CHECK(result == std::vector<u8>({16, 16, 235, 235, 128, 128}));
    frame.color.fullRange = true;
    AUREA_CHECK(proxy_frame_nv12(frame, 2, 2, result));
    AUREA_CHECK(result == std::vector<u8>({16, 16, 234, 234, 128, 128}));
    AUREA_CHECK(frame.color.transfer == TransferFunction::PQ && frame.color.bitDepth == 10);
}

#ifdef __ANDROID__
#include "MediaCodecSource.hpp"
#include "MediaCodecExport.hpp"
#include <fcntl.h>

AUREA_TEST(PreviewProxy, RealCodecVfrRoundTripAndExportUsesOriginal) {
    const char* path = std::getenv("AUREA_PROXY_MEDIA");
    if (!path) { std::printf("(set AUREA_PROXY_MEDIA to run real MediaCodec proxy validation) "); return; }
    android::MediaCodecFactory factory; factory.set_zero_copy(false);
    factory.set_fd_opener([](const char*, void* context) { return ::open(static_cast<const char*>(context), O_RDONLY | O_CLOEXEC); }, const_cast<char*>(path));
    MediaProbe probe;
    AUREA_CHECK(factory.probe(path, probe));
    Asset source; source.kind = AssetKind::Video; source.sourcePath = "content://aurea-proxy-regression/source";
    AUREA_CHECK(!factory.cache_identity(source.sourcePath.c_str()).empty());
    source.video.width = probe.video.display_width(); source.video.height = probe.video.display_height();
    MediaManager manager; manager.set_factory(&factory);
    manager.proxies().configure(&factory, android::make_mediacodec_export_sink, nullptr, "/data/local/tmp/aurea-proxy-regression");
    const u32 target = std::max(2u, std::min(source.video.width, source.video.height) / 2 / 2 * 2);
    manager.proxies().set_policy(target);
    std::shared_ptr<const PreviewProxy> proxy;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (!(proxy = manager.proxies().request(source)) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    AUREA_CHECK(proxy != nullptr);
    if (!proxy) return;
    std::printf(" proxy=%s frames=%zu size=%ux%u ", proxy->path.c_str(), proxy->times.size(), proxy->width, proxy->height);
    Asset encoded = source; encoded.sourcePath = proxy->path;
    auto original = factory.open_video(source, MediaPriority::Thumbnail);
    auto decoded = proxy_decoder(factory.open_video(encoded, MediaPriority::Thumbnail), proxy);
    AUREA_CHECK(original && decoded);
    if (!original || !decoded) return;
    AUREA_CHECK(original->seek_to_keyframe(0).ok() && decoded->seek_to_keyframe(0).ok());
    usize count = 0;
    for (;;) {
        FrameRef a, b; i64 ap = 0, bp = 0; bool ae = false, be = false;
        for (int attempt = 0; attempt < 500 && !a && !ae; ++attempt) AUREA_CHECK(original->next_frame(-1, a, ap, ae).ok());
        for (int attempt = 0; attempt < 500 && !b && !be; ++attempt) AUREA_CHECK(decoded->next_frame(-1, b, bp, be).ok());
        if (!a || !b) { AUREA_CHECK(!a && !b); break; }
        AUREA_CHECK_EQ(ap, bp);
        AUREA_CHECK_EQ(b->rotation, 0u);
        AUREA_CHECK(b->color.matrix == a->color.matrix && b->color.transfer == a->color.transfer && b->color.fullRange == a->color.fullRange);
        std::vector<u8> expected, actual;
        AUREA_CHECK(proxy_frame_nv12(*a.get(), proxy->width, proxy->height, expected));
        AUREA_CHECK(proxy_frame_nv12(*b.get(), proxy->width, proxy->height, actual));
        if (expected.size() == actual.size() && !actual.empty()) {
            f64 error = 0; for (usize i = 0; i < actual.size(); ++i) error += std::abs(static_cast<int>(actual[i]) - expected[i]);
            AUREA_CHECK(error / actual.size() < 8.0);
        }
        ++count;
    }
    AUREA_CHECK_EQ(count, proxy->times.size());
    original.reset(); decoded.reset();
    auto awaitSource = [&](bool finalQuality) {
        VideoSource* result = nullptr;
        for (int i = 0; i < 500 && !result; ++i) {
            result = manager.source_for(LayerId{0, 1}, AssetId{0, 1}, source, 1, finalQuality);
            if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return result;
    };
    auto* preview = awaitSource(false);
    AUREA_CHECK(preview && preview->info().display_width() == proxy->width);
    auto* final = awaitSource(true);
    AUREA_CHECK(final && final->info().display_width() == source.video.width);
    manager.close_all(); manager.proxies().stop();
}
#endif
