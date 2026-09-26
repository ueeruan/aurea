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
