#pragma once
#include "aurea/core/Result.hpp"
#include <vector>

namespace aurea::ai {
// Conservative temporal filtering of static detail after neural inference.
// Moving areas and cuts use only the current frame: no motion trails/warping.
// This is not a trained multi-frame reconstruction model.
class TemporalStabilizer {
public:
    [[nodiscard]] Status process(const u8* rgb, u32 width, u32 height,
                                  u8* luma, u32 outWidth, u32 outHeight, u64 frame);
    void reset() noexcept;
private:
    std::vector<u8> previousRgb_, previousLuma_, stable_;
    u32 width_ = 0, height_ = 0, outWidth_ = 0, outHeight_ = 0;
    u64 frame_ = 0;
};
}
