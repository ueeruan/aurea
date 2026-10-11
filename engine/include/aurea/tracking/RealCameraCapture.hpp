#pragma once
#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

namespace aurea::tracking {
// Receives a rigid, display-oriented OpenGL camera pose: right/up/-Z forward.
// No accelerometer integration: translation is admitted only from visual tracking.
class RealCameraCapture final {
public:
    void reset(Vec3 position, Vec3 rotationDegrees, f32 pixelsPerMeter) noexcept;
    void recenter() noexcept;
    [[nodiscard]] bool sample(const f32* columnMajorPose, u64 timestampNs,
                              bool positional, f32 sensitivity, bool inverted) noexcept;
    [[nodiscard]] Vec3 position() const noexcept { return outputPosition_; }
    [[nodiscard]] Vec3 rotation() const noexcept { return outputRotation_; }
    [[nodiscard]] bool positional() const noexcept { return positional_; }
    [[nodiscard]] bool ready() const noexcept { return referenceValid_; }
private:
    Vec3 initialPosition_{}, initialRotation_{}, basePosition_{}, outputPosition_{}, outputRotation_{};
    Quat initialRotationQ_{}, baseRotation_{}, referenceRotation_{}, filteredRotation_{};
    Vec3 referencePosition_{}, filteredPosition_{}, relativeEuler_{};
    f32 pixelsPerMeter_ = 1;
    u64 lastTimestamp_ = 0;
    bool referenceValid_ = false, positional_ = false;
};
} // namespace aurea::tracking
