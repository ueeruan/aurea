#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "aurea/Engine.hpp"
#include "aurea/media/PreviewProxy.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/platform/DevicePolicy.hpp"
#include <cmath>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>

using namespace aurea;

namespace {
struct ProxyTestSink final : ExportSink {
    std::ofstream file;
    std::promise<void>* opened = nullptr;
    Status open(const char* path, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override {
        file.open(path, std::ios::binary);
        if (opened) opened->set_value();
        return file ? OkStatus : Status{Errc::IoError};
    }
    Status write_video(const u8* y, u32, const u8*, u32, i64) noexcept override {
        file.put(static_cast<char>(*y)); return OkStatus;
    }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override { file.close(); return OkStatus; }
    void abort() noexcept override { file.close(); }
};
std::unique_ptr<ExportSink> proxy_test_sink(void* context) {
    auto sink = std::make_unique<ProxyTestSink>();
    sink->opened = static_cast<std::promise<void>*>(context);
    return sink;
}

/// Pasta própria em temp (independe do diretório de onde o teste roda).
std::filesystem::path proxy_dir(const char* tag) {
    return std::filesystem::temp_directory_path() /
        (std::string("aurea-proxy-") + tag + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

/// Fonte sintética minúscula tratada como pesada (o limiar real é 1080p).
ProxyThresholds tiny_thresholds() { ProxyThresholds t; t.heavyPixels = 0; return t; }

template <class Predicate>
bool wait_for_ms(Predicate predicate, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return predicate();
}

Asset video_asset(const char* path, u32 w = 64, u32 h = 36, f64 fps = 30.0) {
    Asset a; a.kind = AssetKind::Video; a.sourcePath = path; a.name = path;
    a.video.width = w; a.video.height = h; a.video.fps = fps;
    a.video.frameCount = FrameIndex{static_cast<i64>(std::llround(fps * 10))};
    return a;
}

/// Sintética com contadores: Thumbnail = geração do proxy; o resto = preview.
/// `.mp4` (o proxy no cache) sonda com o tamanho do proxy.
struct ProxyFactory final : VideoSourceFactory {
    test::SyntheticConfig config;
    std::string identity;
    u32 proxyW = 32, proxyH = 18;
    std::atomic<u32> generated{0}, previews{0};
    std::atomic<bool> failGenerate{false};
    ProxyFactory() { config.width = 64; config.height = 36; config.frameCount = 6; }
    bool probe(const char* path, MediaProbe& out) override {
        test::SyntheticConfig c = config;
        const std::string p = path ? path : "";
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".mp4") == 0) { c.width = proxyW; c.height = proxyH; }
        test::SyntheticFactory f(c);
        return f.probe(path, out);
    }
    std::string cache_identity(const char*) override { return identity; }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
        if (priority == MediaPriority::Thumbnail) {
            ++generated;
            if (failGenerate.load()) return nullptr;
        } else {
            ++previews;
        }
        auto c = config;
        c.width = asset.video.width; c.height = asset.video.height;
        return std::make_unique<test::SyntheticDecoder>(c);
    }
};

// A área em f64 de antes (referência): cada amostra recalcula a faixa da fonte.
void reference_rect(const DecodedFrame& f, f64& x0, f64& y0, f64& x1, f64& y1) {
    const f64 a = x0, b = y0, c = x1, d = y1;
    if (f.rotation == 90) { x0 = b; x1 = d; y0 = 1 - c; y1 = 1 - a; }
    else if (f.rotation == 180) { x0 = 1 - c; x1 = 1 - a; y0 = 1 - d; y1 = 1 - b; }
    else if (f.rotation == 270) { x0 = 1 - d; x1 = 1 - b; y0 = a; y1 = c; }
    const u32 w = f.visibleWidth ? f.visibleWidth : f.width, h = f.visibleHeight ? f.visibleHeight : f.height;
    x0 = f.cropLeft + x0 * w; x1 = f.cropLeft + x1 * w;
    y0 = f.cropTop + y0 * h; y1 = f.cropTop + y1 * h;
}
u8 reference_area(const DecodedFrame& f, u32 channel, f64 x0, f64 y0, f64 x1, f64 y1) {
    const bool ten = f.format == PixelFormat::P010, planar = f.format == PixelFormat::YUV420P;
    const u32 plane = channel == 0 ? 0 : planar ? channel : 1;
    const u32 component = channel == 0 || planar ? 0 : (f.format == PixelFormat::NV21 ? 2 - channel : channel - 1);
    const u32 step = (channel == 0 || planar ? 1 : 2) * (ten ? 2 : 1);
    if (channel) { x0 *= .5; x1 *= .5; y0 *= .5; y1 *= .5; }
    f64 sum = 0, weight = 0;
    for (u32 y = static_cast<u32>(y0); y < static_cast<u32>(std::ceil(y1)); ++y) {
        const f64 wy = std::min(y1, static_cast<f64>(y + 1)) - std::max(y0, static_cast<f64>(y));
        for (u32 x = static_cast<u32>(x0); x < static_cast<u32>(std::ceil(x1)); ++x) {
            const f64 w = wy * (std::min(x1, static_cast<f64>(x + 1)) - std::max(x0, static_cast<f64>(x)));
            const u8* p = f.planes[plane] + static_cast<usize>(y) * f.strides[plane] + static_cast<usize>(x) * step + component * (ten ? 2 : 1);
            const f64 value = ten ? static_cast<f64>((p[0] | (p[1] << 8)) >> 6) * (f.color.fullRange ? 255.0 / 1023.0 : .25) : p[0];
            sum += value * w; weight += w;
        }
    }
    return static_cast<u8>(std::clamp(std::lround(sum / std::max(weight, 1e-20)), 0l, 255l));
}
std::vector<u8> reference_nv12(const DecodedFrame& f, u32 width, u32 height) {
    std::vector<u8> pixels(static_cast<usize>(width) * height * 3 / 2);
    for (u32 channel = 0; channel < 3; ++channel) {
        const u32 w = channel ? width / 2 : width, h = channel ? height / 2 : height;
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
            f64 x0 = static_cast<f64>(x) / w, x1 = static_cast<f64>(x + 1) / w;
            f64 y0 = static_cast<f64>(y) / h, y1 = static_cast<f64>(y + 1) / h;
            reference_rect(f, x0, y0, x1, y1);
            const usize index = channel ? static_cast<usize>(width) * height + (static_cast<usize>(y) * w + x) * 2 + channel - 1
                                        : static_cast<usize>(y) * w + x;
            pixels[index] = reference_area(f, channel, x0, y0, x1, y1);
        }
    }
    return pixels;
}
} // namespace

