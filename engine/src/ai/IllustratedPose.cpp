#include "aurea/ai/IllustratedPose.hpp"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <net.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace aurea::ai {
namespace {

constexpr u32 kInputWidth = 192;
constexpr u32 kInputHeight = 256;
constexpr u32 kKeypoints = 17;
// The COCO eye centers and ears map to the corresponding BlazePose slots.
constexpr std::array<u32, kKeypoints> kPoseSlots{0, 2, 5, 7, 8, 11, 12, 13, 14, 15, 16, 23, 24, 25, 26, 27, 28};
constexpr std::array<f32, 3> kMean{123.675f, 116.28f, 103.53f};
constexpr std::array<f32, 3> kStdDev{58.395f, 57.12f, 57.375f};
constexpr f32 kNeutral = 176.0f;

} // namespace

bool detect_illustrated_pose(const u8* rgba, u32 width, u32 height, u32 stride,
                             const char* modelPrefix, f32* pose) noexcept try {
    if (!rgba || !pose || !modelPrefix || !*modelPrefix || width < 8 || height < 8 ||
        width > 2048 || height > 2048 || stride < width * 4u) return false;
    const std::string prefix(modelPrefix);
    const std::unique_ptr<FILE, decltype(&std::fclose)> param(std::fopen((prefix + ".param").c_str(), "rb"), &std::fclose);
    const std::unique_ptr<FILE, decltype(&std::fclose)> weights(std::fopen((prefix + ".bin").c_str(), "rb"), &std::fclose);
    if (!param || !weights) return false;

    u32 left = width, top = height, right = 0, bottom = 0;
    for (u32 y = 0; y < height; ++y) {
        const u8* row = rgba + static_cast<usize>(y) * stride;
        for (u32 x = 0; x < width; ++x) {
            if (row[4 * x + 3] < 26) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (right <= left || bottom <= top || right - left < 8 || bottom - top < 8) return false;
    const f32 cx = .5f * static_cast<f32>(left + right);
    const f32 cy = .5f * static_cast<f32>(top + bottom);
    // Letterbox around the opaque character and retain context for cropped limbs.
    const f32 scale = std::min(static_cast<f32>(kInputWidth) / static_cast<f32>(right - left),
                               static_cast<f32>(kInputHeight) / static_cast<f32>(bottom - top)) / 1.28f;
    const f32 xOffset = cx - static_cast<f32>(kInputWidth) / (2 * scale);
    const f32 yOffset = cy - static_cast<f32>(kInputHeight) / (2 * scale);
    const auto sample = [&](i32 x, i32 y, u32 channel) {
        if (x < 0 || y < 0 || static_cast<u32>(x) >= width || static_cast<u32>(y) >= height) return kNeutral;
        const u8* pixel = rgba + static_cast<usize>(y) * stride + static_cast<usize>(x) * 4;
        return static_cast<f32>(pixel[channel]) + kNeutral * (1.0f - pixel[3] / 255.0f);
    };
    ncnn::Mat input(static_cast<int>(kInputWidth), static_cast<int>(kInputHeight), 3);
    if (input.empty()) return false;
    for (u32 channel = 0; channel < 3; ++channel) {
        ncnn::Mat plane = input.channel(static_cast<int>(channel));
        for (u32 y = 0; y < kInputHeight; ++y) {
            f32* row = plane.row(static_cast<int>(y));
            const f32 sy = yOffset + static_cast<f32>(y) / scale;
            const auto iy = static_cast<i32>(std::floor(sy));
            const f32 dy = sy - static_cast<f32>(iy);
            for (u32 x = 0; x < kInputWidth; ++x) {
                const f32 sx = xOffset + static_cast<f32>(x) / scale;
                const auto ix = static_cast<i32>(std::floor(sx));
                const f32 dx = sx - static_cast<f32>(ix);
                const f32 a = std::lerp(sample(ix, iy, channel), sample(ix + 1, iy, channel), dx);
                const f32 b = std::lerp(sample(ix, iy + 1, channel), sample(ix + 1, iy + 1, channel), dx);
                row[x] = (std::lerp(a, b, dy) - kMean[channel]) / kStdDev[channel];
            }
        }
    }

    ncnn::Net net;
    net.opt.num_threads = 1;
    net.opt.use_vulkan_compute = false;
    net.opt.use_packing_layout = false;
    net.opt.use_fp16_arithmetic = false;
    if (net.load_param(param.get()) != 0 || net.load_model(weights.get()) != 0) return false;
    auto extractor = net.create_extractor();
    ncnn::Mat x, y;
    if (extractor.input("in0", input) != 0 || extractor.extract("out0", x) != 0 || extractor.extract("out1", y) != 0 ||
        x.w != 384 || y.w != 512 || x.h != kKeypoints || y.h != kKeypoints ||
        x.elemsize != sizeof(f32) || y.elemsize != sizeof(f32) || x.elempack != 1 || y.elempack != 1) return false;
    std::fill_n(pose, kIllustratedPoseFloats, 0.0f);
    u32 visible = 0;
    for (u32 i = 0; i < kKeypoints; ++i) {
        const f32* xs = x.row(static_cast<int>(i));
        const f32* ys = y.row(static_cast<int>(i));
        const auto px = std::max_element(xs, xs + x.w);
        const auto py = std::max_element(ys, ys + y.w);
        const f32 confidence = std::min(*px, *py);
        const f32 sx = xOffset + static_cast<f32>(px - xs) / (2 * scale);
        const f32 sy = yOffset + static_cast<f32>(py - ys) / (2 * scale);
        if (!std::isfinite(confidence) || confidence < .45f || sx < 0 || sy < 0 || sx >= width || sy >= height ||
            rgba[static_cast<usize>(sy) * stride + static_cast<usize>(sx) * 4 + 3] < 26) continue;
        const u32 slot = 2 + 4 * kPoseSlots[i];
        pose[slot] = sx / static_cast<f32>(width);
        pose[slot + 1] = sy / static_cast<f32>(height);
        pose[slot + 3] = std::min(confidence, 1.0f);
        ++visible;
    }
    return visible >= 3;
} catch (...) {
    return false;
}

} // namespace aurea::ai
