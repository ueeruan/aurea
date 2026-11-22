#pragma once

#include "aurea/core/Types.hpp"

namespace aurea::ai {

inline constexpr u32 kIllustratedPoseFloats = 2 + 33 * 4;

/// Optional local RTMPose HumanArt model. Input is top-down premultiplied RGBA8.
/// Output has the same normalized layout as the platform pose estimators.
[[nodiscard]] bool detect_illustrated_pose(const u8* rgba, u32 width, u32 height, u32 stride,
                                          const char* modelPrefix, f32* pose) noexcept;

} // namespace aurea::ai
