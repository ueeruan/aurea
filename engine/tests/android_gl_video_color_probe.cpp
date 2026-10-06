// Native Android regression: real YUV AHardwareBuffer -> production EGL bridge.
// Run on device; a host source audit does not establish EXT_YUV_target behavior.
#include "GlVideoBridge.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <memory>

using namespace aurea;

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
    u32 comparisons = 0, failures = 0, maxDelta = 0;
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
    std::printf("YUV COLOR RESULT matrices=601,709,2020 ranges=limited,full contexts=4 rounds=3 comparisons=%u maxDelta=%u failures=%u\n",
        comparisons, maxDelta, failures);
    return failures ? 1 : 0;
}