AUREA_TEST(PreviewProxy, ExpensivePreparationYieldsAndCancellationInterruptsPacing) {
    test::SyntheticConfig config;
    config.width = 64; config.height = 36; config.frameCount = 6; config.decodeCostUs = 40'000;
    test::SyntheticFactory factory(config);
    const auto dir = proxy_dir("duty");
    Asset asset = video_asset("synthetic");
    PreviewProxyService service;
    service.set_thresholds(tiny_thresholds());
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    const auto start = std::chrono::steady_clock::now();
    std::shared_ptr<const PreviewProxy> proxy;
    while (!(proxy = service.request(asset)) && std::chrono::steady_clock::now() - start < std::chrono::seconds(8))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!proxy) std::printf(" proxy decoder opens=%u ", factory.opened.load());
    AUREA_CHECK(proxy != nullptr);
    // Six costly frames may not run back-to-back and monopolize the worker.
    AUREA_CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(800));
    if (proxy) {
        AUREA_CHECK_EQ(proxy->times.size(), 6u);
        for (usize i = 1; i < proxy->times.size(); ++i) AUREA_CHECK(proxy->times[i] > proxy->times[i-1]);
    }
    service.stop();
    std::promise<void> opened;
    auto ready = opened.get_future();
    service.configure(&factory, proxy_test_sink, &opened, dir.generic_string());
    service.set_policy(16, 10'000);
    (void)service.request(asset);
    AUREA_CHECK(ready.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto cancel = std::chrono::steady_clock::now();
    service.stop();
    std::printf(" cancel=%.1fms ", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cancel).count());
    AUREA_CHECK(std::chrono::steady_clock::now() - cancel < std::chrono::seconds(1));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, OriginalSourceLeasesOverlapMoveAndPreserveOtherPauses) {
    struct Factory final : VideoSourceFactory {
        test::SyntheticConfig config;
        std::atomic<u32> originals{0}, proxies{0}, generated{0};
        Factory() { config.width = 64; config.height = 36; config.frameCount = 6; }
        bool probe(const char* path, MediaProbe& out) override { test::SyntheticFactory f(config); return f.probe(path, out); }
        std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
            if (priority == MediaPriority::Thumbnail) ++generated;
            // A conferência do proxy pronto (bugs-034) relê o arquivo por software:
            // não é a prévia usando o proxy.
            else if (priority == MediaPriority::ExportSoftwareDecode) {}
            else if (asset.sourcePath == "lease-original") ++originals;
            else ++proxies;
            auto c = config;
            c.width = asset.video.width; c.height = asset.video.height;
            return std::make_unique<test::SyntheticDecoder>(c);
        }
    } factory;
    MediaManager manager; manager.set_factory(&factory);
    const auto directory = proxy_dir("lease");
    manager.proxies().set_thresholds(tiny_thresholds());
    manager.proxies().configure(&factory, proxy_test_sink, nullptr, directory.generic_string());
    manager.proxies().set_policy(18);
    Asset asset = video_asset("lease-original");
    auto wait = [&](auto predicate) { return wait_for_ms(predicate, 3000); };
    AUREA_CHECK(wait([&] { return !!manager.proxies().request(asset); }));
    auto source = [&] { return manager.source_for(LayerId{0, 1}, AssetId{0, 1}, asset, 1, false); };
    AUREA_CHECK(wait([&] { return source() != nullptr; }));
    AUREA_CHECK_EQ(factory.proxies.load(), 1u);
    auto first = manager.retain_original_sources();
    auto moved = std::move(first);
    // A second capture lives on another thread. Cancelling/releasing the first
    // must not resume proxy selection or generation until this one also exits.
    std::promise<void> retained, release;
    auto releaseFuture = release.get_future();
    auto second = std::async(std::launch::async, [&] {
        auto lease = manager.retain_original_sources();
        retained.set_value(); releaseFuture.wait();
    });
    retained.get_future().wait();
    VideoSource* original = nullptr;
    AUREA_CHECK(wait([&] { original = source(); return original != nullptr; }));
    AUREA_CHECK_EQ(factory.originals.load(), 1u);
    if (original) AUREA_CHECK_EQ(original->info().display_width(), 64u);
    moved = {};
    for (u32 i = 0; i < 20; ++i) AUREA_CHECK(source() == original);
    AUREA_CHECK_EQ(factory.proxies.load(), 1u);
    Asset another = asset; another.sourcePath = "lease-other-original";
    const u32 generated = factory.generated.load();
    (void)manager.proxies().request(another);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    AUREA_CHECK_EQ(factory.generated.load(), generated);
    manager.proxies().set_pause_reason(PreviewProxyService::Manual, true);
    release.set_value(); second.get();
    AUREA_CHECK(wait([&] { return source() != nullptr; }));
    AUREA_CHECK_EQ(factory.proxies.load(), 2u); // ready proxy resumes after the last lease
    // Manual remains active after Capture is cleared; no competing encoder.
    (void)manager.proxies().request(another);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    AUREA_CHECK_EQ(factory.generated.load(), generated);
    manager.proxies().set_pause_reason(PreviewProxyService::Manual, false);
    AUREA_CHECK(wait([&] { return !!manager.proxies().request(another); }));
    AUREA_CHECK_EQ(factory.generated.load(), generated + 1);
    manager.close_all(); manager.proxies().stop();
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
}

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

