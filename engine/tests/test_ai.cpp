#include "TestFramework.hpp"
#include "aurea/ai/Upscaler.hpp"
#include "aurea/ai/TemporalStabilizer.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace aurea;
namespace {
struct Output {
    u32 width, height;
    std::vector<u8> pixels;
    u32 calls = 0;
    f32 progress = 0;
    std::atomic<bool>* cancel = nullptr;
    static bool tile(void* context, const ai::Upscaler::Tile& t) {
        auto& o = *static_cast<Output*>(context);
        if (!t.rgb || t.x + t.width > o.width || t.y + t.height > o.height || t.progress <= o.progress)
            return false;
        for (u32 y = 0; y < t.height; ++y)
            std::copy_n(t.rgb + static_cast<usize>(y) * t.stride, t.width * 3,
                o.pixels.data() + (static_cast<usize>(y + t.y) * o.width + t.x) * 3);
        ++o.calls; o.progress = t.progress;
        if (o.cancel) o.cancel->store(true);
        return true;
    }
};
}

AUREA_TEST(Ai, TrainedUpscalerTilesMatchWholeImage) {
    constexpr u32 w = 65, h = 49;
    // Odd dimensions force partial boundary tiles and an interior seam.
    std::vector<u8> input(w * h * 3);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        const usize p = (y * w + x) * 3;
        input[p] = static_cast<u8>((x * 9 + y * 3) % 256);
        input[p + 1] = static_cast<u8>(((x / 8 + y / 8) % 2) * 220);
        input[p + 2] = static_cast<u8>((x * y) % 256);
    }
    for (u32 scale : {2u, 4u}) {
        std::atomic<bool> cancel{false};
        ai::Upscaler model;
        AUREA_CHECK(model.load(scale, 1, 128, ai::Upscaler::Backend::Cpu).ok());
        Output whole{w * scale, h * scale, std::vector<u8>(w * h * scale * scale * 3)};
        const auto start = std::chrono::steady_clock::now();
        AUREA_CHECK(model.run(input.data(), w, h, w * 3, cancel, Output::tile, &whole).ok());
        AUREA_CHECK_EQ(whole.calls, 1u);
        AUREA_CHECK(model.load(scale, 1, 32, ai::Upscaler::Backend::Cpu).ok());
        Output tiled{whole.width, whole.height, std::vector<u8>(whole.pixels.size())};
        AUREA_CHECK(model.run(input.data(), w, h, w * 3, cancel, Output::tile, &tiled).ok());
        AUREA_CHECK_EQ(tiled.calls, 6u);
        AUREA_CHECK_EQ(tiled.progress, 1.f);
        AUREA_CHECK(model.peak_working_bytes() > 0);
        AUREA_CHECK(model.peak_working_bytes() <= 48u * 1024u * 1024u);
        int worst = 0; u64 error = 0, nonNearest = 0;
        for (usize i = 0; i < whole.pixels.size(); ++i) {
            const int delta = std::abs(static_cast<int>(whole.pixels[i]) - tiled.pixels[i]);
            worst = std::max(worst, delta); error += delta;
            const usize pixel = i / 3;
            const usize source = ((pixel / whole.width / scale) * w + (pixel % whole.width / scale)) * 3 + i % 3;
            if (std::abs(static_cast<int>(whole.pixels[i]) - input[source]) > 2) ++nonNearest;
        }
        AUREA_CHECK(worst <= 1); // FP reduction order may differ with tile shape.
        AUREA_CHECK(error * 100 < whole.pixels.size());
        AUREA_CHECK(nonNearest > whole.pixels.size() / 10);
        std::printf("    AI x%u: whole/tiled max=%d mean=%.6f, peak=%.2f MiB, %.1f ms\n", scale, worst,
            static_cast<double>(error) / whole.pixels.size(),
            static_cast<double>(model.peak_working_bytes()) / (1024 * 1024),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        Output cancelled{whole.width, whole.height, std::vector<u8>(whole.pixels.size()), 0, 0, &cancel};
        const Status stopped = model.run(input.data(), w, h, w * 3, cancel, Output::tile, &cancelled);
        AUREA_CHECK(stopped.code() == Errc::Cancelled);
        AUREA_CHECK_EQ(cancelled.calls, 1u);
        cancelled.calls = 0;
        AUREA_CHECK(model.run(input.data(), w, h, w * 3, cancel, Output::tile, &cancelled).code() == Errc::Cancelled);
        AUREA_CHECK_EQ(cancelled.calls, 0u);
    }
}

