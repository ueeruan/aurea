#include "TestFramework.hpp"
#include "aurea/ai/Upscaler.hpp"
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
        AUREA_CHECK(model.load(scale, 1, 128).ok());
        Output whole{w * scale, h * scale, std::vector<u8>(w * h * scale * scale * 3)};
        const auto start = std::chrono::steady_clock::now();
        AUREA_CHECK(model.run(input.data(), w, h, w * 3, cancel, Output::tile, &whole).ok());
        AUREA_CHECK_EQ(whole.calls, 1u);
        AUREA_CHECK(model.load(scale, 1, 32).ok());
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