AUREA_TEST(PreviewProxy, FastResamplerMatchesAreaReferenceAndShrinks4KCheaply) {
    // Tabelas inteiras por eixo × a área em f64 de antes: no máximo 1 nível de
    // diferença (pesos em 1/256), em todo formato, recorte e rotação.
    std::mt19937 random(2025);
    std::vector<u8> y(66 * 41 * 2), uv(66 * 41 * 2), v(66 * 41 * 2);
    for (auto* plane : {&y, &uv, &v}) for (u8& b : *plane) b = static_cast<u8>(random());
    u32 worst = 0, compared = 0;
    for (PixelFormat format : {PixelFormat::NV12, PixelFormat::NV21, PixelFormat::YUV420P, PixelFormat::P010}) {
        for (u32 rotation : {0u, 90u, 180u, 270u}) {
            for (bool full : {false, true}) {
                DecodedFrame f;
                const bool ten = format == PixelFormat::P010, planar = format == PixelFormat::YUV420P;
                f.width = 64; f.height = 40; f.cropLeft = 2; f.cropTop = 1; f.visibleWidth = 60; f.visibleHeight = 36;
                f.rotation = rotation; f.format = format; f.color.fullRange = full;
                f.planeCount = planar ? 3 : 2;
                f.planes[0] = y.data(); f.planes[1] = uv.data(); f.planes[2] = planar ? v.data() : nullptr;
                f.strides[0] = 66 * (ten ? 2 : 1);
                f.strides[1] = planar ? 33 : 66 * (ten ? 2 : 1);
                f.strides[2] = planar ? 33 : 0;
                const bool rotated = rotation == 90 || rotation == 270;
                const u32 w = rotated ? 22 : 38, h = rotated ? 38 : 22;
                std::vector<u8> fast;
                AUREA_CHECK(proxy_frame_nv12(f, w, h, fast));
                const auto expected = reference_nv12(f, w, h);
                AUREA_CHECK_EQ(fast.size(), expected.size());
                for (usize i = 0; i < std::min(fast.size(), expected.size()); ++i) {
                    worst = std::max<u32>(worst, static_cast<u32>(std::abs(static_cast<int>(fast[i]) - expected[i])));
                    ++compared;
                }
            }
        }
    }
    std::printf(" amostras=%u pior=%u ", compared, worst);
    AUREA_CHECK(worst <= 1u);
    // 4K NV12 → 720p: o custo que trava a geração no aparelho (informativo no host).
    DecodedFrame big;
    std::vector<u8> by(3840ull * 2160), buv(3840ull * 1080);
    for (usize i = 0; i < by.size(); ++i) by[i] = static_cast<u8>(i * 7);
    for (usize i = 0; i < buv.size(); ++i) buv[i] = static_cast<u8>(i * 3);
    big.width = 3840; big.height = 2160; big.format = PixelFormat::NV12; big.planeCount = 2;
    big.planes[0] = by.data(); big.planes[1] = buv.data(); big.strides[0] = big.strides[1] = 3840;
    std::vector<u8> out;
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(proxy_frame_nv12(big, 1280, 720, out));
    const f64 fastMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const auto t1 = std::chrono::steady_clock::now();
    const auto reference = reference_nv12(big, 1280, 720);
    const f64 referenceMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::printf(" 4K->720p rapido=%.1fms referencia=%.1fms ", fastMs, referenceMs);
    AUREA_CHECK(out.size() == reference.size());
}

