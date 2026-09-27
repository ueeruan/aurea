#pragma once
#include "aurea/tracking/MotionGeometry.hpp"

namespace aurea::tracking {
enum class MotionTool : u32 { Point, TwoPoint, Planar, CornerPin, Stabilizer };
struct MotionTrackData {
    MotionTool tool = MotionTool::Point;
    MotionModel model = MotionModel::Auto;
    u32 sourceW = 0, sourceH = 0, analysisW = 0, analysisH = 0;
    u32 lost = 0, reacquired = 0, pointCount = 0;
    f64 fps = 30;
    u64 sourceSignature = 0;
    std::vector<i64> localFrames; // relative to the source layer start, may descend
    std::vector<i64> sourceUs;
    std::vector<MotionFrame> path;
    std::vector<std::array<Vec2, 4>> points; // source-resolution pixels
    [[nodiscard]] u64 memory_bytes() const noexcept {
        return sizeof(*this)+(localFrames.capacity()+sourceUs.capacity())*sizeof(i64)
            +path.capacity()*sizeof(MotionFrame)+points.capacity()*sizeof(std::array<Vec2,4>);
    }
};
}