AUREA_TEST(Ai, UpscalerRejectsInvalidInputs) {
    ai::Upscaler model;
    AUREA_CHECK(model.load(3).code() == Errc::InvalidArgument);
    AUREA_CHECK(model.load(2, 1, 129).code() == Errc::InvalidArgument);
    std::atomic<bool> cancel{false};
    AUREA_CHECK(model.run(nullptr, 1, 1, 3, cancel, nullptr, nullptr).code() == Errc::InvalidState);
}

AUREA_TEST(Ai, VulkanMatchesCpuWhenAvailable) {
    constexpr u32 w = 65, h = 49; // Multiple tiles and odd boundary dimensions.
    std::vector<u8> rgb(w * h * 3);
    for (usize i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<u8>(i * 17);
    std::atomic<bool> cancel{false};
    for (u32 scale : {2u, 4u}) {
        ai::Upscaler gpu, cpu;
        const Status loaded = gpu.load(scale, 1, 32, ai::Upscaler::Backend::Vulkan);
        if (loaded.code() == Errc::NotSupported) {
            std::printf("    Vulkan inference unavailable on this host (GPU acceptance NOT run)\n");
            AUREA_CHECK(cpu.load(scale).ok());
            AUREA_CHECK(cpu.backend() == ai::Upscaler::Backend::Cpu);
            continue;
        }
        AUREA_CHECK(loaded.ok());
        if (!loaded.ok()) continue;
        AUREA_CHECK(gpu.backend() == ai::Upscaler::Backend::Vulkan);
        AUREA_CHECK(cpu.load(scale, 1, 32, ai::Upscaler::Backend::Cpu).ok());
        Output a{w * scale, h * scale, std::vector<u8>(w * h * scale * scale * 3)};
        Output b{a.width, a.height, std::vector<u8>(a.pixels.size())};
        const auto begin = std::chrono::steady_clock::now();
        AUREA_CHECK(cpu.run(rgb.data(), w, h, w * 3, cancel, Output::tile, &a).ok());
        const auto middle = std::chrono::steady_clock::now();
        AUREA_CHECK(gpu.run(rgb.data(), w, h, w * 3, cancel, Output::tile, &b).ok());
        const auto end = std::chrono::steady_clock::now();
        std::printf("    tiled CPU %.1f ms, Vulkan %.1f ms (host fixture, not phone FPS)\n",
            std::chrono::duration<double, std::milli>(middle - begin).count(),
            std::chrono::duration<double, std::milli>(end - middle).count());
        int worst = 0; u64 error = 0;
        for (usize i = 0; i < a.pixels.size(); ++i) {
            const int d = std::abs(int(a.pixels[i]) - int(b.pixels[i]));
            worst = std::max(worst, d); error += d;
        }
        std::printf("    Vulkan x%u vs CPU: max %d, mean %.6f\n", scale, worst, double(error) / a.pixels.size());
        AUREA_CHECK(worst <= 4);
        AUREA_CHECK(error < a.pixels.size());
        Output interrupted{a.width, a.height, std::vector<u8>(a.pixels.size()), 0, 0, &cancel};
        AUREA_CHECK(gpu.run(rgb.data(), w, h, w * 3, cancel, Output::tile, &interrupted).code() == Errc::Cancelled);
        AUREA_CHECK_EQ(interrupted.calls, 1u);
        cancel.store(false);
    }
}

AUREA_TEST(Ai, TemporalStaticDetailDoesNotTrailMotionCutsOrSeeks) {
    ai::TemporalStabilizer temporal;
    constexpr u32 w = 8, h = 8, ow = 16, oh = 16;
    std::vector<u8> rgb(w * h * 3, 100), y(ow * oh, 100);
    AUREA_CHECK(temporal.process(rgb.data(), w, h, y.data(), ow, oh, 0).ok());
    std::fill(y.begin(), y.end(), 108);
    rgb[(3 * w + 3) * 3] = 140; // Local moving detail, less than 25% of scene.
    AUREA_CHECK(temporal.process(rgb.data(), w, h, y.data(), ow, oh, 1).ok());
    AUREA_CHECK_EQ(y[0], u8{102});
    AUREA_CHECK_EQ(y[6 * ow + 6], u8{108});
    AUREA_CHECK_EQ(y[4 * ow + 4], u8{108}); // Neighbourhood also protected.
    std::fill(rgb.begin(), rgb.end(), 230); // Cut invalidates history.
    std::fill(y.begin(), y.end(), 110);
    AUREA_CHECK(temporal.process(rgb.data(), w, h, y.data(), ow, oh, 2).ok());
    AUREA_CHECK(std::all_of(y.begin(), y.end(), [](u8 p) { return p == 110; }));
    std::fill(y.begin(), y.end(), 118);
    AUREA_CHECK(temporal.process(rgb.data(), w, h, y.data(), ow, oh, 9).ok());
    AUREA_CHECK_EQ(y[0], u8{118}); // Discontinuous frame never reuses history.
    temporal.reset();
    std::fill(y.begin(), y.end(), 120);
    AUREA_CHECK(temporal.process(rgb.data(), w, h, y.data(), ow, oh, 10).ok());
    AUREA_CHECK_EQ(y[0], u8{120});
    std::fill(rgb.begin(), rgb.end(), 231); // Small coherent fade is intentional.
    std::fill(y.begin(), y.end(), 124);
    AUREA_CHECK(temporal.process(rgb.data(), w, h, y.data(), ow, oh, 11).ok());
    AUREA_CHECK_EQ(y[0], u8{124});
    AUREA_CHECK(temporal.process(rgb.data(), 16384, 16384, y.data(), ow, oh, 11).code() == Errc::BudgetExceeded);
    AUREA_CHECK(temporal.process(nullptr, w, h, y.data(), ow, oh, 11).code() == Errc::InvalidArgument);
}

// =============================================================================
// Mapa de profundidade (MiDaS v2.1 small, conversão nossa — assets/ai/README.md)
// =============================================================================
#include "aurea/ai/DepthEstimator.hpp"
#include "aurea/ai/DepthMapService.hpp"

namespace aurea::ai {
struct DepthMapCacheTestAccess {
    static void put(DepthMapService& service, u64 source, u64 key, DepthMapPtr map) {
        service.insert(key, std::move(map));
        service.publish_latest(source, key, 0, 33333);
    }
    static void publish(DepthMapService& service, u64 source, u64 key) {
        service.publish_latest(source, key, 0, 33333);
    }
};
}

AUREA_TEST(Ai, LatestVideoFallbackObeysLruAndCannotRepublishEvictedFrames) {
    for (bool foreground : {false, true}) {
        ai::DepthMapService service(foreground ? "unused-model" : "");
        const usize capacity = foreground ? 16 : ai::DepthMapService::kMaxCached;
        std::weak_ptr<const ai::DepthMap> evicted;
        for (usize i = 0; i <= capacity; ++i) {
            auto map = std::make_shared<ai::DepthMap>(); map->disparity.resize(4);
            if (i == 0) evicted = map;
            ai::DepthMapCacheTestAccess::put(service, i + 1, i + 100, std::move(map));
        }
        u64 key = 0; i64 target = 0, duration = 0;
        AUREA_CHECK(evicted.expired());
        AUREA_CHECK(!service.latest_video(1, key, target, duration));
        ai::DepthMapCacheTestAccess::publish(service, 1, 100);
        AUREA_CHECK(!service.latest_video(1, key, target, duration));
        AUREA_CHECK(service.latest_video(capacity + 1, key, target, duration));
        AUREA_CHECK_EQ(key, capacity + 100);
        service.clear();
        AUREA_CHECK(!service.latest_video(capacity + 1, key, target, duration));
    }
}
#include <cmath>
#include <cstdlib>
#include <string>

extern "C" unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern "C" void stbi_image_free(void*);

namespace {
/// A foto das prévias de efeito do app: uma pessoa em primeiro plano sobre um
/// fundo escuro distante — perto e longe sem ambiguidade.
bool depth_test_photo(std::vector<u8>& rgba, u32& w, u32& h) {
    const std::string path = std::string(AUREA_APP_PRESETS_DIR) + "/../previa_efeitos.jpg";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<u8> bytes;
    u8 buf[65536];
    for (usize n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    int iw = 0, ih = 0, c = 0;
    u8* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &iw, &ih, &c, 4);
    if (!px) return false;
    rgba.assign(px, px + static_cast<usize>(iw) * ih * 4);
    stbi_image_free(px);
    w = static_cast<u32>(iw);
    h = static_cast<u32>(ih);
    return true;
}
f32 depth_region_mean(const std::vector<f32>& d, u32 x0, u32 y0, u32 x1, u32 y1) {
    f64 s = 0;
    for (u32 y = y0; y < y1; ++y) for (u32 x = x0; x < x1; ++x) s += d[y * ai::DepthEstimator::kSize + x];
    return static_cast<f32>(s / ((x1 - x0) * (y1 - y0)));
}
}

// Qual lado é perto: a disparidade da pessoa (centro) tem de passar a do fundo
// (cantos de cima). CPU e Vulkan (quando há) dão o mesmo mapa.
AUREA_TEST(Ai, DepthModelSeesThePersonNearerThanTheBackground) {
    std::vector<u8> photo;
    u32 w = 0, h = 0;
    AUREA_CHECK(depth_test_photo(photo, w, h));
    if (photo.empty()) return;
    constexpr u32 N = ai::DepthEstimator::kPixels;
    std::atomic<bool> cancel{false};
    std::vector<f32> cpu(N), gpuMap(N);
    ai::DepthEstimator est;
    AUREA_CHECK(est.load(ai::DepthEstimator::Backend::Cpu).ok());
    AUREA_CHECK(est.run(photo.data(), w, h, w * 4, 4, cancel, cpu.data()).ok());
    f32 best = 1e9f;
    for (int i = 0; i < 3; ++i) {
        AUREA_CHECK(est.run(photo.data(), w, h, w * 4, 4, cancel, cpu.data()).ok());
        best = std::min(best, est.last_inference_ms());
    }
    const f32 person = depth_region_mean(cpu, 96, 96, 160, 192);
    const f32 background = 0.5f * (depth_region_mean(cpu, 0, 0, 32, 32) + depth_region_mean(cpu, 224, 0, 256, 32));
    f32 p2 = 0, p98 = 0;
    ai::depth_percentiles(cpu.data(), N, p2, p98);
    std::printf("    CPU %.1f ms; pessoa %.1f fundo %.1f; p2 %.1f p98 %.1f\n", best, person, background, p2, p98);
    AUREA_CHECK(person > background * 1.3f);
    AUREA_CHECK(p98 > p2);
    // Cancelado antes de começar: nada roda.
    std::atomic<bool> stop{true};
    AUREA_CHECK(est.run(photo.data(), w, h, w * 4, 4, stop, gpuMap.data()).code() == Errc::Cancelled);

    ai::DepthEstimator vk;
    if (vk.load(ai::DepthEstimator::Backend::Vulkan).ok()) {
        AUREA_CHECK(vk.run(photo.data(), w, h, w * 4, 4, cancel, gpuMap.data()).ok());
        f32 vbest = 1e9f;
        for (int i = 0; i < 3; ++i) {
            AUREA_CHECK(vk.run(photo.data(), w, h, w * 4, 4, cancel, gpuMap.data()).ok());
            vbest = std::min(vbest, vk.last_inference_ms());
        }
        f32 worst = 0;
        for (u32 i = 0; i < N; ++i) worst = std::max(worst, std::fabs(gpuMap[i] - cpu[i]));
        std::printf("    Vulkan %.1f ms; maior diferenca CPU x Vulkan %.2e do intervalo\n", vbest, worst / (p98 - p2));
        AUREA_CHECK(worst < 0.01f * (p98 - p2));
    } else {
        std::printf("    (sem Vulkan para o ncnn: so CPU)\n");
    }

    // Conferência com o onnxruntime (manual): grava a entrada da rede e as saídas.
    if (const char* dir = std::getenv("AUREA_DEPTH_DUMP"); dir && *dir) {
        std::vector<u8> rgb(static_cast<usize>(N) * 3);
        ai::depth_input_rgb(photo.data(), w, h, w * 4, 4, rgb.data());
        auto dump = [&](const char* name, const void* data, usize bytes) {
            std::FILE* f = std::fopen((std::string(dir) + "/" + name).c_str(), "wb");
            if (f) { std::fwrite(data, 1, bytes, f); std::fclose(f); }
        };
        dump("depth_input_rgb256.u8", rgb.data(), rgb.size());
        dump("depth_cpu.f32", cpu.data(), cpu.size() * 4);
        dump("depth_vulkan.f32", gpuMap.data(), gpuMap.size() * 4);
    }
}

// O mesmo quadro-fonte não roda a rede duas vezes.
AUREA_TEST(Ai, DepthServiceRunsTheNetworkOncePerSourceFrame) {
    std::vector<u8> photo;
    u32 w = 0, h = 0;
    AUREA_CHECK(depth_test_photo(photo, w, h));
    if (photo.empty()) return;
    ai::DepthMapService svc;
    const ai::DepthMapPtr a = svc.image(7, photo.data(), w, h, w * 4, 4);
    AUREA_CHECK(a != nullptr);
    const ai::DepthMapPtr b = svc.image(7, photo.data(), w, h, w * 4, 4);
    AUREA_CHECK(a == b);
    AUREA_CHECK_EQ(svc.stats().inferences, u64{1});
    AUREA_CHECK(svc.stats().hits >= 1);
    // Outro quadro (outra chave) roda de novo; limpar esvazia o cache.
    AUREA_CHECK(svc.image(8, photo.data(), w, h, w * 4, 4) != nullptr);
    AUREA_CHECK_EQ(svc.stats().inferences, u64{2});
    svc.clear();
    AUREA_CHECK(svc.cached(7) == nullptr);
    if (a) {
        AUREA_CHECK_EQ(a->disparity.size(), static_cast<usize>(ai::DepthEstimator::kPixels));
        AUREA_CHECK(a->p98 > a->p2);
    }
}
