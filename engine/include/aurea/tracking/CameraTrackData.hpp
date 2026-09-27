#pragma once

#include "aurea/tracking/CameraTracker.hpp"

namespace aurea::tracking {

// Immutable analysis owned by the video layer. History snapshots share the
// graph instead of copying it on every property edit. Platform-independent.
struct CameraTrackData {
    CameraSolution solution;
    Tracks2D tracks;
    u32 frames = 0, analysisW = 0, analysisH = 0;
    u64 cacheKey = 0;
    u64 sourceSignature = 0;
    u32 mode = 1;
    std::vector<i64> sourceUs;
    Mat4 sceneCalibration = Mat4::identity(); // uniform scene rebase; preserves projection

    [[nodiscard]] u64 memory_bytes() const noexcept {
        u64 bytes = sizeof(*this) + sourceUs.capacity() * sizeof(i64)
            + solution.poses.capacity() * sizeof(CameraPose)
            + solution.points.capacity() * sizeof(Vec3)
            + solution.trackSolved.capacity() + solution.failure.capacity()
            + tracks.pos.capacity() * sizeof(std::vector<Vec2>);
        for (const auto& row : tracks.pos) bytes += row.capacity() * sizeof(Vec2);
        return bytes;
    }
};

inline constexpr u64 kMaxCameraTrackCells = 8'000'000;
inline constexpr u32 kMaxCameraTrackFrames = 1800;

} // namespace aurea::tracking