AUREA_TEST(PreviewProxy, PolicyFollowsDecodeCostOnEveryDeviceTier) {
    constexpr u32 avc = 0x61766331u, hevc = 0x68766331u;
    auto clip = [](u32 w, u32 h, f64 fps = 30.0, u8 bits = 8, u32 tag = 0x61766331u, u64 mbps = 0) {
        Asset a = video_asset("policy", w, h, fps);
        a.profile.bitDepth = bits; a.profile.codecTag = tag;
        // 10 s de clipe: bytes = Mbps × 10 / 8.
        a.fileSizeBytes = mbps * 1'000'000ull * 10 / 8;
        return a;
    };
    // O caso do relato: 4K no Snapdragon 8s Gen 4 (HIGH/ULTRA) nunca tinha proxy.
    for (DeviceTier tier : {DeviceTier::Low, DeviceTier::Mid, DeviceTier::High, DeviceTier::Ultra}) {
        const DevicePolicy p = device_policy(tier, ThermalTier::Normal);
        ProxyThresholds t; t.heavyPixels = p.proxyHeavyPixels;
        AUREA_CHECK(proxy_wanted(clip(3840, 2160), ProxyMode::Automatic, false, t));
        AUREA_CHECK(proxy_wanted(clip(2160, 3840), ProxyMode::Automatic, false, t));
        AUREA_CHECK(proxy_wanted(clip(2560, 1440), ProxyMode::Automatic, false, t));
        AUREA_CHECK_EQ(p.proxyShortSide, tier == DeviceTier::Low ? 540u : 720u);
        // 1080p H.264 comum: só no LOW o Automático já otimiza.
        AUREA_CHECK_EQ(proxy_wanted(clip(1920, 1080), ProxyMode::Automatic, false, t), tier == DeviceTier::Low);
        AUREA_CHECK(!proxy_wanted(clip(1280, 720), ProxyMode::Automatic, false, t));
    }
    const ProxyThresholds t;
    // Taxa: acima de 60 é pesado (59,94 e 60 não), e o proxy desce para 60.
    AUREA_CHECK(proxy_source_heavy(clip(1280, 720, 120.0), t));
    AUREA_CHECK(proxy_source_heavy(clip(1920, 1080, 90.0), t));
    AUREA_CHECK(!proxy_source_heavy(clip(1920, 1080, 60.0), t));
    AUREA_CHECK(!proxy_source_heavy(clip(1920, 1080, 59.94), t));
    AUREA_CHECK_NEAR(proxy_target_fps(120.0), 60.0, 1e-9);
    AUREA_CHECK_NEAR(proxy_target_fps(59.94), 59.94, 1e-9);
    AUREA_CHECK_NEAR(proxy_target_fps(24.0), 24.0, 1e-9);
    // HEVC / 10 bits / HDR a partir de 1080p; abaixo disso não.
    AUREA_CHECK(proxy_source_heavy(clip(1920, 1080, 30.0, 8, hevc), t));
    AUREA_CHECK(proxy_source_heavy(clip(1080, 1920, 30.0, 10, avc), t));
    Asset hdr = clip(1920, 1080); hdr.profile.hdr = true;
    AUREA_CHECK(proxy_source_heavy(hdr, t));
    AUREA_CHECK(!proxy_source_heavy(clip(1280, 720, 30.0, 10, hevc), t));
    // Bitrate muito alto (tamanho ÷ duração); clipe curto não conta.
    AUREA_CHECK(proxy_source_heavy(clip(1920, 1080, 30.0, 8, avc, 60), t));
    AUREA_CHECK(!proxy_source_heavy(clip(1920, 1080, 30.0, 8, avc, 20), t));
    AUREA_CHECK_EQ(proxy_source_bitrate(clip(1920, 1080, 30.0, 8, avc, 60)), 60'000'000ull);
    Asset shortClip = clip(1920, 1080, 30.0, 8, avc, 60); shortClip.video.frameCount = FrameIndex{10};
    AUREA_CHECK_EQ(proxy_source_bitrate(shortClip), 0ull);
    // Modos: Sempre e prévia reduzida pegam tudo acima de 720p; Desligado, nada.
    AUREA_CHECK(proxy_wanted(clip(1920, 1080), ProxyMode::Always, false, t));
    AUREA_CHECK(proxy_wanted(clip(1920, 1080), ProxyMode::Automatic, true, t));
    AUREA_CHECK(!proxy_wanted(clip(1280, 720), ProxyMode::Always, true, t));
    AUREA_CHECK(!proxy_wanted(clip(3840, 2160), ProxyMode::Off, true, t));
    AUREA_CHECK(!proxy_wanted(clip(1920, 1080), ProxyMode::Off, true, t));
    Asset image = clip(3840, 2160); image.kind = AssetKind::Image;
    AUREA_CHECK(!proxy_wanted(image, ProxyMode::Always, true, t));
    // Codec do mime da sondagem (Android "video/hevc", iOS idem).
    AUREA_CHECK_EQ(proxy_codec_tag("video/hevc"), hevc);
    AUREA_CHECK_EQ(proxy_codec_tag("video/avc"), avc);
    AUREA_CHECK(proxy_complex_codec(proxy_codec_tag("video/av01")));
    AUREA_CHECK(proxy_complex_codec(proxy_codec_tag("video/x-vnd.on2.vp9")));
    AUREA_CHECK_EQ(proxy_codec_tag(""), 0u);
    // Encoder: H.264, keyframe a cada ~0,5 s, bitrate moderado.
    const VideoStreamConfig c30 = proxy_stream_config(1280, 720, 30.0);
    AUREA_CHECK(c30.codec == ExportCodec::H264);
    AUREA_CHECK_EQ(c30.keyframeIntervalFrames, 15u);
    AUREA_CHECK(c30.bitrateBps >= 1'500'000u && c30.bitrateBps <= 4'000'000u);
    const VideoStreamConfig c60 = proxy_stream_config(1280, 720, 60.0);
    AUREA_CHECK_EQ(c60.keyframeIntervalFrames, 30u);
    AUREA_CHECK(c60.bitrateBps > c30.bitrateBps && c60.bitrateBps <= 8'000'000u);
    AUREA_CHECK_EQ(proxy_stream_config(960, 540, 24.0).keyframeIntervalFrames, 12u);
}

AUREA_TEST(PreviewProxy, DecimatorKeepsSixtyFramesPerSecond) {
    auto kept = [](f64 fps, u32 frames) {
        ProxyFrameDecimator d(fps, proxy_target_fps(fps));
        u32 n = 0;
        for (u32 i = 0; i < frames; ++i) n += d.keep(static_cast<i64>(std::llround(i * 1e6 / fps))) ? 1 : 0;
        return n;
    };
    AUREA_CHECK_EQ(kept(120.0, 120), 60u);
    AUREA_CHECK_EQ(kept(240.0, 240), 60u);
    const u32 ninety = kept(90.0, 90);
    AUREA_CHECK(ninety >= 59u && ninety <= 61u);
    AUREA_CHECK_EQ(kept(60.0, 60), 60u);
    AUREA_CHECK_EQ(kept(59.94, 60), 60u);
    AUREA_CHECK_EQ(kept(30.0, 30), 30u);
    // Carimbo com jitter (VFR do celular): nunca dois guardados seguidos a 120.
    ProxyFrameDecimator d(120.0, 60.0);
    std::vector<bool> keep;
    for (u32 i = 0; i < 24; ++i) keep.push_back(d.keep(static_cast<i64>(i * 8333 + ((i % 3) ? 700 : -600))));
    for (usize i = 1; i < keep.size(); ++i) AUREA_CHECK(!(keep[i] && keep[i - 1]));
}

