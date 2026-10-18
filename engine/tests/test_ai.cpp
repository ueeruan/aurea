#include "TestFramework.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace aurea;
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
    static std::unique_lock<std::mutex> block_inference(DepthMapService& service) {
        return std::unique_lock<std::mutex>(service.work_);
    }
    static bool fair_bounded_queue(DepthMapService& service) {
        service.running_ = true; // inspect scheduling without starting inference
        for (u64 source = 1; source <= DepthMapService::kMaxPending; ++source) {
            DepthMapService::Job job; job.sourceKey = source; job.key = source;
            service.enqueue(std::move(job));
        }
        for (u64 key = 100; key < 120; ++key) {
            DepthMapService::Job job; job.sourceKey = 1; job.key = key;
            service.enqueue(std::move(job));
        }
        DepthMapService::Job overflow; overflow.sourceKey = 99; overflow.key = 99;
        service.enqueue(std::move(overflow));
        bool ok = service.pending_.size() == DepthMapService::kMaxPending;
        u64 expected = 1;
        for (const auto& job : service.pending_) {
            ok = ok && job.sourceKey == expected && job.key == (expected == 1 ? 119 : expected);
            ++expected;
        }
        service.running_ = false;
        service.pending_.clear();
        return ok;
    }
};
}

AUREA_TEST(Ai, LatestVideoFallbackObeysLruAndCannotRepublishEvictedFrames) {
    for (bool foreground : {false, true}) {
        ai::DepthMapService service(foreground);
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

AUREA_TEST(Ai, DepthQueueKeepsOtherLayersWhenOneSourceSeeksRapidly) {
    ai::DepthMapService service;
    AUREA_CHECK(ai::DepthMapCacheTestAccess::fair_bounded_queue(service));
}

AUREA_TEST(Ai, DepthAsyncImagesAllCompleteWithTheSamePixelsAsExport) {
    std::vector<u8> photo;
    u32 w = 0, h = 0;
    AUREA_CHECK(depth_test_photo(photo, w, h));
    if (photo.empty()) return;
    ai::DepthMapService service;
    std::atomic<u32> ready{0};
    service.set_ready_callback([](void* ctx) { ++*static_cast<std::atomic<u32>*>(ctx); }, &ready);
    {
        // All three requests coexist before the network can consume the queue.
        auto hold = ai::DepthMapCacheTestAccess::block_inference(service);
        for (u64 key = 101; key <= 103; ++key)
            AUREA_CHECK(!service.image(key, photo.data(), w, h, w * 4, 4, false));
        AUREA_CHECK_EQ(service.activity_state(), 1u);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (ready.load() < 3 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    AUREA_CHECK_EQ(ready.load(), 3u);
    AUREA_CHECK_EQ(service.activity_state(), 0u);
    const auto exact = service.image(200, photo.data(), w, h, w * 4, 4, true);
    AUREA_CHECK(exact != nullptr);
    for (u64 key = 101; key <= 103; ++key) {
        const auto cached = service.cached(key);
        AUREA_CHECK(cached != nullptr);
        if (cached && exact) AUREA_CHECK(cached->disparity == exact->disparity);
    }
    AUREA_CHECK_EQ(service.stats().inferences, u64{4});
    service.clear(); // callback context remains alive until the worker joins
}
