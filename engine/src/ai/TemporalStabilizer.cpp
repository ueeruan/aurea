#include "aurea/ai/TemporalStabilizer.hpp"
#include <algorithm>
#include <cstdlib>
#include <new>

namespace aurea::ai {
void TemporalStabilizer::reset() noexcept {
    std::vector<u8>().swap(previousRgb_);
    std::vector<u8>().swap(previousLuma_);
    std::vector<u8>().swap(stable_);
    width_ = height_ = outWidth_ = outHeight_ = 0;
}

Status TemporalStabilizer::process(const u8* rgb, u32 width, u32 height,
                                    u8* luma, u32 outWidth, u32 outHeight, u64 frame) try {
    if (!rgb || !luma || !width || !height || !outWidth || !outHeight ||
        width > 16384 || height > 16384 || outWidth > 65536 || outHeight > 65536)
        return Errc::InvalidArgument;
    const usize source = static_cast<usize>(width) * height;
    const usize output = static_cast<usize>(outWidth) * outHeight;
    if (source * 5 + output > 64u * 1024u * 1024u) {
        reset();
        return Errc::BudgetExceeded;
    }
    const bool consecutive = width_ == width && height_ == height && outWidth_ == outWidth &&
        outHeight_ == outHeight && frame > frame_ && frame - frame_ == 1;
    // Allocate before modifying the caller's frame.
    if (!consecutive) reset();
    previousRgb_.reserve(source * 3);
    previousLuma_.reserve(output);
    stable_.resize(source * 2);
    if (consecutive) {
        usize changed = 0;
        i64 brightnessDrift = 0;
        for (usize p = 0; p < source; ++p) {
            int delta = 0;
            for (usize c = 0; c < 3; ++c) {
                delta = std::max(delta, std::abs(int(rgb[p * 3 + c]) - int(previousRgb_[p * 3 + c])));
                brightnessDrift += int(rgb[p * 3 + c]) - int(previousRgb_[p * 3 + c]);
            }
            stable_[p] = delta <= 2;
            changed += delta > 12;
        }
        // Cuts and large motion invalidate the complete history.
        // Do not delay intentional fades/exposure animation, even when each
        // individual frame changes by only one RGB code value.
        if (changed * 4 <= source && std::abs(brightnessDrift) * 2 <= static_cast<i64>(source * 3)) {
            // Erode at source resolution once, not nine times per output pixel.
            for (u32 sy = 0; sy < height; ++sy) for (u32 sx = 0; sx < width; ++sx) {
                bool stationary = true;
                for (u32 yy = sy ? sy - 1 : 0; yy <= std::min(sy + 1, height - 1) && stationary; ++yy)
                    for (u32 xx = sx ? sx - 1 : 0; xx <= std::min(sx + 1, width - 1); ++xx)
                        if (!stable_[static_cast<usize>(yy) * width + xx]) { stationary = false; break; }
                stable_[source + static_cast<usize>(sy) * width + sx] = stationary;
            }
            for (u32 y = 0; y < outHeight; ++y) for (u32 x = 0; x < outWidth; ++x) {
                const u32 sx = static_cast<u64>(x) * width / outWidth;
                const u32 sy = static_cast<u64>(y) * height / outHeight;
                const usize p = static_cast<usize>(y) * outWidth + x;
                if (stable_[source + static_cast<usize>(sy) * width + sx] &&
                    std::abs(int(luma[p]) - int(previousLuma_[p])) <= 12)
                    luma[p] = static_cast<u8>((3u * previousLuma_[p] + luma[p] + 2u) / 4u);
            }
        }
    }
    previousRgb_.assign(rgb, rgb + source * 3);
    previousLuma_.assign(luma, luma + output);
    width_ = width; height_ = height; outWidth_ = outWidth; outHeight_ = outHeight; frame_ = frame;
    return OkStatus;
} catch (const std::bad_alloc&) {
    reset();
    return Errc::OutOfMemory;
}
}