AUREA_TEST(PreviewProxy, HighFrameRateProxyIsDecimatedWithoutLosingTime) {
    ProxyFactory factory;
    factory.config.fps = 120.0; factory.config.frameCount = 24;
    const auto dir = proxy_dir("decimate");
    PreviewProxyService service;
    // Limiar padrão: a fonte minúscula só é "pesada" pelos 120 fps.
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    const Asset asset = video_asset("decimate", 64, 36, 120.0);
    std::shared_ptr<const PreviewProxy> proxy;
    AUREA_CHECK(wait_for_ms([&] { return !!(proxy = service.request(asset)); }, 5000));
    if (proxy) {
        AUREA_CHECK_NEAR(proxy->fps, 60.0, 1e-9);
        AUREA_CHECK_EQ(proxy->times.size(), 12u);
        for (usize i = 0; i < proxy->durations.size(); ++i) AUREA_CHECK_NEAR(proxy->durations[i], 16'667.0, 1.0);
        // O último cobre até o fim da fonte (24 quadros a 120 = 200 ms).
        AUREA_CHECK_NEAR(proxy->times.back() + proxy->durations.back(), 200'000.0, 1.0);
        test::SyntheticConfig c = factory.config; c.width = proxy->width; c.height = proxy->height; c.fps = 60.0;
        auto decoder = proxy_decoder(std::make_unique<test::SyntheticDecoder>(c), proxy);
        AUREA_CHECK(decoder && std::fabs(decoder->info().fps - 60.0) < 1e-9);
    }
    service.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, StatesProgressCancelRegenerateFailureAndModeOff) {
    ProxyFactory factory;
    factory.config.frameCount = 12; factory.config.decodeCostUs = 15'000;
    const auto dir = proxy_dir("states");
    PreviewProxyService service;
    service.set_thresholds(tiny_thresholds());
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    const Asset a = video_asset("states-a");
    AUREA_CHECK(service.status(a).state == ProxyState::None);
    AUREA_CHECK(!service.request(a));
    AUREA_CHECK(service.status(a).state == ProxyState::Queued || service.status(a).state == ProxyState::Generating);
    AUREA_CHECK(wait_for_ms([&] { const auto s = service.status(a); return s.state == ProxyState::Generating && s.progress > 0.0f; }, 3000));
    const ProxySummary busy = service.summary();
    AUREA_CHECK_EQ(busy.generating, 1u);
    AUREA_CHECK(busy.name == "states-a");
    AUREA_CHECK(busy.progress > 0.0f && busy.progress < 1.0f);
    // Cancelar: sai da fila e não volta sozinho com o preview pedindo de novo.
    AUREA_CHECK(service.cancel(a));
    AUREA_CHECK(service.status(a).state == ProxyState::None);
    AUREA_CHECK(wait_for_ms([&] { return service.summary().generating == 0; }, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    AUREA_CHECK(!service.request(a));
    AUREA_CHECK(service.status(a).state == ProxyState::None);
    const u32 afterCancel = factory.generated.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    AUREA_CHECK_EQ(factory.generated.load(), afterCancel);
    // Gerar de novo: à frente da fila, até ficar pronto.
    AUREA_CHECK(service.regenerate(a));
    std::shared_ptr<const PreviewProxy> proxy;
    AUREA_CHECK(wait_for_ms([&] { return !!(proxy = service.request(a)); }, 6000));
    AUREA_CHECK(service.status(a).state == ProxyState::Ready);
    AUREA_CHECK_EQ(factory.generated.load(), afterCancel + 1);
    AUREA_CHECK_EQ(service.summary().ready, 1u);
    // Desligado: nada usa nem gera. Voltar reaproveita o pronto.
    service.set_mode(ProxyMode::Off);
    AUREA_CHECK(!service.request(a));
    AUREA_CHECK(service.status(a).state == ProxyState::None);
    AUREA_CHECK(!service.regenerate(a));
    service.set_mode(ProxyMode::Automatic);
    AUREA_CHECK(service.request(a) == proxy);
    // Falha: fica Failed, sem nova tentativa imediata a cada quadro.
    factory.failGenerate = true;
    const Asset b = video_asset("states-b");
    (void)service.request(b);
    AUREA_CHECK(wait_for_ms([&] { return service.status(b).state == ProxyState::Failed; }, 3000));
    const u32 failedOpens = factory.generated.load();
    for (int i = 0; i < 10; ++i) (void)service.request(b);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    AUREA_CHECK_EQ(factory.generated.load(), failedOpens);
    AUREA_CHECK_EQ(service.summary().failed, 1u);
    // Trocar o modo interrompe o que deixou de valer.
    factory.failGenerate = false;
    service.set_thresholds(ProxyThresholds{});
    const Asset hd = video_asset("states-hd", 1920, 1080);
    factory.config.decodeCostUs = 30'000;
    service.set_mode(ProxyMode::Always);
    (void)service.request(hd);
    AUREA_CHECK(wait_for_ms([&] { return service.status(hd).state == ProxyState::Generating; }, 3000));
    service.set_mode(ProxyMode::Automatic);
    AUREA_CHECK(service.status(hd).state == ProxyState::None);
    AUREA_CHECK(wait_for_ms([&] { return service.summary().generating == 0; }, 2000));
    service.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, PlaybackHoldsProgressAndHardPauseRequeuesAtFront) {
    ProxyFactory factory;
    factory.config.frameCount = 30; factory.config.decodeCostUs = 10'000;
    const auto dir = proxy_dir("hold");
    PreviewProxyService service;
    service.set_thresholds(tiny_thresholds());
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    const Asset a = video_asset("hold-a");
    (void)service.request(a);
    AUREA_CHECK(wait_for_ms([&] { const auto s = service.status(a); return s.state == ProxyState::Generating && s.progress > 0.1f; }, 4000));
    // Play: o trabalho para sem perder o que já fez (nada de reabrir o decoder).
    service.set_pause_reason(PreviewProxyService::Playback, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const f32 held = service.status(a).progress;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    AUREA_CHECK(service.status(a).state == ProxyState::Generating);
    AUREA_CHECK_NEAR(service.status(a).progress, held, 1e-6);
    AUREA_CHECK_EQ(factory.generated.load(), 1u);
    service.set_pause_reason(PreviewProxyService::Playback, false);
    AUREA_CHECK(wait_for_ms([&] { return !!service.request(a); }, 6000));
    AUREA_CHECK_EQ(factory.generated.load(), 1u);
    // Segundo plano/export: o arquivo em curso para, volta à frente da fila e
    // recomeça quando a pausa sai. A fila sobrevive.
    const Asset b = video_asset("hold-b"), c = video_asset("hold-c");
    (void)service.request(b);
    AUREA_CHECK(wait_for_ms([&] { const auto s = service.status(b); return s.state == ProxyState::Generating && s.progress > 0.1f; }, 4000));
    (void)service.request(c);
    service.set_pause_reason(PreviewProxyService::Background, true);
    AUREA_CHECK(wait_for_ms([&] { return service.status(b).state == ProxyState::Queued; }, 2000));
    AUREA_CHECK(service.status(c).state == ProxyState::Queued);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    AUREA_CHECK_EQ(factory.generated.load(), 2u);
    service.set_pause_reason(PreviewProxyService::Background, false);
    AUREA_CHECK(wait_for_ms([&] { return service.status(b).state == ProxyState::Generating; }, 2000));
    AUREA_CHECK(service.status(c).state == ProxyState::Queued);
    AUREA_CHECK(wait_for_ms([&] { return !!service.request(b) && !!service.request(c); }, 10000));
    AUREA_CHECK_EQ(factory.generated.load(), 4u);
    service.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, ReadyProxyIsReusedAcrossSessionsAndCacheClears) {
    ProxyFactory factory;
    factory.identity = "arquivo-estavel";
    const auto dir = proxy_dir("reuse");
    const Asset a = video_asset("reuse-a");
    {
        PreviewProxyService service;
        service.set_thresholds(tiny_thresholds());
        service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
        service.set_policy(18);
        AUREA_CHECK(wait_for_ms([&] { return !!service.request(a); }, 4000));
        AUREA_CHECK_EQ(factory.generated.load(), 1u);
        // Troca de projeto: a memória some, o arquivo fica.
        service.clear();
        AUREA_CHECK(wait_for_ms([&] { return !!service.request(a); }, 4000));
        AUREA_CHECK_EQ(factory.generated.load(), 1u);
    }
    PreviewProxyService service;   // outra sessão do app
    service.set_thresholds(tiny_thresholds());
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    AUREA_CHECK(wait_for_ms([&] { return !!service.request(a); }, 4000));
    AUREA_CHECK_EQ(factory.generated.load(), 1u);
    const u64 bytes = service.cache_bytes();
    AUREA_CHECK(bytes > 0);
    AUREA_CHECK_EQ(service.clear_cache(), bytes);
    AUREA_CHECK_EQ(service.cache_bytes(), 0ull);
    AUREA_CHECK(service.status(a).state == ProxyState::None);
    AUREA_CHECK(wait_for_ms([&] { return !!service.request(a); }, 4000));
    AUREA_CHECK_EQ(factory.generated.load(), 2u);
    service.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, ReducedPreviewPrefersProxyOnlyAboveTheAlwaysLimit) {
    ProxyFactory factory;
    const auto dir = proxy_dir("reduced");
    // Em escala: 64×36 faz o papel do 1080p comum (não pesado, acima de "720p").
    ProxyThresholds t; t.heavyPixels = 64 * 36; t.alwaysPixels = 32 * 18;
    PreviewProxyService service;
    service.set_thresholds(t);
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    const Asset a = video_asset("reduced-a");
    AUREA_CHECK(!service.request(a));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    AUREA_CHECK_EQ(factory.generated.load(), 0u);
    AUREA_CHECK(service.status(a).state == ProxyState::None);
    service.set_reduced_preview(true);
    std::shared_ptr<const PreviewProxy> proxy;
    AUREA_CHECK(wait_for_ms([&] { return !!(proxy = service.request(a)); }, 4000));
    AUREA_CHECK_EQ(factory.generated.load(), 1u);
    // Prévia cheia de novo: o original volta; o pronto fica para depois.
    service.set_reduced_preview(false);
    AUREA_CHECK(!service.request(a));
    service.set_reduced_preview(true);
    AUREA_CHECK(service.request(a) == proxy);
    service.set_reduced_preview(false);
    service.set_mode(ProxyMode::Always);
    AUREA_CHECK(service.request(a) == proxy);
    AUREA_CHECK_EQ(factory.generated.load(), 1u);
    service.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, FinalQualityAlwaysOpensTheOriginal) {
    ProxyFactory factory;
    MediaManager manager; manager.set_factory(&factory);
    const auto dir = proxy_dir("final");
    manager.proxies().set_thresholds(tiny_thresholds());
    manager.proxies().configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    manager.proxies().set_policy(18);
    const Asset a = video_asset("final-a");
    AUREA_CHECK(wait_for_ms([&] { return !!manager.proxies().request(a); }, 4000));
    auto open = [&](u32 layer, bool finalQuality) {
        VideoSource* s = nullptr;
        (void)wait_for_ms([&] { return (s = manager.source_for(LayerId{0, layer}, AssetId{0, 1}, a, 1, finalQuality)) != nullptr; }, 3000);
        return s;
    };
    VideoSource* preview = open(1, false);
    AUREA_CHECK(preview && preview->info().display_width() == 32u);
    // Export (qualidade final): o original, com proxy pronto e em qualquer modo.
    VideoSource* final = open(2, true);
    AUREA_CHECK(final && final->info().display_width() == 64u);
    manager.proxies().set_mode(ProxyMode::Always);
    manager.proxies().set_reduced_preview(true);
    final = open(2, true);
    AUREA_CHECK(final && final->info().display_width() == 64u);
    manager.close_all(); manager.proxies().stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

AUREA_TEST(PreviewProxy, EngineStartsProxyRightAfterImportWithoutRendering) {
    ProxyFactory factory;
    factory.config.width = 3840; factory.config.height = 2160; factory.config.frameCount = 4;
    factory.config.pattern = test::SyntheticPattern::FrameGray;
    const auto dir = proxy_dir("engine");
    std::filesystem::create_directories(dir);
    Engine e;
    EngineConfig cfg;
    cfg.workerCount = 2; cfg.memoryBudgetBytes = 256ull << 20; cfg.disableAutosave = true;
    cfg.mediaFactory = &factory; cfg.exportSinkFactory = proxy_test_sink; cfg.cacheDirectory = dir.generic_string();
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "proxy").ok());
    const u32 side = e.caps().policy().proxyShortSide;
    factory.proxyW = side * 16 / 9 / 2 * 2; factory.proxyH = side;
    VideoImport vi; vi.sourcePath = "fonte-4k"; vi.displayName = "4K";
    const auto id = e.import_video(vi);
    AUREA_CHECK(id.ok()); if (!id.ok()) { e.shutdown(); return; }
    // Nenhum render_frame: a importação já pôs o 4K na fila.
    AUREA_CHECK(wait_for_ms([&] { return e.proxy_status(*id).state == ProxyState::Ready; }, 15000));
    AUREA_CHECK_EQ(e.proxy_summary().ready, 1u);
    AUREA_CHECK_EQ(factory.generated.load(), 1u);
    AUREA_CHECK_EQ(e.proxy_mode(), 0u);
    e.set_proxy_mode(2);
    AUREA_CHECK(e.proxy_status(*id).state == ProxyState::None);
    e.set_proxy_mode(0);
    AUREA_CHECK(e.proxy_status(*id).state == ProxyState::Ready);
    AUREA_CHECK(e.proxy_cache_bytes() > 0);
    AUREA_CHECK(e.clear_proxy_cache() > 0);
    AUREA_CHECK_EQ(e.proxy_cache_bytes(), 0ull);
    e.shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// =============================================================================
//  Bugs 0.3.4: "Prévia com qualidade em 1/4 deixa o clipe verde e congelado".
//  A escala 1/2, 1/4 ou 1/8 liga a prévia reduzida, que troca a fonte pelo
//  proxy de edição. Um proxy que decodifica verde (Y=U=V=0) ou parado num
//  quadro era servido assim mesmo (só o 1º quadro era conferido, pelo encoder).
//  Agora o arquivo pronto é decodificado de volta e conferido com a fonte.
// =============================================================================
namespace {
/// O proxy "ruim" do aparelho: quadros NV12 zerados (verde chapado) e, com
/// `frozen`, sempre o conteúdo do quadro 0 (o clipe parado).
class BadProxyDecoder final : public VideoDecoderBackend {
public:
    BadProxyDecoder(const test::SyntheticConfig& c, bool frozen) : cfg_(c), frozen_(frozen), inner_(c) {
        info_ = inner_.info();
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 us) noexcept override { return inner_.seek_to_keyframe(frozen_ ? 0 : us); }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        if (frozen_) {
            // O mesmo quadro 0, com o carimbo que o pedido espera.
            FrameRef first; i64 p0 = 0; bool e0 = false;
            (void)inner_.seek_to_keyframe(0);
            const Status s = inner_.next_frame(-1, first, p0, e0);
            if (!s.ok() || !first) return s;
            first->ptsUs = pts = std::max<i64>(0, from + 1);
            eos = false;
            out = std::move(first);
            return OkStatus;
        }
        const Status s = inner_.next_frame(from, out, pts, eos);
        if (s.ok() && out) {
            auto* f = static_cast<test::SyntheticFrame*>(out.get());
            std::fill(f->y.begin(), f->y.end(), u8{0});
            std::fill(f->uv.begin(), f->uv.end(), u8{0});
        }
        return s;
    }
private:
    test::SyntheticConfig cfg_;
    bool frozen_ = false;
    test::SyntheticDecoder inner_;
    VideoStreamInfo info_;
};

/// Fonte FrameGray (cada quadro tem seu cinza); o proxy decodifica bom, verde ou parado.
struct BadProxyFactory final : VideoSourceFactory {
    enum class Proxy { Good, Green, Frozen };
    test::SyntheticConfig config;
    std::atomic<Proxy> proxy{Proxy::Good};
    std::atomic<u32> verifications{0};
    BadProxyFactory() {
        config.width = 64; config.height = 36; config.frameCount = 12; config.gop = 4;
        config.pattern = test::SyntheticPattern::FrameGray;
    }
    bool probe(const char* path, MediaProbe& out) override {
        test::SyntheticConfig c = config;
        const std::string p = path ? path : "";
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".mp4") == 0) { c.width = 32; c.height = 18; }
        test::SyntheticFactory f(c);
        return f.probe(path, out);
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
        auto c = config;
        c.width = asset.video.width; c.height = asset.video.height;
        const std::string p = asset.sourcePath;
        const bool proxyFile = p.size() > 4 && p.compare(p.size() - 4, 4, ".mp4") == 0;
        if (priority == MediaPriority::ExportSoftwareDecode) ++verifications;
        if (proxyFile && proxy.load() != Proxy::Good) return std::make_unique<BadProxyDecoder>(c, proxy.load() == Proxy::Frozen);
        return std::make_unique<test::SyntheticDecoder>(c);
    }
};

/// Quadro `index` decodificado de um decoder (seek + andar até ele).
FrameRef decoded_frame(VideoDecoderBackend& d, i64 us) {
    FrameRef f; i64 pts = 0; bool eos = false;
    if (!d.seek_to_keyframe(us).ok()) return f;
    for (int i = 0; i < 64 && !f; ++i) if (!d.next_frame(us - 1000, f, pts, eos).ok() || (eos && !f)) break;
    return f;
}
} // namespace

AUREA_TEST(PreviewProxy, Bugs034SignatureRejectsGreenAndFrozenFrames) {
    test::SyntheticConfig c; c.width = 64; c.height = 36; c.frameCount = 12; c.gop = 4;
    c.pattern = test::SyntheticPattern::FrameGray;
    test::SyntheticDecoder source(c);
    test::SyntheticDecoder same(c);
    BadProxyDecoder green(c, false), frozen(c, true);
    const FrameRef f8 = decoded_frame(source, source.pts_of(8));
    const FrameRef s8 = decoded_frame(same, same.pts_of(8));
    const FrameRef g8 = decoded_frame(green, source.pts_of(8));
    const FrameRef z8 = decoded_frame(frozen, source.pts_of(8));
    AUREA_CHECK(f8 && s8 && g8 && z8);
    if (!f8 || !s8 || !g8 || !z8) return;
    AUREA_CHECK(proxy_signature_matches(*f8.get(), *s8.get()));      // o mesmo quadro passa
    AUREA_CHECK(!proxy_signature_matches(*f8.get(), *g8.get()));     // verde (YUV zerado) não
    AUREA_CHECK(!proxy_signature_matches(*f8.get(), *z8.get()));     // o quadro 0 no lugar do 8 não
}

AUREA_TEST(PreviewProxy, Bugs034QuarterPreviewNeverServesAGreenOrFrozenProxy) {
    // A prévia em 1/4 (prévia reduzida): proxy verde ou parado fica Failed e a
    // camada continua no original — cor certa e quadros andando.
    for (const auto kind : {BadProxyFactory::Proxy::Green, BadProxyFactory::Proxy::Frozen}) {
        BadProxyFactory factory;
        factory.proxy = kind;
        const auto dir = proxy_dir(kind == BadProxyFactory::Proxy::Green ? "q-green" : "q-frozen");
        MediaManager manager; manager.set_factory(&factory);
        ProxyThresholds t; t.heavyPixels = 64 * 36; t.alwaysPixels = 32 * 18;   // em escala: o "1080p" comum
        manager.proxies().set_thresholds(t);
        manager.proxies().configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
        manager.proxies().set_policy(18);
        manager.proxies().set_reduced_preview(true);   // o que ViewportSetPreviewScale 1/4 faz
        Asset a = video_asset("quarter-a");
        a.video.frameCount = FrameIndex{12};
        (void)manager.proxies().request(a);
        AUREA_CHECK(wait_for_ms([&] { return manager.proxies().status(a).state == ProxyState::Failed; }, 6000));
        AUREA_CHECK(!manager.proxies().request(a));
        AUREA_CHECK(factory.verifications.load() >= 1u);
        VideoSource* s = nullptr;
        (void)wait_for_ms([&] { return (s = manager.source_for(LayerId{0, 1}, AssetId{0, 1}, a, 1, false)) != nullptr; }, 3000);
        AUREA_CHECK(s != nullptr);
        if (s) {
            AUREA_CHECK_EQ(s->info().display_width(), 64u);   // o original, não o proxy
            u8 previous = 0;
            for (u32 i : {1u, 5u, 9u}) {
                const i64 us = static_cast<i64>(std::llround(i * 1e6 / 30.0));
                s->request({us, DecodeMode::Still, 0, 1.f});
                AUREA_CHECK(s->wait_for(us, 3000));
                bool exact = false;
                const FrameRef f = s->frame_for(us, &exact, 0);
                AUREA_CHECK(f && f->planes[0] && f->planes[1]);
                if (!f || !f->planes[0] || !f->planes[1]) continue;
                // Cor: croma neutro (o verde de YUV zerado teria Cb = Cr = 0).
                AUREA_CHECK_EQ(f->planes[1][0], u8{128});
                AUREA_CHECK_EQ(f->planes[1][1], u8{128});
                // Avanço: cada quadro tem o seu cinza.
                AUREA_CHECK_EQ(f->planes[0][0], test::frame_gray_code(i));
                AUREA_CHECK(f->planes[0][0] != previous);
                previous = f->planes[0][0];
            }
        }
        manager.close_all(); manager.proxies().stop();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    // Proxy bom: passa na conferência e serve a prévia reduzida.
    BadProxyFactory factory;
    const auto dir = proxy_dir("q-good");
    PreviewProxyService service;
    ProxyThresholds t; t.heavyPixels = 64 * 36; t.alwaysPixels = 32 * 18;
    service.set_thresholds(t);
    service.configure(&factory, proxy_test_sink, nullptr, dir.generic_string());
    service.set_policy(18);
    service.set_reduced_preview(true);
    Asset a = video_asset("quarter-good");
    a.video.frameCount = FrameIndex{12};
    AUREA_CHECK(wait_for_ms([&] { return !!service.request(a); }, 6000));
    AUREA_CHECK(service.status(a).state == ProxyState::Ready);
    service.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

#ifdef __ANDROID__
#include "MediaCodecSource.hpp"
#include "aurea/export/FFmpegExportSink.hpp"
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
    source.video.fps = probe.video.fps;
    MediaManager manager; manager.set_factory(&factory);
    manager.proxies().set_thresholds(tiny_thresholds());
    manager.proxies().configure(&factory, make_ffmpeg_export_sink, nullptr, "/data/local/tmp/aurea-proxy-regression");
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
    AUREA_CHECK(!original->info().hardwareDecoder && !decoded->info().hardwareDecoder);
    AUREA_CHECK(original->seek_to_keyframe(0).ok() && decoded->seek_to_keyframe(0).ok());
    usize count = 0;
    for (;;) {
        FrameRef a, b; i64 ap = 0, bp = 0; bool ae = false, be = false;
        for (int attempt = 0; attempt < 500 && !a && !ae; ++attempt) AUREA_CHECK(original->next_frame(-1, a, ap, ae).ok());
        for (int attempt = 0; attempt < 500 && !b && !be; ++attempt) AUREA_CHECK(decoded->next_frame(-1, b, bp, be).ok());
        if (!a || !b) { AUREA_CHECK(!a && !b); break; }
        // Fonte acima de 60 fps é dizimada: o proxy pula quadros da original.
        while (a && ap < bp) { a.reset(); ae = false; for (int attempt = 0; attempt < 500 && !a && !ae; ++attempt) AUREA_CHECK(original->next_frame(-1, a, ap, ae).ok()); }
        if (!a) break;
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
