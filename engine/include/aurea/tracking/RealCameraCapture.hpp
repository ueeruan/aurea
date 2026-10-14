#pragma once
#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <algorithm>
#include <cmath>

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
    /// Andar (botões de segurar): m/s ao longo do rumo da câmera, no plano do
    /// chão (+ frente, − trás, 0 parado). Funciona sem AR: cada amostra soma.
    void set_walk(f32 metersPerSecond) noexcept { walkSpeed_ = std::isfinite(metersPerSecond) ? std::clamp(metersPerSecond, -5.0f, 5.0f) : 0.0f; }
    /// Profundidade: multiplica só o deslocamento medido para frente/trás (AR).
    void set_depth_scale(f32 scale) noexcept { depthScale_ = std::isfinite(scale) ? std::clamp(scale, 0.25f, 8.0f) : 1.0f; }
    /// A tomada andou (há posição para gravar mesmo sem AR)?
    [[nodiscard]] bool walked() const noexcept { return walkOffset_.length_sq() > 0; }
private:
    bool sample_pose(const f32* columnMajorPose, u64 timestampNs, bool positional, f32 sensitivity, bool inverted) noexcept;
    Vec3 deepen(Vec3 world) const noexcept;
    f32 walkSpeed_ = 0, depthScale_ = 1;
    Vec3 walkOffset_{};
    Vec3 initialPosition_{}, initialRotation_{}, basePosition_{}, outputPosition_{}, outputRotation_{};
    Quat initialRotationQ_{}, baseRotation_{}, referenceRotation_{}, filteredRotation_{};
    Vec3 referencePosition_{}, filteredPosition_{}, relativeEuler_{};
    f32 pixelsPerMeter_ = 1;
    u64 lastTimestamp_ = 0;
    bool referenceValid_ = false, positional_ = false;
};
} // namespace aurea::tracking
