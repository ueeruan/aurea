// Native Android regression: real YUV AHardwareBuffer -> production EGL bridge.
// Run on device; a host source audit does not establish EXT_YUV_target behavior.
#include "GlVideoBridge.hpp"
#include "aurea/media/DecodedRgbaCopy.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <memory>

using namespace aurea;

static bool read_rgba_target(const std::shared_ptr<const android::GlVideoBridge::Target>& target,
                             std::vector<u8>& pixels) {
    if (!target || !target->buffer) return false;
    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(target->buffer, &desc);
    if (desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM || desc.stride < target->width) return false;
    void* mapped = nullptr;
    if (AHardwareBuffer_lock(target->buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &mapped) != 0)
        return false;
    const bool copied = media::copy_decoded_rgba8(target->width, target->height,
        static_cast<const u8*>(mapped), size_t(desc.stride) * 4, size_t(desc.stride) * desc.height * 4, pixels);
    return AHardwareBuffer_unlock(target->buffer, nullptr) == 0 && copied;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    using LockPlanes = int (*)(AHardwareBuffer*, uint64_t, int32_t, const ARect*, AHardwareBuffer_Planes*);
    const auto lockPlanes = reinterpret_cast<LockPlanes>(dlsym(RTLD_DEFAULT, "AHardwareBuffer_lockPlanes"));
    if (!lockPlanes) { std::puts("SKIP: readable YUV buffers require Android API29+"); return 77; }

    std::array<std::unique_ptr<android::GlVideoBridge>, 4> bridges;
    for (auto& bridge : bridges) {
        bridge = android::GlVideoBridge::create(true);
        if (!bridge) { std::puts("SKIP: ES3/EXT_YUV_target bridge unavailable"); return 77; }
    }
    AHardwareBuffer_Desc desc{};
    desc.width = desc.height = 64; desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
    desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer* raw = nullptr;
    if (AHardwareBuffer_allocate(&desc, &raw) != 0 || !raw) {
        std::puts("SKIP: GPU-readable, CPU-writable YUV420 buffer unavailable"); return 77;
    }
    struct Release { AHardwareBuffer* buffer; ~Release() { AHardwareBuffer_release(buffer); } } release{raw};
    const android::ExternalQuad quad{0, 0, 1, 1, .5f / 64, .5f / 64, 1 - .5f / 64, 1 - .5f / 64, 64, 64};
    constexpr u8 samples[6][3] = {{81,90,240}, {145,54,34}, {41,240,110}, {128,96,176}, {16,128,128}, {235,128,128}};
    // Independent integer-code goldens (8-bit), rounded only after the full
    // BT.601/709/2020 matrix. Same codes, distinct range and matrix metadata.
    constexpr u8 expected[6][6][3] = {
        {{254,0,0},{0,255,1},{0,0,255},{207,104,66},{0,0,0},{255,255,255}},
        {{238,14,14},{13,238,14},{16,15,239},{195,105,71},{16,16,16},{235,235,235}},
        {{255,24,0},{0,216,0},{0,15,255},{216,112,63},{0,0,0},{255,255,255}},
        {{255,36,10},{0,203,8},{13,28,249},{204,112,69},{16,16,16},{235,235,235}},
        {{255,10,0},{0,225,0},{0,20,255},{211,105,62},{0,0,0},{255,255,255}},
        {{246,23,10},{6,211,6},{14,33,252},{199,106,68},{16,16,16},{235,235,235}}
    };
    u32 comparisons = 0, failures = 0, maxDelta = 0, readableTargets = 0;
    std::array<std::shared_ptr<const android::GlVideoBridge::Target>, 4> retained;
    for (u32 round = 0; round < 3; ++round) {
        for (u32 sample = 0; sample < 6; ++sample) {
            AHardwareBuffer_Planes planes{};
            if (lockPlanes(raw, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &planes) != 0) {
                std::puts("FAIL: cannot map three YUV planes"); return 2;
            }
            if (planes.planeCount != 3) {
                (void)AHardwareBuffer_unlock(raw, nullptr);
                std::puts("FAIL: expected three YUV planes"); return 2;
            }
            for (u32 p = 0; p < 3; ++p) {
                const u32 side = p ? 32 : 64;
                auto* data = static_cast<u8*>(planes.planes[p].data);
                for (u32 y = 0; y < side; ++y) for (u32 x = 0; x < side; ++x)
                    data[usize(y) * planes.planes[p].rowStride + usize(x) * planes.planes[p].pixelStride] = samples[sample][p];
            }
            if (AHardwareBuffer_unlock(raw, nullptr) != 0) { std::puts("FAIL: unlock YUV planes"); return 2; }
            // Reuse the SAME EGLImage/source with every matrix/range, alternating
            // four contexts. The shader must consume metadata on every frame.
            for (u32 profile = 0; profile < 6; ++profile) for (u32 decoder = 0; decoder < bridges.size(); ++decoder) {
                VideoColorInfo color;
                color.matrix = static_cast<YCbCrMatrix>(profile / 2);
                color.fullRange = profile % 2 != 0; color.bitDepth = 8; color.fromStream = true;
                std::shared_ptr<const android::GlVideoBridge::Target> target;
                android::GlVideoBridge::DiagnosticPixels pixels;
                const Status status = bridges[decoder]->convert(raw, quad, color, target, &pixels);
                if (!status.ok() || !pixels.valid || !target) {
                    std::printf("FAIL: conversion round=%u sample=%u profile=%u decoder=%u status=%u valid=%d\n",
                        round, sample, profile, decoder, static_cast<u32>(status.code()), pixels.valid);
                    return 3;
                }
                AHardwareBuffer_Desc targetDesc{};
                AHardwareBuffer_describe(target->buffer, &targetDesc);
                if (targetDesc.usage & AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN) {
                    std::vector<u8> mapped;
                    if (!read_rgba_target(target, mapped)) { std::puts("FAIL: RGBA fallback mapping"); return 5; }
                    ++readableTargets;
                    for (size_t i = 0; i < mapped.size(); ++i) {
                        const int golden = i % 4 == 3 ? 255 : expected[profile][sample][i % 4];
                        if (std::abs(int(mapped[i]) - golden) > 2) {
                            std::puts("FAIL: native RGBA fallback copy changed pixels"); return 5;
                        }
                    }
                    if (round == 0 && sample == 0 && profile == 0) retained[decoder] = target;
                }
                for (u32 p = 0; p < 4; ++p) for (u32 channel = 0; channel < 4; ++channel) {
                    const int golden = channel == 3 ? 255 : expected[profile][sample][channel];
                    const u32 delta = static_cast<u32>(std::abs(int(pixels.rgba[p * 4 + channel]) - golden));
                    ++comparisons; maxDelta = std::max(maxDelta, delta);
                    if (delta > 2) {
                        if (failures < 16) std::printf("FAIL: round=%u sample=%u profile=%u decoder=%u channel=%u actual=%u expected=%d\n",
                            round, sample, profile, decoder, channel, pixels.rgba[p * 4 + channel], golden);
                        ++failures;
                    }
                }
            }
        }
        // Recreating contexts must not change color or invalidate a retained
        // source buffer. This mirrors repeated project/decoder reopen.
        for (auto& bridge : bridges) {
            bridge.reset(); bridge = android::GlVideoBridge::create(true);
            if (!bridge) { std::puts("FAIL: bridge reopen"); return 4; }
        }
    }
    // A frame retained by the renderer must survive reuse and destruction of
    // the bridge that made it. Its lazy CPU fallback remains the original red.
    for (const auto& target : retained) if (target) {
        std::vector<u8> mapped;
        if (!read_rgba_target(target, mapped)) { std::puts("FAIL: retained RGBA fallback mapping"); return 6; }
        for (size_t i = 0; i < mapped.size(); ++i) {
            const int golden = i % 4 == 3 ? 255 : expected[0][0][i % 4];
            if (std::abs(int(mapped[i]) - golden) > 2) {
                std::puts("FAIL: retained RGBA buffer was overwritten"); return 6;
            }
        }
    }
    u32 resizeChecks = 0;
    for (auto& bridge : bridges) {
        bridge->set_max_live_targets(2);
        VideoColorInfo color;
        color.matrix = YCbCrMatrix::BT601;
        color.fromStream = true;
        auto a = quad, b = quad;
        b.width = b.height = 48;
        std::shared_ptr<const android::GlVideoBridge::Target> oldA, activeA, secondA, temporary;
        if (!bridge->convert(raw, a, color, oldA).ok() || !oldA) return 7;
        if (!bridge->convert(raw, b, color, temporary).ok() || !temporary) return 7;
        temporary.reset();
        if (!bridge->convert(raw, a, color, activeA).ok() || !activeA) return 7;
        oldA.reset(); // A->B->A: an old generation cannot become a free new-A slot.
        if (!bridge->convert(raw, a, color, secondA).ok() || !secondA) return 7;
        const Status pressure = bridge->convert(raw, a, color, temporary);
        if (pressure.code() != Errc::BudgetExceeded || temporary) {
            std::puts("FAIL: old-size lease bypassed the bounded RGBA pool"); return 7;
        }
        activeA.reset(); secondA.reset();
        for (u32 side : {32u, 64u, 48u, 16u, 64u, 32u, 48u, 64u}) {
            auto resized = quad; resized.width = resized.height = side;
            if (!bridge->convert(raw, resized, color, temporary).ok() || !temporary) return 7;
            const auto stats = bridge->cache_stats();
            if (stats.targetImports > bridge->max_live_targets() || stats.liveTargets > bridge->max_live_targets()
                || stats.freeTargets > stats.liveTargets) {
                std::puts("FAIL: resize retained stale EGL/RGBA imports or invalid pool accounting"); return 7;
            }
            ++resizeChecks; temporary.reset();
        }
    }
    std::printf("RGBA POOL RESULT contexts=4 resizeChecks=%u generationAccounting=passed\n", resizeChecks);
    std::printf("YUV COLOR RESULT matrices=601,709,2020 ranges=limited,full contexts=4 rounds=3 comparisons=%u readableTargets=%u maxDelta=%u failures=%u\n",
        comparisons, readableTargets, maxDelta, failures);
    return failures ? 1 : 0;
}
